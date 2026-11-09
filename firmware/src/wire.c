#include "wire.h"

#include <string.h>

#define MS_PER_S 1000u

void wire_config_out(const store_t *s, sp_config_t *w)
{
	const sd_config_t *c = &s->cfg;

	w->version = PROTO_VERSION;
	w->capture_mode = s->capture_mode;
	w->trig_hf_mg = (uint16_t)c->trig_hf_mg;
	w->move_mg = (uint16_t)c->move_mg;
	w->pre_still_min_ms = c->pre_still_min_ms;
	w->post_start_ms = c->post_start_ms;
	w->post_end_ms = c->post_end_ms;
	w->post_lf_min_mg = (uint16_t)c->post_lf_min_mg;
	w->refractory_ms = c->refractory_ms;
	w->wake_mg = (uint16_t)c->wake_mg;
	w->idle_timeout_s = (uint16_t)(c->idle_timeout_ms / MS_PER_S);
}

void wire_config_in(const sp_config_t *w, store_t *s)
{
	sd_config_t *c = &s->cfg;

	s->capture_mode = w->capture_mode;
	c->trig_hf_mg = w->trig_hf_mg;
	c->move_mg = w->move_mg;
	c->pre_still_min_ms = w->pre_still_min_ms;
	c->post_start_ms = w->post_start_ms;
	c->post_end_ms = w->post_end_ms;
	c->post_lf_min_mg = w->post_lf_min_mg;
	c->refractory_ms = w->refractory_ms;
	c->wake_mg = w->wake_mg;
	c->idle_timeout_ms = (uint32_t)w->idle_timeout_s * MS_PER_S;
}

typedef struct {
	uint32_t low;
	uint32_t high;
} range_t;

static const range_t trig_range = {WIRE_TRIG_MIN_MG, WIRE_TRIG_MAX_MG};
static const range_t move_range = {WIRE_MOVE_MIN_MG, WIRE_MOVE_MAX_MG};
static const range_t refractory_range = {WIRE_REFRACTORY_MIN_MS, WIRE_REFRACTORY_MAX_MS};
static const range_t wake_range = {WIRE_WAKE_MIN_MG, WIRE_WAKE_MAX_MG};
static const range_t idle_range = {WIRE_IDLE_MIN_S, WIRE_IDLE_MAX_S};

static bool in_range(uint32_t value, const range_t *range)
{
	return value >= range->low && value <= range->high;
}

bool wire_config_valid(const sp_config_t *w)
{
	return in_range(w->trig_hf_mg, &trig_range) && in_range(w->move_mg, &move_range) &&
	       w->pre_still_min_ms <= WIRE_PRE_STILL_MAX_MS &&
	       w->post_start_ms < w->post_end_ms && w->post_end_ms <= WIRE_POST_END_MAX_MS &&
	       w->post_lf_min_mg <= WIRE_POST_LF_MAX_MG &&
	       in_range(w->refractory_ms, &refractory_range) &&
	       in_range(w->wake_mg, &wake_range) && in_range(w->idle_timeout_s, &idle_range) &&
	       w->capture_mode <= SP_CAPTURE_EXTENDED;
}

wire_cfg_result_t wire_config_parse(sp_config_t *cfg, const wire_bytes_t *in)
{
	sp_config_t c = *cfg;
	bool current = in->len == sizeof(c);

	if (!current && in->len != SP_CONFIG_V2_BYTES) {
		return WIRE_CFG_BAD_LENGTH;
	}
	memcpy(&c, in->data, in->len);
	if (c.version != (current ? PROTO_VERSION : PROTO_VERSION_V2)) {
		return WIRE_CFG_BAD_VALUE;
	}
	c.version = PROTO_VERSION;
	if (!wire_config_valid(&c)) {
		return WIRE_CFG_BAD_VALUE;
	}
	*cfg = c;
	return WIRE_CFG_OK;
}

bool wire_config_normalize(store_t *s)
{
	sp_config_t w;

	wire_config_out(s, &w);
	if (!wire_config_valid(&w)) {
		return false;
	}
	wire_config_in(&w, s);
	return true;
}

void wire_clock_set(wire_clock_t *clock, const wire_time_t *now)
{
	clock->offset_ms = now->unix_ms - now->uptime_ms;
	clock->set = true;
}

int64_t wire_clock_unix_ms(const wire_clock_t *clock, int64_t uptime_ms)
{
	return clock->set ? clock->offset_ms + uptime_ms : 0;
}
