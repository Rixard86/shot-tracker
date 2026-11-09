#include "factory.h"

#include <hal/nrf_power.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/reboot.h>

#include "claim.h"
#include "diag.h"
#include "led.h"
#include "ota.h"
#include "recovery.h"
#include "store.h"

LOG_MODULE_REGISTER(factory, LOG_LEVEL_INF);

#define BOOT_COUNT_REG 1u
#define HEALTHY_AFTER_MS 60000u
#define DONE_WAIT_POLL_MS 20
#define SECTOR_BYTES 4096u

static const struct gpio_dt_spec pads = GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), reset_pads_gpios);
static pad_hold_t hold;
static bool healthy;

static int erase_partition(uint8_t id)
{
	const struct flash_area *area;
	int err = flash_area_open(id, &area);

	if (err) {
		return err;
	}
	for (uint32_t offset = 0; offset < area->fa_size && !err; offset += SECTOR_BYTES) {
		err = flash_area_erase(area, offset, SECTOR_BYTES);
		ota_feed();
	}
	flash_area_close(area);
	return err;
}

static int wipe_user_data(void)
{
	int failed = 0;

	failed += store_forget_except_seq() != 0;
	failed += claim_forget_all() != 0;
	failed += erase_partition(FIXED_PARTITION_ID(eventlog_partition)) != 0;
	failed += erase_partition(FIXED_PARTITION_ID(capture_partition)) != 0;
	return failed;
}

void factory_boot_guard(void)
{
	bool crashed = diag_last_reset_crashed();
	uint32_t count = boot_loop_count_next(nrf_power_gpregret_get(NRF_POWER, BOOT_COUNT_REG), crashed);

	if (boot_loop_tripped(count)) {
		LOG_ERR("boot loop: erasing settings and pairings");
		if (erase_partition(FIXED_PARTITION_ID(storage_partition)) == 0) {
			count = 0u;
		}
	}
	nrf_power_gpregret_set(NRF_POWER, BOOT_COUNT_REG, count);
}

int factory_pads_init(void)
{
	pad_hold_init(&hold);
	return gpio_pin_configure_dt(&pads, GPIO_INPUT);
}

bool factory_pads_held(void)
{
	return pad_hold_active(&hold);
}

static void blink_done(void)
{
	led_play(LED_RESET_DONE);
	while (led_busy()) {
		k_msleep(DONE_WAIT_POLL_MS);
	}
}

static void factory_reset(void)
{
	LOG_WRN("factory reset from the reset pads");
	if (wipe_user_data()) {
		LOG_ERR("factory reset: erase failed");
	}
	blink_done();
	sys_reboot(SYS_REBOOT_COLD);
}

static void mark_healthy(uint32_t now_ms)
{
	if (!healthy && now_ms >= HEALTHY_AFTER_MS) {
		healthy = true;
		nrf_power_gpregret_set(NRF_POWER, BOOT_COUNT_REG, 0u);
	}
}

void factory_poll(void)
{
	pad_sample_t s = {.bridged = gpio_pin_get_dt(&pads) == 1, .now_ms = k_uptime_get_32()};
	bool was_held = pad_hold_active(&hold);

	mark_healthy(s.now_ms);
	if (pad_hold_step(&hold, &s)) {
		factory_reset();
	}
	if (pad_hold_active(&hold) != was_held) {
		led_steady((led_steady_t){LED_STEADY_PAD_HOLD, pad_hold_active(&hold)});
	}
}
