#include <stdio.h>
#include <string.h>

#include "../src/capwriter.h"

#define SECTOR_BYTES 4096u
#define FLASH_BYTES 0x66000u
#define ERASED 0xFFu
#define BATCH 26u
#define FAULT (-5)
#define NO_FAULT (-1)
#define FIRST_TRIGGER 600u
#define TRIGGER_VARIANTS 32u
#define REQUEST_DELAY 400u
#define FAULT_PHASE_END 2800u
#define CHECK_TRIGGER 2500u
#define CHECK_PHASE_END 4000u
#define FAULTY_SEQ 1u
#define CHECK_SEQ 2u
#define ACC_HASH 2654435761u
#define GYRO_HASH 40503u
#define ACC_SHIFT 7u
#define GYRO_SHIFT 5u
#define ACC_MASK 0x3FFu
#define GYRO_MASK 0x1FFu
#define AXES_PER_SENSOR 3u

typedef struct {
	int write_at;
	int erase_at;
} fault_plan_t;

typedef struct {
	int writes;
	int erases;
} flash_ops_t;

static const fault_plan_t no_faults = {NO_FAULT, NO_FAULT};
static uint8_t flash[FLASH_BYTES];
static fault_plan_t plan;
static flash_ops_t ops;
static flash_ops_t fault_phase;
static flash_ops_t baseline;
static cap_t cap;
static capstore_t store;
static capw_t writer;
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
	if (ops.writes++ == plan.write_at) {
		return FAULT;
	}
	for (uint32_t i = 0; i < span->len; i++) {
		flash[span->offset + i] &= span->data[i];
	}
	return 0;
}

static int fake_erase(uint32_t offset)
{
	if (ops.erases++ == plan.erase_at) {
		return FAULT;
	}
	memset(&flash[offset], ERASED, SECTOR_BYTES);
	return 0;
}

static const capstore_flash_t dev = {fake_read, fake_write, fake_erase, FLASH_BYTES, SECTOR_BYTES};

static sp_cap_sample_t signal_at(uint32_t index)
{
	sp_cap_sample_t s;

	for (uint32_t a = 0; a < AXES_PER_SENSOR; a++) {
		s.acc[a] = (int16_t)((index * ACC_HASH >> (ACC_SHIFT + a)) & ACC_MASK);
		s.gyro[a] = (int16_t)((index * GYRO_HASH >> (GYRO_SHIFT + a)) & GYRO_MASK);
	}
	return s;
}

static void run_to(uint32_t count)
{
	while (cap.count < count) {
		for (uint32_t i = 0; i < BATCH; i++) {
			sp_cap_sample_t s = signal_at(cap.count);
			cap_push(&cap, &s);
		}
		while (capw_busy(&writer) && capw_pump(&writer) == CAPW_COMMITTED) {
		}
	}
}

static int refill(capdec_t *dec)
{
	const capstore_entry_t *e = capstore_find(&store, CHECK_SEQ);
	uint32_t left = e ? e->payload_len - dec->source_offset : 0u;
	capstore_read_t req = {CHECK_SEQ, dec->source_offset, dec->in, left < CODEC_IN_BYTES ? left : CODEC_IN_BYTES};

	dec->in_len = req.len;
	dec->source_offset += req.len;
	return req.len ? capstore_read(&store, &req) : 0;
}

static bool check_capture_intact(void)
{
	const capstore_entry_t *e = capstore_find(&store, CHECK_SEQ);
	uint32_t first = CHECK_TRIGGER - CAP_STANDARD_PRE;
	capdec_t dec = {.refill = refill};
	sp_cap_sample_t got;

	if (!e || e->n_samples != CAP_STANDARD_PRE + CAP_STANDARD_POST) {
		return false;
	}
	capdec_init(&dec, e->n_samples);
	for (uint32_t k = 0; k < e->n_samples; k++) {
		sp_cap_sample_t want = signal_at(first + k);
		if (capdec_next(&dec, &got) != 1 || memcmp(&got, &want, sizeof(got)) != 0) {
			return false;
		}
	}
	return capdec_next(&dec, &got) == 0;
}

static void request(uint32_t seq, uint32_t trigger)
{
	capw_request_t req = {seq, trigger, cap.epoch, SP_CAPTURE_STANDARD};

	capw_request(&writer, &req);
}

static bool recovers(uint32_t trigger, fault_plan_t faults)
{
	capw_deps_t deps = {&cap, &store};

	memset(flash, ERASED, sizeof(flash));
	cap_init(&cap);
	check(capstore_init(&store, &dev) == 0, "store init");
	capw_init(&writer, &deps);
	ops = (flash_ops_t){0, 0};
	plan = faults;
	run_to(trigger + REQUEST_DELAY);
	request(FAULTY_SEQ, trigger);
	run_to(FAULT_PHASE_END);
	fault_phase = ops;
	plan = no_faults;
	request(CHECK_SEQ, CHECK_TRIGGER);
	run_to(CHECK_PHASE_END);
	if (!check_capture_intact()) {
		return false;
	}
	check(capstore_init(&store, &dev) == 0, "store reboot");
	return check_capture_intact();
}

static int lost_after_faults(uint32_t trigger, bool erase_faults)
{
	int count = erase_faults ? baseline.erases : baseline.writes;
	int lost = 0;

	for (int n = 0; n < count; n++) {
		fault_plan_t faults = {erase_faults ? NO_FAULT : n, erase_faults ? n : NO_FAULT};
		lost += !recovers(trigger, faults);
	}
	return lost;
}

static void test_single_faults(void)
{
	int points = 0;
	int lost = 0;

	for (uint32_t trigger = FIRST_TRIGGER; trigger < FIRST_TRIGGER + TRIGGER_VARIANTS; trigger++) {
		check(recovers(trigger, no_faults), "fault-free run stores the next capture");
		baseline = fault_phase;
		points += baseline.writes + baseline.erases;
		lost += lost_after_faults(trigger, false) + lost_after_faults(trigger, true);
	}
	printf("info: %d single write/erase faults, %d lost the next capture (live or after a reboot)\n", points, lost);
	check(lost == 0, "a failed flash write or erase never loses a later capture, also after a reboot");
}

int main(void)
{
	test_single_faults();
	printf("CAPWRITER FAULT TESTS: %s\n", failures ? "FAIL" : "PASS");
	return failures ? 1 : 0;
}
