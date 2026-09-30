#include "factory.h"

#include <hal/nrf_power.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/reboot.h>

#include "recovery.h"

LOG_MODULE_REGISTER(factory, LOG_LEVEL_INF);

#define BOOT_COUNT_REG 1u
#define HEALTHY_AFTER_MS 60000u
#define DONE_BLINKS 5
#define DONE_BLINK_MS 80

static const struct gpio_dt_spec pads = GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), reset_pads_gpios);
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static pad_hold_t hold;
static bool healthy;

static int erase_partition(uint8_t id)
{
	const struct flash_area *area;
	int err = flash_area_open(id, &area);

	if (err) {
		return err;
	}
	err = flash_area_erase(area, 0, area->fa_size);
	flash_area_close(area);
	return err;
}

static int erase_user_data(void)
{
	int err = erase_partition(FIXED_PARTITION_ID(storage_partition));

	return err ? err : erase_partition(FIXED_PARTITION_ID(eventlog_partition));
}

void factory_boot_guard(void)
{
	uint32_t cause = 0u;

	(void)hwinfo_get_reset_cause(&cause);
	(void)hwinfo_clear_reset_cause();
	bool crashed = (cause & (RESET_WATCHDOG | RESET_CPU_LOCKUP)) != 0u;
	uint32_t count = boot_loop_count_next(nrf_power_gpregret_get(NRF_POWER, BOOT_COUNT_REG), crashed);

	if (boot_loop_tripped(count)) {
		LOG_ERR("boot loop: erasing settings, pairings and shot log");
		if (erase_user_data() == 0) {
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
	for (int i = 0; i < DONE_BLINKS; i++) {
		gpio_pin_set_dt(&led, 1);
		k_msleep(DONE_BLINK_MS);
		gpio_pin_set_dt(&led, 0);
		k_msleep(DONE_BLINK_MS);
	}
}

static void factory_reset(void)
{
	LOG_WRN("factory reset from the reset pads");
	blink_done();
	if (erase_user_data()) {
		LOG_ERR("factory reset: erase failed");
	}
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
		gpio_pin_set_dt(&led, pad_hold_active(&hold) ? 1 : 0);
	}
}
