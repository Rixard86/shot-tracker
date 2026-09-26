#include <stdio.h>
#include <string.h>

#include "../src/capture.h"

#define ROUND_TRIP_SAMPLES 2000000u
#define WRAP_PASSES 10u
#define MTU_MIN_PAYLOAD 20u
#define MTU_MAX_PAYLOAD 244u
#define POST_END_DEFAULT_MS 600u
#define POST_END_MAX_MS 1500u
#define SHOT_SEQ 7u
#define HALF_WORD_BITS 16u
#define HALF_WORD_MASK 0xFFFFu
#define HEADER_WIRE_BYTES 15u

static int failures;

static void check(int ok, const char *what)
{
	if (!ok) {
		printf("FAIL: %s\n", what);
		failures++;
	}
}

static sp_cap_sample_t sample_for(uint32_t index)
{
	sp_cap_sample_t s = {0};

	s.acc[0] = (int16_t)(index & HALF_WORD_MASK);
	s.acc[1] = (int16_t)(index >> HALF_WORD_BITS);
	s.gyro[2] = (int16_t)~(index & HALF_WORD_MASK);
	return s;
}

static int encodes(const sp_cap_sample_t *s, uint32_t index)
{
	sp_cap_sample_t want = sample_for(index);

	return memcmp(s, &want, sizeof(want)) == 0;
}

static int push_until(cap_t *cap, uint32_t count)
{
	int done = CAP_NO_SLOT;

	while (cap->count < count) {
		sp_cap_sample_t s = sample_for(cap->count);
		int slot = cap_push(cap, &s);
		if (slot != CAP_NO_SLOT) {
			done = slot;
		}
	}
	return done;
}

static void test_round_trip(void)
{
	int ok = 1;

	for (uint32_t i = 0; i < ROUND_TRIP_SAMPLES && ok; i++) {
		ok = cap_ms_to_index(cap_index_to_ms(i)) == i;
	}
	check(ok, "ms <-> sample index round trip");
}

static int window_ok(const cap_t *cap, int slot)
{
	uint32_t start = cap->slots[slot].trigger_index - CAP_PRE_SAMPLES;

	for (uint32_t k = 0; k < CAP_WINDOW_SAMPLES; k++) {
		if (!encodes(&cap->slots[slot].samples[k], start + k)) {
			return 0;
		}
	}
	return 1;
}

static void test_window(uint32_t trigger, uint32_t post_end_ms)
{
	static cap_t cap;
	cap_request_t req = {.seq = SHOT_SEQ, .trigger_index = trigger};

	cap_init(&cap);
	push_until(&cap, trigger + cap_ms_to_index(post_end_ms));
	int slot = cap_request(&cap, &req);
	check(slot != CAP_NO_SLOT, "request accepted");
	check(push_until(&cap, cap.count + 1) == slot, "window completes on next push");
	check(slot != CAP_NO_SLOT && window_ok(&cap, slot), "window holds trigger -1.0 s .. +0.5 s");
	check(cap_find(&cap, SHOT_SEQ) == slot, "ready capture found by seq");
}

static void test_rejects(void)
{
	static cap_t cap;
	cap_request_t early = {.seq = 1, .trigger_index = CAP_PRE_SAMPLES - 1};
	cap_request_t late = {.seq = 2, .trigger_index = CAP_PRE_SAMPLES};

	cap_init(&cap);
	check(cap_request(&cap, &early) == CAP_NO_SLOT, "trigger before 1 s of data rejected");
	push_until(&cap, CAP_RING_SAMPLES + 1);
	check(cap_request(&cap, &late) == CAP_NO_SLOT, "window start already overwritten rejected");
}

