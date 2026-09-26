/*
 * shot_detect.c - see shot_detect.h for the detection model.
 * Pure C99 + libm. Used by firmware and by firmware/sim.
 */
#include "shot_detect.h"
#include <math.h>
#include <string.h>

#define GUARD_SAMPLES 8u   /* motion within this many samples of the trigger
                              is attributed to the shot itself (~20 ms)   */
#define PEAK_WIN_MS 20u
#define TAU_GRAVITY_S 0.25f /* gravity estimate time constant             */
#define TAU_LF_S 0.03f     /* low-frequency motion time constant (~5 Hz)  */

void sd_default_config(sd_config_t *c)
{
	c->fs_hz = 400.0f;
	c->trig_hf_mg = 6000.0f;
	c->move_mg = 300.0f;
	c->pre_still_min_ms = 500;
	c->post_start_ms = 100;
	c->post_end_ms = 600;
	c->post_lf_min_mg = 250.0f;
	c->refractory_ms = 1500;
	c->wake_mg = 150.0f;
	c->idle_timeout_ms = 30000;
}

static float norm3(float x, float y, float z)
{
	return sqrtf(x * x + y * y + z * z);
}

static uint16_t sat16(float v)
{
	if (v < 0.0f) {
		return 0;
	}
	return v > 65535.0f ? 65535u : (uint16_t)(v + 0.5f);
}

void sd_init(sd_state_t *s, const sd_config_t *cfg)
{
	memset(s, 0, sizeof(*s));
	s->cfg = *cfg;
	float dt = 1.0f / cfg->fs_hz;
	s->a_g_lp = 1.0f - expf(-dt / TAU_GRAVITY_S);
	s->a_lf = 1.0f - expf(-dt / TAU_LF_S);
	s->mode = SD_MODE_IDLE;
}

void sd_start_active(sd_state_t *s, uint32_t now_ms)
{
	s->have_prev = 0;
	s->cand_active = 0;
	s->move_hist = 0;
	s->last_move_ms = now_ms;
	s->mode = SD_MODE_ACTIVE;
}

static void finalize(sd_state_t *s, sd_event_t *ev)
{
	const sd_config_t *c = &s->cfg;
	float post = s->post_n ? s->post_sum / (float)s->post_n : 0.0f;

	s->cand.post_lf_mg = sat16(post);
	if (s->cand.pre_still_ms < c->pre_still_min_ms) {
		s->cand.accepted = 0;
		s->cand.reason = SD_REJ_NOT_STILL;
	} else if (post < c->post_lf_min_mg) {
		s->cand.accepted = 0;
		s->cand.reason = SD_REJ_NO_FOLLOW;
	} else {
		s->cand.accepted = 1;
		s->cand.reason = SD_REJ_NONE;
	}
	*ev = s->cand;
	s->cand_active = 0;
}

int sd_process(sd_state_t *s, const int16_t xyz_mg[3], uint32_t t_ms, sd_event_t *ev)
{
	const sd_config_t *c = &s->cfg;
	float x[3] = {(float)xyz_mg[0], (float)xyz_mg[1], (float)xyz_mg[2]};
	int done = 0;

	if (!s->have_prev) {
		for (int i = 0; i < 3; i++) {
			s->g_lp[i] = x[i];
			s->lf[i] = x[i];
			s->prev[i] = x[i];
		}
		s->have_prev = 1;
		return 0;
	}

	float hf = norm3(x[0] - s->prev[0], x[1] - s->prev[1], x[2] - s->prev[2]);
	for (int i = 0; i < 3; i++) {
		s->prev[i] = x[i];
		s->lf[i] += s->a_lf * (x[i] - s->lf[i]);
	}

	/* Motion bookkeeping, delayed by GUARD_SAMPLES so that the onset of a
	 * shot never counts as "moving before the shot". */
	float m = norm3(x[0] - s->g_lp[0], x[1] - s->g_lp[1], x[2] - s->g_lp[2]);
	uint32_t guard_ms = (uint32_t)(GUARD_SAMPLES * 1000.0f / c->fs_hz);
	if (s->move_hist & (1u << (GUARD_SAMPLES - 1))) {
		s->last_move_ms = t_ms - guard_ms;
	}
	s->move_hist = (s->move_hist << 1) | (m > c->move_mg ? 1u : 0u);

	if (s->cand_active) {
		uint32_t dt = t_ms - s->cand.t_ms;
		if (dt <= PEAK_WIN_MS) {
			uint16_t p = sat16(hf);
			if (p > s->cand.peak_hf_mg) {
				s->cand.peak_hf_mg = p;
			}
		}
		if (dt >= c->post_start_ms && dt <= c->post_end_ms) {
			s->post_sum += norm3(s->lf[0] - s->g_pre[0], s->lf[1] - s->g_pre[1],
					     s->lf[2] - s->g_pre[2]);
			s->post_n++;
		}
		if (dt > c->post_end_ms) {
			finalize(s, ev);
			done = 1;
		}
		/* gravity estimate frozen while evaluating */
		return done;
	}

	if (hf >= c->trig_hf_mg &&
	    (!s->have_trig || (t_ms - s->last_trig_ms) >= c->refractory_ms)) {
		uint32_t still = t_ms - s->last_move_ms;
		s->cand_active = 1;
		s->have_trig = 1;
		s->last_trig_ms = t_ms;
		memset(&s->cand, 0, sizeof(s->cand));
		s->cand.t_ms = t_ms;
		s->cand.peak_hf_mg = sat16(hf);
		s->cand.pre_still_ms = still > 65535u ? 65535u : (uint16_t)still;
		for (int i = 0; i < 3; i++) {
			s->g_pre[i] = s->g_lp[i];
		}
		s->post_sum = 0.0f;
		s->post_n = 0;
		return 0;
	}

	for (int i = 0; i < 3; i++) {
		s->g_lp[i] += s->a_g_lp * (x[i] - s->g_lp[i]);
	}
	return 0;
}

int sd_idle_sample(sd_state_t *s, const int16_t xyz_mg[3], uint32_t t_ms)
{
	(void)t_ms;
	float x[3] = {(float)xyz_mg[0], (float)xyz_mg[1], (float)xyz_mg[2]};
	int wake = 0;

	if (s->have_idle_ref) {
		float d = norm3(x[0] - s->idle_ref[0], x[1] - s->idle_ref[1],
				x[2] - s->idle_ref[2]);
		wake = d > s->cfg.wake_mg;
	}
	for (int i = 0; i < 3; i++) {
		s->idle_ref[i] = x[i];
	}
	s->have_idle_ref = !wake;
	return wake;
}

int sd_active_should_sleep(const sd_state_t *s, uint32_t t_ms)
{
	return !s->cand_active && (t_ms - s->last_move_ms) >= s->cfg.idle_timeout_ms;
}
