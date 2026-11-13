#include "recovery.h"

#include "protocol.h"

#define BITS_PER_BYTE 8u
#define CODE_OFFSET 1u

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

uint32_t seq_resume(uint32_t saved_seq, uint32_t newest_logged_seq)
{
	uint32_t newest = saved_seq > newest_logged_seq ? saved_seq : newest_logged_seq;

	return newest + RECOVERY_SEQ_MARGIN;
}

uint32_t low_battery_count_next(uint32_t count, bool low)
{
	if (!low) {
		return 0u;
	}
	return count < RECOVERY_LOW_BATTERY_READINGS ? count + 1u : RECOVERY_LOW_BATTERY_READINGS;
}

bool low_battery_confirmed(uint32_t count)
{
	return count >= RECOVERY_LOW_BATTERY_READINGS;
}

uint8_t crash_mark_encode(uint32_t reason)
{
	uint32_t kept = reason < RECOVERY_CRASH_REASON_MAX ? reason : RECOVERY_CRASH_REASON_MAX;

	return (uint8_t)(kept + 1u);
}

uint8_t crash_mark_reason(uint8_t mark)
{
	return mark ? (uint8_t)(mark - 1u) : SP_CRASH_NONE;
}

uint8_t reset_classify(const reset_flags_t *f)
{
	if (f->crash_mark) {
		return SP_RESET_CRASH;
	}
	if (f->lockup) {
		return SP_RESET_LOCKUP;
	}
	if (f->watchdog) {
		return SP_RESET_WATCHDOG;
	}
	if (f->pin) {
		return SP_RESET_PIN;
	}
	if (f->wake) {
		return SP_RESET_WAKE;
	}
	if (f->software) {
		return SP_RESET_SOFTWARE;
	}
	return f->other ? SP_RESET_OTHER : SP_RESET_POWER_ON;
}

bool reset_is_crash(uint8_t cause)
{
	return cause == SP_RESET_CRASH || cause == SP_RESET_WATCHDOG || cause == SP_RESET_LOCKUP;
}

uint16_t crash_count_next(uint16_t count, uint8_t cause)
{
	if (!reset_is_crash(cause) || count == RECOVERY_CRASH_COUNT_MAX) {
		return count;
	}
	return (uint16_t)(count + 1u);
}

bool pairing_window_step(pairing_window_t *w, const vbus_sample_t *s)
{
	if (s->vbus && !w->vbus_seen) {
		w->open = true;
		w->opened_at_ms = s->now_ms;
	}
	w->vbus_seen = s->vbus;
	if (!s->vbus || s->now_ms - w->opened_at_ms >= RECOVERY_PAIRING_WINDOW_MS) {
		w->open = false;
	}
	return w->open;
}

static uint32_t read_le32(const uint8_t *p)
{
	uint32_t v = 0u;

	for (uint32_t i = 0; i < sizeof(v); i++) {
		v |= (uint32_t)p[i] << (i * BITS_PER_BYTE);
	}
	return v;
}

remote_reset_verdict_t remote_reset_check(const remote_reset_req_t *r)
{
	if (r->len < SP_FACTORY_RESET_LEN || read_le32(&r->data[CODE_OFFSET]) != SP_FACTORY_RESET_CODE) {
		return REMOTE_RESET_BAD_CODE;
	}
	return r->vbus ? REMOTE_RESET_OK : REMOTE_RESET_NO_CHARGER;
}
