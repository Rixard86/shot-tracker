#ifndef CAPAIM_H
#define CAPAIM_H

#include <stdbool.h>
#include <stdint.h>

#include "capcodec.h"
#include "capture.h"

#define CAPAIM_GROUP_SAMPLES 4u
#define CAPAIM_SLOW_GROUP_SAMPLES CAP_HISTORY_GROUP
#define CAPAIM_FULL_RATE_PRE CAP_ODR_HZ
#define CAPAIM_MEDIUM_RATE_PRE CAP_EXTENDED_PRE

typedef struct {
	uint16_t n_samples;
	uint16_t trigger_index;
	uint8_t format;
} capaim_shape_t;

typedef struct {
	capdec_t *dec;
	uint32_t slow_left;
	uint32_t aim_left;
	uint32_t repeat_left;
	sp_cap_sample_t mean;
} capaim_reader_t;

uint32_t capaim_aim_samples(const capaim_shape_t *shape);
uint32_t capaim_slow_samples(const capaim_shape_t *shape);
uint32_t capaim_stored(const capaim_shape_t *shape);
uint32_t capaim_group(uint32_t aim_left);
sp_cap_sample_t capaim_mean(const sp_cap_sample_t *group, uint32_t count);
void capaim_open(capaim_reader_t *reader, const capaim_shape_t *shape);
int capaim_next(capaim_reader_t *reader, sp_cap_sample_t *sample);

#endif
