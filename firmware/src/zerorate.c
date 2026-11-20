#include "zerorate.h"

#include <string.h>

#include "capture.h"

#define MS_PER_S 1000u
#define WINDOW_SQ ((int64_t)ZR_WINDOW_SAMPLES * ZR_WINDOW_SAMPLES)
#define SPREAD_MAX(std, per_lsb) ((int64_t)(std) * (std) * WINDOW_SQ / ((int64_t)(per_lsb) * (per_lsb)))
#define GYRO_SPREAD_MAX SPREAD_MAX(ZR_GYRO_STD_MAX_MDPS, CAP_GYRO_MDPS_PER_LSB)
#define ACC_SPREAD_MAX SPREAD_MAX(ZR_ACC_STD_MAX_UG, CAP_ACC_UG_PER_LSB)
#define GYRO_SUM_MAX ((int64_t)ZR_OFFSET_MAX_MDPS * ZR_WINDOW_SAMPLES / CAP_GYRO_MDPS_PER_LSB)

static void clear_window(zr_t *zr)
{
	memset(&zr->gyro, 0, sizeof(zr->gyro));
	memset(&zr->acc, 0, sizeof(zr->acc));
	zr->n = 0;
}

void zr_restart(zr_t *zr)
{
	clear_window(zr);
	zr->settle = ZR_SETTLE_SAMPLES;
}

static void add(zr_moments_t *m, const int16_t *axes)
{
	for (uint32_t i = 0; i < ZR_AXES; i++) {
		m->sum[i] += axes[i];
		m->sq[i] += (int64_t)axes[i] * axes[i];
	}
}

static bool spread_within(const zr_moments_t *m, int64_t spread_max)
{
	for (uint32_t i = 0; i < ZR_AXES; i++) {
		int64_t spread = (int64_t)ZR_WINDOW_SAMPLES * m->sq[i] - m->sum[i] * m->sum[i];

		if (spread > spread_max) {
			return false;
		}
	}
	return true;
}

static bool plausible(const zr_moments_t *gyro)
{
	for (uint32_t i = 0; i < ZR_AXES; i++) {
		if (gyro->sum[i] > GYRO_SUM_MAX || gyro->sum[i] < -GYRO_SUM_MAX) {
			return false;
		}
	}
	return true;
}

static int16_t mean_mdps(int64_t sum)
{
	int64_t scaled = sum * CAP_GYRO_MDPS_PER_LSB;
	int64_t half = ZR_WINDOW_SAMPLES / 2u;

	return (int16_t)(scaled >= 0 ? (scaled + half) / ZR_WINDOW_SAMPLES : -((-scaled + half) / ZR_WINDOW_SAMPLES));
}

static void take_window(zr_t *zr, uint32_t end_ms)
{
	for (uint32_t i = 0; i < ZR_AXES; i++) {
		zr->mdps[i] = mean_mdps(zr->gyro.sum[i]);
	}
	zr->measured_ms = end_ms;
	zr->valid = true;
}

void zr_push(zr_t *zr, const zr_input_t *in)
{
	if (zr->settle > 0u) {
		zr->settle--;
		return;
	}
	int16_t gyro[ZR_AXES];
	int16_t acc[ZR_AXES];

	memcpy(gyro, in->sample->gyro, sizeof(gyro));
	memcpy(acc, in->sample->acc, sizeof(acc));
	add(&zr->gyro, gyro);
	add(&zr->acc, acc);
	if (++zr->n < ZR_WINDOW_SAMPLES) {
		return;
	}
	if (spread_within(&zr->gyro, GYRO_SPREAD_MAX) && spread_within(&zr->acc, ACC_SPREAD_MAX) &&
	    plausible(&zr->gyro)) {
		take_window(zr, in->uptime_ms);
	}
	clear_window(zr);
}

sp_gyro_offset_t zr_offset_at(const zr_t *zr, uint32_t trigger_ms)
{
	sp_gyro_offset_t out = {.age_s = SP_GYRO_OFFSET_UNKNOWN};

	if (!zr->valid) {
		return out;
	}
	uint32_t age_s = (trigger_ms - zr->measured_ms) / MS_PER_S;

	memcpy(out.mdps, zr->mdps, sizeof(out.mdps));
	out.age_s = (uint16_t)(age_s < SP_GYRO_OFFSET_AGE_MAX_S ? age_s : SP_GYRO_OFFSET_AGE_MAX_S);
	return out;
}
