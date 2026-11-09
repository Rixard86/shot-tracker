#include "recovery.h"

#include "protocol.h"

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
