#include <stdio.h>
#include <string.h>

#include "../src/capture.h"

#define ROUND_TRIP_SAMPLES 100000u
#define SHOT_SEQ 42u
#define WAKE_TRIGGER 100u
#define LONG_RUN (CAP_RING_SAMPLES * 3u)
#define HEADER_WIRE_BYTES 15u
#define MTU_PAYLOAD 244u
#define TINY_PAYLOAD 10u
#define AXIS_STRIDE 6u

static cap_t cap;
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

static int window_is(const cap_window_t *w, const cap_window_t *expected)
{
	return w->first == expected->first && w->trigger == expected->trigger && w->end == expected->end;
}

static void test_windows(void)
{
	uint32_t trigger = CAP_RING_SAMPLES;
	cap_request_t standard = cap_mode_request(SP_CAPTURE_STANDARD, trigger);
	cap_request_t extended = cap_mode_request(SP_CAPTURE_EXTENDED, trigger);

	cap_init(&cap);
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
	cap_init(&cap);
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

static void test_packing(void)
{
	uint8_t buf[MTU_PAYLOAD];
	cap_buf_t out = {buf, sizeof(buf)};
	cap_header_info_t info = {SHOT_SEQ, CAP_STANDARD_PRE + CAP_STANDARD_POST, CAP_STANDARD_PRE};
	sp_cap_header_t h;
	sp_cap_sample_t samples[MTU_PAYLOAD / sizeof(sp_cap_sample_t)];

	check(sizeof(h) == HEADER_WIRE_BYTES, "header is 15 bytes");
	check(cap_pack_header(&info, &out) == sizeof(h), "header packed");
	memcpy(&h, buf, sizeof(h));
	check(h.type == SP_CAP_HEADER && h.seq == SHOT_SEQ && h.n_samples == info.n_samples &&
	      h.trigger_index == CAP_STANDARD_PRE && h.odr_hz == CAP_ODR_HZ, "header fields");
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

int main(void)
{
	test_round_trip();
	test_windows();
	test_short_and_missing();
	test_reset();
	test_packing();
	printf("CAPTURE TESTS: %s\n", failures ? "FAIL" : "PASS");
	return failures ? 1 : 0;
}
