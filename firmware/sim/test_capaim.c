#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/capaim.h"

#define GROUP CAPAIM_GROUP_SAMPLES
#define FULL_RATE CAPAIM_FULL_RATE_PRE
#define EXTENDED_SAMPLES (CAP_EXTENDED_PRE + CAP_EXTENDED_POST)
#define EXTENDED_AIM (CAP_EXTENDED_PRE - FULL_RATE)
#define EXTENDED_STORED (EXTENDED_AIM / GROUP + FULL_RATE + CAP_EXTENDED_POST)
#define SUM_PERIOD 24
#define SUM_PERIODS 340
#define SUM_LOW (-SUM_PERIOD * SUM_PERIODS / 2)
#define SUM_HIGH (SUM_PERIOD * SUM_PERIODS / 2)
#define INT16_TOP 32767
#define INT16_BOTTOM (-32768)
#define STREAM_BYTES 65536u
#define HASH_MUL 2654435761u
#define HASH_SHIFT 16u
#define VALUE_SPAN 601u
#define VALUE_CENTER 300
#define ODD_AIM_TRIGGER (CAP_EXTENDED_PRE + 3u)
#define SLOW CAPAIM_SLOW_GROUP_SAMPLES
#define ULTRA_SAMPLES (CAP_ULTRA_PRE + CAP_ULTRA_POST)
#define ULTRA_SLOW (CAP_ULTRA_PRE - CAP_EXTENDED_PRE)
#define ULTRA_STORED (ULTRA_SLOW / SLOW + EXTENDED_AIM / GROUP + FULL_RATE + CAP_ULTRA_POST)
#define ULTRA_ODD_TRIGGER 9003u
#define ULTRA_ODD_SLOW 6120u
#define ULTRA_SHORT_TRIGGER 2001u

static uint8_t stream[STREAM_BYTES];
static uint32_t stream_len;
static sp_cap_sample_t stored[ULTRA_SAMPLES];
static int failures;

static void check(int ok, const char *what)
{
	if (!ok) {
		printf("FAIL: %s\n", what);
		failures++;
	}
}

static void test_shape(void)
{
	capaim_shape_t ext = {EXTENDED_SAMPLES, CAP_EXTENDED_PRE, SP_CAPFMT_EXTENDED};
	capaim_shape_t full = {EXTENDED_SAMPLES, CAP_EXTENDED_PRE, SP_CAPFMT_FULL};
	capaim_shape_t one = {FULL_RATE + 1u + CAP_EXTENDED_POST, FULL_RATE + 1u, SP_CAPFMT_EXTENDED};
	capaim_shape_t none = {FULL_RATE + CAP_EXTENDED_POST, FULL_RATE, SP_CAPFMT_EXTENDED};

	check(capaim_aim_samples(&ext) == EXTENDED_AIM && capaim_stored(&ext) == EXTENDED_STORED,
	      "extended capture keeps 600 aiming means and 1440 full-rate samples");
	check(capaim_aim_samples(&full) == 0u && capaim_stored(&full) == EXTENDED_SAMPLES,
	      "a full-rate record stores every sample");
	check(capaim_aim_samples(&one) == 1u && capaim_stored(&one) == one.n_samples, "one aiming sample is its own mean");
	check(capaim_aim_samples(&none) == 0u && capaim_stored(&none) == none.n_samples,
	      "no aiming part when the trigger is 1 s into the capture or less");
	capaim_shape_t ultra = {ULTRA_SAMPLES, CAP_ULTRA_PRE, SP_CAPFMT_ULTRA};
	capaim_shape_t odd = {ULTRA_ODD_TRIGGER + CAP_ULTRA_POST, ULTRA_ODD_TRIGGER, SP_CAPFMT_ULTRA};
	capaim_shape_t brief = {ULTRA_SHORT_TRIGGER + CAP_ULTRA_POST, ULTRA_SHORT_TRIGGER, SP_CAPFMT_ULTRA};
	check(capaim_slow_samples(&ultra) == ULTRA_SLOW && capaim_stored(&ultra) == ULTRA_STORED,
	      "ultra keeps 1440 means of 8, 600 means of 4 and 1440 full-rate samples");
	check(capaim_slow_samples(&odd) == ULTRA_ODD_SLOW && capaim_slow_samples(&ext) == 0u &&
	      capaim_slow_samples(&brief) == 0u, "the 60 Hz part ends at a group of 8, at most 6 s before the trigger");
	check(capaim_group(EXTENDED_AIM) == GROUP && capaim_group(EXTENDED_AIM + 1u) == 1u &&
	      capaim_group(EXTENDED_AIM + GROUP - 1u) == GROUP - 1u, "only the first group can be partial");
}

