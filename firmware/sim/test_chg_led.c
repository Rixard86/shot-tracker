#include <stdio.h>

#include "../src/chg_led.h"

#define CELL_FULL_MV 4180u
#define CELL_LOW_MV 3900u
#define CHARGING_BLINKS 1u
#define PAUSED_BLINKS 3u

static int failures;

static void check(int ok, const char *what)
{
	if (!ok) {
		printf("FAIL: %s\n", what);
		failures++;
	}
}

static power_state_t on_charger(uint16_t battery_mv, bool charging)
{
	power_state_t p = {.battery_mv = battery_mv, .vbus = true, .charging = charging};
	return p;
}

static void test_unplugged(void)
{
	power_state_t p = on_charger(CELL_FULL_MV, true);
	p.vbus = false;
	p.chg_inhibit = true;
	check(chg_led_mode(&p) == CHG_LED_OFF, "unplugged is dark");
	check(chg_led_pulses(CHG_LED_OFF) == 0u, "dark has no blinks");
}

static void test_charging(void)
{
	power_state_t p = on_charger(CELL_LOW_MV, true);
	check(chg_led_mode(&p) == CHG_LED_CHARGING, "charging");
	check(chg_led_pulses(CHG_LED_CHARGING) == CHARGING_BLINKS, "charging blinks once");
	p.battery_mv = CELL_FULL_MV;
	check(chg_led_mode(&p) == CHG_LED_CHARGING, "top-up near full still blinks");
}

static void test_full(void)
{
	power_state_t p = on_charger(CELL_FULL_MV, false);
	check(chg_led_mode(&p) == CHG_LED_FULL, "done and high is full");
	check(chg_led_pulses(CHG_LED_FULL) == 0u, "full is steady, no blinks");
	p.battery_mv = CHG_LED_FULL_MIN_MV;
	check(chg_led_mode(&p) == CHG_LED_FULL, "full at the threshold");
	p.battery_mv = CHG_LED_FULL_MIN_MV - 1u;
	check(chg_led_mode(&p) == CHG_LED_OFF, "not charging and low is not full");
}

static void test_paused(void)
{
	power_state_t p = on_charger(CELL_LOW_MV, false);
	p.chg_inhibit = true;
	check(chg_led_mode(&p) == CHG_LED_PAUSED, "temperature pause");
	check(chg_led_pulses(CHG_LED_PAUSED) == PAUSED_BLINKS, "pause blinks three times");
	p.battery_mv = CELL_FULL_MV;
	check(chg_led_mode(&p) == CHG_LED_PAUSED, "pause wins over full");
}

int main(void)
{
	test_unplugged();
	test_charging();
	test_full();
	test_paused();
	printf("CHG LED TESTS: %s\n", failures ? "FAIL" : "PASS");
	return failures ? 1 : 0;
}
