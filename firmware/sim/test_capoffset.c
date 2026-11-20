#include <stdio.h>
#include <string.h>

#include "../src/capstore_priv.h"
#include "../src/crc32.h"
#include "cap_harness.h"

#define SHOT_TRIGGER 9600u
#define ULTRA_TRIGGER 20003u
#define CRC_INIT 0xFFFFFFFFu
#define MAX_BODY 65536u
#define TRAILER_BYTES 12u
#define TRAILER_SAMPLES_AT 4u
#define TRAILER_TRIGGER_AT 6u
#define TRAILER_LENGTH_AT 8u
#define OLD_SEQ 7u
#define OLD_SAMPLES 720u
#define OLD_TRIGGER 480u
#define OLD_PAYLOAD_BYTES 40u
#define PAYLOAD_STRIDE 7u

typedef struct {
	const capstore_entry_t *entry;
	uint32_t magic;
	uint32_t first;
} expected_t;

static const sp_gyro_offset_t harness_offset = HARNESS_OFFSET;
static uint8_t body[MAX_BODY];
static int failures;

static void check(int ok, const char *what)
{
	if (!ok) {
		printf("FAIL: %s\n", what);
		failures++;
	}
}

static uint32_t magic_of(const capstore_entry_t *e)
{
	capstore_at_t at = {.pos = e->start};

	capstore_read_at(harness_store(), &at);
	return at.header.magic;
}

static uint32_t app_check(const capstore_entry_t *e)
{
	uint32_t len = e->payload_len - CAPSTORE_OFFSET_BYTES;
	capstore_read_t req = {e->seq, CAPSTORE_OFFSET_BYTES, body, len};
	uint8_t trailer[TRAILER_BYTES];

	capstore_read(harness_store(), &req);
	memcpy(trailer, &e->seq, sizeof(e->seq));
	memcpy(trailer + TRAILER_SAMPLES_AT, &e->n_samples, sizeof(e->n_samples));
	memcpy(trailer + TRAILER_TRIGGER_AT, &e->trigger_index, sizeof(e->trigger_index));
	memcpy(trailer + TRAILER_LENGTH_AT, &len, sizeof(len));
	crc32_bytes_t data = {body, len};
	crc32_bytes_t tail = {trailer, sizeof(trailer)};
	return ~crc32_step(crc32_step(CRC_INIT, &data), &tail);
}

static void check_record(const expected_t *want)
{
	const capstore_entry_t *e = want->entry;
	sp_gyro_offset_t stored;
	capstore_read_t req = {e ? e->seq : 0u, 0u, (uint8_t *)&stored, sizeof(stored)};
	capstore_check_t sent = {.seq = e ? e->seq : 0u};

	check(e && e->gyro_offset && capstore_body_start(e) == CAPSTORE_OFFSET_BYTES, "a new capture carries the offset");
	if (!e) {
		return;
	}
	check(magic_of(e) == want->magic, "the record magic marks the offset");
	check(capstore_read(harness_store(), &req) == 0 && memcmp(&stored, &harness_offset, sizeof(stored)) == 0,
	      "the first 8 payload bytes are the offset");
	check(capstore_body_check(harness_store(), &sent) == 0 && sent.check == app_check(e),
	      "the CAPREC check covers only the bytes sent, as the app computes it");
	harness_want_t samples = harness_want(e, want->first);
	check(harness_matches(&samples), "the samples after the offset decode");
}

static void test_new_records(void)
{
	harness_setup(HARNESS_BOW);
	expected_t standard = {harness_shoot(SHOT_TRIGGER, SP_CAPTURE_STANDARD), CAPSTORE_OFFSET_RECORD_MAGIC,
			       SHOT_TRIGGER - CAP_STANDARD_PRE};
	check_record(&standard);
	expected_t extended = {harness_shoot(2u * SHOT_TRIGGER, SP_CAPTURE_EXTENDED), CAPSTORE_OFFSET_AIM_AVERAGED_MAGIC,
			       2u * SHOT_TRIGGER - CAP_EXTENDED_PRE};
	check_record(&extended);
	uint32_t ultra_at = 2u * SHOT_TRIGGER + ULTRA_TRIGGER;
	uint32_t ultra_first = (ultra_at - CAP_ULTRA_PRE) / CAP_HISTORY_GROUP * CAP_HISTORY_GROUP;
	expected_t ultra = {harness_shoot(ultra_at, SP_CAPTURE_ULTRA), CAPSTORE_OFFSET_ULTRA_MAGIC, ultra_first};
	uint32_t ultra_seq = ultra.entry ? ultra.entry->seq : 0u;
	check_record(&ultra);
	check(harness_reboot() == 0, "store re-scans");
	ultra.entry = capstore_find(harness_store(), ultra_seq);
	check_record(&ultra);
}

static void test_old_record(void)
{
	uint8_t payload[OLD_PAYLOAD_BYTES];
	capstore_entry_t header = {.seq = OLD_SEQ, .n_samples = OLD_SAMPLES, .trigger_index = OLD_TRIGGER};
	capstore_chunk_t chunk = {payload, sizeof(payload)};
	capstore_check_t stored = {.seq = OLD_SEQ};
	capstore_check_t sent = {.seq = OLD_SEQ};

	for (uint32_t i = 0; i < sizeof(payload); i++) {
		payload[i] = (uint8_t)(i * PAYLOAD_STRIDE);
	}
	harness_setup(HARNESS_BOW);
	check(capstore_begin(harness_store(), &header) == 0 && capstore_write(harness_store(), &chunk) == 0 &&
	      capstore_commit(harness_store()) == 0, "a 0.3.15-style record is written");
	check(harness_reboot() == 0, "store re-scans");
	const capstore_entry_t *e = capstore_find(harness_store(), OLD_SEQ);
	check(e && !e->gyro_offset && capstore_body_start(e) == 0u && magic_of(e) == CAPSTORE_RECORD_MAGIC,
	      "a record without the offset keeps its old magic and body");
	check(capstore_read_check(harness_store(), &stored) == 0 && capstore_body_check(harness_store(), &sent) == 0 &&
	      sent.check == stored.check, "its CAPREC check is the stored one");
}

int main(void)
{
	test_new_records();
	test_old_record();
	printf("CAPOFFSET TESTS: %s\n", failures ? "FAIL" : "PASS");
	return failures ? 1 : 0;
}
