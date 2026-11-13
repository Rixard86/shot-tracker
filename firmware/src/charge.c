#include "charge.h"

#include <string.h>

void charge_init(charge_ctl_t *c)
{
	memset(c, 0, sizeof(*c));
}

static void end_cycle(charge_ctl_t *c)
{
	c->cycle = false;
	c->at_top = false;
}

static void time_cycle(charge_ctl_t *c, const charge_sample_t *s)
{
	if (!c->cycle) {
		c->cycle = true;
		c->cycle_since_ms = s->now_ms;
	}
	if (!c->at_top && s->mv >= CHARGE_TOP_MV) {
		c->at_top = true;
		c->top_since_ms = s->now_ms;
	}
	if (c->at_top && s->now_ms - c->top_since_ms >= CHARGE_FULL_AFTER_MS) {
		c->full = true;
		end_cycle(c);
	} else if (s->now_ms - c->cycle_since_ms >= CHARGE_TIMEOUT_MS) {
		c->timed_out = true;
		end_cycle(c);
	}
}

void charge_step(charge_ctl_t *c, const charge_sample_t *s)
{
	if (!s->vbus) {
		charge_init(c);
		return;
	}
	c->charging = s->charging;
	if (c->timed_out) {
		return;
	}
	if (c->full) {
		c->full = s->mv >= CHARGE_RECHARGE_MV;
		return;
	}
	if (!s->charging) {
		end_cycle(c);
		c->full = !s->paused && s->mv >= CHARGE_DONE_MIN_MV;
		return;
	}
	time_cycle(c, s);
}

bool charge_holds_off(const charge_ctl_t *c)
{
	return c->full || c->timed_out;
}

uint16_t charge_cell_mv(const charge_ctl_t *c, uint16_t mv)
{
	if (!c->charging) {
		return mv;
	}
	return mv > CHARGE_LIFT_MV ? (uint16_t)(mv - CHARGE_LIFT_MV) : 0u;
}

uint8_t charge_shown_pct(const charge_ctl_t *c, uint8_t pct)
{
	if (c->full) {
		return CHARGE_FULL_PCT;
	}
	if (c->charging && pct > CHARGE_SHOWN_MAX_PCT) {
		return CHARGE_SHOWN_MAX_PCT;
	}
	return pct;
}
