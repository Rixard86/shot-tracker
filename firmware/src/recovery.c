#include "recovery.h"

void pad_hold_init(pad_hold_t *h)
{
	h->armed = false;
	h->held = false;
	h->since_ms = 0u;
}

bool pad_hold_step(pad_hold_t *h, const pad_sample_t *s)
{
	if (!s->bridged) {
		h->armed = true;
		h->held = false;
		return false;
	}
	if (!h->armed) {
		return false;
	}
	if (!h->held) {
		h->held = true;
		h->since_ms = s->now_ms;
		return false;
	}
	if (s->now_ms - h->since_ms < RECOVERY_HOLD_MS) {
		return false;
	}
	h->armed = false;
	h->held = false;
	return true;
}

bool pad_hold_active(const pad_hold_t *h)
{
	return h->armed && h->held;
}

uint32_t boot_loop_count_next(uint32_t count, bool crashed)
{
	if (!crashed) {
		return 0u;
	}
	return count < RECOVERY_BOOT_LOOP_LIMIT ? count + 1u : RECOVERY_BOOT_LOOP_LIMIT;
}

bool boot_loop_tripped(uint32_t count)
{
	return count >= RECOVERY_BOOT_LOOP_LIMIT;
}
