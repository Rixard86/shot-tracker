#ifndef WIRE_H
#define WIRE_H

#include <stdbool.h>
#include <stdint.h>

#include "protocol.h"
#include "store.h"

#define WIRE_TRIG_MIN_MG 1000u
#define WIRE_TRIG_MAX_MG 30000u
#define WIRE_MOVE_MIN_MG 50u
#define WIRE_MOVE_MAX_MG 2000u
#define WIRE_PRE_STILL_MAX_MS 10000u
#define WIRE_POST_END_MAX_MS 1500u
#define WIRE_POST_LF_MAX_MG 5000u
#define WIRE_REFRACTORY_MIN_MS 200u
#define WIRE_REFRACTORY_MAX_MS 10000u
#define WIRE_WAKE_MIN_MG 16u
#define WIRE_WAKE_MAX_MG 2000u
#define WIRE_IDLE_MIN_S 5u
#define WIRE_IDLE_MAX_S 3600u

typedef enum {
	WIRE_CFG_OK,
	WIRE_CFG_BAD_LENGTH,
	WIRE_CFG_BAD_VALUE,
} wire_cfg_result_t;

typedef struct {
	const void *data;
	uint16_t len;
} wire_bytes_t;

typedef struct {
	int64_t offset_ms;
	bool set;
} wire_clock_t;

typedef struct {
	int64_t unix_ms;
	int64_t uptime_ms;
} wire_time_t;

void wire_config_out(const store_t *s, sp_config_t *w);
void wire_config_in(const sp_config_t *w, store_t *s);
bool wire_config_valid(const sp_config_t *w);
wire_cfg_result_t wire_config_parse(sp_config_t *cfg, const wire_bytes_t *in);
bool wire_config_normalize(store_t *s);

void wire_clock_set(wire_clock_t *clock, const wire_time_t *now);
int64_t wire_clock_unix_ms(const wire_clock_t *clock, int64_t uptime_ms);

#endif
