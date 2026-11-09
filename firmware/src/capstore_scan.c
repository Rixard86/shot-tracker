#include <string.h>

#include "capstore_priv.h"

#define ERASED_CHECK_BYTES 64u
#define STEP_CONTINUE 1
#define STEP_END 0

typedef struct {
	uint32_t sector;
	uint32_t magic;
	uint32_t first;
} sector_info_t;

typedef struct {
	uint32_t sector;
	capstore_at_t at;
	bool found;
} anchor_t;

typedef struct {
	uint32_t pos;
	uint32_t max_seq;
	bool have_seq;
	uint32_t frontier;
} walk_t;

static bool layout_ok(const capstore_flash_t *flash)
{
	return flash->sector_size > CAPSTORE_SECTOR_HEADER_BYTES + CAPSTORE_HEADER_BYTES &&
	       flash->sector_size % CAPSTORE_WORD_BYTES == 0 && flash->size % flash->sector_size == 0 &&
	       flash->size / flash->sector_size >= CAPSTORE_MIN_SECTORS;
}

static int read_sector(const capstore_t *store, sector_info_t *info)
{
	uint32_t words[] = {0, 0};
	capstore_span_t span = {info->sector * store->flash->sector_size, (uint8_t *)words, sizeof(words)};
	int err = store->flash->read(&span);

	info->magic = words[0];
	info->first = words[1];
	return err;
}

static int read_anchor(const capstore_t *store, anchor_t *anchor)
{
	sector_info_t info = {.sector = anchor->sector};
	int err = read_sector(store, &info);

	anchor->found = false;
	if (err || info.magic != CAPSTORE_SECTOR_MAGIC || info.first > store->usable - CAPSTORE_HEADER_BYTES) {
		return err;
	}
	anchor->at.pos = capstore_sector_start(store, anchor->sector) + info.first;
	err = capstore_read_at(store, &anchor->at);
	anchor->found = !err && anchor->at.header.magic == CAPSTORE_RECORD_MAGIC;
	return err;
}

static int find_oldest(const capstore_t *store, anchor_t *oldest)
{
	oldest->found = false;
	for (uint32_t s = 0; s < store->sectors; s++) {
		anchor_t a = {.sector = s};
		int err = read_anchor(store, &a);
		if (err) {
			return err;
		}
		if (a.found && (!oldest->found || a.at.header.seq < oldest->at.header.seq)) {
			*oldest = a;
		}
	}
	return 0;
}

static int resync(const capstore_t *store, walk_t *walk)
{
	uint32_t from = capstore_sector_of(store, walk->pos);

	for (uint32_t i = 1; i < store->sectors; i++) {
		anchor_t a = {.sector = (from + i) % store->sectors};
		int err = read_anchor(store, &a);
		if (err) {
			return err;
		}
		if (a.found && (!walk->have_seq || a.at.header.seq > walk->max_seq)) {
			walk->pos = a.at.pos;
			return STEP_CONTINUE;
		}
	}
	return STEP_END;
}

static int judge(const capstore_t *store, const walk_t *walk)
{
	capstore_at_t at = {.pos = walk->pos};
	int err = capstore_read_at(store, &at);

	if (err) {
		return err;
	}
	if (walk->have_seq && at.header.seq <= walk->max_seq) {
		return CAPSTORE_RECORD_BAD;
	}
	return capstore_classify(store, &at);
}

static int rescan(const capstore_t *store, walk_t *walk)
{
	uint32_t base = walk->pos - walk->pos % store->usable;
	walk_t probe = *walk;

	for (uint32_t off = walk->pos % store->usable + CAPSTORE_WORD_BYTES; off + CAPSTORE_HEADER_BYTES <= store->usable;
	     off += CAPSTORE_WORD_BYTES) {
		probe.pos = base + off;
		int kind = judge(store, &probe);
		if (kind < 0) {
			return kind;
		}
		if (kind != CAPSTORE_RECORD_BAD) {
			walk->pos = probe.pos;
			return STEP_CONTINUE;
		}
	}
	return resync(store, walk);
}

static int walk_step(capstore_t *store, walk_t *walk)
{
	capstore_at_t at = {.pos = walk->pos};
	const capstore_header_t *h = &at.header;
	int err = capstore_read_at(store, &at);

	if (err) {
		return err;
	}
	if (h->magic == CAPSTORE_ERASED_WORD) {
		walk->frontier = walk->pos;
		return STEP_END;
	}
	int kind = judge(store, walk);
	if (kind < 0) {
		return kind;
	}
	if (kind == CAPSTORE_RECORD_BAD) {
		return rescan(store, walk);
	}
	capstore_entry_t e = {h->seq, walk->pos, h->payload_len, h->n_samples, h->trigger_index};
	if (kind == CAPSTORE_RECORD_KEPT) {
		capstore_index_push(store, &e);
	}
	walk->max_seq = h->seq;
	walk->have_seq = true;
	walk->pos = capstore_after(store, &e);
	walk->frontier = walk->pos;
	return STEP_CONTINUE;
}

static int walk_all(capstore_t *store, walk_t *walk)
{
	uint32_t max_steps = store->total / CAPSTORE_HEADER_BYTES + store->sectors;

	for (uint32_t step = 0; step < max_steps; step++) {
		int result = walk_step(store, walk);
		if (result != STEP_CONTINUE) {
			return result < 0 ? result : 0;
		}
	}
	return 0;
}

static int rest_erased(const capstore_t *store, uint32_t pos)
{
	uint8_t buf[ERASED_CHECK_BYTES];
	uint32_t left = store->usable - pos % store->usable;

	while (left > 0) {
		capstore_span_t span = {pos, buf, left < ERASED_CHECK_BYTES ? left : ERASED_CHECK_BYTES};
		int err = capstore_read_usable(store, &span);
		if (err) {
			return err;
		}
		for (uint32_t i = 0; i < span.len; i++) {
			if (buf[i] != CAPSTORE_ERASED_BYTE) {
				return 0;
			}
		}
		pos += span.len;
		left -= span.len;
	}
	return 1;
}

static int settle(capstore_t *store, uint32_t pos)
{
	uint32_t sector = capstore_sector_of(store, pos);
	sector_info_t info = {.sector = sector};

	store->write_pos = pos;
	if (pos % store->usable == 0) {
		return 0;
	}
	int erased = rest_erased(store, pos);
	int err = erased < 0 ? erased : read_sector(store, &info);
	if (err) {
		return err;
	}
	if (!erased || info.magic != CAPSTORE_SECTOR_MAGIC) {
		store->write_pos = capstore_sector_start(store, sector + 1u);
		return 0;
	}
	store->ready_sector = (int32_t)sector;
	store->first_sector = info.first != CAPSTORE_ERASED_WORD ? (int32_t)sector : -1;
	return 0;
}

int capstore_init(capstore_t *store, const capstore_flash_t *flash)
{
	anchor_t oldest = {0};
	walk_t walk = {0};

	memset(store, 0, sizeof(*store));
	store->flash = flash;
	store->ready_sector = -1;
	store->first_sector = -1;
	if (!layout_ok(flash)) {
		return CAPSTORE_ERR_LAYOUT;
	}
	store->sectors = flash->size / flash->sector_size;
	store->usable = flash->sector_size - CAPSTORE_SECTOR_HEADER_BYTES;
	store->total = store->sectors * store->usable;
	int err = find_oldest(store, &oldest);
	if (!err && oldest.found) {
		walk.pos = oldest.at.pos;
		err = walk_all(store, &walk);
	}
	return err ? err : settle(store, walk.frontier);
}
