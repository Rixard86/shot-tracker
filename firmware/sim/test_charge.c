#include <stdio.h>

#include "../src/charge.h"

#define CC_MV 3900u
#define CV_MV 4200u
#define RESTED_FULL_MV 4160u
#define NEAR_WRAP_MS 0xFFFFF000u
#define ONE_MS 1u
#define ONE_MV 1u
#define HALF_HOUR_MS (30u * CHARGE_MS_PER_MIN)
#define LOW_RAW_MV 30u
#define MID_PCT 60u

static int failures;

static void check(int ok, const char *what)
{
	if (!ok) {
		printf("FAIL: %s\n", what);
		failures++;
	}
}

static charge_ctl_t fresh(void)
{
	charge_ctl_t c;
	charge_init(&c);
	return c;
}

static charge_sample_t charging_at(uint16_t mv, uint32_t now_ms)
{
	charge_sample_t s = {.vbus = true, .charging = true, .mv = mv, .now_ms = now_ms};
	return s;
}

static void step(charge_ctl_t *c, charge_sample_t s)
{
	charge_step(c, &s);
}

static void test_full_after_top(uint32_t t0)
{
	charge_ctl_t c = fresh();
	step(&c, charging_at(CC_MV, t0));
	step(&c, charging_at(CHARGE_TOP_MV, t0 + HALF_HOUR_MS));
	step(&c, charging_at(CHARGE_TOP_MV - ONE_MV, t0 + HALF_HOUR_MS + ONE_MS));
	step(&c, charging_at(CV_MV, t0 + HALF_HOUR_MS + CHARGE_FULL_AFTER_MS - ONE_MS));
	check(!c.full && !charge_holds_off(&c), "not full one ms early, dip did not restart the timer");
	step(&c, charging_at(CV_MV, t0 + HALF_HOUR_MS + CHARGE_FULL_AFTER_MS));
	check(c.full && charge_holds_off(&c), "full after the top time");
	check(!c.timed_out, "full is not a timeout");
}

static void test_charger_done(void)
{
	charge_ctl_t c = fresh();
	charge_sample_t s = charging_at(CHARGE_DONE_MIN_MV - ONE_MV, 0u);
	s.charging = false;
	step(&c, s);
	check(!c.full, "charger idle below the done level is not full");
	s.mv = CHARGE_DONE_MIN_MV;
	s.paused = true;
	step(&c, s);
	check(!c.full, "temperature pause is not full");
	s.paused = false;
	step(&c, s);
	check(c.full && charge_holds_off(&c), "charger done at the level is full");
}

static void test_recharge(void)
{
	charge_ctl_t c = fresh();
	charge_sample_t s = charging_at(RESTED_FULL_MV, 0u);
	s.charging = false;
	step(&c, s);
	s.mv = CHARGE_RECHARGE_MV;
	step(&c, s);
	check(c.full, "hold at the recharge level");
	s.mv = CHARGE_RECHARGE_MV - ONE_MV;
	step(&c, s);
	check(!c.full && !charge_holds_off(&c), "release below the recharge level");
	step(&c, s);
	check(!c.full, "no new full while the charger restarts");
}

static void test_timeout(void)
{
	charge_ctl_t c = fresh();
	step(&c, charging_at(CC_MV, 0u));
	step(&c, charging_at(CC_MV, CHARGE_TIMEOUT_MS - ONE_MS));
	check(!c.timed_out, "no timeout one ms early");
	step(&c, charging_at(CC_MV, CHARGE_TIMEOUT_MS));
	check(c.timed_out && charge_holds_off(&c) && !c.full, "timeout holds the charger off");
	charge_sample_t s = charging_at(RESTED_FULL_MV, CHARGE_TIMEOUT_MS + ONE_MS);
	s.charging = false;
	step(&c, s);
	check(c.timed_out && !c.full, "timeout stays until unplugged");
	s.vbus = false;
	step(&c, s);
	check(!c.timed_out && !charge_holds_off(&c), "unplug clears the timeout");
}

static void test_late_top_times_out(void)
{
	charge_ctl_t c = fresh();
	step(&c, charging_at(CC_MV, 0u));
	step(&c, charging_at(CHARGE_TOP_MV, CHARGE_TIMEOUT_MS - HALF_HOUR_MS));
	step(&c, charging_at(CV_MV, CHARGE_TIMEOUT_MS));
	check(c.timed_out && !c.full, "the cycle limit counts from the start of charging");
	c = fresh();
	step(&c, charging_at(CC_MV, 0u));
	step(&c, charging_at(CHARGE_TOP_MV, CHARGE_TIMEOUT_MS - CHARGE_FULL_AFTER_MS));
	step(&c, charging_at(CV_MV, CHARGE_TIMEOUT_MS));
	check(c.full && !c.timed_out, "full wins when both fall due together");
}