static void test_eviction_and_reset(void)
{
	static cap_t cap;

	cap_init(&cap);
	for (uint32_t seq = 1; seq <= CAP_SLOTS + 1; seq++) {
		cap_request_t req = {.seq = seq, .trigger_index = cap.count + CAP_PRE_SAMPLES};
		push_until(&cap, req.trigger_index);
		cap_request(&cap, &req);
		push_until(&cap, cap.count + CAP_POST_SAMPLES);
	}
	check(cap_find(&cap, 1) == CAP_NO_SLOT, "oldest capture evicted");
	check(cap_find(&cap, CAP_SLOTS + 1) != CAP_NO_SLOT, "newest capture kept");
	cap_request_t pending = {.seq = SHOT_SEQ, .trigger_index = cap.count};
	check(cap_request(&cap, &pending) != CAP_NO_SLOT, "pending request accepted");
	cap_reset(&cap);
	push_until(&cap, CAP_WINDOW_SAMPLES);
	check(cap_find(&cap, SHOT_SEQ) == CAP_NO_SLOT, "reset drops pending windows");
	check(cap_find(&cap, CAP_SLOTS + 1) != CAP_NO_SLOT, "reset keeps finished captures");
}

static void check_header(const uint8_t *buf, uint16_t n_samples)
{
	sp_cap_header_t h;

	memcpy(&h, buf, sizeof(h));
	check(sizeof(h) == HEADER_WIRE_BYTES, "header is 15 bytes");
	check(h.type == SP_CAP_HEADER && h.seq == SHOT_SEQ, "header type and seq");
	check(h.n_samples == n_samples && h.trigger_index == CAP_PRE_SAMPLES, "header counts");
	check(h.odr_hz == CAP_ODR_HZ && h.acc_ug_per_lsb == CAP_ACC_UG_PER_LSB &&
		      h.gyro_mdps_per_lsb == CAP_GYRO_MDPS_PER_LSB,
	      "header scales");
}

static uint32_t unpack_all(const cap_t *cap, cap_packet_t *pkt)
{
	uint32_t got = 0;
	size_t len;

	while ((len = cap_pack_data(cap, pkt)) > 0) {
		sp_cap_data_t d;
		memcpy(&d, pkt->buf, sizeof(d));
		uint32_t n = (uint32_t)((len - sizeof(d)) / sizeof(sp_cap_sample_t));
		check(d.type == SP_CAP_DATA && d.seq == SHOT_SEQ && d.first == got, "data packet header");
		check(len <= pkt->capacity, "data packet fits the MTU");
		const sp_cap_sample_t *s = (const sp_cap_sample_t *)(pkt->buf + sizeof(d));
		uint32_t start = cap->slots[pkt->slot].trigger_index - CAP_PRE_SAMPLES;
		for (uint32_t k = 0; k < n; k++) {
			check(encodes(&s[k], start + got + k), "data packet samples");
		}
		got += n;
	}
	return got;
}

static void test_packing(size_t capacity)
{
	static cap_t cap;
	static uint8_t buf[MTU_MAX_PAYLOAD];
	uint32_t trigger = WRAP_PASSES * CAP_RING_SAMPLES;
	cap_request_t req = {.seq = SHOT_SEQ, .trigger_index = trigger};

	cap_init(&cap);
	push_until(&cap, trigger);
	cap_request(&cap, &req);
	push_until(&cap, trigger + CAP_POST_SAMPLES);
	cap_packet_t pkt = {.slot = cap_find(&cap, SHOT_SEQ), .seq = SHOT_SEQ, .buf = buf,
			    .capacity = capacity};
	check(cap_pack_header(&cap, &pkt) == sizeof(sp_cap_header_t), "header packed");
	check_header(buf, CAP_WINDOW_SAMPLES);
	check(unpack_all(&cap, &pkt) == CAP_WINDOW_SAMPLES, "all samples streamed");
	cap_packet_t missing = {.slot = CAP_NO_SLOT, .seq = SHOT_SEQ, .buf = buf, .capacity = capacity};
	cap_pack_header(&cap, &missing);
	check_header(buf, 0);
	check(cap_pack_data(&cap, &missing) == 0, "missing capture sends no data");
}

int main(void)
{
	test_round_trip();
	test_window(CAP_PRE_SAMPLES, POST_END_DEFAULT_MS);
	test_window(WRAP_PASSES * CAP_RING_SAMPLES + 1u, POST_END_DEFAULT_MS);
	test_window(WRAP_PASSES * CAP_RING_SAMPLES, POST_END_MAX_MS);
	test_rejects();
	test_eviction_and_reset();
	test_packing(MTU_MIN_PAYLOAD);
	test_packing(MTU_MAX_PAYLOAD);
	printf("CAPTURE TESTS: %s\n", failures ? "FAIL" : "PASS");
	return failures ? 1 : 0;
}
