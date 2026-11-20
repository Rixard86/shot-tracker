#include <stdio.h>
#include <string.h>

#include "../src/devrec.h"

#define SECTOR_BYTES 4096u
#define FLASH_BYTES 0x66000u
#define ERASED 0xFFu
#define BATCH 26u
#define WARM_UP 1000u
#define SHORT_S 3u
#define FIRST_SEQ 7u
#define SECOND_SEQ 8u
#define LONG_RUN 12000u
#define HASH_MUL 2654435761u
#define HASH_SHIFT 16u
#define VALUE_MASK 0x3FFu
#define AXES_PER_SENSOR 3u
#define CMD_BYTES 2u
#define OTHER_COMMAND 0x05u

static uint8_t flash[FLASH_BYTES];
static cap_t cap;
static sp_cap_sample_t history[CAP_HISTORY_SAMPLES];
static capstore_t store;
static capw_t writer;
static devrec_t rec;
static uint32_t decoding_seq;
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

static sp_cap_sample_t signal_at(uint32_t index)
{
	sp_cap_sample_t s;

	for (uint32_t a = 0; a < AXES_PER_SENSOR; a++) {
		s.acc[a] = (int16_t)(((index * AXES_PER_SENSOR + a) * HASH_MUL >> HASH_SHIFT) & VALUE_MASK);
		s.gyro[a] = (int16_t)(((index + a) * HASH_MUL >> HASH_SHIFT) & VALUE_MASK);
	}
	return s;
}

static void push_batch(void)
{
	for (uint32_t i = 0; i < BATCH; i++) {
		sp_cap_sample_t s = signal_at(cap.count);
		cap_push(&cap, &s);
	}
}

static void step(uint32_t seq)
{
	push_batch();
	if (devrec_due(&rec, cap.count)) {
		devrec_at_t at = {seq, cap.count, cap.epoch};
		capw_request_t req = devrec_take(&rec, &at);
		capw_request(&writer, &req);
	}
	while (capw_busy(&writer) && capw_pump(&writer) == CAPW_COMMITTED) {
	}
}

static void setup(void)
{
	capw_deps_t deps = {&cap, &store};

	memset(flash, ERASED, sizeof(flash));
	memset(&rec, 0, sizeof(rec));
	cap_init(&cap, history);
	check(capstore_init(&store, &dev) == 0, "store init");
	capw_init(&writer, &deps);
	while (cap.count < WARM_UP) {
		push_batch();
	}
}

static int refill(capdec_t *dec)
{
	const capstore_entry_t *e = capstore_find(&store, decoding_seq);
	uint32_t body = e ? capstore_body_start(e) : 0u;
	uint32_t left = e ? e->payload_len - body - dec->source_offset : 0u;
	capstore_read_t req = {decoding_seq, body + dec->source_offset, dec->in, left < CODEC_IN_BYTES ? left : CODEC_IN_BYTES};

	dec->in_len = req.len;
	dec->source_offset += req.len;
	return req.len ? capstore_read(&store, &req) : 0;
}

static int recording_matches(uint32_t seq, uint32_t first)
{
	const capstore_entry_t *e = capstore_find(&store, seq);
	capdec_t dec = {.refill = refill};
	sp_cap_sample_t got;

	decoding_seq = seq;
	if (!e || e->trigger_index != 0u) {
		return 0;
	}
	capdec_init(&dec, e->n_samples);
	for (uint32_t k = 0; k < e->n_samples; k++) {
		sp_cap_sample_t want = signal_at(first + k);
		if (capdec_next(&dec, &got) != 1 || memcmp(&got, &want, sizeof(got)) != 0) {
			return 0;
		}
	}
	return capdec_next(&dec, &got) == 0;
}

static int command_ok(uint8_t command, uint8_t value)
{
	uint8_t data[CMD_BYTES] = {command, value};

	return devcmd_valid(&(devcmd_req_t){data, CMD_BYTES});
}

