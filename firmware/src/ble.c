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
#include <zephyr/settings/settings.h>
#include <zephyr/sys/atomic.h>

#include "identity.h"

LOG_MODULE_REGISTER(ble, LOG_LEVEL_INF);

#define ATT_NOTIFY_OVERHEAD 3u
#define NOTIFY_MAX_IN_FLIGHT 2
#define ENCRYPTED_CCC (BT_GATT_PERM_READ | BT_GATT_PERM_WRITE_ENCRYPT)

static struct bt_uuid_128 uuid_svc = BT_UUID_INIT_128(SP_UUID_SERVICE);
static struct bt_uuid_128 uuid_count = BT_UUID_INIT_128(SP_UUID_COUNT);
static struct bt_uuid_128 uuid_event = BT_UUID_INIT_128(SP_UUID_EVENT);
static struct bt_uuid_128 uuid_ctrl = BT_UUID_INIT_128(SP_UUID_CONTROL);
static struct bt_uuid_128 uuid_cfg = BT_UUID_INIT_128(SP_UUID_CONFIG);
static struct bt_uuid_128 uuid_status = BT_UUID_INIT_128(SP_UUID_STATUS);
static struct bt_uuid_128 uuid_capture = BT_UUID_INIT_128(SP_UUID_CAPTURE);

static sp_count_t v_count;
static sp_status_t v_status;
static sp_config_t v_cfg;

static ble_ctrl_cb_t on_ctrl;
static ble_cfg_cb_t on_cfg;
static ble_conn_cb_t on_conn;
static ble_tx_done_cb_t on_tx_done;
static atomic_t notify_in_flight;

static struct bt_conn *cur_conn;
static bool advertising;
static bool notify_count, notify_event, notify_status, notify_capture;

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
static void ccc_capture(const struct bt_gatt_attr *a, uint16_t v)
{
	ARG_UNUSED(a);
	notify_capture = v == BT_GATT_CCC_NOTIFY;
}

BT_GATT_SERVICE_DEFINE(sp_svc,
	BT_GATT_PRIMARY_SERVICE(&uuid_svc),
	/* [1] count */
	BT_GATT_CHARACTERISTIC(&uuid_count.uuid, BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_READ_ENCRYPT, rd_blob, NULL, &v_count),
	BT_GATT_CCC(ccc_count, ENCRYPTED_CCC),
	/* [4] event */
	BT_GATT_CHARACTERISTIC(&uuid_event.uuid, BT_GATT_CHRC_NOTIFY, BT_GATT_PERM_NONE,
			       NULL, NULL, NULL),
	BT_GATT_CCC(ccc_event, ENCRYPTED_CCC),
	/* [7] control */
	BT_GATT_CHARACTERISTIC(&uuid_ctrl.uuid, BT_GATT_CHRC_WRITE, BT_GATT_PERM_WRITE_ENCRYPT, NULL,
			       wr_ctrl, NULL),
	/* [9] config */
	BT_GATT_CHARACTERISTIC(&uuid_cfg.uuid, BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE,
			       BT_GATT_PERM_READ_ENCRYPT | BT_GATT_PERM_WRITE_ENCRYPT, rd_blob, wr_cfg,
			       &v_cfg),
	/* [11] status */
	BT_GATT_CHARACTERISTIC(&uuid_status.uuid, BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_READ_ENCRYPT, rd_blob, NULL, &v_status),
	BT_GATT_CCC(ccc_status, ENCRYPTED_CCC),
	BT_GATT_CHARACTERISTIC(&uuid_capture.uuid, BT_GATT_CHRC_NOTIFY, BT_GATT_PERM_NONE,
			       NULL, NULL, NULL),
	BT_GATT_CCC(ccc_capture, ENCRYPTED_CCC),
);

#define ATTR_COUNT (&sp_svc.attrs[1])
#define ATTR_EVENT (&sp_svc.attrs[4])
#define ATTR_STATUS (&sp_svc.attrs[11])
#define ATTR_CAPTURE (&sp_svc.attrs[14])

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA_BYTES(BT_DATA_UUID128_ALL, SP_UUID_SERVICE),
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
	atomic_clear(&notify_in_flight);
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
	notify_count = notify_event = notify_status = notify_capture = false;
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
	int err = bt_enable(NULL);

	if (err) {
		return err;
	}
	settings_load_subtree("bt");
	return identity_apply();
}

int ble_adv_start(void)
{
	if (advertising || cur_conn) {
		return 0;
	}
	const char *name = bt_get_name();
	struct bt_data sd[] = {
		BT_DATA(BT_DATA_NAME_COMPLETE, name, strlen(name)),
	};
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

void ble_set_tx_done_cb(ble_tx_done_cb_t cb)
{
	on_tx_done = cb;
}

static void paced_sent(struct bt_conn *conn, void *user_data)
{
	ARG_UNUSED(conn);
	ARG_UNUSED(user_data);
	if (atomic_get(&notify_in_flight) > 0) {
		atomic_dec(&notify_in_flight);
	}
	if (on_tx_done) {
		on_tx_done();
	}
}

static uint16_t room_if(bool subscribed)
{
	if (!cur_conn || !subscribed) {
		return 0;
	}
	return (uint16_t)(bt_gatt_get_mtu(cur_conn) - ATT_NOTIFY_OVERHEAD);
}

uint16_t ble_capture_room(void)
{
	return room_if(notify_capture);
}

uint16_t ble_event_room(void)
{
	return room_if(notify_event);
}

static int send_paced(struct bt_gatt_notify_params *params)
{
	if (atomic_get(&notify_in_flight) >= NOTIFY_MAX_IN_FLIGHT) {
		return -EAGAIN;
	}
	params->func = paced_sent;
	atomic_inc(&notify_in_flight);
	int err = bt_gatt_notify_cb(cur_conn, params);
	if (err) {
		atomic_dec(&notify_in_flight);
	}
	return err;
}

int ble_send_events(const uint8_t *data, uint16_t len)
{
	struct bt_gatt_notify_params params = {
		.attr = ATTR_EVENT,
		.data = data,
		.len = len,
	};

	if (!cur_conn || !notify_event) {
		return -ENOTCONN;
	}
	return send_paced(&params);
}

int ble_send_capture(const uint8_t *data, uint16_t len)
{
	struct bt_gatt_notify_params params = {
		.attr = ATTR_CAPTURE,
		.data = data,
		.len = len,
	};

	if (!cur_conn || !notify_capture) {
		return -ENOTCONN;
	}
	return send_paced(&params);
}
