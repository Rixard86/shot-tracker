#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "../src/lifecount.h"

#define SECTOR_BYTES 4096u
#define FLASH_BYTES (LIFECOUNT_SECTORS * SECTOR_BYTES)
#define PER_SECTOR (SECTOR_BYTES / sizeof(lifecount_record_t))
#define ERASED 0xFFu
#define START_SEQ 5000u
#define FEW 10u
#define MANY 3000u
#define REBOOT_EVERY 97u
#define TORN_BYTES 8u
#define FAULT (-5)

static uint8_t flash[FLASH_BYTES];
static int violations;
static int failures;
static uint32_t erases;
static bool fail_next_write;
static bool fail_next_erase;

static void check(int ok, const char *what)
{
	if (!ok) {
		printf("FAIL: %s\n", what);
		failures++;
	}
}

static int fake_read(uint32_t offset, lifecount_record_t *rec)
{
	memcpy(rec, &flash[offset], sizeof(*rec));
	return 0;
}

static int program(uint32_t offset, const void *src, size_t len)
{
	for (size_t i = 0; i < len; i++) {
		if (flash[offset + i] != ERASED) {
			violations++;
			return FAULT;
		}
	}
	memcpy(&flash[offset], src, len);
	return 0;
}

static int fake_write(uint32_t offset, const lifecount_record_t *rec)
{
	if (fail_next_write) {
		fail_next_write = false;
		return FAULT;
	}
	return program(offset, rec, sizeof(*rec));
}

static int fake_erase(uint32_t offset)
{
	if (fail_next_erase) {
		fail_next_erase = false;
		return FAULT;
	}
	memset(&flash[offset], ERASED, SECTOR_BYTES);
	erases++;
	return 0;
}

static const lifecount_flash_t dev = {
	.read = fake_read,
	.write = fake_write,
	.erase_sector = fake_erase,
	.sector_size = SECTOR_BYTES,
};

static void boot(lifecount_t *lc)
{
	check(lifecount_init(lc, &dev) == 0, "init");
}

static void fresh_started(lifecount_t *lc)
{
	memset(flash, ERASED, sizeof(flash));
	erases = 0u;
	boot(lc);
	check(lifecount_start(lc, START_SEQ) == 0, "start");
}

static void shots(lifecount_t *lc, uint32_t n)
{
	for (uint32_t i = 0; i < n; i++) {
		check(lifecount_shot(lc) == 0, "shot");
	}
}

static void test_blank_and_start(void)
{
	lifecount_t lc;

	memset(flash, ERASED, sizeof(flash));
	boot(&lc);
	check(!lc.started && lifecount_value(&lc) == 0u, "blank area: not started, zero");
	check(lifecount_shot(&lc) == LIFECOUNT_ERR_STATE, "no shot before the start");
	check(lifecount_start(&lc, START_SEQ) == 0 && lifecount_value(&lc) == 0u, "starts at zero");
	boot(&lc);
	check(lc.started && lc.now.start_seq == START_SEQ, "start survives a reboot");
	check(lifecount_start(&lc, START_SEQ + FEW) == 0 && lc.now.start_seq == START_SEQ,
	      "a second start changes nothing");
}

static void test_count_and_unshot(void)
{
	lifecount_t lc;

	fresh_started(&lc);
	shots(&lc, FEW);
	boot(&lc);
	check(lifecount_value(&lc) == FEW, "count survives a reboot");
	check(lifecount_unshot(&lc, START_SEQ) == 0 && lifecount_value(&lc) == FEW,
	      "a shot from before the start does not lower it");
	check(lifecount_unshot(&lc, START_SEQ + 1u) == 1 && lifecount_value(&lc) == FEW - 1u,
	      "a deleted shot lowers it");
	boot(&lc);
	check(lifecount_value(&lc) == FEW - 1u, "lowered count survives a reboot");

	fresh_started(&lc);
	check(lifecount_unshot(&lc, START_SEQ + 1u) == 0 && lifecount_value(&lc) == 0u, "never below zero");
}

