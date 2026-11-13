#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "../src/evlog.h"

#define FLASH_BYTES 32768u
#define SECTOR_BYTES 4096u
#define WORD_BYTES 4u
#define WORDS (FLASH_BYTES / WORD_BYTES)
#define ERASED 0xFFu
#define MAX_WORD_WRITES 2u
#define FEW 10u
#define VICTIM 4u
#define MANY 3000u
#define WRITE_FAULT (-5)

static uint8_t flash[FLASH_BYTES];
static uint8_t word_writes[WORDS];
static int violations;
static int failures;
static bool fail_next_word;
static bool fail_next_read;

static void check(int ok, const char *what)
{
	if (!ok) {
		printf("FAIL: %s\n", what);
		failures++;
	}
}

static int fake_read(uint32_t offset, evlog_record_t *rec)
{
	if (fail_next_read) {
		fail_next_read = false;
		return WRITE_FAULT;
	}
	memcpy(rec, &flash[offset], sizeof(*rec));
	return 0;
}

static void count_writes(uint32_t offset, uint32_t len)
{
	for (uint32_t w = offset / WORD_BYTES; w < (offset + len) / WORD_BYTES; w++) {
		if (++word_writes[w] > MAX_WORD_WRITES) {
			violations++;
		}
	}
}

static int fake_write(uint32_t offset, const evlog_record_t *rec)
{
	for (size_t i = 0; i < sizeof(*rec); i++) {
		if (flash[offset + i] != ERASED) {
			violations++;
			return WRITE_FAULT;
		}
	}
	count_writes(offset, sizeof(*rec));
	memcpy(&flash[offset], rec, sizeof(*rec));
	return 0;
}

static int fake_erase(uint32_t offset)
{
	memset(&flash[offset], ERASED, SECTOR_BYTES);
	memset(&word_writes[offset / WORD_BYTES], 0, SECTOR_BYTES / WORD_BYTES);
	return 0;
}

static int fake_write_word(uint32_t offset, uint32_t word)
{
	uint32_t old;

	if (fail_next_word) {
		fail_next_word = false;
		return WRITE_FAULT;
	}
	memcpy(&old, &flash[offset], sizeof(old));
	if ((word & ~old) != 0u || offset % WORD_BYTES != 0u) {
		violations++;
		return WRITE_FAULT;
	}
	count_writes(offset, WORD_BYTES);
	memcpy(&flash[offset], &word, sizeof(word));
	return 0;
}

static const evlog_flash_t dev = {
	.read = fake_read,
	.write = fake_write,
	.erase_sector = fake_erase,
	.write_word = fake_write_word,
	.size = FLASH_BYTES,
	.sector_size = SECTOR_BYTES,
};

static void fresh_log(evlog_t *log, uint32_t events)
{
	memset(flash, ERASED, sizeof(flash));
	memset(word_writes, 0, sizeof(word_writes));
	check(evlog_init(log, &dev) == 0, "init");
	for (uint32_t seq = 1; seq <= events; seq++) {
		sp_event_t e = {.seq = seq, .t_uptime_ms = seq, .accepted = 1};
		check(evlog_append(log, &e) == 0, "append");
	}
}

static uint32_t count_seen(const evlog_t *log, uint32_t seq)
{
	evlog_cursor_t cur = {.from_seq = 0};
	uint32_t n = 0;

	evlog_rewind(log, &cur);
	while (evlog_next(log, &cur) == 1) {
		check(cur.event.t_uptime_ms == cur.event.seq, "payload intact");
		n += cur.event.seq == seq ? 1u : 0u;
	}
	return n;
}

static uint32_t total_seen(const evlog_t *log)
{
	evlog_cursor_t cur = {.from_seq = 0};
	uint32_t n = 0;

	evlog_rewind(log, &cur);
	while (evlog_next(log, &cur) == 1) {
		n++;
	}
	return n;
}

static void test_delete_one(void)
{
	evlog_t log;

	fresh_log(&log, FEW);
	check(evlog_delete(&log, VICTIM) == 1, "delete finds the shot");
	check(count_seen(&log, VICTIM) == 0u, "deleted shot is not replayed");
	check(total_seen(&log) == FEW - 1u, "the other shots stay");
	check(evlog_delete(&log, VICTIM) == 0, "second delete is a no-op");
	check(evlog_delete(&log, FEW + 1u) == 0, "unknown seq is a no-op");
	check(evlog_init(&log, &dev) == 0 && count_seen(&log, VICTIM) == 0u, "delete survives a reboot");
}

static void test_delete_newest(void)
{
	evlog_t log;
	sp_event_t next = {.seq = FEW + 1u, .t_uptime_ms = FEW + 1u, .accepted = 1};

	fresh_log(&log, FEW);
	check(evlog_delete(&log, FEW) == 1, "delete the newest");
	check(evlog_init(&log, &dev) == 0, "reboot");
	check(log.newest_seq == FEW, "a deleted newest shot still sets the newest seq");
	check(evlog_append(&log, &next) == 0, "append after the deleted newest");
	check(count_seen(&log, FEW + 1u) == 1u && total_seen(&log) == FEW, "log continues after it");
}

static void test_delete_then_wrap(void)
{
	evlog_t log;

	fresh_log(&log, FEW);
	check(evlog_delete(&log, VICTIM) == 1, "delete before the wrap");
	for (uint32_t seq = FEW + 1u; seq <= MANY; seq++) {
		sp_event_t e = {.seq = seq, .t_uptime_ms = seq, .accepted = 1};
		check(evlog_append(&log, &e) == 0, "append over deleted records");
	}
	check(count_seen(&log, MANY) == 1u, "newest shot after the wrap");
	check(total_seen(&log) <= log.slots, "wrapped log stays bounded");
}

static void test_write_fault(void)
{
	evlog_t log;

	fresh_log(&log, FEW);
	fail_next_word = true;
	check(evlog_delete(&log, VICTIM) == WRITE_FAULT, "a failed write is reported");
	check(count_seen(&log, VICTIM) == 1u, "the shot stays after a failed delete");
	fail_next_read = true;
	check(evlog_delete(&log, VICTIM) == WRITE_FAULT, "a failed read is reported");
	check(count_seen(&log, VICTIM) == 1u, "the shot stays after a failed read");
}

int main(void)
{
	test_delete_one();
	test_delete_newest();
	test_delete_then_wrap();
	test_write_fault();
	check(violations == 0, "only clears bits, at most two writes per word");
	printf("EVLOG DELETE TESTS: %s\n", failures ? "FAIL" : "PASS");
	return failures ? 1 : 0;
}
