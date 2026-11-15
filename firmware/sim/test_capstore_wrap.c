#include <stdio.h>
#include <string.h>

#include "../src/capstore_priv.h"

#define SECTOR_BYTES 4096u
#define SECTORS 4u
#define FLASH_BYTES (SECTORS * SECTOR_BYTES)
#define ERASED 0xFFu
#define LAPS 3u
#define SHORT_BY 8u
#define RECORDS_PER_SECTOR 4u
#define SAMPLES 720u
#define TRIGGER 480u
#define BYTE_STRIDE 13u

static uint8_t flash[FLASH_BYTES];
static uint8_t payload[SECTOR_BYTES];
static capstore_t store;
static uint32_t next_seq;
static int failures;

static void check(int ok, const char *what)
{
	if (!ok) {
		printf("FAIL: %s\n", what);
		failures++;
	}
}

static int fake_read(const capstore_span_t *span)
{
	memcpy(span->data, &flash[span->offset], span->len);
	return 0;
}

static int fake_write(const capstore_wspan_t *span)
{
	for (uint32_t i = 0; i < span->len; i++) {
		flash[span->offset + i] &= span->data[i];
	}
	return 0;
}

static int fake_erase(uint32_t offset)
{
	memset(&flash[offset], ERASED, SECTOR_BYTES);
	return 0;
}

static const capstore_flash_t dev = {fake_read, fake_write, fake_erase, FLASH_BYTES, SECTOR_BYTES};

static uint32_t sector_payload(void)
{
	return store.usable - CAPSTORE_HEADER_BYTES;
}

static void store_record(uint32_t len)
{
	capstore_entry_t entry = {.seq = ++next_seq, .n_samples = SAMPLES, .trigger_index = TRIGGER};
	capstore_chunk_t chunk = {payload, len};

	for (uint32_t i = 0; i < len; i++) {
		payload[i] = (uint8_t)(next_seq + i * BYTE_STRIDE);
	}
	check(capstore_begin(&store, &entry) == 0 && capstore_write(&store, &chunk) == 0 &&
	      capstore_commit(&store) == 0, "record stored");
}

static int index_in_order(void)
{
	uint32_t count = capstore_count(&store);
	uint32_t last = 0;

	for (uint32_t i = 0; i < count; i++) {
		const capstore_entry_t *e = &store.entries[(store.oldest + i) % CAPSTORE_MAX_RECORDS];
		if (i > 0 && e->seq <= last) {
			return 0;
		}
		last = e->seq;
	}
	return count > 0 && last == next_seq;
}

static void fill_laps(uint32_t per_sector)
{
	memset(flash, ERASED, sizeof(flash));
	next_seq = 0;
	check(capstore_init(&store, &dev) == 0, "store init");
	uint32_t record = store.usable / per_sector / CAPSTORE_WORD_BYTES * CAPSTORE_WORD_BYTES;

	for (uint32_t i = 0; i < LAPS * SECTORS * per_sector; i++) {
		store_record(record - CAPSTORE_HEADER_BYTES);
	}
	check(capstore_count(&store) < LAPS * SECTORS * per_sector, "the ring has wrapped");
}

static void fill_to_sector_end(void)
{
	uint32_t start = capstore_fit_header(&store, store.write_pos);

	store_record(store.usable - start % store.usable - CAPSTORE_HEADER_BYTES);
}

static void check_reboot(const char *case_name)
{
	uint32_t count = capstore_count(&store);
	uint32_t next_start = capstore_fit_header(&store, store.write_pos);
	uint32_t oldest_seq = store.entries[store.oldest].seq;

	check(capstore_init(&store, &dev) == 0, "store reboot");
	if (capstore_count(&store) != count || !index_in_order()) {
		printf("  %s: index after the reboot is wrong\n", case_name);
		check(0, "after a reboot the index holds each capture once, the newest last");
	}
	check(capstore_fit_header(&store, store.write_pos) == next_start,
	      "after a reboot the next capture starts where it would have without the reboot");
	store_record(sector_payload());
	check(capstore_find(&store, next_seq) != NULL && capstore_find(&store, oldest_seq) == NULL,
	      "the next capture pushes out the oldest");
	check(capstore_init(&store, &dev) == 0 && index_in_order(), "and survives the next reboot in order");
}

static void test_newest_ends_at_sector_end(void)
{
	fill_laps(1u);
	check(store.write_pos % store.usable == 0, "the newest record ends exactly at a sector end");
	check_reboot("ends at the sector end");
}

static void test_old_small_records_follow(void)
{
	fill_laps(RECORDS_PER_SECTOR);
	fill_to_sector_end();
	check(store.write_pos % store.usable == 0, "the newest record ends where several old records follow");
	check_reboot("old small records follow");
}

static void test_next_header_does_not_fit(void)
{
	fill_laps(1u);
	store_record(sector_payload() - SHORT_BY);
	check(store.usable - store.write_pos % store.usable < CAPSTORE_HEADER_BYTES,
	      "the newest record leaves less than a header in its sector");
	check_reboot("next header does not fit");
}

int main(void)
{
	test_newest_ends_at_sector_end();
	test_old_small_records_follow();
	test_next_header_does_not_fit();
	printf("CAPSTORE WRAP TESTS: %s\n", failures ? "FAIL" : "PASS");
	return failures ? 1 : 0;
}
