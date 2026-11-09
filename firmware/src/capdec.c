#include "capcodec.h"

#include <string.h>

#define BITS_PER_BYTE 8u
#define TOP_BIT (BITS_PER_BYTE - 1u)
#define INT16_LOW (-32768)
#define INT16_HIGH 32767

typedef struct {
	uint32_t axis;
	uint32_t mode;
	uint32_t k;
	int32_t value;
} axis_plan_t;

void capdec_init(capdec_t *dec, uint32_t n_samples)
{
	capdec_refill_t refill = dec->refill;
	void *context = dec->context;

	memset(dec, 0, sizeof(*dec));
	dec->refill = refill;
	dec->context = context;
	dec->remaining = n_samples;
}

static int get_bit(capdec_t *dec)
{
	if (dec->in_bit == dec->in_len * BITS_PER_BYTE) {
		dec->in_len = 0;
		dec->in_bit = 0;
		if (!dec->refill || dec->refill(dec) != 0 || dec->in_len == 0) {
			return CODEC_ERR_DATA;
		}
	}
	uint32_t byte = dec->in[dec->in_bit / BITS_PER_BYTE];
	uint32_t shift = TOP_BIT - dec->in_bit % BITS_PER_BYTE;

	dec->in_bit++;
	return (int)((byte >> shift) & 1u);
}

static int32_t get_bits(capdec_t *dec, uint32_t count)
{
	int32_t value = 0;

	for (uint32_t i = 0; i < count; i++) {
		int bit = get_bit(dec);
		if (bit < 0) {
			return bit;
		}
		value = (value << 1) | bit;
	}
	return value;
}

static int32_t get_rice(capdec_t *dec, uint32_t k)
{
	uint32_t q = 0;

	for (;;) {
		int bit = get_bit(dec);
		if (bit < 0) {
			return bit;
		}
		if (bit == 0) {
			break;
		}
		if (++q == CODEC_ESCAPE_Q) {
			return get_bits(dec, CODEC_ESCAPE_BITS);
		}
	}
	int32_t low = get_bits(dec, k);

	return low < 0 ? low : (int32_t)((q << k) | (uint32_t)low);
}

static int next_value(capdec_t *dec, axis_plan_t *plan)
{
	if (plan->mode == CODEC_MODE_RAW) {
		int32_t raw = get_bits(dec, CODEC_RAW_BITS);
		if (raw < 0) {
			return raw;
		}
		plan->value = (int16_t)(uint16_t)raw;
		return 0;
	}
	int32_t code = get_rice(dec, plan->k);
	if (code < 0) {
		return code;
	}
	int32_t last = dec->last[plan->axis];
	int32_t predicted = plan->mode == CODEC_MODE_DELTA ? last : 2 * last - dec->before_last[plan->axis];
	int32_t x = predicted + codec_unzigzag((uint32_t)code);

	if (x < INT16_LOW || x > INT16_HIGH) {
		return CODEC_ERR_DATA;
	}
	plan->value = x;
	return 0;
}

static int read_plan(capdec_t *dec, axis_plan_t *plan)
{
	int32_t mode = get_bits(dec, CODEC_MODE_BITS);

	if (mode < 0 || mode > CODEC_MODE_RAW) {
		return CODEC_ERR_DATA;
	}
	plan->mode = (uint32_t)mode;
	plan->k = 0;
	if (plan->mode == CODEC_MODE_RAW) {
		return 0;
	}
	int32_t k = get_bits(dec, CODEC_K_BITS);
	if (k < 0 || k > (int32_t)CODEC_MAX_K) {
		return CODEC_ERR_DATA;
	}
	plan->k = (uint32_t)k;
	return 0;
}

static int decode_axis(capdec_t *dec, uint32_t axis)
{
	axis_plan_t plan = {.axis = axis};
	int err = read_plan(dec, &plan);

	if (err) {
		return err;
	}
	for (uint32_t n = 0; n < dec->block_len; n++) {
		err = next_value(dec, &plan);
		if (err) {
			return err;
		}
		codec_set_axis(&dec->block[n], (codec_axis_set_t){axis, (int16_t)plan.value});
		dec->before_last[axis] = dec->last[axis];
		dec->last[axis] = plan.value;
	}
	return 0;
}

static int decode_block(capdec_t *dec)
{
	dec->block_len = dec->remaining < CODEC_BLOCK_SAMPLES ? dec->remaining : CODEC_BLOCK_SAMPLES;
	dec->block_next = 0;
	for (uint32_t axis = 0; axis < CODEC_AXES; axis++) {
		int err = decode_axis(dec, axis);
		if (err) {
			dec->remaining = 0;
			dec->block_len = 0;
			return err;
		}
	}
	dec->remaining -= dec->block_len;
	return 0;
}

int capdec_next(capdec_t *dec, sp_cap_sample_t *sample)
{
	if (dec->block_next == dec->block_len) {
		if (dec->remaining == 0) {
			return 0;
		}
		int err = decode_block(dec);
		if (err) {
			return err;
		}
	}
	*sample = dec->block[dec->block_next++];
	return 1;
}
