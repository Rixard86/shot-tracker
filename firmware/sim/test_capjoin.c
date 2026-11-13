#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "../src/capjoin.h"
#include "../src/capstore.h"

#define SECTOR_BYTES 1024u
#define MAIN_SECTORS 8u
#define SPARE_SECTORS 6u
#define MAIN_BYTES (SECTOR_BYTES * MAIN_SECTORS)
#define SPARE_BYTES (SECTOR_BYTES * SPARE_SECTORS)
#define ERASED 0xFFu
#define RECORD_LEN 700u
#define FILL_RECORDS 60u
#define MORE_RECORDS 25u
#define PATTERN_SEQ 31u
#define PATTERN_POS 7u
#define IMAGE_STEP 13u
#define CROSS_BACK 10u
#define CROSS_LEN 30u
#define BUSY (-16)

static uint8_t main_flash[MAIN_BYTES];
static uint8_t spare_flash[SPARE_BYTES];
static bool claimed;
static int violations, failures;
static capstore_t store;
static uint32_t next_seq;

static void check(int ok, const char *what)
{
	if (!ok) {
		printf("FAIL: %s\n", what);
		failures++;
	}
}

static capjoin_piece_t piece_of(uint32_t offset, uint32_t len)
{
	capjoin_req_t req = {MAIN_BYTES, offset, len};

	return capjoin_piece(&req);
}

static uint8_t *backing(const capjoin_piece_t *p)
{
	return p->spare ? &spare_flash[p->local] : &main_flash[p->local];
}

static int fake_read(const capstore_span_t *span)
{
	for (uint32_t done = 0; done < span->len;) {
		capjoin_piece_t p = piece_of(span->offset + done, span->len - done);
		if (p.spare && claimed) {
			return BUSY;
		}
		memcpy(span->data + done, backing(&p), p.len);
		done += p.len;
	}
	return 0;
}

static int fake_write(const capstore_wspan_t *span)
{
	for (uint32_t done = 0; done < span->len;) {
		capjoin_piece_t p = piece_of(span->offset + done, span->len - done);
		uint8_t *dst = backing(&p);
		if (p.spare && claimed) {
			return BUSY;
		}
		for (uint32_t i = 0; i < p.len; i++) {
			violations += dst[i] != ERASED;
			dst[i] &= span->data[done + i];
		}
		done += p.len;
	}
	return 0;
}

static int fake_erase(uint32_t offset)
{
	capjoin_piece_t p = piece_of(offset, SECTOR_BYTES);

	if (p.spare && claimed) {
		return BUSY;
	}
	memset(backing(&p), ERASED, SECTOR_BYTES);
	return 0;
}

static capstore_flash_t dev = {fake_read, fake_write, fake_erase, MAIN_BYTES, SECTOR_BYTES};

static uint8_t pattern(uint32_t seq, uint32_t pos)
{
	return (uint8_t)(seq * PATTERN_SEQ + pos * PATTERN_POS);
}

static void open_store(bool with_spare)
{
	dev.size = MAIN_BYTES + (with_spare ? SPARE_BYTES : 0u);
	check(capstore_init(&store, &dev) == 0, "init");
}

static int put(void)
{
	uint8_t buf[RECORD_LEN];
	capstore_entry_t h = {.seq = next_seq++, .n_samples = 1};
	capstore_chunk_t chunk = {buf, RECORD_LEN};

	for (uint32_t i = 0; i < RECORD_LEN; i++) {
		buf[i] = pattern(h.seq, i);
	}
	int err = capstore_begin(&store, &h);
	err = err ? err : capstore_write(&store, &chunk);
	if (err) {
		capstore_abort(&store);
		return err;
	}
	return capstore_commit(&store);
}

static uint32_t fill(uint32_t n)
{
	uint32_t failed = 0;

	for (uint32_t i = 0; i < n; i++) {
		failed += put() != 0;
	}
	return failed;
}

static bool intact(const capstore_entry_t *e)
{
	uint8_t buf[RECORD_LEN];
	capstore_read_t r = {e->seq, 0, buf, RECORD_LEN};

	if (e->payload_len != RECORD_LEN || capstore_read(&store, &r) != 0) {
		return false;
	}
	for (uint32_t i = 0; i < RECORD_LEN; i++) {
		if (buf[i] != pattern(e->seq, i)) {
			return false;
		}
	}
	return true;
}

static bool all_intact(void)
{
	for (uint32_t i = 0; i < store.count; i++) {
		if (!intact(&store.entries[(store.oldest + i) % CAPSTORE_MAX_RECORDS])) {
			return false;
		}
	}
	return true;
}

static void wipe(void)
{
	memset(main_flash, ERASED, sizeof(main_flash));
	memset(spare_flash, ERASED, sizeof(spare_flash));
	claimed = false;
	next_seq = 1u;
}

static uint32_t capacity(bool with_spare)
{
	wipe();
	open_store(with_spare);
	check(fill(FILL_RECORDS) == 0u, "every capture stored");
	check(all_intact(), "captures read back intact");
	return capstore_count(&store);
}

static void test_pieces(void)
{
	capjoin_piece_t in_main = piece_of(MAIN_BYTES - CROSS_BACK, CROSS_LEN);
	capjoin_piece_t in_spare = piece_of(MAIN_BYTES + CROSS_LEN, CROSS_BACK);

	check(!in_main.spare && in_main.local == MAIN_BYTES - CROSS_BACK && in_main.len == CROSS_BACK,
	      "a span across the join is cut at the end of the main area");
	check(in_spare.spare && in_spare.local == CROSS_LEN && in_spare.len == CROSS_BACK,
	      "offsets past the main area land in the spare area");
	check(piece_of(MAIN_BYTES, CROSS_LEN).spare, "the join itself belongs to the spare area");
}

static void test_shrink_and_grow(uint32_t main_only)
{
	wipe();
	open_store(true);
	fill(FILL_RECORDS);
	claimed = true;
	put();
	open_store(false);
	check(all_intact(), "after the claim: every indexed capture is in the main area");
	check(fill(MORE_RECORDS) == 0u && all_intact(), "captures keep working without slot 1");
	for (uint32_t i = 0; i < SPARE_BYTES; i++) {
		spare_flash[i] = (uint8_t)(i * IMAGE_STEP);
	}
	claimed = false;
	open_store(true);
	check(all_intact() && capstore_find(&store, next_seq - 1u) != NULL,
	      "slot 1 full of image bytes: the main captures are all still there");
	check(fill(FILL_RECORDS) == 0u && all_intact(), "captures move back into slot 1");
	check(capstore_count(&store) > main_only, "the extra room is back");
}

int main(void)
{
	test_pieces();
	uint32_t main_only = capacity(false);
	uint32_t joined = capacity(true);
	check(joined > main_only, "slot 1 adds room");
	test_shrink_and_grow(main_only);
	check(violations == 0, "never writes over unerased flash");
	printf("CAPJOIN TESTS: %s (%u captures without slot 1, %u with)\n", failures ? "FAIL" : "PASS",
	       (unsigned)main_only, (unsigned)joined);
	return failures ? 1 : 0;
}
