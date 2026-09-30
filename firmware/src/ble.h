/* ble.h - ShotPuck GATT service + advertising, see docs/PROTOCOL.md */
#ifndef BLE_H
#define BLE_H

#include <stdbool.h>
#include <stdint.h>
#include "protocol.h"

/* Called from the BT RX thread: copy and hand off, do not block. */
typedef void (*ble_ctrl_cb_t)(const uint8_t *data, uint16_t len);
typedef int (*ble_cfg_cb_t)(const sp_config_t *cfg);   /* 0 = accepted */
typedef void (*ble_conn_cb_t)(bool connected);
typedef void (*ble_tx_done_cb_t)(void);

int ble_init(ble_ctrl_cb_t ctrl, ble_cfg_cb_t cfg, ble_conn_cb_t conn);
int ble_adv_start(void);
void ble_adv_stop(void);
bool ble_is_connected(void);
bool ble_is_advertising(void);
bool ble_link_secured(void);

void ble_update_count(const sp_count_t *c);
void ble_update_status(const sp_status_t *s);
void ble_update_config(const sp_config_t *c);
void ble_set_tx_done_cb(ble_tx_done_cb_t cb);
uint16_t ble_capture_room(void);
uint16_t ble_event_room(void);
int ble_send_events(const uint8_t *data, uint16_t len);
int ble_send_capture(const uint8_t *data, uint16_t len);

#endif
