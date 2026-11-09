#ifndef LED_H
#define LED_H

#include <stdbool.h>
#include <stdint.h>

#include "led_seq.h"

typedef enum {
	LED_SHOT,
	LED_BOOT,
	LED_NO_SENSOR,
	LED_CLAIM,
	LED_PAIRED,
	LED_FIND,
	LED_RESET_DONE,
	LED_PATTERNS,
} led_pattern_id_t;

enum {
	LED_STEADY_CHARGE_FULL,
	LED_STEADY_PAD_HOLD,
};

int led_init(void);
void led_play(led_pattern_id_t id);
void led_play_pulses(uint8_t count);
void led_steady(led_steady_t request);
bool led_busy(void);

#endif
