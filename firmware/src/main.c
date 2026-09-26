/*
 * main.c - ShotPuck application.
 *
 * Event loop on one thread; all I2C and flash work happens here. ISRs and
 * BT callbacks only set flags / queue messages and give wake_sem.
 *
 *   IDLE   : LIS2DE12 at 10 Hz with HP wake interrupt, CPU asleep.
 *            Advertising stops ADV_LINGER_MS after entering IDLE.
 *   ACTIVE : 400 Hz FIFO -> shot_detect.c; advertising / connected.
 *            Back to IDLE after cfg.idle_timeout_ms of stillness.
 */
#include <string.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/byteorder.h>

#include "accel.h"
#include "ble.h"
#include "power.h"
#include "protocol.h"
#include "shot_detect.h"
#include "store.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

#define FW_MAJOR 0
#define FW_MINOR 1
#define EVENT_LOG_LEN 128
#define ADV_LINGER_MS 120000
#define HOUSEKEEP_ACTIVE_MS 10000
#define HOUSEKEEP_IDLE_MS 60000
#define SAMPLE_PERIOD_X2_MS 5 /* 2.5 ms at 400 Hz, stored x2 */

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);

static K_SEM_DEFINE(wake_sem, 0, 1);
static atomic_t accel_pending;
static atomic_t conn_event;

struct ctrl_msg {
	uint8_t len;
	uint8_t data[16];
};
K_MSGQ_DEFINE(ctrl_q, sizeof(struct ctrl_msg), 4, 4);

static store_t st;
static sd_state_t det;
static sp_config_t pending_cfg;
static atomic_t cfg_pending;

static sp_event_t ev_log[EVENT_LOG_LEN];
static uint32_t ev_log_n; /* total ever written */

static int64_t unix_offset_ms;
static bool time_set;

static uint32_t active_start_ms;
static uint32_t sample_idx;
static uint32_t idle_since_ms;

/* ---------------------------------------------------------------- utils */

static void led_pulse(int ms)
{
	gpio_pin_set_dt(&led, 1);
	k_msleep(ms);
	gpio_pin_set_dt(&led, 0);
}

static void cfg_to_wire(const sd_config_t *c, sp_config_t *w)
{
	w->version = PROTO_VERSION;
	w->trig_hf_mg = (uint16_t)c->trig_hf_mg;
	w->move_mg = (uint16_t)c->move_mg;
	w->pre_still_min_ms = c->pre_still_min_ms;
	w->post_start_ms = c->post_start_ms;
	w->post_end_ms = c->post_end_ms;
	w->post_lf_min_mg = (uint16_t)c->post_lf_min_mg;
	w->refractory_ms = c->refractory_ms;
	w->wake_mg = (uint16_t)c->wake_mg;
	w->idle_timeout_s = (uint16_t)(c->idle_timeout_ms / 1000);
}

static void wire_to_cfg(const sp_config_t *w, sd_config_t *c)
{
	c->trig_hf_mg = w->trig_hf_mg;
	c->move_mg = w->move_mg;
	c->pre_still_min_ms = w->pre_still_min_ms;
	c->post_start_ms = w->post_start_ms;
	c->post_end_ms = w->post_end_ms;
	c->post_lf_min_mg = w->post_lf_min_mg;
	c->refractory_ms = w->refractory_ms;
	c->wake_mg = w->wake_mg;
	c->idle_timeout_ms = (uint32_t)w->idle_timeout_s * 1000u;
}

static bool cfg_valid(const sp_config_t *w)
{
	return w->trig_hf_mg >= 1000 && w->trig_hf_mg <= 30000 &&
	       w->move_mg >= 50 && w->move_mg <= 2000 &&
	       w->pre_still_min_ms <= 10000 &&
	       w->post_start_ms < w->post_end_ms && w->post_end_ms <= 1500 &&
	       w->post_lf_min_mg <= 5000 &&
	       w->refractory_ms >= 200 && w->refractory_ms <= 10000 &&
	       w->wake_mg >= 16 && w->wake_mg <= 2000 &&
	       w->idle_timeout_s >= 5 && w->idle_timeout_s <= 3600;
}

static void publish_count(void)
{
	sp_count_t c = {.total = st.total, .last_seq = st.seq};
	ble_update_count(&c);
}

