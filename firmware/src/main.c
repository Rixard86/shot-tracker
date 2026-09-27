/*
 * main.c - ShotPuck application.
 *
 * Event loop on one thread; all I2C and flash work happens here. ISRs and
 * BT callbacks only set flags / queue messages and give wake_sem.
 *
 *   IDLE   : LSM6DSO32 accelerometer at 12.5 Hz with wake interrupt, gyro
 *            off, CPU asleep. Advertising stops ADV_LINGER_MS after IDLE.
 *   ACTIVE : 416 Hz accel + gyro FIFO -> shot_detect.c and the capture
 *            ring; advertising / connected. Back to IDLE after
 *            cfg.idle_timeout_ms of stillness.
 */
#include <app_version.h>
#include <string.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/byteorder.h>

#include "accel.h"
#include "ble.h"
#include "capture.h"
#include "capture_tx.h"
#include "claim.h"
#include "event_tx.h"
#include "ota.h"
#include "power.h"
#include "protocol.h"
#include "shot_detect.h"
#include "store.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

#define FW_MAJOR APP_VERSION_MAJOR
#define FW_MINOR APP_VERSION_MINOR
#define ADV_LINGER_MS 120000
#define HOUSEKEEP_ACTIVE_MS 10000
#define HOUSEKEEP_IDLE_MS 60000
#define FIFO_BATCH_SAMPLES 32
#define FIFO_WATERMARK_SAMPLES 26
#define FIFO_DRAIN_ROUNDS 4
#define CTRL_SEQ_MSG_LEN 5
#define TX_POLL_MS 20
#define ACTIVE_POLL_MS 1000
#define IDLE_POLL_MS 2000
#define CLAIM_POLL_MS 250
#define CLAIM_BLINK_PERIOD_MS 1000
#define CLAIM_BLINK_MS 30
#define PAIRED_BLINKS 2
#define PAIRED_BLINK_MS 60

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
static cap_t cap;
static sd_state_t det;
static sp_config_t pending_cfg;
static atomic_t cfg_pending;

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

static void on_tx_done(void)
{
	k_sem_give(&wake_sem);
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
	cap_reset(&cap);
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

static void request_capture(const sd_event_t *e, uint32_t seq)
{
	cap_request_t req = {.seq = seq, .trigger_index = cap_ms_to_index(e->t_ms - active_start_ms)};

	if (cap_request(&cap, &req) == CAP_NO_SLOT) {
		LOG_WRN("no capture for seq %u", seq);
	}
}

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
	event_tx_record(&w);
	if (e->accepted) {
		st.total++;
		store_save_counts(&st);
		publish_count();
		request_capture(e, w.seq);
		led_pulse(20);
		LOG_INF("SHOT #%u pk=%u still=%u lf=%u", st.total, e->peak_hf_mg,
			e->pre_still_ms, e->post_lf_mg);
	} else {
		LOG_INF("reject r=%u pk=%u still=%u lf=%u", e->reason, e->peak_hf_mg,
			e->pre_still_ms, e->post_lf_mg);
	}
}

static uint32_t sample_ms(uint32_t index)
{
	return active_start_ms + cap_index_to_ms(index);
}

static void restart_stream(uint32_t now)
{
	LOG_WRN("FIFO overrun");
	active_start_ms = now;
	sample_idx = 0;
	cap_reset(&cap);
	sd_start_active(&det, now);
}

static void process_batch(const accel_fifo_t *fifo, uint32_t n)
{
	uint32_t now = k_uptime_get_32();
	uint32_t t_last = sample_ms(sample_idx + n - 1u);

	for (uint32_t i = 0; i < n; i++) {
		int16_t mg[3];
		sd_event_t e;
		int slot = cap_push(&cap, &fifo->samples[i]);

		if (slot != CAP_NO_SLOT) {
			cap_tx_queue(cap.slots[slot].seq);
		}
		accel_to_mg(&fifo->samples[i], mg);
		if (sd_process(&det, mg, sample_ms(sample_idx++), &e)) {
			handle_detection(&e, now - (t_last - e.t_ms));
		}
	}
}

