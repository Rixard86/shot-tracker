/* ble.c - see ble.h */
#include "ble.h"

#include <string.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/services/bas.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(ble, LOG_LEVEL_INF);

static struct bt_uuid_128 uuid_svc = BT_UUID_INIT_128(SP_UUID_SERVICE);
static struct bt_uuid_128 uuid_count = BT_UUID_INIT_128(SP_UUID_COUNT);
static struct bt_uuid_128 uuid_event = BT_UUID_INIT_128(SP_UUID_EVENT);
static struct bt_uuid_128 uuid_ctrl = BT_UUID_INIT_128(SP_UUID_CONTROL);
static struct bt_uuid_128 uuid_cfg = BT_UUID_INIT_128(SP_UUID_CONFIG);
static struct bt_uuid_128 uuid_status = BT_UUID_INIT_128(SP_UUID_STATUS);

static sp_count_t v_count;
static sp_status_t v_status;
static sp_config_t v_cfg;

static ble_ctrl_cb_t on_ctrl;
static ble_cfg_cb_t on_cfg;
static ble_conn_cb_t on_conn;

static struct bt_conn *cur_conn;
static bool advertising;
static bool notify_count, notify_event, notify_status;

static ssize_t rd_blob(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
		       uint16_t len, uint16_t offset)
{
	const void *v = attr->user_data;
	size_t sz = (v == &v_count) ? sizeof(v_count) :
		    (v == &v_status) ? sizeof(v_status) : sizeof(v_cfg);
	return bt_gatt_attr_read(conn, attr, buf, len, offset, v, sz);
}

static ssize_t wr_ctrl(struct bt_conn *conn, const struct bt_gatt_attr *attr, const void *buf,
		       uint16_t len, uint16_t offset, uint8_t flags)
{
	ARG_UNUSED(conn);
	ARG_UNUSED(attr);
	ARG_UNUSED(flags);
	if (offset != 0 || len < 1 || len > 16) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
	}
	if (on_ctrl) {
		on_ctrl(buf, len);
	}
	return len;
}

static ssize_t wr_cfg(struct bt_conn *conn, const struct bt_gatt_attr *attr, const void *buf,
		      uint16_t len, uint16_t offset, uint8_t flags)
{
	ARG_UNUSED(conn);
	ARG_UNUSED(attr);
	ARG_UNUSED(flags);
	sp_config_t c;

	if (offset != 0 || len != sizeof(c)) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
	}
	memcpy(&c, buf, sizeof(c));
	if (c.version != PROTO_VERSION || !on_cfg || on_cfg(&c) != 0) {
		return BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED);
	}
	v_cfg = c;
	return len;
}

static void ccc_count(const struct bt_gatt_attr *a, uint16_t v)
{
	ARG_UNUSED(a);
	notify_count = v == BT_GATT_CCC_NOTIFY;
}
static void ccc_event(const struct bt_gatt_attr *a, uint16_t v)
{
	ARG_UNUSED(a);
	notify_event = v == BT_GATT_CCC_NOTIFY;
}
static void ccc_status(const struct bt_gatt_attr *a, uint16_t v)
{
	ARG_UNUSED(a);
	notify_status = v == BT_GATT_CCC_NOTIFY;
}