static void publish_status(void)
{
	const power_state_t *p = power_state();
	sp_status_t s = {
		.battery_pct = p->battery_pct,
		.battery_mv = p->battery_mv,
		.temp_c = p->temp_c,
		.flags = (p->vbus ? SP_ST_VBUS : 0) | (p->charging ? SP_ST_CHARGING : 0) |
			 (p->chg_inhibit ? SP_ST_CHG_INHIBIT : 0) |
			 (det.mode == SD_MODE_ACTIVE ? SP_ST_ACTIVE : 0) |
			 (time_set ? SP_ST_TIME_SET : 0),
		.fw_major = FW_MAJOR,
		.fw_minor = FW_MINOR,
	};
	ble_update_status(&s);
}

/* ---------------------------------------------------------- callbacks */

static void accel_isr(void)
{
	atomic_set(&accel_pending, 1);
	k_sem_give(&wake_sem);
}

static void on_ctrl(const uint8_t *data, uint16_t len)
{
	struct ctrl_msg m = {.len = (uint8_t)MIN(len, sizeof(m.data))};
	memcpy(m.data, data, m.len);
	if (k_msgq_put(&ctrl_q, &m, K_NO_WAIT) == 0) {
		k_sem_give(&wake_sem);
	}
}

static int on_cfg(const sp_config_t *c)
{
	if (!cfg_valid(c)) {
		return -EINVAL;
	}
	pending_cfg = *c;
	atomic_set(&cfg_pending, 1);
	k_sem_give(&wake_sem);
	return 0;
}

static void on_conn(bool connected)
{
	ARG_UNUSED(connected);
	atomic_set(&conn_event, 1);
	k_sem_give(&wake_sem);
}

/* -------------------------------------------------------------- modes */

static void go_active(void)
{
	accel_set_active();
	active_start_ms = k_uptime_get_32();
	sample_idx = 0;
	sd_start_active(&det, active_start_ms);
	ble_adv_start();
	LOG_INF("ACTIVE");
}

static void go_idle(void)
{
	accel_set_idle((uint16_t)st.cfg.wake_mg);
	det.mode = SD_MODE_IDLE;
	idle_since_ms = k_uptime_get_32();
	store_save_counts(&st);
	LOG_INF("IDLE");
}

/* ------------------------------------------------------------- events */

static void handle_detection(const sd_event_t *e, uint32_t uptime_ms)
{
	sp_event_t w = {
		.seq = ++st.seq,
		.t_unix_ms = time_set ? unix_offset_ms + uptime_ms : 0,
		.t_uptime_ms = uptime_ms,
		.peak_hf_mg = e->peak_hf_mg,
		.pre_still_ms = e->pre_still_ms,
		.post_lf_mg = e->post_lf_mg,
		.accepted = e->accepted,
		.reason = e->reason,
	};
	ev_log[ev_log_n % EVENT_LOG_LEN] = w;
	ev_log_n++;

	if (e->accepted) {
		st.total++;
		store_save_counts(&st);
		publish_count();
		led_pulse(20);
		LOG_INF("SHOT #%u pk=%u still=%u lf=%u", st.total, e->peak_hf_mg,
			e->pre_still_ms, e->post_lf_mg);
	} else {
		LOG_INF("reject r=%u pk=%u still=%u lf=%u", e->reason, e->peak_hf_mg,
			e->pre_still_ms, e->post_lf_mg);
	}
	ble_send_event(&w);
}

static void handle_accel(void)
{
	static int16_t buf[32][3];

	if (accel_mode() == ACCEL_IDLE) {
		if (accel_clear_wake() > 0) {
			go_active(); /* re-arms the interrupt */
		} else {
			accel_irq_rearm();
		}
		return;
	}
	if (accel_mode() != ACCEL_ACTIVE) {
		return;
	}

	for (int guard = 0; guard < 4; guard++) {
		int ovr = 0;
		int n = accel_read_fifo(buf, ARRAY_SIZE(buf), &ovr);
		if (n <= 0) {
			break;
		}
		uint32_t now = k_uptime_get_32();
		if (ovr) {
			/* samples were lost: restart the detector's time base */
			LOG_WRN("FIFO overrun");
			active_start_ms = now;
			sample_idx = 0;
			sd_start_active(&det, now);
		}
		uint32_t t_last = active_start_ms + ((sample_idx + n - 1) * SAMPLE_PERIOD_X2_MS) / 2;
		for (int i = 0; i < n; i++) {
			uint32_t t = active_start_ms + (sample_idx * SAMPLE_PERIOD_X2_MS) / 2;
			sample_idx++;
			sd_event_t e;
			if (sd_process(&det, buf[i], t, &e)) {
				/* map detector time to uptime (ODR may deviate +-10 %) */
				handle_detection(&e, now - (t_last - e.t_ms));
			}
		}
		if (n < 25) {
			break;
		}
	}

	uint32_t t_now = active_start_ms + (sample_idx * SAMPLE_PERIOD_X2_MS) / 2;
	if (sd_active_should_sleep(&det, t_now)) {
		go_idle();
	} else {
		accel_irq_rearm();
	}
}

