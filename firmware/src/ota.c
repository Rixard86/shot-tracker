#include "ota.h"

#include <zephyr/dfu/mcuboot.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/mgmt/mcumgr/mgmt/callbacks.h>
#include <zephyr/mgmt/mcumgr/mgmt/mgmt_defines.h>
#include <zephyr/sys/atomic.h>

#include "capflash.h"
#include "power.h"
#include "supply.h"

LOG_MODULE_REGISTER(ota, LOG_LEVEL_INF);

#define WDT_TIMEOUT_MS 10000u
#define CONFIRM_AFTER_MS 10000u
#define NO_CHANNEL (-1)
#define UPLOAD_ACTIVE_MS 2000u

static const struct device *const wdt = DEVICE_DT_GET(DT_ALIAS(watchdog0));
static int channel = NO_CHANNEL;
static bool confirmed;
static bool slot1_idle;
static uint32_t started_ms;
static atomic_t upload_seen;
static atomic_t last_chunk_ms;

static enum mgmt_cb_return upload_gate(uint32_t event, enum mgmt_cb_return prev_status,
				       int32_t *rc, uint16_t *group, bool *abort_more,
				       void *data, size_t data_size)
{
	ARG_UNUSED(event);
	ARG_UNUSED(prev_status);
	ARG_UNUSED(group);
	ARG_UNUSED(abort_more);
	ARG_UNUSED(data);
	ARG_UNUSED(data_size);
	if (power_vbus_present()) {
		capflash_spare_claim();
		atomic_set(&last_chunk_ms, (atomic_val_t)k_uptime_get_32());
		atomic_set(&upload_seen, 1);
		return MGMT_CB_OK;
	}
	*rc = MGMT_ERR_EBADSTATE;
	return MGMT_CB_ERROR_RC;
}

static struct mgmt_callback upload_gate_cb = {
	.callback = upload_gate,
	.event_id = MGMT_EVT_OP_IMG_MGMT_DFU_CHUNK,
};

static void note_image_state(bool is_confirmed)
{
	confirmed = is_confirmed;
	slot1_idle = confirmed && mcuboot_swap_type() == BOOT_SWAP_TYPE_NONE;
	if (confirmed) {
		supply_unused_ram_off();
	}
}

int ota_init(void)
{
	struct wdt_timeout_cfg cfg = {
		.window = {.min = 0, .max = WDT_TIMEOUT_MS},
		.flags = WDT_FLAG_RESET_SOC,
	};

	started_ms = k_uptime_get_32();
	note_image_state(boot_is_img_confirmed());
	mgmt_callback_register(&upload_gate_cb);
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
	note_image_state(boot_write_img_confirmed() == 0);
	LOG_INF("image %s", confirmed ? "confirmed" : "confirm failed");
}

bool ota_spare_allowed(void)
{
	return confirmed && slot1_idle && !capflash_spare_claimed();
}

bool ota_upload_active(void)
{
	uint32_t since = k_uptime_get_32() - (uint32_t)atomic_get(&last_chunk_ms);

	return atomic_get(&upload_seen) && since < UPLOAD_ACTIVE_MS;
}

