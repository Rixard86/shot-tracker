#include "claim.h"

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>

LOG_MODULE_REGISTER(claim, LOG_LEVEL_INF);

#define CLAIM_WINDOW_MS 60000u

static atomic_t window_open;
static atomic_t paired;
static bool vbus_seen;
static uint32_t opened_at_ms;

static enum bt_security_err pairing_accept(struct bt_conn *conn,
					   const struct bt_conn_pairing_feat *const feat)
{
	ARG_UNUSED(conn);
	ARG_UNUSED(feat);
	return atomic_get(&window_open) ? BT_SECURITY_ERR_SUCCESS : BT_SECURITY_ERR_PAIR_NOT_ALLOWED;
}

static void pairing_complete(struct bt_conn *conn, bool bonded)
{
	ARG_UNUSED(conn);
	if (bonded) {
		atomic_set(&paired, 1);
	}
}

static struct bt_conn_auth_cb auth_cb = {
	.pairing_accept = pairing_accept,
};

static struct bt_conn_auth_info_cb auth_info_cb = {
	.pairing_complete = pairing_complete,
};

int claim_init(void)
{
	int err = bt_conn_auth_cb_register(&auth_cb);

	return err ? err : bt_conn_auth_info_cb_register(&auth_info_cb);
}

void claim_poll(bool vbus)
{
	uint32_t now = k_uptime_get_32();

	if (vbus && !vbus_seen) {
		opened_at_ms = now;
		atomic_set(&window_open, 1);
		LOG_INF("pairing window open");
	}
	vbus_seen = vbus;
	if (atomic_get(&window_open) && now - opened_at_ms >= CLAIM_WINDOW_MS) {
		atomic_clear(&window_open);
		LOG_INF("pairing window closed");
	}
}

bool claim_is_open(void)
{
	return atomic_get(&window_open) != 0;
}

bool claim_take_paired(void)
{
	return atomic_cas(&paired, 1, 0);
}

int claim_forget_all(void)
{
	return bt_unpair(BT_ID_DEFAULT, BT_ADDR_LE_ANY);
}