static void replay_from(uint32_t from_seq)
{
	uint32_t first = ev_log_n > EVENT_LOG_LEN ? ev_log_n - EVENT_LOG_LEN : 0;
	for (uint32_t i = first; i < ev_log_n; i++) {
		const sp_event_t *e = &ev_log[i % EVENT_LOG_LEN];
		if (e->seq < from_seq) {
			continue;
		}
		for (int tries = 0; tries < 50; tries++) {
			int err = ble_send_event(e);
			if (err != -ENOMEM) {
				break;
			}
			k_msleep(20);
		}
	}
}

static void handle_ctrl(void)
{
	struct ctrl_msg m;

	while (k_msgq_get(&ctrl_q, &m, K_NO_WAIT) == 0) {
		switch (m.data[0]) {
		case SP_CTRL_SET_TIME:
			if (m.len >= 9) {
				int64_t unix_ms = (int64_t)sys_get_le64(&m.data[1]);
				unix_offset_ms = unix_ms - k_uptime_get();
				time_set = true;
				publish_status();
			}
			break;
		case SP_CTRL_RESET_COUNT:
			st.total = 0;
			store_save_counts(&st);
			publish_count();
			break;
		case SP_CTRL_REPLAY:
			if (m.len >= 5) {
				replay_from(sys_get_le32(&m.data[1]));
			}
			break;
		case SP_CTRL_FACTORY_CFG: {
			sp_config_t w;
			sd_default_config(&st.cfg);
			det.cfg = st.cfg;
			store_save_cfg(&st);
			cfg_to_wire(&st.cfg, &w);
			ble_update_config(&w);
			break;
		}
		case SP_CTRL_LED_BLINK:
			for (int i = 0; i < 5; i++) {
				led_pulse(80);
				k_msleep(120);
			}
			break;
		default:
			break;
		}
	}
}

static void housekeeping(void)
{
	power_update();
	if (power_should_cutoff()) {
		store_save_counts(&st);
		accel_off();
		ble_adv_stop();
		power_off_until_charger();
	}
	publish_status();
}

/* --------------------------------------------------------------- main */

int main(void)
{
	sp_config_t w;

	power_regout0_3v0(); /* may reset once on first boot */

	gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);
	if (power_init() == 0) {
		power_update();
		if (power_should_cutoff()) {
			power_off_until_charger();
		}
	}

	sd_default_config(&st.cfg);
	store_init(&st);
	st.cfg.fs_hz = 400.0f; /* hardware rate is fixed */
	sd_init(&det, &st.cfg);

	cfg_to_wire(&st.cfg, &w);
	ble_update_config(&w);
	if (ble_init(on_ctrl, on_cfg, on_conn)) {
		LOG_ERR("BLE init failed");
	}
	publish_count();
	publish_status();

	if (accel_init(accel_isr)) {
		/* no sensor: blink SOS-ish forever, keep BLE up for diagnostics */
		ble_adv_start();
		for (;;) {
			led_pulse(50);
			k_msleep(1000);
		}
	}
	led_pulse(200);
	go_active(); /* start awake so a phone can connect right after boot */

	uint32_t last_hk = k_uptime_get_32();

	for (;;) {
		bool active = det.mode == SD_MODE_ACTIVE;
		uint32_t hk_period = active ? HOUSEKEEP_ACTIVE_MS : HOUSEKEEP_IDLE_MS;

		k_sem_take(&wake_sem, K_MSEC(active ? 1000 : 10000));

		if (atomic_cas(&accel_pending, 1, 0)) {
			handle_accel();
		}
		handle_ctrl();
		if (atomic_cas(&cfg_pending, 1, 0)) {
			wire_to_cfg(&pending_cfg, &st.cfg);
			det.cfg = st.cfg;
			store_save_cfg(&st);
			LOG_INF("config updated");
		}

		uint32_t now = k_uptime_get_32();
		bool in_linger = det.mode == SD_MODE_ACTIVE || (now - idle_since_ms) < ADV_LINGER_MS;
		if (atomic_cas(&conn_event, 1, 0) || !ble_is_advertising()) {
			if (!ble_is_connected() && in_linger) {
				ble_adv_start();
			}
		}
		if (!in_linger && ble_is_advertising()) {
			ble_adv_stop();
		}
		if (now - last_hk >= hk_period) {
			last_hk = now;
			housekeeping();
		}
	}
	return 0;
}
