#ifndef EVLOG_H
#define EVLOG_H

#include <stdint.h>

#include "protocol.h"

#define EVLOG_MAGIC 0x53504556u

typedef struct {
	sp_event_t event;
	uint32_t magic;
	uint32_t check;
} evlog_record_t;

typedef struct {
	int (*read)(uint32_t offset, evlog_record_t *rec);
	int (*write)(uint32_t offset, const evlog_record_t *rec);
	int (*erase_sector)(uint32_t offset);
	uint32_t size;
	uint32_t sector_size;
} evlog_flash_t;

typedef struct {
	const evlog_flash_t *flash;
	uint32_t slots;
	uint32_t head;
} evlog_t;

typedef struct {
	uint32_t from_seq;
	uint32_t slot;
	uint32_t remaining;
	sp_event_t event;
} evlog_cursor_t;

int evlog_init(evlog_t *log, const evlog_flash_t *flash);
int evlog_append(evlog_t *log, const sp_event_t *event);
void evlog_rewind(const evlog_t *log, evlog_cursor_t *cur);
int evlog_next(const evlog_t *log, evlog_cursor_t *cur);

#endif
