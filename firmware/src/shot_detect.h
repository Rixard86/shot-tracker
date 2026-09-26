/*
 * shot_detect.h - portable arrow-shot detector (no Zephyr dependencies).
 *
 * Same file is compiled into the firmware and into the host simulation
 * (firmware/sim), so the logic that ships is the logic that was simulated.
 *
 * Detection model (per candidate):
 *   1. TRIGGER   high-frequency shock: |a[n] - a[n-1]| >= trig_hf_mg
 *   2. PRE-STILL the bow was held still (aiming) for >= pre_still_min_ms
 *                before the trigger. Rejects knocks while walking,
 *                set-downs and drops.
 *   3. POST-LF   low-frequency motion after release (bow jump / rotation
 *                into the sling): mean |a_lf - g_pre| over the post window
 *                >= post_lf_min_mg. Rejects knocks/taps on a resting bow.
 *
 * A mode manager (IDLE <-> ACTIVE) is included so the firmware's low-power
 * behaviour is simulated too: IDLE samples slowly and wakes on motion,
 * ACTIVE runs the detector at full rate and returns to IDLE after
 * idle_timeout_ms of stillness.
 */
#ifndef SHOT_DETECT_H
#define SHOT_DETECT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
	float fs_hz;               /* ACTIVE sample rate (Hz)                    */
	float trig_hf_mg;          /* trigger: sample-to-sample jump (mg)        */
	float move_mg;             /* |a - g_lp| above this counts as "moving"   */
	uint16_t pre_still_min_ms; /* required stillness before trigger (ms)     */
	uint16_t post_start_ms;    /* post window start after trigger (ms)       */
	uint16_t post_end_ms;      /* post window end after trigger (ms)         */
	float post_lf_min_mg;      /* required mean LF deviation in window (mg)  */
	uint16_t refractory_ms;    /* no new trigger this long after a trigger   */
	float wake_mg;             /* IDLE -> ACTIVE wake threshold (mg)         */
	uint32_t idle_timeout_ms;  /* ACTIVE -> IDLE after this much stillness   */
} sd_config_t;

typedef enum {
	SD_REJ_NONE = 0,       /* accepted as a shot                         */
	SD_REJ_NOT_STILL = 1,  /* bow was moving before the trigger          */
	SD_REJ_NO_FOLLOW = 2,  /* no post-release bow motion (knock/tap)     */
} sd_reason_t;

typedef struct {
	uint32_t t_ms;         /* trigger time                               */
	uint16_t peak_hf_mg;   /* max HF jump in first 20 ms (saturates)      */
	uint16_t pre_still_ms; /* stillness before trigger (capped 65535)     */
	uint16_t post_lf_mg;   /* mean LF deviation in post window            */
	uint8_t accepted;      /* 1 = counted as shot                        */
	uint8_t reason;        /* sd_reason_t                                */
} sd_event_t;

typedef enum { SD_MODE_IDLE = 0, SD_MODE_ACTIVE = 1 } sd_mode_t;

typedef struct {
	sd_config_t cfg;
	float a_g_lp, a_lf;        /* filter coefficients                        */
	float g_lp[3];             /* gravity estimate (slow LP)                 */
	float lf[3];               /* low-frequency acceleration (fast LP)       */
	float prev[3];
	uint8_t have_prev;
	uint32_t move_hist;        /* delayed motion flags (guard window)        */
	uint32_t last_move_ms;
	uint32_t last_trig_ms;
	uint8_t have_trig;
	/* candidate under evaluation */
	uint8_t cand_active;
	sd_event_t cand;
	float g_pre[3];
	float post_sum;
	uint32_t post_n;
	/* mode manager */
	sd_mode_t mode;
	float idle_ref[3];
	uint8_t have_idle_ref;
} sd_state_t;

/* Fill cfg with the defaults used in simulation. */
void sd_default_config(sd_config_t *cfg);

void sd_init(sd_state_t *s, const sd_config_t *cfg);

/* Call when entering ACTIVE (fresh stream). Treats "now" as last motion,
 * because the device only wakes on motion. */
void sd_start_active(sd_state_t *s, uint32_t now_ms);

/*
 * Feed one ACTIVE-rate sample (mg, any orientation).
 * Returns 1 and fills *ev when a candidate finished evaluation
 * (accepted or rejected), otherwise 0.
 */
int sd_process(sd_state_t *s, const int16_t xyz_mg[3], uint32_t t_ms, sd_event_t *ev);

/* Mode manager. In IDLE, feed slow samples here; returns 1 to request
 * switch to ACTIVE. In ACTIVE, returns 1 to request switch to IDLE. */
int sd_idle_sample(sd_state_t *s, const int16_t xyz_mg[3], uint32_t t_ms);
int sd_active_should_sleep(const sd_state_t *s, uint32_t t_ms);

#ifdef __cplusplus
}
#endif

#endif /* SHOT_DETECT_H */
