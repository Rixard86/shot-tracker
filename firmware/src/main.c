/*
 * main.c - ShotPuck application.
 *
 * Event loop on one thread; all I2C and flash work happens here. ISRs and
 * BT callbacks only set flags / queue messages and give wake_sem.
 *
 *   IDLE   : LSM6DSV32X accelerometer at 15 Hz with wake interrupt, gyro
 *            off, CPU asleep. Advertising stops ADV_LINGER_MS after IDLE.
 *   ACTIVE : 480 Hz accel + gyro FIFO -> shot_detect.c and the capture
 *            ring; advertising / connected. Back to IDLE after
 *            cfg.idle_timeout_ms of stillness.
 */
#include <app_version.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/byteorder.h>

#include "accel.h"
#include "ble.h"
#include "capflash.h"
#include "capture.h"
#include "capture_tx.h"
#include "capwriter.h"
#include "chg_led.h"
#include "claim.h"
#include "diag.h"
#include "event_tx.h"
#include "factory.h"
#include "led.h"
#include "ota.h"
#include "power.h"
#include "protocol.h"
#include "recovery.h"
#include "shot_detect.h"
#include "store.h"
#include "wire.h"

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
#define CHARGER_POLL_MS 500
#define PAD_POLL_MS 250
#define CLAIM_BLINK_PERIOD_MS 1000
#define SENSOR_RETRY_MS 5000
#define SENSOR_STALL_MS 5000
#define NO_SENSOR_POLL_MS 1000

static K_SEM_DEFINE(wake_sem, 0, 1);
static atomic_t accel_pending;
static atomic_t conn_event;

struct ctrl_msg {
	uint8_t len;
	uint8_t data[16];
};
K_MSGQ_DEFINE(ctrl_q, sizeof(struct ctrl_msg), 4, 4);
K_MSGQ_DEFINE(cfg_q, sizeof(sp_config_t), 1, 4);

static store_t st;
static cap_t cap;
static capstore_t capstore;
static capw_t capw;
static bool captures_ok;
static sd_state_t det;

static wire_clock_t wall_clock;

static uint32_t active_start_ms;
static uint32_t sample_idx;
static uint32_t idle_since_ms;
static uint32_t last_batch_ms;

/* ---------------------------------------------------------------- utils */

static void resume_seq(void)
{
	st.seq = seq_resume(st.seq, event_tx_newest_seq());
	store_save_counts(&st);
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
			 (wall_clock.set ? SP_ST_TIME_SET : 0),
		.fw_major = FW_MAJOR,
		.fw_minor = FW_MINOR,
	};
	ble_update_status(&s);
	diag_publish(&st);
}

/* ---------------------------------------------------------- callbacks */

static void accel_isr(void)
{
	atomic_set(&accel_pending, 1);
	k_sem_give(&wake_sem);
}

static int on_ctrl(const uint8_t *data, uint16_t len)
{
	struct ctrl_msg m = {.len = (uint8_t)MIN(len, sizeof(m.data))};
	memcpy(m.data, data, m.len);
	int err = k_msgq_put(&ctrl_q, &m, K_NO_WAIT);

	if (err == 0) {
		k_sem_give(&wake_sem);
	}
	return err;
}

static int on_cfg(const sp_config_t *c)
{
	k_msgq_purge(&cfg_q);
	int err = k_msgq_put(&cfg_q, c, K_NO_WAIT);

	k_sem_give(&wake_sem);
	return err;
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
	last_batch_ms = active_start_ms;
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
	capw_request_t req = {
		.seq = seq,
		.trigger = cap_ms_to_index(e->t_ms - active_start_ms),
		.epoch = cap.epoch,
		.mode = st.capture_mode,
	};

	if (captures_ok) {
		capw_request(&capw, &req);
	}
}

static void handle_detection(const sd_event_t *e, int64_t uptime_ms)
{
	sp_event_t w = {
		.seq = ++st.seq,
		.t_unix_ms = wire_clock_unix_ms(&wall_clock, uptime_ms),
		.t_uptime_ms = (uint32_t)uptime_ms,
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
		led_play(LED_SHOT);
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
	int64_t now = k_uptime_get();
	uint32_t t_last = sample_ms(sample_idx + n - 1u);

	for (uint32_t i = 0; i < n; i++) {
		int16_t mg[3];
		sd_event_t e;

		cap_push(&cap, &fifo->samples[i]);
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
		last_batch_ms = k_uptime_get_32();
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
				wire_time_t now = {
					.unix_ms = (int64_t)sys_get_le64(&m.data[1]),
					.uptime_ms = k_uptime_get(),
				};
				wire_clock_set(&wall_clock, &now);
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
			wire_config_out(&st, &w);
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
			led_play(LED_FIND);
			break;
		default:
			break;
		}
	}
}

static bool sensor_stuck(void)
{
	if (det.mode == SD_MODE_ACTIVE) {
		return k_uptime_get_32() - last_batch_ms >= SENSOR_STALL_MS;
	}
	return !accel_idle_ok();
}

static void check_sensor(void)
{
	if (!sensor_stuck()) {
		return;
	}
	LOG_WRN("IMU not responding: re-initialising");
	if (accel_init(accel_isr) != 0) {
		return;
	}
	if (det.mode == SD_MODE_ACTIVE) {
		go_active();
	} else {
		accel_set_idle((uint16_t)st.cfg.wake_mg);
	}
}