static sp_cap_sample_t uniform(int32_t value)
{
	sp_cap_sample_t s;

	for (uint32_t axis = 0; axis < CODEC_AXES; axis++) {
		codec_set_axis(&s, (codec_axis_set_t){axis, (int16_t)value});
	}
	return s;
}

static int32_t mean_of_sum(int32_t sum, uint32_t count)
{
	sp_cap_sample_t group[GROUP];

	for (uint32_t i = 0; i < count; i++) {
		group[i] = uniform(0);
	}
	group[0] = uniform(sum);
	sp_cap_sample_t mean = capaim_mean(group, count);
	return codec_axis_value(&mean, CODEC_AXES - 1u);
}

static void test_rounding(void)
{
	int near = 1;
	int even = 1;
	int unbiased = 1;

	for (uint32_t count = 1; count <= GROUP; count++) {
		int64_t error_sum = 0;

		for (int32_t sum = SUM_LOW; sum < SUM_HIGH; sum++) {
			int32_t mean = mean_of_sum(sum, count);
			int64_t error = (int64_t)mean * count - sum;

			near &= 2 * llabs(error) <= (int64_t)count;
			even &= mean == (int32_t)nearbyint((double)sum / count);
			error_sum += error;
		}
		unbiased &= error_sum == 0;
	}
	check(near, "every mean is within 0.5 LSB of the true mean");
	check(even, "ties round to even, as nearbyint does");
	check(unbiased, "rounding errors add up to zero over every remainder (no drift)");
	sp_cap_sample_t top[GROUP] = {uniform(INT16_TOP), uniform(INT16_TOP), uniform(INT16_TOP), uniform(INT16_TOP)};
	sp_cap_sample_t bottom[GROUP] = {uniform(INT16_BOTTOM), uniform(INT16_BOTTOM), uniform(INT16_BOTTOM),
					 uniform(INT16_BOTTOM)};
	sp_cap_sample_t high = capaim_mean(top, GROUP);
	sp_cap_sample_t low = capaim_mean(bottom, GROUP);
	check(memcmp(&high, &top[0], sizeof(high)) == 0 && memcmp(&low, &bottom[0], sizeof(low)) == 0,
	      "means of full-scale values stay at full scale");
}

static void drain(capenc_t *enc)
{
	uint32_t ready = capenc_ready(enc);

	memcpy(&stream[stream_len], enc->out, ready);
	stream_len += ready;
	capenc_drop(enc, ready);
}

static void encode_stored(uint32_t count)
{
	static capenc_t enc;

	capenc_init(&enc);
	stream_len = 0;
	for (uint32_t at = 0; at < count; at += CODEC_BLOCK_SAMPLES) {
		uint32_t left = count - at;
		codec_block_t block = {&stored[at], left < CODEC_BLOCK_SAMPLES ? left : CODEC_BLOCK_SAMPLES};
		capenc_block(&enc, &block);
		drain(&enc);
	}
	capenc_finish(&enc);
	drain(&enc);
}

static int refill(capdec_t *dec)
{
	uint32_t left = stream_len - dec->source_offset;

	dec->in_len = left < CODEC_IN_BYTES ? left : CODEC_IN_BYTES;
	memcpy(dec->in, &stream[dec->source_offset], dec->in_len);
	dec->source_offset += dec->in_len;
	return 0;
}

