#include "lifecount.h"

#define ERASED_WORD 0xFFFFFFFFu

static uint32_t check_of(const lifecount_record_t *r)
{
	return LIFECOUNT_MAGIC ^ r->count ^ r->serial ^ r->start_seq;
}

static bool is_valid(const lifecount_record_t *r)
{
	return r->check == check_of(r) && r->count <= r->serial;
}

static bool is_blank(const lifecount_record_t *r)
{
	return r->count == ERASED_WORD && r->serial == ERASED_WORD && r->start_seq == ERASED_WORD &&
	       r->check == ERASED_WORD;
}

static uint32_t per_sector(const lifecount_t *lc)
{
	return lc->flash->sector_size / (uint32_t)sizeof(lifecount_record_t);
}

static uint32_t slot_count(const lifecount_t *lc)
{
	return per_sector(lc) * LIFECOUNT_SECTORS;
}

static uint32_t offset_of(uint32_t slot)
{
	return slot * (uint32_t)sizeof(lifecount_record_t);
}

static int find_newest(lifecount_t *lc, uint32_t *newest)
{
	for (uint32_t slot = 0; slot < slot_count(lc); slot++) {
		lifecount_record_t rec;
		int err = lc->flash->read(offset_of(slot), &rec);

		if (err) {
			return err;
		}
		if (is_valid(&rec) && (!lc->started || rec.serial > lc->now.serial)) {
			lc->started = true;
			lc->now = rec;
			*newest = slot;
		}
	}
	return 0;
}

static int skip_used(lifecount_t *lc)
{
	while (lc->next_slot % per_sector(lc) != 0u) {
		lifecount_record_t rec;
		int err = lc->flash->read(offset_of(lc->next_slot), &rec);

		if (err) {
			return err;
		}
		if (is_blank(&rec)) {
			return 0;
		}
		lc->next_slot = (lc->next_slot + 1u) % slot_count(lc);
	}
	return 0;
}

int lifecount_init(lifecount_t *lc, const lifecount_flash_t *flash)
{
	uint32_t newest = 0u;

	lc->flash = flash;
	lc->started = false;
	lc->now = (lifecount_record_t){0};
	lc->next_slot = 0u;
	if (flash->sector_size < sizeof(lifecount_record_t) ||
	    flash->sector_size % sizeof(lifecount_record_t) != 0u) {
		return LIFECOUNT_ERR_LAYOUT;
	}
	int err = find_newest(lc, &newest);

	if (err || !lc->started) {
		return err;
	}
	lc->next_slot = (newest + 1u) % slot_count(lc);
	return skip_used(lc);
}

static int write_next(lifecount_t *lc, lifecount_record_t *rec)
{
	uint32_t slot = lc->next_slot;

	rec->check = check_of(rec);
	if (slot % per_sector(lc) == 0u) {
		int err = lc->flash->erase_sector(offset_of(slot));
		if (err) {
			return err;
		}
	}
	lc->next_slot = (slot + 1u) % slot_count(lc);
	int err = lc->flash->write(offset_of(slot), rec);

	if (err) {
		return err;
	}
	lc->now = *rec;
	lc->started = true;
	return 0;
}

static int write_count(lifecount_t *lc, uint32_t count)
{
	lifecount_record_t rec = {
		.count = count, .serial = lc->now.serial + 1u, .start_seq = lc->now.start_seq};

	return write_next(lc, &rec);
}

int lifecount_start(lifecount_t *lc, uint32_t start_seq)
{
	lifecount_record_t rec = {.count = 0u, .serial = 0u, .start_seq = start_seq};

	return lc->started ? 0 : write_next(lc, &rec);
}

int lifecount_shot(lifecount_t *lc)
{
	return lc->started ? write_count(lc, lc->now.count + 1u) : LIFECOUNT_ERR_STATE;
}

int lifecount_unshot(lifecount_t *lc, uint32_t seq)
{
	if (!lc->started || seq <= lc->now.start_seq || lc->now.count == 0u) {
		return 0;
	}
	int err = write_count(lc, lc->now.count - 1u);

	return err ? err : 1;
}

uint32_t lifecount_value(const lifecount_t *lc)
{
	return lc->started ? lc->now.count : 0u;
}
