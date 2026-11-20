#include <stdio.h>
#include <string.h>

#include "../src/capaim.h"
#include "../src/capture.h"

#define ROUND_TRIP_SAMPLES 100000u
#define WIDE_SAMPLES 100000u
#define WIDE_STRIDE 104729u
#define MS_PER_S 1000u
#define SHOT_SEQ 42u
#define WAKE_TRIGGER 100u
#define LONG_RUN (CAP_RING_SAMPLES * 3u)
#define HEADER_WIRE_BYTES 23u
#define HEADER_OFFSET_AT 15u
#define MTU_PAYLOAD 244u
#define TINY_PAYLOAD 10u
#define AXIS_STRIDE 6u
#define GROUP8 CAP_HISTORY_GROUP
#define HISTORY_SPAN (CAP_HISTORY_SAMPLES * CAP_HISTORY_GROUP)
#define ULTRA_TRIGGER 20003u
#define ULTRA_WAKE_TRIGGER 9003u
#define LATE_BY 4000u
#define DROPPED_GROUPS 5u
#define REC_HEADER_WIRE_BYTES 32u
#define REC_HEADER_OFFSET_AT 24u
#define OFFSET_X_MDPS (-18)
#define OFFSET_Y_MDPS (-181)
#define OFFSET_Z_MDPS (-15)
#define OFFSET_AGE_S 42u
#define REC_DATA_WIRE_BYTES 9u
#define REC_PAYLOAD_LEN 9912u
#define REC_CHECK 0x50085E05u
#define REC_OFFSET 470u

static cap_t cap;
static sp_cap_sample_t history[CAP_HISTORY_SAMPLES];
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
	sp_cap_sample_t s;

	for (uint32_t i = 0; i < 3u; i++) {
		s.acc[i] = (int16_t)(index * AXIS_STRIDE + i);
		s.gyro[i] = (int16_t)(index * AXIS_STRIDE + 3u + i);
	}
	return s;
}

static void push_until(uint32_t count)
{
	while (cap.count < count) {
		sp_cap_sample_t s = sample_for(cap.count);
		cap_push(&cap, &s);
	}
}

static void test_round_trip(void)
{
	int ok = 1;

	for (uint32_t i = 0; i < ROUND_TRIP_SAMPLES; i++) {
		ok &= cap_ms_to_index(cap_index_to_ms(i)) == i;
	}
	check(ok, "ms <-> sample index round trip");
}

static void test_wide_range(void)
{
	int ok = 1;

	for (uint32_t i = 0; i < WIDE_SAMPLES; i++) {
		uint32_t index = i * WIDE_STRIDE;

		ok &= cap_index_to_ms(index) == (uint32_t)((uint64_t)index * MS_PER_S / CAP_ODR_HZ);
	}
	check(ok, "sample index -> ms equals the 64-bit division over the whole index range");
}

static int window_is(const cap_window_t *w, const cap_window_t *expected)
{
	return w->first == expected->first && w->trigger == expected->trigger && w->end == expected->end;
}

static void test_windows(void)
{
	uint32_t trigger = CAP_RING_SAMPLES;
	cap_request_t standard = cap_mode_request(SP_CAPTURE_STANDARD, trigger);
	cap_request_t extended = cap_mode_request(SP_CAPTURE_EXTENDED, trigger);

	cap_init(&cap, history);
	push_until(trigger + 1u);
	cap_window_t w = cap_window(&cap, &standard);
	cap_window_t want_std = {trigger - CAP_STANDARD_PRE, trigger, trigger + CAP_STANDARD_POST};
	check(window_is(&w, &want_std), "standard window -1.0 s .. +0.5 s");
	w = cap_window(&cap, &extended);
	cap_window_t want_ext = {trigger - CAP_EXTENDED_PRE, trigger, trigger + CAP_EXTENDED_POST};
	check(window_is(&w, &want_ext), "extended window -6 s .. +2 s");
	check(w.end - w.first <= CAP_RING_SAMPLES, "extended window fits the ring");
}

