#include "led.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#include "chg_led.h"

#define SHOT_MS 20
#define BOOT_MS 200
#define NO_SENSOR_MS 50
#define CLAIM_MS 30
#define PAIRED_MS 60
#define FIND_ON_MS 80
#define FIND_OFF_MS 120
#define RESET_DONE_MS 80
#define MAX_PULSES 3u
#define ONE_PULSE_STEPS 1u
#define TWO_PULSE_STEPS 3u
#define THREE_PULSE_STEPS 5u

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);

static const uint16_t shot_ms[] = {SHOT_MS};
static const uint16_t boot_ms[] = {BOOT_MS};
static const uint16_t no_sensor_ms[] = {NO_SENSOR_MS};
static const uint16_t claim_ms[] = {CLAIM_MS};
static const uint16_t paired_ms[] = {PAIRED_MS, PAIRED_MS, PAIRED_MS};
static const uint16_t find_ms[] = {FIND_ON_MS, FIND_OFF_MS, FIND_ON_MS, FIND_OFF_MS, FIND_ON_MS,
				   FIND_OFF_MS, FIND_ON_MS, FIND_OFF_MS, FIND_ON_MS};
static const uint16_t reset_done_ms[] = {RESET_DONE_MS, RESET_DONE_MS, RESET_DONE_MS, RESET_DONE_MS, RESET_DONE_MS,
					 RESET_DONE_MS, RESET_DONE_MS, RESET_DONE_MS, RESET_DONE_MS};
static const uint16_t pulses_ms[] = {CHG_LED_PULSE_MS, CHG_LED_GAP_MS, CHG_LED_PULSE_MS, CHG_LED_GAP_MS,
				     CHG_LED_PULSE_MS};

static const led_pattern_t patterns[LED_PATTERNS] = {
	[LED_SHOT] = {shot_ms, ARRAY_SIZE(shot_ms)},
	[LED_BOOT] = {boot_ms, ARRAY_SIZE(boot_ms)},
	[LED_NO_SENSOR] = {no_sensor_ms, ARRAY_SIZE(no_sensor_ms)},
	[LED_CLAIM] = {claim_ms, ARRAY_SIZE(claim_ms)},
	[LED_PAIRED] = {paired_ms, ARRAY_SIZE(paired_ms)},
	[LED_FIND] = {find_ms, ARRAY_SIZE(find_ms)},
	[LED_RESET_DONE] = {reset_done_ms, ARRAY_SIZE(reset_done_ms)},
};

static const led_pattern_t pulse_trains[MAX_PULSES] = {
	{pulses_ms, ONE_PULSE_STEPS},
	{pulses_ms, TWO_PULSE_STEPS},
	{pulses_ms, THREE_PULSE_STEPS},
};

static struct k_timer timer;
static struct k_spinlock lock;
static led_seq_t seq;

static void show_locked(void)
{
	uint32_t ms = led_seq_step_ms(&seq);

	gpio_pin_set_dt(&led, led_seq_level(&seq) ? 1 : 0);
	if (ms > 0u) {
		k_timer_start(&timer, K_MSEC(ms), K_NO_WAIT);
	}
}

static void step_done(struct k_timer *t)
{
	ARG_UNUSED(t);
	k_spinlock_key_t key = k_spin_lock(&lock);

	led_seq_advance(&seq);
	show_locked();
	k_spin_unlock(&lock, key);
}

static void push(const led_pattern_t *pattern)
{
	k_spinlock_key_t key = k_spin_lock(&lock);

	if (led_seq_push(&seq, pattern)) {
		show_locked();
	}
	k_spin_unlock(&lock, key);
}

int led_init(void)
{
	led_seq_init(&seq);
	k_timer_init(&timer, step_done, NULL);
	return gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);
}

void led_play(led_pattern_id_t id)
{
	if (id < LED_PATTERNS) {
		push(&patterns[id]);
	}
}

void led_play_pulses(uint8_t count)
{
	if (count > 0u && count <= MAX_PULSES) {
		push(&pulse_trains[count - 1u]);
	}
}

void led_steady(led_steady_t request)
{
	k_spinlock_key_t key = k_spin_lock(&lock);

	led_seq_steady(&seq, request);
	if (!led_seq_busy(&seq)) {
		gpio_pin_set_dt(&led, led_seq_level(&seq) ? 1 : 0);
	}
	k_spin_unlock(&lock, key);
}

bool led_busy(void)
{
	k_spinlock_key_t key = k_spin_lock(&lock);
	bool busy = led_seq_busy(&seq);

	k_spin_unlock(&lock, key);
	return busy;
}
