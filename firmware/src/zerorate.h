#ifndef ZERORATE_H
#define ZERORATE_H

#include <stdbool.h>
#include <stdint.h>

#include "protocol.h"

#define ZR_AXES 3u
#define ZR_WINDOW_SAMPLES 480u
#define ZR_SETTLE_SAMPLES 48u
#define ZR_GYRO_STD_MAX_MDPS 250
#define ZR_ACC_STD_MAX_UG 5000
#define ZR_OFFSET_MAX_MDPS 5000

typedef struct {
	const sp_cap_sample_t *sample;
	uint32_t uptime_ms;
} zr_input_t;

typedef struct {
	int64_t sum[ZR_AXES];
	int64_t sq[ZR_AXES];
} zr_moments_t;

typedef struct {
	zr_moments_t gyro;
	zr_moments_t acc;
	uint32_t n;
	uint32_t settle;
	int16_t mdps[ZR_AXES];
	uint32_t measured_ms;
	bool valid;
} zr_t;

void zr_restart(zr_t *zr);
void zr_push(zr_t *zr, const zr_input_t *in);
sp_gyro_offset_t zr_offset_at(const zr_t *zr, uint32_t trigger_ms);

#endif
