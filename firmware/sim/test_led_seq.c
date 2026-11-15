#include <stdio.h>

#include "../src/led_seq.h"

#define SHORT_MS 20u
#define ON_MS 60u
#define OFF_MS 40u
#define FIRST_SOURCE 0u
#define SECOND_SOURCE 3u
#define LOOP_ROUNDS 3u

static const uint16_t short_steps[] = {SHORT_MS};
static const uint16_t blink_steps[] = {ON_MS, OFF_MS, ON_MS};
static const led_pattern_t short_pattern = {short_steps, 1u};
static const led_pattern_t blink_pattern = {blink_steps, 3u};
static const uint16_t loop_steps[] = {ON_MS, OFF_MS};
static const led_pattern_t loop_pattern = {loop_steps, 2u};
static int failures;

static void check(int ok, const char *what)
{
	if (!ok) {
		printf("FAIL: %s\n", what);
		failures++;
	}
}

static uint32_t play_out(led_seq_t *s)
{
	uint32_t total = 0;

	while (led_seq_busy(s)) {
		total += led_seq_step_ms(s);
		led_seq_advance(s);
	}
	return total;
}

static void test_idle(void)
{
	led_seq_t s;

	led_seq_init(&s);
	check(!led_seq_level(&s) && !led_seq_busy(&s) && led_seq_step_ms(&s) == 0u, "idle LED is dark");
	led_seq_advance(&s);
	check(!led_seq_busy(&s), "advancing an idle sequencer does nothing");
}

static void test_single(void)
{
	led_seq_t s;

	led_seq_init(&s);
	check(led_seq_push(&s, &short_pattern), "first pattern starts at once");
	check(led_seq_level(&s) && led_seq_step_ms(&s) == SHORT_MS, "pulse is on for its time");
	led_seq_advance(&s);
	check(!led_seq_busy(&s) && !led_seq_level(&s), "LED dark after the pulse");
}

static void test_steps(void)
{
	led_seq_t s;

	led_seq_init(&s);
	led_seq_push(&s, &blink_pattern);
	check(led_seq_level(&s) && led_seq_step_ms(&s) == ON_MS, "step 1 on");
	led_seq_advance(&s);
	check(!led_seq_level(&s) && led_seq_step_ms(&s) == OFF_MS, "step 2 off");
	led_seq_advance(&s);
	check(led_seq_level(&s) && led_seq_step_ms(&s) == ON_MS, "step 3 on");
	led_seq_advance(&s);
	check(!led_seq_busy(&s), "pattern ends after its last step");
}

static void test_queue(void)
{
	led_seq_t s;

	led_seq_init(&s);
	led_seq_push(&s, &blink_pattern);
	check(!led_seq_push(&s, &short_pattern), "a pattern queued behind a playing one does not restart the timer");
	led_seq_advance(&s);
	led_seq_advance(&s);
	led_seq_advance(&s);
	check(led_seq_busy(&s) && led_seq_level(&s) && led_seq_step_ms(&s) == SHORT_MS, "queued pattern plays next");
	led_seq_advance(&s);
	check(!led_seq_busy(&s), "queue drains");
}

static void test_full_queue(void)
{
	led_seq_t s;

	led_seq_init(&s);
	for (uint32_t i = 0; i <= LED_SEQ_QUEUE_LEN; i++) {
		led_seq_push(&s, &short_pattern);
	}
	check(play_out(&s) == LED_SEQ_QUEUE_LEN * SHORT_MS, "a full queue drops the extra pattern");
}

static void test_steady(void)
{
	led_seq_t s;

	led_seq_init(&s);
	led_seq_steady(&s, (led_steady_t){FIRST_SOURCE, true});
	check(led_seq_level(&s) && !led_seq_busy(&s), "steady on while idle");
	led_seq_push(&s, &blink_pattern);
	led_seq_advance(&s);
	check(!led_seq_level(&s), "a pattern's off step shows over a steady LED");
	play_out(&s);
	check(led_seq_level(&s), "steady LED returns after the pattern");
	led_seq_steady(&s, (led_steady_t){SECOND_SOURCE, true});
	led_seq_steady(&s, (led_steady_t){FIRST_SOURCE, false});
	check(led_seq_level(&s), "another steady source keeps it on");
	led_seq_steady(&s, (led_steady_t){SECOND_SOURCE, false});
	check(!led_seq_level(&s), "dark when no source holds it");
}

static int loops_once(led_seq_t *s)
{
	int on = led_seq_level(s) && led_seq_step_ms(s) == ON_MS;

	led_seq_advance(s);
	on &= !led_seq_level(s) && led_seq_step_ms(s) == OFF_MS;
	led_seq_advance(s);
	return on;
}

static void test_loop(void)
{
	led_seq_t s;
	int repeats = 1;

	led_seq_init(&s);
	check(led_seq_loop(&s, &loop_pattern), "a loop from idle starts at once");
	check(!led_seq_loop(&s, &loop_pattern), "asking again while it loops changes nothing");
	for (uint32_t i = 0; i < LOOP_ROUNDS; i++) {
		repeats &= loops_once(&s);
	}
	check(repeats && led_seq_busy(&s), "the loop repeats on, off, on, off");
	for (uint32_t i = 0; i < LED_SEQ_QUEUE_LEN; i++) {
		led_seq_loop(&s, &loop_pattern);
	}
	led_seq_push(&s, &short_pattern);
	check(loops_once(&s) && led_seq_step_ms(&s) == SHORT_MS, "a pattern asked for during the loop plays after the current blink");
	led_seq_advance(&s);
	check(loops_once(&s), "and the loop resumes after it");
	led_seq_loop(&s, NULL);
	check(loops_once(&s) && !led_seq_busy(&s), "stopping lets the current blink finish, then the LED is idle");
	led_seq_push(&s, &short_pattern);
	check(!led_seq_loop(&s, &loop_pattern), "a loop asked for while busy waits its turn");
	led_seq_advance(&s);
	check(loops_once(&s) && led_seq_busy(&s), "and starts once the queue is through");
}

int main(void)
{
	test_idle();
	test_single();
	test_steps();
	test_queue();
	test_full_queue();
	test_steady();
	test_loop();
	printf("LED SEQ TESTS: %s\n", failures ? "FAIL" : "PASS");
	return failures ? 1 : 0;
}
