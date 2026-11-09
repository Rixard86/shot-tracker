#include "led_seq.h"

#include <string.h>

#define ON_STEP_PARITY 2u

void led_seq_init(led_seq_t *s)
{
	memset(s, 0, sizeof(*s));
}

bool led_seq_push(led_seq_t *s, const led_pattern_t *pattern)
{
	if (s->len == LED_SEQ_QUEUE_LEN) {
		return false;
	}
	s->queue[(s->head + s->len) % LED_SEQ_QUEUE_LEN] = pattern;
	s->len++;
	return s->len == 1u;
}

void led_seq_steady(led_seq_t *s, led_steady_t request)
{
	uint32_t bit = 1u << request.source;

	s->steady = request.on ? s->steady | bit : s->steady & ~bit;
}

void led_seq_advance(led_seq_t *s)
{
	if (s->len == 0u) {
		return;
	}
	s->step++;
	if (s->step < s->queue[s->head]->steps) {
		return;
	}
	s->step = 0;
	s->head = (uint8_t)((s->head + 1u) % LED_SEQ_QUEUE_LEN);
	s->len--;
}

bool led_seq_level(const led_seq_t *s)
{
	if (s->len == 0u) {
		return s->steady != 0u;
	}
	return s->step % ON_STEP_PARITY == 0u;
}

uint32_t led_seq_step_ms(const led_seq_t *s)
{
	return s->len == 0u ? 0u : s->queue[s->head]->steps_ms[s->step];
}

bool led_seq_busy(const led_seq_t *s)
{
	return s->len > 0u;
}
