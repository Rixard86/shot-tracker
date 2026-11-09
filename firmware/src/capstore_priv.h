#ifndef CAPSTORE_PRIV_H
#define CAPSTORE_PRIV_H

#include "capstore.h"

#define CAPSTORE_SECTOR_MAGIC 0x53504353u
#define CAPSTORE_RECORD_MAGIC 0x53504352u
#define CAPSTORE_SECTOR_HEADER_BYTES 8u
#define CAPSTORE_FIRST_WORD_OFFSET 4u
#define CAPSTORE_ERASED_WORD 0xFFFFFFFFu
#define CAPSTORE_ERASED_BYTE 0xFFu
#define CAPSTORE_CRC_INIT 0xFFFFFFFFu
#define CAPSTORE_OPEN_BYTES 12u
#define CAPSTORE_COMMIT_BYTES 8u

typedef struct {
	uint32_t magic;
	uint32_t seq;
	uint16_t n_samples;
	uint16_t trigger_index;
	uint32_t payload_len;
	uint32_t check;
} capstore_header_t;

#define CAPSTORE_HEADER_BYTES ((uint32_t)sizeof(capstore_header_t))

typedef struct {
	uint32_t pos;
	capstore_header_t header;
} capstore_at_t;

typedef struct {
	const capstore_entry_t *entry;
	uint32_t crc;
} capstore_crc_job_t;

uint32_t capstore_crc(uint32_t crc, const capstore_chunk_t *chunk);
uint32_t capstore_check(const capstore_header_t *header, uint32_t payload_crc);
uint32_t capstore_phys(const capstore_t *store, uint32_t pos);
uint32_t capstore_sector_of(const capstore_t *store, uint32_t pos);
uint32_t capstore_sector_start(const capstore_t *store, uint32_t sector);
uint32_t capstore_fit_header(const capstore_t *store, uint32_t pos);
uint32_t capstore_after(const capstore_t *store, const capstore_entry_t *entry);
int capstore_read_usable(const capstore_t *store, const capstore_span_t *span);
int capstore_read_at(const capstore_t *store, capstore_at_t *at);
int capstore_payload_crc(const capstore_t *store, capstore_crc_job_t *job);
void capstore_index_push(capstore_t *store, const capstore_entry_t *entry);

#endif
