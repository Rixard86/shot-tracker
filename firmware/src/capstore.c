#include <string.h>

#include "capstore_priv.h"

static int write_raw(const capstore_t *store, const capstore_wspan_t *span)
{
	return store->flash->write(span);
}

static void evict(capstore_t *store, uint32_t sector)
{
	while (store->count > 0) {
		const capstore_entry_t *e = &store->entries[store->oldest];
		uint32_t first = capstore_sector_of(store, e->start);
		uint32_t span = (e->start % store->usable + CAPSTORE_HEADER_BYTES + e->payload_len - 1u) / store->usable;

		if ((sector + store->sectors - first) % store->sectors > span) {
			return;
		}
		store->oldest = (store->oldest + 1u) % CAPSTORE_MAX_RECORDS;
		store->count--;
	}
}

static int ensure_ready(capstore_t *store, uint32_t pos)
{
	uint32_t sector = capstore_sector_of(store, pos);
	uint32_t base = sector * store->flash->sector_size;
	uint32_t magic = CAPSTORE_SECTOR_MAGIC;
	capstore_wspan_t mark = {base, (const uint8_t *)&magic, sizeof(magic)};

	if ((int32_t)sector == store->ready_sector) {
		return 0;
	}
	evict(store, sector);
	int err = store->flash->erase_sector(base);
	if (!err) {
		err = write_raw(store, &mark);
	}
	if (!err) {
		store->ready_sector = (int32_t)sector;
		store->first_sector = -1;
	}
	return err;
}

static int program(capstore_t *store, const capstore_wspan_t *span)
{
	int err = ensure_ready(store, span->offset);
	capstore_wspan_t phys = {capstore_phys(store, span->offset), span->data, span->len};

	return err ? err : write_raw(store, &phys);
}

static int mark_first(capstore_t *store, uint32_t pos)
{
	uint32_t sector = capstore_sector_of(store, pos);
	uint32_t offset = pos % store->usable;
	capstore_wspan_t word = {sector * store->flash->sector_size + CAPSTORE_FIRST_WORD_OFFSET,
				 (const uint8_t *)&offset, sizeof(offset)};

	if ((int32_t)sector == store->first_sector) {
		return 0;
	}
	int err = write_raw(store, &word);
	if (!err) {
		store->first_sector = (int32_t)sector;
	}
	return err;
}

int capstore_begin(capstore_t *store, const capstore_entry_t *header)
{
	uint32_t pos = capstore_fit_header(store, store->write_pos);
	capstore_header_t h = {.magic = CAPSTORE_RECORD_MAGIC, .seq = header->seq,
			       .n_samples = header->n_samples, .trigger_index = header->trigger_index};
	capstore_wspan_t open = {pos, (const uint8_t *)&h, CAPSTORE_OPEN_BYTES};

	if (store->writing) {
		return CAPSTORE_ERR_STATE;
	}
	int err = ensure_ready(store, pos);
	if (!err) {
		err = mark_first(store, pos);
	}
	if (!err) {
		err = program(store, &open);
	}
	if (err) {
		return err;
	}
	store->pending = *header;
	store->pending.start = pos;
	store->pending.payload_len = 0;
	store->write_pos = (pos + CAPSTORE_HEADER_BYTES) % store->total;
	store->crc = CAPSTORE_CRC_INIT;
	store->stage_len = 0;
	store->writing = true;
	return 0;
}

static int flush_stage(capstore_t *store)
{
	capstore_wspan_t word = {store->write_pos, store->stage, CAPSTORE_WORD_BYTES};
	int err = program(store, &word);

	store->write_pos = (store->write_pos + CAPSTORE_WORD_BYTES) % store->total;
	store->stage_len = 0;
	return err;
}

static uint32_t stage_bytes(capstore_t *store, const capstore_chunk_t *chunk)
{
	uint32_t room = CAPSTORE_WORD_BYTES - store->stage_len;
	uint32_t n = chunk->len < room ? chunk->len : room;

	memcpy(&store->stage[store->stage_len], chunk->data, n);
	store->stage_len += n;
	return n;
}

int capstore_write(capstore_t *store, const capstore_chunk_t *chunk)
{
	capstore_chunk_t left = *chunk;

	if (!store->writing) {
		return CAPSTORE_ERR_STATE;
	}
	store->crc = capstore_crc(store->crc, chunk);
	store->pending.payload_len += chunk->len;
	while (left.len > 0) {
		uint32_t n;
		int err = 0;

		if (store->stage_len > 0 || left.len < CAPSTORE_WORD_BYTES) {
			n = stage_bytes(store, &left);
			if (store->stage_len == CAPSTORE_WORD_BYTES) {
				err = flush_stage(store);
			}
		} else {
			uint32_t room = store->usable - store->write_pos % store->usable;
			uint32_t whole = left.len / CAPSTORE_WORD_BYTES * CAPSTORE_WORD_BYTES;
			capstore_wspan_t run = {store->write_pos, left.data, whole < room ? whole : room};
			err = program(store, &run);
			n = run.len;
			store->write_pos = (store->write_pos + n) % store->total;
		}
		if (err) {
			return err;
		}
		left.data += n;
		left.len -= n;
	}
	return 0;
}

static int finish(capstore_t *store, bool keep)
{
	if (!store->writing) {
		return CAPSTORE_ERR_STATE;
	}
	store->writing = false;
	if (store->stage_len > 0) {
		memset(&store->stage[store->stage_len], CAPSTORE_ERASED_BYTE, CAPSTORE_WORD_BYTES - store->stage_len);
		int err = flush_stage(store);
		if (err) {
			return err;
		}
	}
	capstore_header_t h = {.seq = store->pending.seq, .n_samples = store->pending.n_samples,
			       .trigger_index = store->pending.trigger_index,
			       .payload_len = store->pending.payload_len};
	uint32_t check = capstore_check(&h, store->crc);
	uint32_t words[] = {h.payload_len, keep ? check : ~check};
	capstore_wspan_t commit = {capstore_phys(store, store->pending.start + CAPSTORE_OPEN_BYTES),
				   (const uint8_t *)words, CAPSTORE_COMMIT_BYTES};
	int err = write_raw(store, &commit);

	if (!err && keep) {
		capstore_index_push(store, &store->pending);
	}
	return err;
}

int capstore_commit(capstore_t *store)
{
	return finish(store, true);
}

int capstore_abort(capstore_t *store)
{
	return finish(store, false);
}

const capstore_entry_t *capstore_find(const capstore_t *store, uint32_t seq)
{
	for (uint32_t i = store->count; i > 0; i--) {
		const capstore_entry_t *e = &store->entries[(store->oldest + i - 1u) % CAPSTORE_MAX_RECORDS];
		if (e->seq == seq) {
			return e;
		}
	}
	return NULL;
}

int capstore_read(const capstore_t *store, const capstore_read_t *req)
{
	const capstore_entry_t *e = capstore_find(store, req->seq);

	if (!e) {
		return CAPSTORE_ERR_MISSING;
	}
	if (req->offset > e->payload_len || req->len > e->payload_len - req->offset) {
		return CAPSTORE_ERR_RANGE;
	}
	capstore_span_t span = {(e->start + CAPSTORE_HEADER_BYTES + req->offset) % store->total, req->data, req->len};
	return capstore_read_usable(store, &span);
}

uint32_t capstore_count(const capstore_t *store)
{
	return store->count;
}
