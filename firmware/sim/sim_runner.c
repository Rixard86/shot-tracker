/*
 * sim_runner.c - host-side pipeline around shot_detect.c, mirroring the
 * firmware: IDLE at a slow rate with motion wake, ACTIVE at cfg.fs_hz,
 * LIS2DE12-like sensor model (+-16 g clip, 8-bit / 187.5 mg quantisation,
 * point sampling = worst-case aliasing, no anti-alias filter assumed).
 */
#include <math.h>
#include <stdint.h>
#include "../src/shot_detect.h"

#define LSB_MG 187.5f
#define CLIP_MG 16000.0f

static int16_t sensor(float v)
{
	if (v > CLIP_MG) v = CLIP_MG;
	if (v < -CLIP_MG) v = -CLIP_MG;
	return (int16_t)(roundf(v / LSB_MG) * LSB_MG);
}

typedef struct {
	uint32_t active_ms;
	uint32_t wakes;
	uint32_t n_events;
} sim_stats_t;

int sim_run(const float *ax, const float *ay, const float *az, int n, float fs_true,
	    const sd_config_t *cfg, float idle_hz, int wake_latency_ms,
	    sd_event_t *events, int max_events, sim_stats_t *st)
{
	sd_state_t s;
	sd_init(&s, cfg);
	int step_active = (int)lroundf(fs_true / cfg->fs_hz);
	int step_idle = (int)lroundf(fs_true / idle_hz);
	int ne = 0;
	int pending_wake = -1; /* sample index at which ACTIVE starts */
	st->active_ms = 0;
	st->wakes = 0;

	for (int i = 0; i < n;) {
		uint32_t t_ms = (uint32_t)((double)i * 1000.0 / fs_true);
		int16_t xyz[3] = {sensor(ax[i]), sensor(ay[i]), sensor(az[i])};

		if (s.mode == SD_MODE_IDLE) {
			if (pending_wake >= 0) {
				if (i >= pending_wake) {
					sd_start_active(&s, t_ms);
					pending_wake = -1;
					st->wakes++;
					continue;
				}
				i++;
				continue;
			}
			if (sd_idle_sample(&s, xyz, t_ms)) {
				pending_wake = i + (int)(wake_latency_ms * fs_true / 1000.0f);
			}
			i += step_idle;
		} else {
			sd_event_t ev;
			if (sd_process(&s, xyz, t_ms, &ev) && ne < max_events) {
				events[ne++] = ev;
			}
			if (sd_active_should_sleep(&s, t_ms)) {
				s.mode = SD_MODE_IDLE;
				s.have_idle_ref = 0;
			}
			st->active_ms += (uint32_t)(1000.0f / cfg->fs_hz);
			i += step_active;
		}
	}
	st->n_events = (uint32_t)ne;
	return ne;
}