static void test_short_and_missing(void)
{
	cap_init(&cap, history);
	push_until(WAKE_TRIGGER + 1u);
	cap_request_t early = cap_mode_request(SP_CAPTURE_EXTENDED, WAKE_TRIGGER);
	cap_window_t w = cap_window(&cap, &early);
	check(w.first == 0 && w.trigger == WAKE_TRIGGER, "shot soon after wake starts at the wake");
	cap_request_t future = cap_mode_request(SP_CAPTURE_STANDARD, cap.count);
	w = cap_window(&cap, &future);
	check(w.end == w.first, "trigger not yet sampled gives no window");
	push_until(LONG_RUN);
	cap_request_t stale = cap_mode_request(SP_CAPTURE_STANDARD, WAKE_TRIGGER);
	w = cap_window(&cap, &stale);
	check(w.end == w.first, "overwritten trigger gives no window");
	check(!cap_holds(&cap, LONG_RUN - CAP_RING_SAMPLES - 1u) && cap_holds(&cap, LONG_RUN - 1u),
	      "ring holds only the newest samples");
	sp_cap_sample_t want = sample_for(LONG_RUN - 1u);
	check(memcmp(cap_sample(&cap, LONG_RUN - 1u), &want, sizeof(want)) == 0, "newest sample readable");
}

static void test_reset(void)
{
	uint32_t epoch = cap.epoch;

	cap_reset(&cap);
	check(cap.count == 0 && cap.epoch == epoch + 1u, "reset empties the ring and bumps the epoch");
}

static const sp_gyro_offset_t measured = {{OFFSET_X_MDPS, OFFSET_Y_MDPS, OFFSET_Z_MDPS}, OFFSET_AGE_S};

static void test_packing(void)
{
	uint8_t buf[MTU_PAYLOAD];
	cap_buf_t out = {buf, sizeof(buf)};
	cap_header_info_t info = {SHOT_SEQ, CAP_STANDARD_PRE + CAP_STANDARD_POST, CAP_STANDARD_PRE, measured};
	sp_cap_header_t h;
	sp_cap_sample_t samples[MTU_PAYLOAD / sizeof(sp_cap_sample_t)];

	check(sizeof(h) == HEADER_WIRE_BYTES, "header is 23 bytes");
	check(cap_pack_header(&info, &out) == sizeof(h), "header packed");
	memcpy(&h, buf, sizeof(h));
	check(h.type == SP_CAP_HEADER && h.seq == SHOT_SEQ && h.n_samples == info.n_samples &&
	      h.trigger_index == CAP_STANDARD_PRE && h.odr_hz == CAP_ODR_HZ, "header fields");
	check(memcmp(buf + HEADER_OFFSET_AT, &measured, sizeof(measured)) == 0, "gyro offset at byte 15");
	uint32_t fit = cap_data_fit(sizeof(buf));
	for (uint32_t i = 0; i < fit; i++) {
		samples[i] = sample_for(i);
	}
	cap_data_info_t data = {SHOT_SEQ, 0, samples, fit};
	check(cap_pack_data(&data, &out) == sizeof(sp_cap_data_t) + fit * sizeof(sp_cap_sample_t), "data packed");
	check(memcmp(buf + sizeof(sp_cap_data_t), samples, fit * sizeof(sp_cap_sample_t)) == 0, "data samples");
	data.count = fit + 1u;
	check(cap_pack_data(&data, &out) == 0, "too many samples rejected");
	cap_buf_t tiny = {buf, TINY_PAYLOAD};
	check(cap_pack_header(&info, &tiny) == 0 && cap_data_fit(TINY_PAYLOAD) == 0, "tiny MTU sends nothing");
}

static void test_history(void)
{
	sp_cap_sample_t group[GROUP8];

	cap_init(&cap, history);
	push_until(GROUP8 * 3u - 1u);
	check(cap_history_holds(&cap, 0u) && cap_history_holds(&cap, GROUP8 * 2u - 1u) &&
	      !cap_history_holds(&cap, GROUP8 * 2u), "history holds complete groups of 8 only");
	for (uint32_t i = 0; i < GROUP8; i++) {
		group[i] = sample_for(GROUP8 + i);
	}
	sp_cap_sample_t want = capaim_mean(group, GROUP8);
	check(memcmp(cap_history_sample(&cap, GROUP8 + 3u), &want, sizeof(want)) == 0,
	      "history keeps the rounded mean of each group of 8");
	push_until(HISTORY_SPAN + DROPPED_GROUPS * GROUP8);
	check(!cap_history_holds(&cap, DROPPED_GROUPS * GROUP8 - 1u) && cap_history_holds(&cap, DROPPED_GROUPS * GROUP8),
	      "history keeps the newest 2048 groups (34 s)");
}