BT_GATT_SERVICE_DEFINE(sp_svc,
	BT_GATT_PRIMARY_SERVICE(&uuid_svc),
	/* [1] count */
	BT_GATT_CHARACTERISTIC(&uuid_count.uuid, BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_READ, rd_blob, NULL, &v_count),
	BT_GATT_CCC(ccc_count, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
	/* [4] event */
	BT_GATT_CHARACTERISTIC(&uuid_event.uuid, BT_GATT_CHRC_NOTIFY, BT_GATT_PERM_NONE,
			       NULL, NULL, NULL),
	BT_GATT_CCC(ccc_event, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
	/* [7] control */
	BT_GATT_CHARACTERISTIC(&uuid_ctrl.uuid, BT_GATT_CHRC_WRITE, BT_GATT_PERM_WRITE, NULL,
			       wr_ctrl, NULL),
	/* [9] config */
	BT_GATT_CHARACTERISTIC(&uuid_cfg.uuid, BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE,
			       BT_GATT_PERM_READ | BT_GATT_PERM_WRITE, rd_blob, wr_cfg, &v_cfg),
	/* [11] status */
	BT_GATT_CHARACTERISTIC(&uuid_status.uuid, BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_READ, rd_blob, NULL, &v_status),
	BT_GATT_CCC(ccc_status, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
);

#define ATTR_COUNT (&sp_svc.attrs[1])
#define ATTR_EVENT (&sp_svc.attrs[4])
#define ATTR_STATUS (&sp_svc.attrs[11])

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA_BYTES(BT_DATA_UUID128_ALL, SP_UUID_SERVICE),
};
static const struct bt_data sd[] = {
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

/* 500-525 ms advertising interval: ~10 uA average while advertising */
static const struct bt_le_adv_param adv_param =
	BT_LE_ADV_PARAM_INIT(BT_LE_ADV_OPT_CONN, 0x0320, 0x0348, NULL);

static struct bt_gatt_exchange_params mtu_params;

static void mtu_cb(struct bt_conn *conn, uint8_t err, struct bt_gatt_exchange_params *p)
{
	ARG_UNUSED(p);
	LOG_INF("MTU %u (err %u)", bt_gatt_get_mtu(conn), err);
}

static void connected(struct bt_conn *conn, uint8_t err)
{
	if (err) {
		return;
	}
	advertising = false;
	cur_conn = bt_conn_ref(conn);
	mtu_params.func = mtu_cb;
	bt_gatt_exchange_mtu(conn, &mtu_params);
	if (on_conn) {
		on_conn(true);
	}
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	ARG_UNUSED(reason);
	if (cur_conn == conn) {
		bt_conn_unref(cur_conn);
		cur_conn = NULL;
	}
	notify_count = notify_event = notify_status = false;
	if (on_conn) {
		on_conn(false); /* main decides whether to re-advertise */
	}
}

BT_CONN_CB_DEFINE(conn_cbs) = {
	.connected = connected,
	.disconnected = disconnected,
};

int ble_init(ble_ctrl_cb_t ctrl, ble_cfg_cb_t cfg, ble_conn_cb_t conn)
{
	on_ctrl = ctrl;
	on_cfg = cfg;
	on_conn = conn;
	return bt_enable(NULL);
}

int ble_adv_start(void)
{
	if (advertising || cur_conn) {
		return 0;
	}
	int err = bt_le_adv_start(&adv_param, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
	if (!err || err == -EALREADY) {
		advertising = true;
		return 0;
	}
	LOG_WRN("adv start %d", err);
	return err;
}

void ble_adv_stop(void)
{
	if (advertising) {
		bt_le_adv_stop();
		advertising = false;
	}
}

bool ble_is_connected(void)
{
	return cur_conn != NULL;
}

bool ble_is_advertising(void)
{
	return advertising;
}

void ble_update_count(const sp_count_t *c)
{
	v_count = *c;
	if (cur_conn && notify_count) {
		bt_gatt_notify(cur_conn, ATTR_COUNT, &v_count, sizeof(v_count));
	}
}

void ble_update_status(const sp_status_t *s)
{
	bool changed = memcmp(&v_status, s, sizeof(*s)) != 0;
	v_status = *s;
	bt_bas_set_battery_level(s->battery_pct);
	if (changed && cur_conn && notify_status) {
		bt_gatt_notify(cur_conn, ATTR_STATUS, &v_status, sizeof(v_status));
	}
}

void ble_update_config(const sp_config_t *c)
{
	v_cfg = *c;
}

int ble_send_event(const sp_event_t *e)
{
	if (!cur_conn || !notify_event) {
		return -ENOTCONN;
	}
	return bt_gatt_notify(cur_conn, ATTR_EVENT, e, sizeof(*e));
}
