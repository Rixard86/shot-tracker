#include <stdio.h>
#include <string.h>

#include "../src/capture.h"
#include "../src/zerorate.h"

#define SAMPLE_MS 2u
#define GRAVITY_LSB 1024
#define STILL_NOISE_LSB 1
#define AIM_TREMOR_LSB 20
#define GYRO_EDGE_IN_LSB 3
#define GYRO_EDGE_OUT_LSB 4
#define ACC_EDGE_IN_LSB 5
#define ACC_EDGE_OUT_LSB 6
#define TURN_LSB 100
#define OFFSET_Y_LSB (-3)
#define OFFSET_Z_LSB 1
#define OTHER_OFFSET_LSB 2
#define PARTIAL_SAMPLES 300u
#define AGE_LATER_MS 12345u
#define AGE_LATER_S 12u
#define VERY_LATE_MS 100000000u
#define ROUNDING_SAMPLES 4u
#define ROUNDED_MDPS 1

typedef struct {
	int16_t gyro[ZR_AXES];
	int16_t gyro_noise;
	int16_t acc_noise;
} pattern_t;

static zr_t zr;
static uint32_t now_ms;
static int failures;

static void check(int ok, const char *what)
{
	if (!ok) {
		printf("FAIL: %s\n", what);
		failures++;
	}
}

static void start(void)
{
	memset(&zr, 0, sizeof(zr));
	zr_restart(&zr);
	now_ms = 0;
}

static void feed(const pattern_t *p, uint32_t count)
{
	for (uint32_t i = 0; i < count; i++) {
		int16_t sign = (int16_t)(i % 2u == 0u ? 1 : -1);
		sp_cap_sample_t s = {{0, 0, GRAVITY_LSB}, {0, 0, 0}};

		for (uint32_t a = 0; a < ZR_AXES; a++) {
			s.gyro[a] = (int16_t)(p->gyro[a] + sign * p->gyro_noise);
			s.acc[a] = (int16_t)(s.acc[a] + sign * p->acc_noise);
		}
		now_ms += SAMPLE_MS;
		zr_input_t in = {&s, now_ms};
		zr_push(&zr, &in);
	}
}

static int16_t mdps(int16_t lsb)
{
	return (int16_t)(lsb * (int16_t)CAP_GYRO_MDPS_PER_LSB);
}

static bool accepted_after(const pattern_t *p)
{
	start();
	feed(p, ZR_SETTLE_SAMPLES + ZR_WINDOW_SAMPLES);
	return zr.valid;
}

static void test_still_window(void)
{
	pattern_t still = {{0, OFFSET_Y_LSB, OFFSET_Z_LSB}, STILL_NOISE_LSB, STILL_NOISE_LSB};

	check(accepted_after(&still), "a still second is taken");
	check(zr.mdps[0] == 0 && zr.mdps[1] == mdps(OFFSET_Y_LSB) && zr.mdps[2] == mdps(OFFSET_Z_LSB),
	      "the offset is the window's mean rate in mdps");
	check(zr.measured_ms == now_ms, "measured at the window's last sample");
}

static void test_settle_skips_turn_on(void)
{
	pattern_t off = {{0, 0, 0}, 0, 0};
	pattern_t still = {{0, OFFSET_Y_LSB, OFFSET_Z_LSB}, STILL_NOISE_LSB, STILL_NOISE_LSB};

	start();
	feed(&off, ZR_SETTLE_SAMPLES);
	feed(&still, ZR_WINDOW_SAMPLES);
	check(zr.valid && zr.mdps[1] == mdps(OFFSET_Y_LSB), "zero gyro samples right after a wake are skipped");
}

