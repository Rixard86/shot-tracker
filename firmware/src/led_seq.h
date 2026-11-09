#ifndef LED_SEQ_H
#define LED_SEQ_H

#include <stdbool.h>
#include <stdint.h>

#define LED_SEQ_QUEUE_LEN 4u

typedef struct {
	const uint16_t *steps_ms;
	uint8_t steps;
} led_pattern_t;

typedef struct {
	uint8_t source;
	bool on;
} led_steady_t;

typedef struct {
	const led_pattern_t *queue[LED_SEQ_QUEUE_LEN];
	uint8_t head;
	uint8_t len;
	uint8_t step;
	uint32_t steady;
} led_seq_t;

void led_seq_init(led_seq_t *s);
bool led_seq_push(led_seq_t *s, const led_pattern_t *pattern);
void led_seq_steady(led_seq_t *s, led_steady_t request);
void led_seq_advance(led_seq_t *s);
bool led_seq_level(const led_seq_t *s);
uint32_t led_seq_step_ms(const led_seq_t *s);
bool led_seq_busy(const led_seq_t *s);

#endif