static void apply_config(void)
{
	sp_config_t c;

	if (k_msgq_get(&cfg_q, &c, K_NO_WAIT) != 0) {
		return;
	}
	wire_config_in(&c, &st);
	det.cfg = st.cfg;
	store_save_cfg(&st);
	LOG_INF("config updated");
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
	check_sensor();
	publish_status();
}

static void check_boot_battery(void)
{
	for (uint32_t i = 0; i < RECOVERY_LOW_BATTERY_READINGS; i++) {
		power_update();
	}
	if (power_should_cutoff()) {
		accel_off();
		power_off_until_charger();
	}
}

static void wait_without_sensor(void)
{
	for (uint32_t waited = 0; waited < SENSOR_RETRY_MS; waited += NO_SENSOR_POLL_MS) {
		if (ota_image_confirmed()) {
			ota_feed();
		}
		if (!ble_is_connected()) {
			ble_adv_start();
		}
		led_play(LED_NO_SENSOR);
		k_msleep(NO_SENSOR_POLL_MS);
	}
}

static void wait_for_sensor(void)
{
	while (accel_init(accel_isr) != 0) {
		wait_without_sensor();
	}
}

static void claim_feedback(void)
{
	static uint32_t last_blink_ms;
	uint32_t now = k_uptime_get_32();

	if (claim_take_paired()) {
		led_play(LED_PAIRED);
	}
	if (claim_is_open() && now - last_blink_ms >= CLAIM_BLINK_PERIOD_MS) {
		last_blink_ms = now;
		led_play(LED_CLAIM);
	}
}

static void charge_feedback(void)
{
	static uint32_t last_tick_ms;
	uint32_t now = k_uptime_get_32();

	if (!power_vbus_present() || claim_is_open()) {
		led_steady((led_steady_t){LED_STEADY_CHARGE_FULL, false});
		return;
	}
	if (now - last_tick_ms < CHG_LED_TICK_MS) {
		return;
	}
	last_tick_ms = now;
	chg_led_mode_t mode = chg_led_mode(power_update());
	led_steady((led_steady_t){LED_STEADY_CHARGE_FULL, mode == CHG_LED_FULL});
	led_play_pulses(chg_led_pulses(mode));
}

static uint32_t poll_period(bool active)
{
	if (event_tx_busy() || cap_tx_busy() || capw_busy(&capw)) {
		return TX_POLL_MS;
	}
	if (factory_pads_held()) {
		return PAD_POLL_MS;
	}
	if (claim_is_open()) {
		return CLAIM_POLL_MS;
	}
	if (power_vbus_present()) {
		return CHARGER_POLL_MS;
	}
	return active ? ACTIVE_POLL_MS : IDLE_POLL_MS;
}

/* --------------------------------------------------------------- main */

int main(void)
{
	sp_config_t w;

	power_regout0_3v0(); /* may reset once on first boot */
	diag_boot();
	factory_boot_guard();
	if (ota_init()) {
		LOG_ERR("watchdog unavailable");
	}

	led_init();
	if (factory_pads_init()) {
		LOG_ERR("reset pads unavailable");
	}
	if (power_init() == 0) {
		check_boot_battery();
	}

	sd_default_config(&st.cfg);
	store_init(&st);
	diag_count_crash(&st);
	if (!wire_config_normalize(&st)) {
		LOG_WRN("stored config invalid: using defaults");
		sd_default_config(&st.cfg);
		st.capture_mode = SP_CAPTURE_STANDARD;
	}
	if (event_tx_init()) {
		LOG_ERR("shot log unavailable");
	}
	resume_seq();
	st.cfg.fs_hz = (float)CAP_ODR_HZ;
	sd_init(&det, &st.cfg);
	cap_init(&cap);
	captures_ok = capflash_init(&capstore) == 0;
	if (!captures_ok) {
		LOG_ERR("capture store unavailable");
	}
	capw_init(&capw, &(capw_deps_t){&cap, &capstore});
	cap_tx_init(&capstore, &capw);
	ble_set_tx_done_cb(on_tx_done);

	wire_config_out(&st, &w);
	ble_update_config(&w);
	if (ble_init(on_ctrl, on_cfg, on_conn) || claim_init()) {
		LOG_ERR("BLE init failed");
	}
	publish_count();
	publish_status();

	wait_for_sensor();
	led_play(LED_BOOT);
	go_active(); /* start awake so a phone can connect right after boot */

	uint32_t last_hk = k_uptime_get_32();

	for (;;) {
		bool active = det.mode == SD_MODE_ACTIVE;
		uint32_t hk_period = active ? HOUSEKEEP_ACTIVE_MS : HOUSEKEEP_IDLE_MS;

		k_sem_take(&wake_sem, K_MSEC(poll_period(active)));
		ota_feed();
		ota_confirm_when_healthy(ble_link_secured());

		if (atomic_cas(&accel_pending, 1, 0)) {
			handle_accel();
		}
		handle_ctrl();
		claim_poll(power_vbus_present());
		ble_adv_set_pairing(claim_is_open());
		claim_feedback();
		factory_poll();
		if (!factory_pads_held()) {
			charge_feedback();
		}
		if (event_tx_busy()) {
			event_tx_pump();
		}
		if (capw_busy(&capw) && capw_pump(&capw) == CAPW_COMMITTED) {
			cap_tx_queue(capw.committed_seq);
		}
		if (cap_tx_busy()) {
			cap_tx_pump();
		}
		ble_set_bulk(event_tx_busy() || cap_tx_busy());
		apply_config();

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