static void test_topup_after_timed_full(void)
{
	charge_ctl_t c = fresh();
	step(&c, charging_at(CHARGE_TOP_MV, 0u));
	step(&c, charging_at(CV_MV, CHARGE_FULL_AFTER_MS));
	charge_sample_t s = charging_at(CHARGE_RECHARGE_MV - ONE_MV, CHARGE_FULL_AFTER_MS + HALF_HOUR_MS);
	s.charging = false;
	step(&c, s);
	check(!c.full, "released for a top-up");
	uint32_t restart_ms = s.now_ms + ONE_MS;
	step(&c, charging_at(CV_MV, restart_ms));
	check(!c.full && c.at_top, "a top-up times its own top");
	step(&c, charging_at(CV_MV, restart_ms + CHARGE_FULL_AFTER_MS));
	check(c.full && !c.timed_out, "the top-up ends after its own top time");
}

static void test_pause_restarts_cycle(void)
{
	charge_ctl_t c = fresh();
	step(&c, charging_at(CC_MV, 0u));
	charge_sample_t s = charging_at(CC_MV, CHARGE_TIMEOUT_MS - HALF_HOUR_MS);
	s.charging = false;
	s.paused = true;
	step(&c, s);
	step(&c, charging_at(CC_MV, CHARGE_TIMEOUT_MS));
	check(!c.timed_out, "a pause starts a new cycle");
}

static void test_unplug_resets(void)
{
	charge_ctl_t c = fresh();
	charge_sample_t s = charging_at(RESTED_FULL_MV, 0u);
	s.charging = false;
	step(&c, s);
	s.vbus = false;
	step(&c, s);
	check(!c.full && !c.charging && !charge_holds_off(&c), "unplug clears full");
}

static void test_inhibit_pin(void)
{
	charge_ctl_t c = fresh();
	charge_sample_t cold = {.vbus = false, .paused = true, .mv = CC_MV};

	step(&c, cold);
	check(!charge_inhibit_pin(&c, &cold), "no inhibit current without a cable, even outside 0-45 C");
	cold.vbus = true;
	step(&c, cold);
	check(charge_inhibit_pin(&c, &cold), "with a cable, the temperature pause drives the inhibit pin");
	charge_sample_t warm = charging_at(CC_MV, 0u);
	step(&c, warm);
	check(!charge_inhibit_pin(&c, &warm), "charging allowed: inhibit pin low");
	c.full = true;
	check(charge_inhibit_pin(&c, &warm), "full holds the charger off with a cable");
	warm.vbus = false;
	check(!charge_inhibit_pin(&c, &warm), "nothing to hold off without a cable");
}

static void test_shown(void)
{
	charge_ctl_t c = fresh();
	check(charge_cell_mv(&c, CC_MV) == CC_MV, "no lift correction off charge");
	check(charge_shown_pct(&c, CHARGE_FULL_PCT) == CHARGE_FULL_PCT, "100 off charge");
	step(&c, charging_at(CC_MV, 0u));
	check(charge_cell_mv(&c, CC_MV) == CC_MV - CHARGE_LIFT_MV, "lift removed while charging");
	check(charge_cell_mv(&c, LOW_RAW_MV) == 0u, "lift correction never wraps");
	check(charge_shown_pct(&c, CHARGE_FULL_PCT) == CHARGE_SHOWN_MAX_PCT, "99 at most while charging");
	check(charge_shown_pct(&c, MID_PCT) == MID_PCT, "mid charge unchanged");
	c.full = true;
	check(charge_shown_pct(&c, MID_PCT) == CHARGE_FULL_PCT, "full shows 100");
}

int main(void)
{
	test_full_after_top(0u);
	test_full_after_top(NEAR_WRAP_MS);
	test_charger_done();
	test_recharge();
	test_timeout();
	test_late_top_times_out();
	test_topup_after_timed_full();
	test_pause_restarts_cycle();
	test_unplug_resets();
	test_inhibit_pin();
	test_shown();
	printf("CHARGE TESTS: %s\n", failures ? "FAIL" : "PASS");
	return failures ? 1 : 0;
}
