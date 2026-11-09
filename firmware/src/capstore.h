#ifndef CAPSTORE_H
#define CAPSTORE_H

#include <stdbool.h>
#include <stdint.h>

#define CAPSTORE_MAX_RECORDS 256u
#define CAPSTORE_WORD_BYTES 4u
#define CAPSTORE_ERR_LAYOUT (-1)
#define CAPSTORE_ERR_STATE (-2)
#define CAPSTORE_ERR_MISSING (-3)
#define CAPSTORE_ERR_RANGE (-4)

typedef struct {
	uint32_t offset;
	uint8_t *data;
	uint32_t len;
} capstore_span_t;

typedef struct {
	uint32_t offset;
	const uint8_t *data;
	uint32_t len;
} capstore_wspan_t;

typedef struct {
	int (*read)(const capstore_span_t *span);
	int (*write)(const capstore_wspan_t *span);
	int (*erase_sector)(uint32_t offset);
	uint32_t size;
	uint32_t sector_size;
} capstore_flash_t;

typedef struct {
	uint32_t seq;
	uint32_t start;
	uint32_t payload_len;
	uint16_t n_samples;
	uint16_t trigger_index;
} capstore_entry_t;

typedef struct {
	uint32_t seq;
	uint32_t offset;
	uint8_t *data;
	uint32_t len;
} capstore_read_t;

typedef struct {
	const uint8_t *data;
	uint32_t len;
} capstore_chunk_t;

typedef struct {
	const capstore_flash_t *flash;
	uint32_t sectors;
	uint32_t usable;
	uint32_t total;
	uint32_t write_pos;
	int32_t ready_sector;
	int32_t first_sector;
	capstore_entry_t entries[CAPSTORE_MAX_RECORDS];
	uint32_t oldest;
	uint32_t count;
	bool writing;
	capstore_entry_t pending;
	uint32_t crc;
	uint8_t stage[CAPSTORE_WORD_BYTES];
	uint32_t stage_len;
} capstore_t;

int capstore_init(capstore_t *store, const capstore_flash_t *flash);
int capstore_begin(capstore_t *store, const capstore_entry_t *header);
int capstore_write(capstore_t *store, const capstore_chunk_t *chunk);
int capstore_commit(capstore_t *store);
int capstore_abort(capstore_t *store);
const capstore_entry_t *capstore_find(const capstore_t *store, uint32_t seq);
int capstore_read(const capstore_t *store, const capstore_read_t *req);
uint32_t capstore_count(const capstore_t *store);

#endif