static void handle_accel(void)
{
	static sp_cap_sample_t buf[FIFO_BATCH_SAMPLES];
	accel_fifo_t fifo = {.samples = buf, .max = FIFO_BATCH_SAMPLES};

	if (accel_mode() == ACCEL_IDLE) {
		if (accel_clear_wake() > 0) {
			go_active();
		} else {
			accel_irq_rearm();
		}
		return;
	}
	if (accel_mode() != ACCEL_ACTIVE) {
		return;
	}
	for (int round = 0; round < FIFO_DRAIN_ROUNDS; round++) {
		int n = accel_read_fifo(&fifo);
		if (n <= 0) {
			break;
		}
		if (fifo.overrun) {
			restart_stream(k_uptime_get_32());
		}
		process_batch(&fifo, (uint32_t)n);
		if (n < FIFO_WATERMARK_SAMPLES) {
			break;
		}
	}
	if (sd_active_should_sleep(&det, sample_ms(sample_idx))) {
		go_idle();
	} else {
		accel_irq_rearm();
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
				event_tx_replay(sys_get_le32(&m.data[1]));
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
		case SP_CTRL_GET_CAPTURE:
			if (m.len >= CTRL_SEQ_MSG_LEN) {
				cap_tx_queue(sys_get_le32(&m.data[1]));
			}
			break;
		case SP_CTRL_FORGET_BONDS:
			claim_forget_all();
			break;
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

static void claim_feedback(void)
{
	static uint32_t last_blink_ms;
	uint32_t now = k_uptime_get_32();

	if (claim_take_paired()) {
		for (int i = 0; i < PAIRED_BLINKS; i++) {
			led_pulse(PAIRED_BLINK_MS);
			k_msleep(PAIRED_BLINK_MS);
		}
	}
	if (claim_is_open() && now - last_blink_ms >= CLAIM_BLINK_PERIOD_MS) {
		last_blink_ms = now;
		led_pulse(CLAIM_BLINK_MS);
	}
}

static uint32_t poll_period(bool active)
{
	if (event_tx_busy() || cap_tx_busy()) {
		return TX_POLL_MS;
	}
	if (claim_is_open()) {
		return CLAIM_POLL_MS;
	}
	return active ? ACTIVE_POLL_MS : IDLE_POLL_MS;
}

/* --------------------------------------------------------------- main */

int main(void)
{
	sp_config_t w;

	power_regout0_3v0(); /* may reset once on first boot */
	if (ota_init()) {
		LOG_ERR("watchdog unavailable");
	}

	gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);
	if (power_init() == 0) {
		power_update();
		if (power_should_cutoff()) {
			power_off_until_charger();
		}
	}

	sd_default_config(&st.cfg);
	store_init(&st);
	if (event_tx_init()) {
		LOG_ERR("shot log unavailable");
	}
	st.cfg.fs_hz = (float)CAP_ODR_HZ;
	sd_init(&det, &st.cfg);
	cap_init(&cap);
	cap_tx_init(&cap);
	ble_set_tx_done_cb(on_tx_done);

	cfg_to_wire(&st.cfg, &w);
	ble_update_config(&w);
	if (ble_init(on_ctrl, on_cfg, on_conn) || claim_init()) {
		LOG_ERR("BLE init failed");
	}
	publish_count();
	publish_status();

	if (accel_init(accel_isr)) {
		/* no sensor: blink SOS-ish forever, keep BLE up for diagnostics */
		ble_adv_start();
		for (;;) {
			if (ota_image_confirmed()) {
				ota_feed();
			}
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

		k_sem_take(&wake_sem, K_MSEC(poll_period(active)));
		ota_feed();
		ota_confirm_when_healthy();

		if (atomic_cas(&accel_pending, 1, 0)) {
			handle_accel();
		}
		handle_ctrl();
		claim_poll(power_vbus_present());
		claim_feedback();
		if (event_tx_busy()) {
			event_tx_pump();
		}
		if (cap_tx_busy()) {
			cap_tx_pump();
		}
		if (atomic_cas(&cfg_pending, 1, 0)) {
			wire_to_cfg(&pending_cfg, &st.cfg);
			det.cfg = st.cfg;
			store_save_cfg(&st);
			LOG_INF("config updated");
		}

		uint32_t now = k_uptime_get_32();
		bool in_linger = det.mode == SD_MODE_ACTIVE || (now - idle_since_ms) < ADV_LINGER_MS ||
				 claim_is_open();
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
