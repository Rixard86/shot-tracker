#ifndef CHG_LED_H
#define CHG_LED_H

#include <stdint.h>

#include "power.h"

#define CHG_LED_TICK_MS 2000u
#define CHG_LED_PULSE_MS 30
#define CHG_LED_GAP_MS 150
#define CHG_LED_FULL_MIN_MV 4100u

typedef enum {
	CHG_LED_OFF,
	CHG_LED_CHARGING,
	CHG_LED_FULL,
	CHG_LED_PAUSED,
} chg_led_mode_t;

chg_led_mode_t chg_led_mode(const power_state_t *p);
uint8_t chg_led_pulses(chg_led_mode_t mode);

#endif
