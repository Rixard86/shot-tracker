#include <stdio.h>
#include <string.h>

#include "../src/evlog.h"

#define FLASH_BYTES 32768u
#define SECTOR_BYTES 4096u
#define ERASED 0xFFu
#define FEW 10u
#define MANY 3000u
#define REBOOT_EVERY 250u
#define TAIL 11u
#define TORN_AFTER 5u
#define TORN_BYTES 12u
#define PEEK 3u
#define APPEND_DURING_READ 5u
#define WRITE_FAULT (-5)

static uint8_t flash[FLASH_BYTES];
static int violations;
static int failures;

static int fake_read(uint32_t offset, evlog_record_t *rec)
{
	memcpy(rec, &flash[offset], sizeof(*rec));
	return 0;
}

static int program(uint32_t offset, const uint8_t *src, size_t len)
{
	for (size_t i = 0; i < len; i++) {
		if (flash[offset + i] != ERASED) {
			violations++;
			return WRITE_FAULT;
		}
	}
	memcpy(&flash[offset], src, len);
	return 0;
}

static int fake_write(uint32_t offset, const evlog_record_t *rec)
{
	return program(offset, (const uint8_t *)rec, sizeof(*rec));
}

static int fake_erase(uint32_t offset)
{
	memset(&flash[offset], ERASED, SECTOR_BYTES);
	return 0;
}

static const evlog_flash_t dev = {
	.read = fake_read,
	.write = fake_write,
	.erase_sector = fake_erase,
	.size = FLASH_BYTES,
	.sector_size = SECTOR_BYTES,
};

static void check(int ok, const char *what)
{
	if (!ok) {
		printf("FAIL: %s\n", what);
		failures++;
	}
}

static void boot(evlog_t *log)
{
	check(evlog_init(log, &dev) == 0, "init");
}

static void append_seq(evlog_t *log, uint32_t seq)
{
	sp_event_t e = {.seq = seq, .t_uptime_ms = seq, .accepted = 1};

	check(evlog_append(log, &e) == 0, "append");
}

static uint32_t scan(const evlog_t *log, uint32_t from, uint32_t *first)
{
	evlog_cursor_t cur = {.from_seq = from};
	uint32_t n = 0;
	uint32_t prev = 0;

	evlog_rewind(log, &cur);
	while (evlog_next(log, &cur) == 1) {
		check(n == 0 || cur.event.seq == prev + 1u, "events come oldest first, no gaps");
		check(cur.event.t_uptime_ms == cur.event.seq, "event payload intact");
		*first = n == 0 ? cur.event.seq : *first;
		prev = cur.event.seq;
		n++;
	}
	return n;
}

static void test_fresh_and_reboot(void)
{
	evlog_t log;
	uint32_t first = 0;

	memset(flash, ERASED, sizeof(flash));
	boot(&log);
	check(scan(&log, 0, &first) == 0, "fresh log is empty");
	for (uint32_t seq = 1; seq <= FEW; seq++) {
		append_seq(&log, seq);
	}
	boot(&log);
	check(scan(&log, 0, &first) == FEW && first == 1, "events survive a reboot");
	check(log.head == FEW, "head found after reboot");
}

static void test_wrap(void)
{
	evlog_t log;
	uint32_t first = 0;
	uint32_t per = SECTOR_BYTES / sizeof(evlog_record_t);
	uint32_t sectors = FLASH_BYTES / SECTOR_BYTES;

	memset(flash, ERASED, sizeof(flash));
	boot(&log);
	for (uint32_t seq = 1; seq <= MANY; seq++) {
		append_seq(&log, seq);
		if (seq % REBOOT_EVERY == 0) {
			boot(&log);
		}
	}
	uint32_t n = scan(&log, 0, &first);
	check(n >= (sectors - 1u) * per && n <= sectors * per, "keeps at least all-but-one sector");
	check(first + n - 1u == MANY, "newest event kept");
	check(scan(&log, MANY - TAIL + 1u, &first) == TAIL, "from_seq filters");
	printf("wrap: %u of %u events kept (%u per sector)\n", n, MANY, per);
}

static void test_torn_write(void)
{
	evlog_t log;
	uint32_t first = 0;
	evlog_record_t torn;

	memset(flash, ERASED, sizeof(flash));
	boot(&log);
	for (uint32_t seq = 1; seq <= TORN_AFTER; seq++) {
		append_seq(&log, seq);
	}
	memset(&torn, 0, sizeof(torn));
	program(TORN_AFTER * sizeof(evlog_record_t), (const uint8_t *)&torn, TORN_BYTES);
	boot(&log);
	append_seq(&log, TORN_AFTER + 1u);
	check(scan(&log, 0, &first) == TORN_AFTER + 1u, "torn record skipped, log continues");
}

static void test_torn_erase(void)
{
	evlog_t log;
	uint32_t first = 0;

	uint32_t per = SECTOR_BYTES / sizeof(evlog_record_t);
	uint32_t seq = 0;

	memset(flash, ERASED, sizeof(flash));
	boot(&log);
	while (seq < MANY || log.head % per != 0) {
		append_seq(&log, ++seq);
	}
	memset(&flash[log.head * sizeof(evlog_record_t)], ERASED, SECTOR_BYTES / 2u);
	boot(&log);
	check(scan(&log, seq, &first) == 1u && first == seq, "newest event survives a torn erase");
	for (uint32_t k = 1; k <= REBOOT_EVERY; k++) {
		append_seq(&log, seq + k);
	}
	check(scan(&log, seq + 1u, &first) == REBOOT_EVERY, "log continues after a torn erase");
}

static void test_append_while_reading(void)
{
	evlog_t log;
	evlog_cursor_t cur = {.from_seq = 0};
	uint32_t n = 0;

	memset(flash, ERASED, sizeof(flash));
	boot(&log);
	for (uint32_t seq = 1; seq <= MANY; seq++) {
		append_seq(&log, seq);
	}
	evlog_rewind(&log, &cur);
	while (evlog_next(&log, &cur) == 1) {
		if (++n == PEEK) {
			for (uint32_t k = 1; k <= APPEND_DURING_READ; k++) {
				append_seq(&log, MANY + k);
			}
		}
	}
	check(n > PEEK && n <= log.slots, "iteration ends while appending");
}

int main(void)
{
	test_fresh_and_reboot();
	test_wrap();
	test_torn_write();
	test_torn_erase();
	test_append_while_reading();
	check(violations == 0, "never writes over unerased flash");
	printf("EVLOG TESTS: %s\n", failures ? "FAIL" : "PASS");
	return failures ? 1 : 0;
}