static void test_many_with_reboots(void)
{
	lifecount_t lc;

	fresh_started(&lc);
	for (uint32_t i = 1; i <= MANY; i++) {
		check(lifecount_shot(&lc) == 0, "shot");
		if (i % REBOOT_EVERY == 0u) {
			boot(&lc);
		}
	}
	boot(&lc);
	check(lifecount_value(&lc) == MANY, "count over many sector switches");
	check(erases <= MANY / PER_SECTOR + LIFECOUNT_SECTORS, "one erase per sector of records");
}

static void test_torn_write(void)
{
	lifecount_t lc;
	lifecount_record_t torn = {.count = FEW + FEW, .serial = FEW + FEW};

	fresh_started(&lc);
	shots(&lc, FEW);
	program(lc.next_slot * sizeof(lifecount_record_t), &torn, TORN_BYTES);
	boot(&lc);
	check(lifecount_value(&lc) == FEW, "a torn record is ignored");
	shots(&lc, 1u);
	boot(&lc);
	check(lifecount_value(&lc) == FEW + 1u, "counting continues after a torn record");
}

static void test_power_cut_in_erase(void)
{
	lifecount_t lc;
	uint32_t both_full = LIFECOUNT_SECTORS * PER_SECTOR - 1u;

	fresh_started(&lc);
	shots(&lc, both_full);
	memset(flash, ERASED, SECTOR_BYTES / LIFECOUNT_SECTORS);
	boot(&lc);
	check(lifecount_value(&lc) == both_full, "half-erased old sector: newest record still wins");
	fake_erase(0u);
	boot(&lc);
	check(lifecount_value(&lc) == both_full, "erased old sector: the full one keeps the count");
	shots(&lc, 1u);
	boot(&lc);
	check(lifecount_value(&lc) == both_full + 1u, "counting wraps into the erased sector");
}

static void test_faults(void)
{
	lifecount_t lc;

	fresh_started(&lc);
	shots(&lc, FEW);
	fail_next_write = true;
	check(lifecount_shot(&lc) == FAULT && lifecount_value(&lc) == FEW, "failed write keeps the count");
	shots(&lc, 1u);
	boot(&lc);
	check(lifecount_value(&lc) == FEW + 1u, "next write lands after the failed slot");
	shots(&lc, PER_SECTOR - FEW - 3u);
	fail_next_erase = true;
	check(lifecount_shot(&lc) == FAULT && lifecount_value(&lc) == PER_SECTOR - 2u,
	      "failed erase at the sector end is reported");
	shots(&lc, 1u);
	boot(&lc);
	check(lifecount_value(&lc) == PER_SECTOR - 1u, "erase retried on the next shot");
	fail_next_write = true;
	check(lifecount_unshot(&lc, START_SEQ + 1u) == FAULT, "failed write on a delete is reported");
}

static void test_corrupt_records(void)
{
	lifecount_t lc;
	uint32_t newest;

	fresh_started(&lc);
	shots(&lc, FEW);
	newest = (lc.next_slot - 1u) * sizeof(lifecount_record_t);
	flash[newest + offsetof(lifecount_record_t, start_seq)] &= (uint8_t)~START_SEQ;
	boot(&lc);
	check(lifecount_value(&lc) == FEW - 1u, "a record with a damaged start seq is ignored");

	lifecount_record_t forged = {.count = MANY, .serial = FEW + FEW, .start_seq = START_SEQ};
	forged.check = LIFECOUNT_MAGIC ^ forged.count ^ forged.serial ^ forged.start_seq;
	program(lc.next_slot * sizeof(lifecount_record_t), &forged, sizeof(forged));
	boot(&lc);
	check(lifecount_value(&lc) == FEW - 1u, "a count above its serial is ignored");
}

int main(void)
{
	test_blank_and_start();
	test_count_and_unshot();
	test_many_with_reboots();
	test_torn_write();
	test_power_cut_in_erase();
	test_faults();
	test_corrupt_records();
	check(violations == 0, "never writes over unerased flash");
	printf("LIFECOUNT TESTS: %s\n", failures ? "FAIL" : "PASS");
	return failures ? 1 : 0;
}
