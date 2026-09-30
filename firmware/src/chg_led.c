#include "chg_led.h"

#define CHARGING_PULSES 1u
#define PAUSED_PULSES 3u

chg_led_mode_t chg_led_mode(const power_state_t *p)
{
	if (!p->vbus) {
		return CHG_LED_OFF;
	}
	if (p->chg_inhibit) {
		return CHG_LED_PAUSED;
	}
	if (p->charging) {
		return CHG_LED_CHARGING;
	}
	return p->battery_mv >= CHG_LED_FULL_MIN_MV ? CHG_LED_FULL : CHG_LED_OFF;
}

uint8_t chg_led_pulses(chg_led_mode_t mode)
{
	if (mode == CHG_LED_CHARGING) {
		return CHARGING_PULSES;
	}
	return mode == CHG_LED_PAUSED ? PAUSED_PULSES : 0u;
}
