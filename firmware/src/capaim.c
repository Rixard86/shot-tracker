#include "capaim.h"

#define PARITY_DIVISOR 2

uint32_t capaim_aim_samples(const capaim_shape_t *shape)
{
	if (shape->format == SP_CAPFMT_FULL || shape->trigger_index <= CAPAIM_FULL_RATE_PRE) {
		return 0u;
	}
	return shape->trigger_index - CAPAIM_FULL_RATE_PRE;
}

uint32_t capaim_slow_samples(const capaim_shape_t *shape)
{
	if (shape->format != SP_CAPFMT_ULTRA || shape->trigger_index <= CAPAIM_MEDIUM_RATE_PRE) {
		return 0u;
	}
	uint32_t span = shape->trigger_index - CAPAIM_MEDIUM_RATE_PRE;

	return span - span % CAPAIM_SLOW_GROUP_SAMPLES;
}

uint32_t capaim_stored(const capaim_shape_t *shape)
{
	uint32_t aim = capaim_aim_samples(shape);
	uint32_t slow = capaim_slow_samples(shape);
	uint32_t means = (aim - slow + CAPAIM_GROUP_SAMPLES - 1u) / CAPAIM_GROUP_SAMPLES;

	return slow / CAPAIM_SLOW_GROUP_SAMPLES + means + shape->n_samples - aim;
}

uint32_t capaim_group(uint32_t aim_left)
{
	uint32_t partial = aim_left % CAPAIM_GROUP_SAMPLES;

	return partial ? partial : CAPAIM_GROUP_SAMPLES;
}

static int16_t round_mean(int32_t sum, uint32_t count)
{
	int32_t divisor = (int32_t)count;
	int32_t quotient = sum / divisor;
	int32_t remainder = sum % divisor;

	if (remainder < 0) {
		quotient--;
		remainder += divisor;
	}
	int32_t to_next = divisor - remainder;
	bool odd = quotient % PARITY_DIVISOR != 0;

	if (remainder > to_next || (remainder == to_next && odd)) {
		quotient++;
	}
	return (int16_t)quotient;
}

sp_cap_sample_t capaim_mean(const sp_cap_sample_t *group, uint32_t count)
{
	sp_cap_sample_t mean;

	for (uint32_t axis = 0; axis < CODEC_AXES; axis++) {
		int32_t sum = 0;

		for (uint32_t i = 0; i < count; i++) {
			sum += codec_axis_value(&group[i], axis);
		}
		codec_set_axis(&mean, (codec_axis_set_t){axis, round_mean(sum, count)});
	}
	return mean;
}

void capaim_open(capaim_reader_t *reader, const capaim_shape_t *shape)
{
	reader->aim_left = capaim_aim_samples(shape);
	reader->slow_left = capaim_slow_samples(shape);
	reader->repeat_left = 0u;
	capdec_init(reader->dec, capaim_stored(shape));
}

int capaim_next(capaim_reader_t *reader, sp_cap_sample_t *sample)
{
	if (reader->aim_left == 0u) {
		return capdec_next(reader->dec, sample);
	}
	if (reader->repeat_left == 0u) {
		int got = capdec_next(reader->dec, &reader->mean);
		if (got != 1) {
			return got;
		}
		reader->repeat_left = reader->slow_left > 0u ? CAPAIM_SLOW_GROUP_SAMPLES : capaim_group(reader->aim_left);
	}
	reader->repeat_left--;
	reader->aim_left--;
	if (reader->slow_left > 0u) {
		reader->slow_left--;
	}
	*sample = reader->mean;
	return 1;
}