static void test_commands(void)
{
	uint8_t record[CMD_BYTES] = {SP_CTRL_RECORD, SP_RECORD_MIN_S};
	uint8_t reg0[CMD_BYTES] = {SP_CTRL_REG0_MODE, SP_REG0_LDO};
	uint8_t stream[CMD_BYTES] = {SP_CTRL_STREAM, 1u};

	check(command_ok(SP_CTRL_REG0_MODE, SP_REG0_DCDC) && command_ok(SP_CTRL_REG0_MODE, SP_REG0_LDO),
	      "REG0 mode accepts DC/DC and LDO");
	check(!command_ok(SP_CTRL_REG0_MODE, SP_REG0_LDO + 1u), "REG0 mode refuses other values");
	check(command_ok(SP_CTRL_RECORD, SP_RECORD_MIN_S) && command_ok(SP_CTRL_RECORD, SP_RECORD_MAX_S),
	      "recording accepts 1 to 10 s");
	check(!command_ok(SP_CTRL_RECORD, SP_RECORD_MIN_S - 1u) && !command_ok(SP_CTRL_RECORD, SP_RECORD_MAX_S + 1u),
	      "recording refuses 0 s and 11 s");
	check(!devcmd_valid(&(devcmd_req_t){record, CMD_BYTES - 1u}), "a recording without its length byte is refused");
	check(!devcmd_valid(&(devcmd_req_t){reg0, CMD_BYTES - 1u}), "a REG0 mode without its value byte is refused");
	check(command_ok(SP_CTRL_STREAM, 0u) && command_ok(SP_CTRL_STREAM, SP_STREAM_MAX_DIVIDER),
	      "the live stream accepts stop and dividers up to 8");
	check(!command_ok(SP_CTRL_STREAM, SP_STREAM_MAX_DIVIDER + 1u), "the live stream refuses divider 9");
	check(!devcmd_valid(&(devcmd_req_t){stream, CMD_BYTES - 1u}), "a live stream command without its byte is refused");
	check(command_ok(OTHER_COMMAND, 0u), "other commands are left alone");
}

static uint32_t record(uint32_t seq, uint8_t seconds)
{
	uint32_t first = cap.count + BATCH - 1u;

	devrec_start(&rec, seconds);
	check(devrec_running(&rec, &writer), "a started recording keeps the puck awake");
	while (devrec_running(&rec, &writer) && cap.count < LONG_RUN) {
		step(seq);
	}
	return first;
}

static void test_lengths(void)
{
	setup();
	uint32_t first = record(FIRST_SEQ, SHORT_S);
	const capstore_entry_t *e = capstore_find(&store, FIRST_SEQ);
	check(e && e->n_samples == SHORT_S * CAP_ODR_HZ && e->format == CAPSTORE_FORMAT_FULL,
	      "a 3 s recording holds 3 s of samples, stored at full rate");
	check(recording_matches(FIRST_SEQ, first), "it starts at the newest sample when asked");
	first = record(SECOND_SEQ, SP_RECORD_MAX_S);
	e = capstore_find(&store, SECOND_SEQ);
	check(e && e->n_samples == SP_RECORD_MAX_S * CAP_ODR_HZ, "a 10 s recording fills the whole sample ring");
	check(recording_matches(SECOND_SEQ, first), "a 10 s recording decodes exactly");
	check(!devrec_running(&rec, &writer), "the puck may sleep once the recording is stored");
}

static void test_wake(void)
{
	setup();
	cap_reset(&cap);
	devrec_start(&rec, SHORT_S);
	check(!devrec_due(&rec, cap.count), "a recording waits for the first samples after a wake");
	uint32_t first = record(FIRST_SEQ, SHORT_S);
	check(first == BATCH - 1u && recording_matches(FIRST_SEQ, first), "after a wake it starts at the first batch");
}

static void test_ring_reset(void)
{
	setup();
	devrec_start(&rec, SHORT_S);
	step(FIRST_SEQ);
	check(devrec_running(&rec, &writer), "the recording is being written");
	cap_reset(&cap);
	for (uint32_t i = 0; i < BATCH; i++) {
		step(FIRST_SEQ);
	}
	check(!devrec_running(&rec, &writer) && capstore_find(&store, FIRST_SEQ) == NULL,
	      "a ring reset drops the recording and lets the puck sleep");
}

int main(void)
{
	test_commands();
	test_lengths();
	test_wake();
	test_ring_reset();
	printf("DEVREC TESTS: %s\n", failures ? "FAIL" : "PASS");
	return failures ? 1 : 0;
}
