#ifndef CAPCODEC_H
#define CAPCODEC_H

#include <stdint.h>

#include "protocol.h"

#define CODEC_BLOCK_SAMPLES 32u
#define CODEC_AXES 6u
#define CODEC_MODE_BITS 2u
#define CODEC_K_BITS 5u
#define CODEC_MAX_K 17u
#define CODEC_RAW_BITS 16u
#define CODEC_ESCAPE_Q 24u
#define CODEC_ESCAPE_BITS 18u
#define CODEC_BYTE_BITS 8u
#define CODEC_BLOCK_MAX_BITS (CODEC_AXES * (CODEC_MODE_BITS + CODEC_RAW_BITS * CODEC_BLOCK_SAMPLES))
#define CODEC_CARRY_BYTES 4u
#define CODEC_OUT_BYTES ((CODEC_BLOCK_MAX_BITS + CODEC_BYTE_BITS - 1u) / CODEC_BYTE_BITS + CODEC_CARRY_BYTES)
#define CODEC_IN_BYTES 64u
#define CODEC_ERR_INPUT (-1)
#define CODEC_ERR_DATA (-2)

enum { CODEC_MODE_DELTA, CODEC_MODE_SLOPE, CODEC_MODE_RAW, CODEC_PREDICTORS = CODEC_MODE_RAW };

typedef struct {
	const sp_cap_sample_t *samples;
	uint32_t count;
} codec_block_t;

typedef struct {
	uint32_t axis;
	int16_t value;
} codec_axis_set_t;

typedef struct {
	int32_t last[CODEC_AXES];
	int32_t before_last[CODEC_AXES];
	uint8_t out[CODEC_OUT_BYTES];
	uint32_t out_bits;
} capenc_t;

typedef struct capdec capdec_t;
typedef int (*capdec_refill_t)(capdec_t *dec);

struct capdec {
	int32_t last[CODEC_AXES];
	int32_t before_last[CODEC_AXES];
	sp_cap_sample_t block[CODEC_BLOCK_SAMPLES];
	uint32_t block_len;
	uint32_t block_next;
	uint32_t remaining;
	uint8_t in[CODEC_IN_BYTES];
	uint32_t in_len;
	uint32_t in_bit;
	uint32_t source_offset;
	capdec_refill_t refill;
	void *context;
};

void capenc_init(capenc_t *enc);
int capenc_block(capenc_t *enc, const codec_block_t *block);
void capenc_finish(capenc_t *enc);
uint32_t capenc_ready(const capenc_t *enc);
void capenc_drop(capenc_t *enc, uint32_t bytes);

void capdec_init(capdec_t *dec, uint32_t n_samples);
int capdec_next(capdec_t *dec, sp_cap_sample_t *sample);

void codec_set_axis(sp_cap_sample_t *sample, codec_axis_set_t set);
int32_t codec_axis_value(const sp_cap_sample_t *sample, uint32_t axis);
uint32_t codec_zigzag(int32_t value);
int32_t codec_unzigzag(uint32_t value);

#endif
