#include "ota.h"

#include <zephyr/dfu/mcuboot.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(ota, LOG_LEVEL_INF);

#define WDT_TIMEOUT_MS 10000u
#define CONFIRM_AFTER_MS 10000u
#define NO_CHANNEL (-1)

static const struct device *const wdt = DEVICE_DT_GET(DT_ALIAS(watchdog0));
static int channel = NO_CHANNEL;
static bool confirmed;
static uint32_t started_ms;

int ota_init(void)
{
	struct wdt_timeout_cfg cfg = {
		.window = {.min = 0, .max = WDT_TIMEOUT_MS},
		.flags = WDT_FLAG_RESET_SOC,
	};

	started_ms = k_uptime_get_32();
	confirmed = boot_is_img_confirmed();
	if (!device_is_ready(wdt)) {
		return -ENODEV;
	}
	channel = wdt_install_timeout(wdt, &cfg);
	if (channel < 0) {
		return channel;
	}
	return wdt_setup(wdt, WDT_OPT_PAUSE_HALTED_BY_DBG);
}

void ota_feed(void)
{
	if (channel != NO_CHANNEL) {
		wdt_feed(wdt, channel);
	}
}

bool ota_image_confirmed(void)
{
	return confirmed;
}

void ota_confirm_when_healthy(bool link_secured)
{
	if (confirmed || !link_secured || k_uptime_get_32() - started_ms < CONFIRM_AFTER_MS) {
		return;
	}
	confirmed = boot_write_img_confirmed() == 0;
	LOG_INF("image %s", confirmed ? "confirmed" : "confirm failed");
}
