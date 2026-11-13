#ifndef LIFECOUNT_H
#define LIFECOUNT_H

#include <stdbool.h>
#include <stdint.h>

#define LIFECOUNT_MAGIC 0x4C494645u
#define LIFECOUNT_SECTORS 2u
#define LIFECOUNT_ERR_LAYOUT (-1)
#define LIFECOUNT_ERR_STATE (-2)

typedef struct {
	uint32_t count;
	uint32_t serial;
	uint32_t start_seq;
	uint32_t check;
} lifecount_record_t;

typedef struct {
	int (*read)(uint32_t offset, lifecount_record_t *rec);
	int (*write)(uint32_t offset, const lifecount_record_t *rec);
	int (*erase_sector)(uint32_t offset);
	uint32_t sector_size;
} lifecount_flash_t;

typedef struct {
	const lifecount_flash_t *flash;
	bool started;
	lifecount_record_t now;
	uint32_t next_slot;
} lifecount_t;

int lifecount_init(lifecount_t *lc, const lifecount_flash_t *flash);
int lifecount_start(lifecount_t *lc, uint32_t start_seq);
int lifecount_shot(lifecount_t *lc);
int lifecount_unshot(lifecount_t *lc, uint32_t seq);
uint32_t lifecount_value(const lifecount_t *lc);

#endif
