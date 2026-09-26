#include "evlog.h"

#include <stdbool.h>
#include <stddef.h>

#define CRC_INIT 0xFFFFFFFFu
#define CRC_POLY 0xEDB88320u
#define BITS_PER_BYTE 8u
#define ERASED_BYTE 0xFFu
#define ERR_LAYOUT (-1)

static uint32_t checksum(const evlog_record_t *rec)
{
	const uint8_t *p = (const uint8_t *)rec;
	uint32_t crc = CRC_INIT;

	for (size_t i = 0; i < offsetof(evlog_record_t, check); i++) {
		crc ^= p[i];
		for (uint32_t b = 0; b < BITS_PER_BYTE; b++) {
			crc = (crc >> 1) ^ (CRC_POLY & (0u - (crc & 1u)));
		}
	}
	return ~crc;
}

static bool is_valid(const evlog_record_t *rec)
{
	return rec->magic == EVLOG_MAGIC && rec->check == checksum(rec);
}

static bool is_blank(const evlog_record_t *rec)
{
	const uint8_t *p = (const uint8_t *)rec;

	for (size_t i = 0; i < sizeof(*rec); i++) {
		if (p[i] != ERASED_BYTE) {
			return false;
		}
	}
	return true;
}

static uint32_t slot_offset(uint32_t slot)
{
	return slot * (uint32_t)sizeof(evlog_record_t);
}

static uint32_t per_sector(const evlog_t *log)
{
	return log->flash->sector_size / (uint32_t)sizeof(evlog_record_t);
}

static int find_head(evlog_t *log)
{
	bool found = false;
	uint32_t newest_seq = 0;
	uint32_t newest = 0;

	for (uint32_t slot = 0; slot < log->slots; slot++) {
		evlog_record_t rec;
		int err = log->flash->read(slot_offset(slot), &rec);
		if (err) {
			return err;
		}
		if (is_valid(&rec) && (!found || rec.event.seq > newest_seq)) {
			found = true;
			newest_seq = rec.event.seq;
			newest = slot;
		}
	}
	log->head = found ? (newest + 1u) % log->slots : 0u;
	return 0;
}

static int skip_used(evlog_t *log)
{
	for (uint32_t n = 0; n < log->slots && log->head % per_sector(log) != 0; n++) {
		evlog_record_t rec;
		int err = log->flash->read(slot_offset(log->head), &rec);
		if (err) {
			return err;
		}
		if (is_blank(&rec)) {
			return 0;
		}
		log->head = (log->head + 1u) % log->slots;
	}
	return 0;
}

int evlog_init(evlog_t *log, const evlog_flash_t *flash)
{
	log->flash = flash;
	log->slots = flash->size / (uint32_t)sizeof(evlog_record_t);
	log->head = 0;
	if (log->slots == 0 || flash->sector_size % sizeof(evlog_record_t) != 0 ||
	    flash->size % flash->sector_size != 0) {
		return ERR_LAYOUT;
	}
	int err = find_head(log);

	return err ? err : skip_used(log);
}

int evlog_append(evlog_t *log, const sp_event_t *event)
{
	evlog_record_t rec = {.event = *event, .magic = EVLOG_MAGIC};
	uint32_t slot = log->head;

	rec.check = checksum(&rec);
	log->head = (slot + 1u) % log->slots;
	if (slot % per_sector(log) == 0) {
		int err = log->flash->erase_sector(slot_offset(slot));
		if (err) {
			return err;
		}
	}
	return log->flash->write(slot_offset(slot), &rec);
}

void evlog_rewind(const evlog_t *log, evlog_cursor_t *cur)
{
	cur->slot = log->head;
	cur->remaining = log->slots;
}

int evlog_next(const evlog_t *log, evlog_cursor_t *cur)
{
	while (cur->remaining > 0) {
		evlog_record_t rec;
		int err = log->flash->read(slot_offset(cur->slot), &rec);

		cur->slot = (cur->slot + 1u) % log->slots;
		cur->remaining--;
		if (err) {
			return err;
		}
		if (is_valid(&rec) && rec.event.seq >= cur->from_seq) {
			cur->event = rec.event;
			return 1;
		}
	}
	return 0;
}