static void test_ultra_window(void)
{
	cap_request_t req = cap_mode_request(SP_CAPTURE_ULTRA, ULTRA_TRIGGER);
	cap_window_t want = {(ULTRA_TRIGGER - CAP_ULTRA_PRE) / GROUP8 * GROUP8, ULTRA_TRIGGER, ULTRA_TRIGGER + CAP_ULTRA_POST};

	cap_init(&cap, history);
	push_until(ULTRA_TRIGGER + CAP_ULTRA_POST);
	cap_window_t w = cap_window(&cap, &req);
	check(req.history && window_is(&w, &want), "ultra window: 30 s before, rounded down to a group of 8");
	cap_init(&cap, history);
	push_until(ULTRA_WAKE_TRIGGER + CAP_ULTRA_POST);
	req = cap_mode_request(SP_CAPTURE_ULTRA, ULTRA_WAKE_TRIGGER);
	w = cap_window(&cap, &req);
	check(w.first == 0u && w.trigger == ULTRA_WAKE_TRIGGER, "ultra after a wake starts at the wake");
	push_until(HISTORY_SPAN + CAP_ULTRA_PRE);
	req = cap_mode_request(SP_CAPTURE_ULTRA, cap.count - LATE_BY);
	w = cap_window(&cap, &req);
	check(w.first == (cap.count / GROUP8 - CAP_HISTORY_SAMPLES) * GROUP8, "a late request starts at the oldest history group");
}

static void test_rec_packing(void)
{
	uint8_t buf[MTU_PAYLOAD];
	uint8_t bytes[MTU_PAYLOAD];
	cap_buf_t out = {buf, sizeof(buf)};
	cap_rec_info_t info = {SHOT_SEQ, CAP_ULTRA_PRE + CAP_ULTRA_POST, CAP_ULTRA_PRE, SP_CAPFMT_ULTRA, REC_PAYLOAD_LEN, REC_CHECK, measured};
	sp_caprec_header_t h;
	sp_caprec_data_t d;

	check(sizeof(h) == REC_HEADER_WIRE_BYTES && sizeof(d) == REC_DATA_WIRE_BYTES, "record header 32 B, data head 9 B");
	check(cap_pack_rec_header(&info, &out) == sizeof(h), "record header packed");
	memcpy(&h, buf, sizeof(h));
	check(h.type == SP_CAPREC_HEADER && h.seq == SHOT_SEQ && h.n_samples == info.n_samples &&
	      h.trigger_index == CAP_ULTRA_PRE && h.format == SP_CAPFMT_ULTRA && h.payload_len == REC_PAYLOAD_LEN &&
	      h.check == REC_CHECK && h.odr_hz == CAP_ODR_HZ, "record header fields");
	check(memcmp(buf + REC_HEADER_OFFSET_AT, &measured, sizeof(measured)) == 0, "record gyro offset at byte 24");
	uint32_t fit = cap_rec_data_fit(sizeof(buf));
	for (uint32_t i = 0; i < fit; i++) {
		bytes[i] = (uint8_t)(i * AXIS_STRIDE);
	}
	cap_rec_data_t data = {SHOT_SEQ, REC_OFFSET, bytes, fit};
	check(fit == MTU_PAYLOAD - REC_DATA_WIRE_BYTES && cap_pack_rec_data(&data, &out) == MTU_PAYLOAD, "data fills the MTU");
	memcpy(&d, buf, sizeof(d));
	check(d.type == SP_CAPREC_DATA && d.seq == SHOT_SEQ && d.offset == REC_OFFSET &&
	      memcmp(buf + sizeof(d), bytes, fit) == 0, "record data fields and bytes");
	data.len = fit + 1u;
	check(cap_pack_rec_data(&data, &out) == 0, "too many bytes rejected");
}

int main(void)
{
	test_round_trip();
	test_wide_range();
	test_windows();
	test_short_and_missing();
	test_reset();
	test_packing();
	test_history();
	test_ultra_window();
	test_rec_packing();
	printf("CAPTURE TESTS: %s\n", failures ? "FAIL" : "PASS");
	return failures ? 1 : 0;
}
