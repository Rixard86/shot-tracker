#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../src/capcodec.h"

#define MAX_SAMPLES 4000u
#define STREAM_BYTES (MAX_SAMPLES * sizeof(sp_cap_sample_t) * 2u)
#define ODD_LENGTH 3841u
#define SHORT_LENGTH 7u
#define STANDARD_LENGTH 720u
#define LCG_MUL 1103515245u
#define LCG_ADD 12345u
#define LCG_SHIFT 16u
#define NOISE_SPAN 7
#define NOISE_CENTER 3
#define AMPLITUDE 2000.0
#define PERIOD 250.0
#define TWO_PI 6.283185307179586
#define FULL_SPAN 65536u
#define INT16_OFFSET 32768
#define EXTREME_HIGH 32767
#define EXTREME_LOW (-32768)
#define CONSTANT_VALUE (-1234)
#define SMOOTH_RATIO_MAX 0.5
#define CONSTANT_RATIO_DIV 10u
#define RAW_OVERHEAD 1.02
#define TRUNCATE_KEEP 0.5

typedef enum { SIG_SMOOTH, SIG_CONSTANT, SIG_EXTREME, SIG_RANDOM } signal_t;

typedef struct {
	const uint8_t *data;
	uint32_t len;
} source_t;

static sp_cap_sample_t input[MAX_SAMPLES];
static uint8_t stream[STREAM_BYTES];
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

static int32_t signal_value(signal_t kind, uint32_t index)
{
	switch (kind) {
	case SIG_SMOOTH:
		return (int32_t)(AMPLITUDE * sin(TWO_PI * index / PERIOD)) +
		       (int32_t)(next_random() % NOISE_SPAN) - NOISE_CENTER;
	case SIG_CONSTANT:
		return CONSTANT_VALUE;
	case SIG_EXTREME:
		return (index % 2u) ? EXTREME_HIGH : EXTREME_LOW;
	default:
		return (int32_t)(next_random() % FULL_SPAN) - INT16_OFFSET;
	}
}

static void fill(signal_t kind, uint32_t count)
{
	for (uint32_t i = 0; i < count; i++) {
		for (uint32_t axis = 0; axis < CODEC_AXES; axis++) {
			codec_set_axis(&input[i], (codec_axis_set_t){axis, (int16_t)signal_value(kind, i + axis)});
		}
	}
}

static uint32_t encode(uint32_t count)
{
	capenc_t enc;
	uint32_t len = 0;

	capenc_init(&enc);
	for (uint32_t first = 0; first < count; first += CODEC_BLOCK_SAMPLES) {
		uint32_t left = count - first;
		codec_block_t block = {&input[first], left < CODEC_BLOCK_SAMPLES ? left : CODEC_BLOCK_SAMPLES};
		check(capenc_block(&enc, &block) == 0, "block encodes");
		codec_bytes_t out = {&stream[len], STREAM_BYTES - len};
		len += capenc_take(&enc, &out);
	}
	capenc_finish(&enc);
	codec_bytes_t out = {&stream[len], STREAM_BYTES - len};
	return len + capenc_take(&enc, &out);
}

static int refill(capdec_t *dec)
{
	const source_t *src = dec->context;
	uint32_t left = src->len - dec->source_offset;
	uint32_t n = left < CODEC_IN_BYTES ? left : CODEC_IN_BYTES;

	memcpy(dec->in, src->data + dec->source_offset, n);
	dec->in_len = n;
	dec->source_offset += n;
	return 0;
}

static int decodes_back(uint32_t count, uint32_t len)
{
	source_t src = {stream, len};
	capdec_t dec = {.refill = refill, .context = &src};
	sp_cap_sample_t s;

	capdec_init(&dec, count);
	for (uint32_t i = 0; i < count; i++) {
		if (capdec_next(&dec, &s) != 1 || memcmp(&s, &input[i], sizeof(s)) != 0) {
			return 0;
		}
	}
	return capdec_next(&dec, &s) == 0;
}

static uint32_t round_trip(signal_t kind, uint32_t count)
{
	fill(kind, count);
	uint32_t len = encode(count);
	check(decodes_back(count, len), "decoded samples equal the input");
	return len;
}

static void test_signals(void)
{
	uint32_t raw = ODD_LENGTH * (uint32_t)sizeof(sp_cap_sample_t);

	check(round_trip(SIG_SMOOTH, ODD_LENGTH) < raw * SMOOTH_RATIO_MAX, "smooth signal compresses 2x");
	check(round_trip(SIG_CONSTANT, ODD_LENGTH) < raw / CONSTANT_RATIO_DIV, "constant signal near free");
	check(round_trip(SIG_EXTREME, ODD_LENGTH) <= raw * RAW_OVERHEAD, "extremes fall back to raw");
	check(round_trip(SIG_RANDOM, ODD_LENGTH) <= raw * RAW_OVERHEAD, "noise never grows past raw");
	round_trip(SIG_SMOOTH, SHORT_LENGTH);
	round_trip(SIG_RANDOM, STANDARD_LENGTH);
}

static void test_truncated(void)
{
	fill(SIG_RANDOM, STANDARD_LENGTH);
	uint32_t len = encode(STANDARD_LENGTH);
	source_t src = {stream, (uint32_t)(len * TRUNCATE_KEEP)};
	capdec_t dec = {.refill = refill, .context = &src};
	sp_cap_sample_t s;
	int result = 1;

	capdec_init(&dec, STANDARD_LENGTH);
	for (uint32_t i = 0; i < STANDARD_LENGTH && result == 1; i++) {
		result = capdec_next(&dec, &s);
	}
	check(result < 0, "truncated stream reports an error");
}

int main(void)
{
	test_signals();
	test_truncated();
	printf("CODEC TESTS: %s\n", failures ? "FAIL" : "PASS");
	return failures ? 1 : 0;
}
