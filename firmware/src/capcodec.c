#include "capcodec.h"

#include <string.h>

#define BITS_PER_BYTE 8u
#define ACC_AXES 3u

typedef struct {
	uint32_t value;
	uint32_t count;
} codec_bits_t;

typedef struct {
	const codec_block_t *block;
	uint32_t axis;
	uint32_t residual[CODEC_PREDICTORS][CODEC_BLOCK_SAMPLES];
	uint32_t mode;
	uint32_t k;
} axis_job_t;

static uint32_t ones(uint32_t count)
{
	return (1u << count) - 1u;
}

void codec_set_axis(sp_cap_sample_t *sample, codec_axis_set_t set)
{
	if (set.axis < ACC_AXES) {
		sample->acc[set.axis] = set.value;
	} else {
		sample->gyro[set.axis - ACC_AXES] = set.value;
	}
}

int32_t codec_axis_value(const sp_cap_sample_t *sample, uint32_t axis)
{
	return axis < ACC_AXES ? sample->acc[axis] : sample->gyro[axis - ACC_AXES];
}

uint32_t codec_zigzag(int32_t value)
{
	return value < 0 ? ((uint32_t)(-(value + 1)) << 1) | 1u : (uint32_t)value << 1;
}

int32_t codec_unzigzag(uint32_t value)
{
	return (value & 1u) ? -(int32_t)(value >> 1) - 1 : (int32_t)(value >> 1);
}

void capenc_init(capenc_t *enc)
{
	memset(enc, 0, sizeof(*enc));
}

static void put_bits(capenc_t *enc, codec_bits_t bits)
{
	for (uint32_t i = bits.count; i > 0; i--) {
		uint32_t byte = enc->out_bits / BITS_PER_BYTE;
		uint32_t shift = (BITS_PER_BYTE - 1u) - enc->out_bits % BITS_PER_BYTE;

		if (shift == BITS_PER_BYTE - 1u) {
			enc->out[byte] = 0;
		}
		enc->out[byte] |= (uint8_t)(((bits.value >> (i - 1u)) & 1u) << shift);
		enc->out_bits++;
	}
}

static uint32_t rice_bits(uint32_t value, uint32_t k)
{
	uint32_t q = value >> k;

	return q < CODEC_ESCAPE_Q ? q + 1u + k : CODEC_ESCAPE_Q + CODEC_ESCAPE_BITS;
}

static void put_rice(capenc_t *enc, codec_bits_t code)
{
	uint32_t q = code.value >> code.count;

	if (q >= CODEC_ESCAPE_Q) {
		put_bits(enc, (codec_bits_t){ones(CODEC_ESCAPE_Q), CODEC_ESCAPE_Q});
		put_bits(enc, (codec_bits_t){code.value, CODEC_ESCAPE_BITS});
		return;
	}
	put_bits(enc, (codec_bits_t){ones(q) << 1, q + 1u});
	put_bits(enc, (codec_bits_t){code.value & ones(code.count), code.count});
}

static void build_series(capenc_t *enc, axis_job_t *job)
{
	int32_t last = enc->last[job->axis];
	int32_t before = enc->before_last[job->axis];

	for (uint32_t n = 0; n < job->block->count; n++) {
		int32_t x = codec_axis_value(&job->block->samples[n], job->axis);

		job->residual[CODEC_MODE_DELTA][n] = codec_zigzag(x - last);
		job->residual[CODEC_MODE_SLOPE][n] = codec_zigzag(x - (2 * last - before));
		before = last;
		last = x;
	}
	enc->last[job->axis] = last;
	enc->before_last[job->axis] = before;
}

static uint32_t series_cost(const axis_job_t *job, uint32_t k)
{
	uint32_t bits = CODEC_K_BITS;

	for (uint32_t n = 0; n < job->block->count; n++) {
		bits += rice_bits(job->residual[job->mode][n], k);
	}
	return bits;
}

static void choose(axis_job_t *job)
{
	uint32_t best = job->block->count * CODEC_RAW_BITS;
	uint32_t best_mode = CODEC_MODE_RAW;
	uint32_t best_k = 0;

	for (job->mode = 0; job->mode < CODEC_PREDICTORS; job->mode++) {
		for (uint32_t k = 0; k <= CODEC_MAX_K; k++) {
			uint32_t bits = series_cost(job, k);
			if (bits < best) {
				best = bits;
				best_mode = job->mode;
				best_k = k;
			}
		}
	}
	job->mode = best_mode;
	job->k = best_k;
}

static void put_axis(capenc_t *enc, const axis_job_t *job)
{
	put_bits(enc, (codec_bits_t){job->mode, CODEC_MODE_BITS});
	if (job->mode == CODEC_MODE_RAW) {
		for (uint32_t n = 0; n < job->block->count; n++) {
			int32_t x = codec_axis_value(&job->block->samples[n], job->axis);
			put_bits(enc, (codec_bits_t){(uint16_t)x, CODEC_RAW_BITS});
		}
		return;
	}
	put_bits(enc, (codec_bits_t){job->k, CODEC_K_BITS});
	for (uint32_t n = 0; n < job->block->count; n++) {
		put_rice(enc, (codec_bits_t){job->residual[job->mode][n], job->k});
	}
}

int capenc_block(capenc_t *enc, const codec_block_t *block)
{
	if (block->count == 0 || block->count > CODEC_BLOCK_SAMPLES) {
		return CODEC_ERR_INPUT;
	}
	for (uint32_t axis = 0; axis < CODEC_AXES; axis++) {
		axis_job_t job = {.block = block, .axis = axis};

		build_series(enc, &job);
		choose(&job);
		put_axis(enc, &job);
	}
	return 0;
}

void capenc_finish(capenc_t *enc)
{
	enc->out_bits = (enc->out_bits + BITS_PER_BYTE - 1u) / BITS_PER_BYTE * BITS_PER_BYTE;
}

uint32_t capenc_take(capenc_t *enc, codec_bytes_t *bytes)
{
	uint32_t whole = enc->out_bits / BITS_PER_BYTE;
	uint32_t n = whole < bytes->len ? whole : bytes->len;
	uint32_t keep_bits = enc->out_bits - n * BITS_PER_BYTE;

	memcpy(bytes->data, enc->out, n);
	memmove(enc->out, enc->out + n, (keep_bits + BITS_PER_BYTE - 1u) / BITS_PER_BYTE);
	enc->out_bits = keep_bits;
	bytes->len = n;
	return n;
}