static uint32_t stored_index(const capaim_shape_t *shape, uint32_t k)
{
	uint32_t aim = capaim_aim_samples(shape);
	uint32_t slow = capaim_slow_samples(shape);
	uint32_t means = slow / SLOW + (aim - slow + GROUP - 1u) / GROUP;

	if (k < slow) {
		return k / SLOW;
	}
	if (k >= aim) {
		return means + k - aim;
	}
	return means - 1u - (aim - 1u - k) / GROUP;
}

static capdec_t dec = {.refill = refill};
static capaim_reader_t reader = {.dec = &dec};

static void abandon_after_one(const capaim_shape_t *shape)
{
	sp_cap_sample_t s;

	capaim_open(&reader, shape);
	capaim_next(&reader, &s);
}

static int reads_back(const capaim_shape_t *shape)
{
	sp_cap_sample_t s;

	for (uint32_t i = 0; i < capaim_stored(shape); i++) {
		uint32_t h = (i + shape->trigger_index) * HASH_MUL >> HASH_SHIFT;
		stored[i] = uniform((int32_t)(h % VALUE_SPAN) - VALUE_CENTER);
		stored[i].gyro[0] = (int16_t)i;
	}
	encode_stored(capaim_stored(shape));
	capaim_open(&reader, shape);
	for (uint32_t k = 0; k < shape->n_samples; k++) {
		if (capaim_next(&reader, &s) != 1 || memcmp(&s, &stored[stored_index(shape, k)], sizeof(s)) != 0) {
			return 0;
		}
	}
	return capaim_next(&reader, &s) == 0;
}

static void test_reader(void)
{
	capaim_shape_t ext = {EXTENDED_SAMPLES, CAP_EXTENDED_PRE, SP_CAPFMT_EXTENDED};
	capaim_shape_t odd = {ODD_AIM_TRIGGER + CAP_EXTENDED_POST, ODD_AIM_TRIGGER, SP_CAPFMT_EXTENDED};
	capaim_shape_t one = {FULL_RATE + 1u + CAP_EXTENDED_POST, FULL_RATE + 1u, SP_CAPFMT_EXTENDED};
	capaim_shape_t none = {FULL_RATE + CAP_EXTENDED_POST, FULL_RATE, SP_CAPFMT_EXTENDED};
	capaim_shape_t full = {EXTENDED_SAMPLES, CAP_EXTENDED_PRE, SP_CAPFMT_FULL};

	check(reads_back(&ext), "reader repeats each mean 4 times, then passes the full-rate part through");
	check(reads_back(&odd), "reader starts with the partial group when the aiming part is not a multiple of 4");
	check(reads_back(&one), "reader handles a single aiming sample");
	check(reads_back(&none), "reader passes a capture without aiming part through");
	check(reads_back(&full), "reader passes a full-rate record through");
	abandon_after_one(&ext);
	check(reads_back(&odd), "a transfer cut off inside a group leaves nothing behind for the next one");
	capaim_shape_t ultra = {ULTRA_SAMPLES, CAP_ULTRA_PRE, SP_CAPFMT_ULTRA};
	capaim_shape_t ultra_odd = {ULTRA_ODD_TRIGGER + CAP_ULTRA_POST, ULTRA_ODD_TRIGGER, SP_CAPFMT_ULTRA};
	check(reads_back(&ultra), "reader repeats 60 Hz means 8 times, then 120 Hz means 4 times");
	abandon_after_one(&ultra);
	check(reads_back(&ultra_odd), "ultra after a wake, read after a transfer cut off inside a 60 Hz group");
}

int main(void)
{
	test_shape();
	test_rounding();
	test_reader();
	printf("CAPAIM TESTS: %s\n", failures ? "FAIL" : "PASS");
	return failures ? 1 : 0;
}
