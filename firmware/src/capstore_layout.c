#include <stddef.h>

#include "capstore_priv.h"
#include "crc32.h"

#define CRC_CHUNK_BYTES 64u

static const uint32_t record_magic[CAPSTORE_FORMATS] = {
	[CAPSTORE_FORMAT_FULL] = CAPSTORE_RECORD_MAGIC,
	[CAPSTORE_FORMAT_AIM_AVERAGED] = CAPSTORE_AIM_AVERAGED_MAGIC,
	[CAPSTORE_FORMAT_ULTRA] = CAPSTORE_ULTRA_MAGIC,
};

static uint8_t format_of(uint32_t magic)
{
	for (uint8_t format = 0; format < CAPSTORE_FORMATS; format++) {
		if (record_magic[format] == magic) {
			return format;
		}
	}
	return CAPSTORE_FORMATS;
}

uint32_t capstore_magic(uint8_t format)
{
	return record_magic[format];
}

bool capstore_is_record(uint32_t magic)
{
	return format_of(magic) < CAPSTORE_FORMATS;
}

capstore_entry_t capstore_entry_at(const capstore_at_t *at)
{
	const capstore_header_t *h = &at->header;

	return (capstore_entry_t){h->seq, at->pos, h->payload_len, h->n_samples, h->trigger_index, format_of(h->magic)};
}

uint32_t capstore_crc(uint32_t crc, const capstore_chunk_t *chunk)
{
	crc32_bytes_t bytes = {chunk->data, chunk->len};

	return crc32_step(crc, &bytes);
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

static bool committed(const capstore_t *store, const capstore_header_t *h)
{
	return h->payload_len != CAPSTORE_ERASED_WORD && h->check != CAPSTORE_ERASED_WORD &&
	       h->payload_len < store->total / CAPSTORE_MIN_SECTORS;
}

int capstore_classify(const capstore_t *store, const capstore_at_t *at)
{
	const capstore_header_t *h = &at->header;
	capstore_entry_t e = capstore_entry_at(at);
	capstore_crc_job_t job = {.entry = &e};

	if (!capstore_is_record(h->magic) || !committed(store, h)) {
		return CAPSTORE_RECORD_BAD;
	}
	int err = capstore_payload_crc(store, &job);
	if (err) {
		return err;
	}
	uint32_t expected = capstore_check(h, job.crc);
	if (h->check == expected) {
		return CAPSTORE_RECORD_KEPT;
	}
	return h->check == ~expected ? CAPSTORE_RECORD_VOID : CAPSTORE_RECORD_BAD;
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
