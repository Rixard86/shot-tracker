#include <stdio.h>
#include <string.h>

#include "../src/capstore.h"

#define SECTOR_BYTES 1024u
#define SECTORS 16u
#define FLASH_BYTES (SECTOR_BYTES * SECTORS)
#define ERASED 0xFFu
#define MAX_SEQ 4000u
#define MIN_LEN 50u
#define LEN_SPAN 2450u
#define CHUNK_SPAN 97u
#define PATTERN_SEQ 31u
#define PATTERN_POS 7u
#define WRAP_RECORDS 300u
#define REBOOT_EVERY 17u
#define CUT_TRIALS 400u
#define TINY_LEN 4u
#define TINY_RECORDS 400u
#define FAULT (-5)
#define NO_BUDGET (-1)
#define LCG_MUL 1103515245u
#define LCG_ADD 12345u
#define LCG_SHIFT 16u
#define FIRST_FEW 3u
#define FEW_LEN_STEP 700u

static uint8_t flash[FLASH_BYTES];
static uint32_t lengths[MAX_SEQ];
static int32_t write_budget = NO_BUDGET;
static uint32_t rng = 7;
static int violations, failures;
static capstore_t store;

static void check(int ok, const char *what)
{
	if (!ok) {
		printf("FAIL: %s\n", what);
		failures++;
	}
}

static uint32_t next_random(void)
{
	rng = rng * LCG_MUL + LCG_ADD;
	return rng >> LCG_SHIFT;
}

static int fake_read(const capstore_span_t *span)
{
	memcpy(span->data, &flash[span->offset], span->len);
	return 0;
}

static int fake_write(const capstore_wspan_t *span)
{
	int cut = write_budget >= 0 && (int32_t)span->len > write_budget;
	uint32_t len = cut ? (uint32_t)write_budget : span->len;

	write_budget -= write_budget >= 0 ? (int32_t)len : 0;
	for (uint32_t i = 0; i < len; i++) {
		violations += flash[span->offset + i] != ERASED;
		flash[span->offset + i] &= span->data[i];
	}
	return cut ? FAULT : 0;
}

static int fake_erase(uint32_t offset)
{
	memset(&flash[offset], ERASED, SECTOR_BYTES);
	return 0;
}

static const capstore_flash_t dev = {fake_read, fake_write, fake_erase, FLASH_BYTES, SECTOR_BYTES};

static uint8_t pattern(uint32_t seq, uint32_t pos)
{
	return (uint8_t)(seq * PATTERN_SEQ + pos * PATTERN_POS);
}

static void boot(void)
{
	check(capstore_init(&store, &dev) == 0, "init");
}

static int write_payload(uint32_t seq, uint32_t len)
{
	uint8_t buf[CHUNK_SPAN];
	uint32_t done = 0;

	while (done < len) {
		uint32_t n = 1u + next_random() % CHUNK_SPAN;
		n = n < len - done ? n : len - done;
		for (uint32_t i = 0; i < n; i++) {
			buf[i] = pattern(seq, done + i);
		}
		capstore_chunk_t chunk = {buf, n};
		int err = capstore_write(&store, &chunk);
		if (err) {
			return err;
		}
		done += n;
	}
	return 0;
}

static int record(uint32_t seq, uint32_t len)
{
	capstore_entry_t h = {.seq = seq, .n_samples = (uint16_t)seq, .trigger_index = (uint16_t)(seq / 2u)};
	int err = capstore_begin(&store, &h);

	lengths[seq] = len;
	err = err ? err : write_payload(seq, len);
	return err ? err : capstore_commit(&store);
}

static int payload_ok(const capstore_entry_t *e)
{
	uint8_t buf[CHUNK_SPAN];

	if (e->payload_len != lengths[e->seq] || e->n_samples != (uint16_t)e->seq) {
		return 0;
	}
	for (uint32_t off = 0; off < e->payload_len; off += CHUNK_SPAN) {
		uint32_t left = e->payload_len - off;
		capstore_read_t req = {e->seq, off, buf, left < CHUNK_SPAN ? left : CHUNK_SPAN};
		if (capstore_read(&store, &req) != 0) {
			return 0;
		}
		for (uint32_t i = 0; i < req.len; i++) {
			if (buf[i] != pattern(e->seq, off + i)) {
				return 0;
			}
		}
	}
	return 1;
}

