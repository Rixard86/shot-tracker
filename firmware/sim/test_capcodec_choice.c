#include <stdio.h>

#include "../src/capcodec.h"
#include "../src/capstore.h"

#define SAMPLES 3841u
#define WRITER_CARRY_BITS (CAPSTORE_WORD_BYTES * CODEC_BYTE_BITS - 1u)
#define LCG_MUL 1103515245u
#define LCG_ADD 12345u
#define LCG_SHIFT 16u
#define GRADES 15u
#define WALK_GRADES 9u
#define QUIET_SPAN 5u
#define SPIKE_ODDS 40u
#define SPIKE_SPAN 60000u
#define RAMP_PERIOD 200u
#define RAMP_STEP 150
#define RAMP_NOISE 3u
#define INT16_HIGH 32767
#define INT16_LOW (-32768)

typedef enum { SIG_GRADED, SIG_WALK, SIG_SPIKES, SIG_RAMP, SIG_EXTREME, SIG_KINDS } signal_t;

typedef struct {
	int32_t last[CODEC_AXES];
	int32_t before[CODEC_AXES];
} history_t;

static sp_cap_sample_t input[SAMPLES];
static int32_t walk[CODEC_AXES];
static uint32_t rng = 1;
static int failures;

static void check(int ok, const char *what)
{
	if (!ok) {
		printf("FAIL: %s\n", what);
		failures++;
	}
}

static uint32_t next_random(void)
{
	rng = rng * LCG_MUL + LCG_ADD;
	return rng >> LCG_SHIFT;
}

static int32_t noise(uint32_t span)
{
	return (int32_t)(next_random() % span) - (int32_t)(span / 2u);
}

static int32_t axis_value(signal_t kind, uint32_t at)
{
	uint32_t axis = at % CODEC_AXES;
	uint32_t index = at / CODEC_AXES;
	uint32_t block = index / CODEC_BLOCK_SAMPLES;

	switch (kind) {
	case SIG_GRADED:
		return noise(2u << (block % GRADES));
	case SIG_WALK:
		walk[axis] += noise(2u << (block % WALK_GRADES));
		return walk[axis];
	case SIG_SPIKES:
		return noise(QUIET_SPAN) + (next_random() % SPIKE_ODDS == 0u ? noise(SPIKE_SPAN) : 0);
	case SIG_RAMP:
		return (int32_t)(index % RAMP_PERIOD) * RAMP_STEP + noise(RAMP_NOISE);
	default:
		return (index % 2u) ? INT16_HIGH : INT16_LOW;
	}
}

static int16_t clamp16(int32_t value)
{
	return (int16_t)(value > INT16_HIGH ? INT16_HIGH : (value < INT16_LOW ? INT16_LOW : value));
}

static void fill(signal_t kind)
{
	for (uint32_t at = 0; at < SAMPLES * CODEC_AXES; at++) {
		codec_axis_set_t set = {at % CODEC_AXES, clamp16(axis_value(kind, at))};

		codec_set_axis(&input[at / CODEC_AXES], set);
	}
}

static uint32_t rice_cost(uint32_t value, uint32_t k)
{
	uint32_t q = value >> k;

	return q < CODEC_ESCAPE_Q ? q + 1u + k : CODEC_ESCAPE_Q + CODEC_ESCAPE_BITS;
}

static uint32_t cheapest(const uint32_t *residual, uint32_t count)
{
	uint32_t best = count * CODEC_RAW_BITS;

	for (uint32_t k = 0; k <= CODEC_MAX_K; k++) {
		uint32_t bits = CODEC_K_BITS;

		for (uint32_t n = 0; n < count; n++) {
			bits += rice_cost(residual[n], k);
		}
		best = bits < best ? bits : best;
	}
	return best;
}

static uint32_t block_bits(history_t *h, const codec_block_t *block)
{
	uint32_t bits = 0;

	for (uint32_t axis = 0; axis < CODEC_AXES; axis++) {
		uint32_t delta[CODEC_BLOCK_SAMPLES];
		uint32_t slope[CODEC_BLOCK_SAMPLES];

		for (uint32_t n = 0; n < block->count; n++) {
			int32_t x = codec_axis_value(&block->samples[n], axis);

			delta[n] = codec_zigzag(x - h->last[axis]);
			slope[n] = codec_zigzag(x - (2 * h->last[axis] - h->before[axis]));
			h->before[axis] = h->last[axis];
			h->last[axis] = x;
		}
		uint32_t by_delta = cheapest(delta, block->count);
		uint32_t by_slope = cheapest(slope, block->count);

		bits += CODEC_MODE_BITS + (by_delta < by_slope ? by_delta : by_slope);
	}
	return bits;
}

static int blocks_are_cheapest(void)
{
	history_t history = {{0}, {0}};
	capenc_t enc;
	int cheapest_used = 1;

	capenc_init(&enc);
	for (uint32_t first = 0; first < SAMPLES; first += CODEC_BLOCK_SAMPLES) {
		uint32_t left = SAMPLES - first;
		codec_block_t block = {&input[first], left < CODEC_BLOCK_SAMPLES ? left : CODEC_BLOCK_SAMPLES};
		uint32_t before = enc.out_bits;

		cheapest_used &= capenc_block(&enc, &block) == 0;
		cheapest_used &= enc.out_bits - before == block_bits(&history, &block);
		capenc_drop(&enc, capenc_ready(&enc));
	}
	return cheapest_used;
}

static void test_cheapest_coding(void)
{
	static const char *const names[SIG_KINDS] = {
		"graded noise uses the cheapest predictor and Rice parameter",
		"random walk uses the cheapest predictor and Rice parameter",
		"spikes use the cheapest predictor and Rice parameter",
		"ramp uses the cheapest predictor and Rice parameter",
		"extremes use the cheapest coding",
	};

	for (uint32_t kind = 0; kind < SIG_KINDS; kind++) {
		fill((signal_t)kind);
		check(blocks_are_cheapest(), names[kind]);
	}
}

static void test_buffer_bound(void)
{
	capenc_t enc;
	codec_block_t block = {input, CODEC_BLOCK_SAMPLES};

	fill(SIG_EXTREME);
	capenc_init(&enc);
	enc.out_bits = WRITER_CARRY_BITS;
	check(capenc_block(&enc, &block) == 0, "worst-case block encodes");
	check(enc.out_bits - WRITER_CARRY_BITS == CODEC_BLOCK_MAX_BITS, "an all-raw block takes the largest size");
	check(enc.out_bits <= CODEC_OUT_BYTES * CODEC_BYTE_BITS,
	      "the largest block fits behind what the capture writer leaves (under one flash word)");
}

int main(void)
{
	test_cheapest_coding();
	test_buffer_bound();
	printf("CODEC CHOICE TESTS: %s\n", failures ? "FAIL" : "PASS");
	return failures ? 1 : 0;
}
