/*
 * protocol.h - ShotPuck BLE wire format (little-endian, packed).
 * Documented for app developers in docs/PROTOCOL.md. Bump PROTO_VERSION on
 * any layout change.
 */
#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>

#define PROTO_VERSION 2

/* 128-bit UUIDs: 7a1e00XX-5b0c-4f3a-9c6e-3a5d2b9e0a01 */
#define SP_UUID_BASE(x) \
	BT_UUID_128_ENCODE(0x7a1e0000 + (x), 0x5b0c, 0x4f3a, 0x9c6e, 0x3a5d2b9e0a01)
#define SP_UUID_SERVICE SP_UUID_BASE(0x01)
#define SP_UUID_COUNT   SP_UUID_BASE(0x02) /* read, notify: sp_count_t        */
#define SP_UUID_EVENT   SP_UUID_BASE(0x03) /* notify: sp_event_t               */
#define SP_UUID_CONTROL SP_UUID_BASE(0x04) /* write: sp_ctrl_* commands        */
#define SP_UUID_CONFIG  SP_UUID_BASE(0x05) /* read, write: sp_config_t         */
#define SP_UUID_STATUS  SP_UUID_BASE(0x06) /* read, notify: sp_status_t        */
#define SP_UUID_CAPTURE SP_UUID_BASE(0x07)

typedef struct __attribute__((packed)) {
	uint32_t total;     /* lifetime accepted shots (persisted)           */
	uint32_t last_seq;  /* seq of newest event in the device log         */
} sp_count_t;

/* Every triggered candidate is reported, accepted or not, so thresholds can
 * be tuned from the app. Only accepted == 1 counts as a shot. */
typedef struct __attribute__((packed)) {
	uint32_t seq;           /* monotonically increasing, persisted base   */
	int64_t t_unix_ms;      /* 0 if the phone never set the time          */
	uint32_t t_uptime_ms;
	uint16_t peak_hf_mg;
	uint16_t pre_still_ms;
	uint16_t post_lf_mg;
	uint8_t accepted;
	uint8_t reason;         /* 0 ok, 1 not still before, 2 no follow-through */
} sp_event_t;              /* 24 bytes: needs ATT MTU >= 27. The device     */
                           /* requests MTU 65 on connect.                   */

enum {
	SP_CTRL_SET_TIME = 0x01,    /* + int64 unix_ms                          */
	SP_CTRL_RESET_COUNT = 0x02, /* no payload                               */
	SP_CTRL_REPLAY = 0x03,      /* + uint32 from_seq: re-notify logged events */
	SP_CTRL_FACTORY_CFG = 0x04, /* restore default thresholds               */
	SP_CTRL_LED_BLINK = 0x05,   /* find-my-puck blink                       */
	SP_CTRL_GET_CAPTURE = 0x06,
	SP_CTRL_FORGET_BONDS = 0x07,
};

enum {
	SP_CAP_HEADER = 0x01,
	SP_CAP_DATA = 0x02,
};

typedef struct __attribute__((packed)) {
	uint8_t type;
	uint32_t seq;
	uint16_t n_samples;
	uint16_t trigger_index;
	uint16_t odr_hz;
	uint16_t acc_ug_per_lsb;
	uint16_t gyro_mdps_per_lsb;
} sp_cap_header_t;

typedef struct __attribute__((packed)) {
	uint8_t type;
	uint32_t seq;
	uint16_t first;
} sp_cap_data_t;

typedef struct __attribute__((packed)) {
	int16_t acc[3];
	int16_t gyro[3];
} sp_cap_sample_t;

typedef struct __attribute__((packed)) {
	uint8_t version;          /* PROTO_VERSION                            */
	uint16_t trig_hf_mg;
	uint16_t move_mg;
	uint16_t pre_still_min_ms;
	uint16_t post_start_ms;
	uint16_t post_end_ms;
	uint16_t post_lf_min_mg;
	uint16_t refractory_ms;
	uint16_t wake_mg;
	uint16_t idle_timeout_s;
} sp_config_t;

enum {
	SP_ST_VBUS = 1u << 0,        /* charger cable present                 */
	SP_ST_CHARGING = 1u << 1,    /* MCP73831 STAT low                      */
	SP_ST_CHG_INHIBIT = 1u << 2, /* charging blocked by temperature        */
	SP_ST_ACTIVE = 1u << 3,      /* detector running                       */
	SP_ST_TIME_SET = 1u << 4,
};

typedef struct __attribute__((packed)) {
	uint8_t battery_pct;
	uint16_t battery_mv;
	int8_t temp_c;
	uint8_t flags;
	uint8_t fw_major, fw_minor;
} sp_status_t;

#endif /* PROTOCOL_H */