static int index_ok(void)
{
	uint32_t prev = 0;

	for (uint32_t i = 0; i < store.count; i++) {
		const capstore_entry_t *e = &store.entries[(store.oldest + i) % CAPSTORE_MAX_RECORDS];
		if ((i > 0 && e->seq <= prev) || !payload_ok(e)) {
			return 0;
		}
		prev = e->seq;
	}
	return 1;
}

static uint32_t random_len(void)
{
	return MIN_LEN + next_random() % LEN_SPAN;
}

static void wipe(void)
{
	memset(flash, ERASED, sizeof(flash));
	memset(lengths, 0, sizeof(lengths));
	violations = 0;
	boot();
}

static void test_basic(void)
{
	wipe();
	check(capstore_count(&store) == 0, "fresh store is empty");
	for (uint32_t seq = 1; seq <= FIRST_FEW; seq++) {
		check(record(seq, seq * FEW_LEN_STEP) == 0, "record written");
	}
	check(capstore_count(&store) == FIRST_FEW && index_ok(), "records read back");
	boot();
	check(capstore_count(&store) == FIRST_FEW && index_ok(), "records survive a reboot");
	check(capstore_find(&store, FIRST_FEW + 1u) == NULL, "unknown seq not found");
	check(violations == 0, "no programming of written flash");
}

static void test_wrap(void)
{
	wipe();
	for (uint32_t seq = 1; seq <= WRAP_RECORDS; seq++) {
		check(record(seq, random_len()) == 0, "wrap record written");
		if (seq % REBOOT_EVERY == 0) {
			uint32_t before = capstore_count(&store);
			boot();
			check(capstore_count(&store) == before, "reboot keeps every indexed record");
		}
	}
	check(index_ok(), "wrapped records read back");
	check(capstore_find(&store, WRAP_RECORDS) != NULL, "newest record kept");
	check(capstore_find(&store, 1) == NULL, "oldest record evicted");
	check(violations == 0, "wrap never programs written flash");
}

static void test_abort(void)
{
	capstore_entry_t h = {.seq = 1, .n_samples = 1};

	wipe();
	check(capstore_begin(&store, &h) == 0 && write_payload(1, FEW_LEN_STEP) == 0, "void record written");
	check(capstore_abort(&store) == 0, "abort");
	check(record(2, FEW_LEN_STEP) == 0, "record after abort");
	boot();
	check(capstore_find(&store, 1) == NULL, "aborted record hidden");
	check(capstore_find(&store, 2) != NULL && index_ok(), "chain continues past an aborted record");
	check(violations == 0, "abort never programs written flash");
}

static void test_power_cuts(void)
{
	uint32_t seq = 1;

	wipe();
	for (uint32_t trial = 0; trial < CUT_TRIALS; trial++) {
		check(record(seq++, random_len()) == 0, "record before cut");
		write_budget = (int32_t)(next_random() % (LEN_SPAN + CHUNK_SPAN));
		record(seq++, random_len());
		write_budget = NO_BUDGET;
		violations = 0;
		boot();
		check(index_ok(), "committed records intact after a power cut");
		check(record(seq++, random_len()) == 0 && violations == 0, "writing resumes cleanly after a cut");
		check(capstore_find(&store, seq - 1u) != NULL, "record after cut kept");
	}
}

static void test_index_overflow(void)
{
	wipe();
	for (uint32_t seq = 1; seq <= TINY_RECORDS; seq++) {
		record(seq, TINY_LEN);
	}
	check(capstore_count(&store) <= CAPSTORE_MAX_RECORDS, "index capped");
	check(capstore_find(&store, TINY_RECORDS) != NULL && index_ok(), "newest tiny record kept");
}

int main(void)
{
	test_basic();
	test_wrap();
	test_abort();
	test_power_cuts();
	test_index_overflow();
	printf("CAPSTORE TESTS: %s\n", failures ? "FAIL" : "PASS");
	return failures ? 1 : 0;
}
