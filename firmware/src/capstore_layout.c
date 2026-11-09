#include <stddef.h>

#include "capstore_priv.h"

#define CRC_POLY 0xEDB88320u
#define BITS_PER_BYTE 8u
#define CRC_CHUNK_BYTES 64u

uint32_t capstore_crc(uint32_t crc, const capstore_chunk_t *chunk)
{
	for (uint32_t i = 0; i < chunk->len; i++) {
		crc ^= chunk->data[i];
		for (uint32_t b = 0; b < BITS_PER_BYTE; b++) {
			crc = (crc >> 1) ^ (CRC_POLY & (0u - (crc & 1u)));
		}
	}
	return crc;
}

uint32_t capstore_check(const capstore_header_t *header, uint32_t payload_crc)
{
	capstore_chunk_t fields = {
		(const uint8_t *)&header->seq,
		(uint32_t)(offsetof(capstore_header_t, check) - offsetof(capstore_header_t, seq)),
	};

	return ~capstore_crc(payload_crc, &fields);
}

uint32_t capstore_sector_of(const capstore_t *store, uint32_t pos)
{
	return pos / store->usable;
}

uint32_t capstore_sector_start(const capstore_t *store, uint32_t sector)
{
	return (sector % store->sectors) * store->usable;
}

uint32_t capstore_phys(const capstore_t *store, uint32_t pos)
{
	return capstore_sector_of(store, pos) * store->flash->sector_size + CAPSTORE_SECTOR_HEADER_BYTES +
	       pos % store->usable;
}

uint32_t capstore_fit_header(const capstore_t *store, uint32_t pos)
{
	if (pos % store->usable + CAPSTORE_HEADER_BYTES > store->usable) {
		return capstore_sector_start(store, capstore_sector_of(store, pos) + 1u);
	}
	return pos;
}

uint32_t capstore_after(const capstore_t *store, const capstore_entry_t *entry)
{
	uint32_t end = entry->start + CAPSTORE_HEADER_BYTES + entry->payload_len;
	uint32_t aligned = (end + CAPSTORE_WORD_BYTES - 1u) / CAPSTORE_WORD_BYTES * CAPSTORE_WORD_BYTES;

	return capstore_fit_header(store, aligned % store->total);
}

int capstore_read_usable(const capstore_t *store, const capstore_span_t *span)
{
	uint32_t pos = span->offset;
	uint32_t done = 0;

	while (done < span->len) {
		uint32_t room = store->usable - pos % store->usable;
		uint32_t left = span->len - done;
		capstore_span_t piece = {capstore_phys(store, pos), span->data + done, left < room ? left : room};
		int err = store->flash->read(&piece);

		if (err) {
			return err;
		}
		done += piece.len;
		pos = (pos + piece.len) % store->total;
	}
	return 0;
}

int capstore_read_at(const capstore_t *store, capstore_at_t *at)
{
	capstore_span_t span = {at->pos, (uint8_t *)&at->header, CAPSTORE_HEADER_BYTES};

	return capstore_read_usable(store, &span);
}

int capstore_payload_crc(const capstore_t *store, capstore_crc_job_t *job)
{
	uint8_t buf[CRC_CHUNK_BYTES];
	uint32_t base = job->entry->start + CAPSTORE_HEADER_BYTES;

	job->crc = CAPSTORE_CRC_INIT;
	for (uint32_t offset = 0; offset < job->entry->payload_len; offset += CRC_CHUNK_BYTES) {
		uint32_t left = job->entry->payload_len - offset;
		capstore_span_t span = {(base + offset) % store->total, buf, left < CRC_CHUNK_BYTES ? left : CRC_CHUNK_BYTES};
		int err = capstore_read_usable(store, &span);

		if (err) {
			return err;
		}
		capstore_chunk_t chunk = {buf, span.len};
		job->crc = capstore_crc(job->crc, &chunk);
	}
	return 0;
}

void capstore_index_push(capstore_t *store, const capstore_entry_t *entry)
{
	if (store->count == CAPSTORE_MAX_RECORDS) {
		store->oldest = (store->oldest + 1u) % CAPSTORE_MAX_RECORDS;
		store->count--;
	}
	store->entries[(store->oldest + store->count) % CAPSTORE_MAX_RECORDS] = *entry;
	store->count++;
}