static void test_motion_rejected(void)
{
	pattern_t aim = {{0, OFFSET_Y_LSB, 0}, AIM_TREMOR_LSB, STILL_NOISE_LSB};
	pattern_t turn = {{TURN_LSB, 0, 0}, 0, STILL_NOISE_LSB};
	pattern_t gyro_in = {{0, 0, 0}, GYRO_EDGE_IN_LSB, 0};
	pattern_t gyro_out = {{0, 0, 0}, GYRO_EDGE_OUT_LSB, 0};
	pattern_t acc_in = {{0, 0, 0}, 0, ACC_EDGE_IN_LSB};
	pattern_t acc_out = {{0, 0, 0}, 0, ACC_EDGE_OUT_LSB};

	check(!accepted_after(&aim), "aiming tremor is not still");
	check(!accepted_after(&turn), "a steady turn above 5 dps is not an offset");
	check(accepted_after(&gyro_in) && !accepted_after(&gyro_out), "gyro spread limit at 0.25 dps");
	check(accepted_after(&acc_in) && !accepted_after(&acc_out), "accelerometer spread limit at 5 mg");
}

static void test_restart_drops_partial(void)
{
	pattern_t first = {{OTHER_OFFSET_LSB, 0, 0}, STILL_NOISE_LSB, STILL_NOISE_LSB};
	pattern_t second = {{0, OFFSET_Y_LSB, 0}, STILL_NOISE_LSB, STILL_NOISE_LSB};

	start();
	feed(&first, ZR_SETTLE_SAMPLES + PARTIAL_SAMPLES);
	zr_restart(&zr);
	feed(&second, ZR_SETTLE_SAMPLES + ZR_WINDOW_SAMPLES);
	check(zr.valid && zr.mdps[0] == 0 && zr.mdps[1] == mdps(OFFSET_Y_LSB), "a restart drops the partial window");
}

static void test_rounding(void)
{
	pattern_t up = {{1, 0, 0}, 0, 0};
	pattern_t flat = {{0, 0, 0}, 0, 0};
	pattern_t down = {{-1, 0, 0}, 0, 0};

	start();
	feed(&flat, ZR_SETTLE_SAMPLES);
	feed(&up, ROUNDING_SAMPLES);
	feed(&flat, ZR_WINDOW_SAMPLES - ROUNDING_SAMPLES);
	check(zr.valid && zr.mdps[0] == ROUNDED_MDPS, "a mean of 0.58 mdps rounds to 1");
	feed(&down, ROUNDING_SAMPLES);
	feed(&flat, ZR_WINDOW_SAMPLES - ROUNDING_SAMPLES);
	check(zr.mdps[0] == -ROUNDED_MDPS, "a mean of -0.58 mdps rounds to -1");
}

static void test_offset_at(void)
{
	pattern_t still = {{0, OFFSET_Y_LSB, OFFSET_Z_LSB}, STILL_NOISE_LSB, STILL_NOISE_LSB};
	sp_gyro_offset_t none;
	sp_gyro_offset_t later;
	sp_gyro_offset_t stale;

	start();
	none = zr_offset_at(&zr, now_ms);
	check(none.age_s == SP_GYRO_OFFSET_UNKNOWN && none.mdps[0] == 0 && none.mdps[1] == 0,
	      "no still second yet: unknown");
	feed(&still, ZR_SETTLE_SAMPLES + ZR_WINDOW_SAMPLES);
	later = zr_offset_at(&zr, zr.measured_ms + AGE_LATER_MS);
	stale = zr_offset_at(&zr, zr.measured_ms + VERY_LATE_MS);
	check(later.age_s == AGE_LATER_S && later.mdps[1] == mdps(OFFSET_Y_LSB), "age in whole seconds");
	check(stale.age_s == SP_GYRO_OFFSET_AGE_MAX_S, "the age saturates below the unknown value");
}

int main(void)
{
	test_still_window();
	test_settle_skips_turn_on();
	test_motion_rejected();
	test_restart_drops_partial();
	test_rounding();
	test_offset_at();
	printf("ZERORATE TESTS: %s\n", failures ? "FAIL" : "PASS");
	return failures ? 1 : 0;
}
