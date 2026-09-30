#include <stdio.h>

#include "../src/recovery.h"

#define POLL_MS 250u
#define SHORT_HOLD_MS 2000u
#define LONG_WAIT_MS 60000u
#define NEAR_WRAP_MS 0xFFFFF000u

static int failures;

static void check(int ok, const char *what)
{
	if (!ok) {
		printf("FAIL: %s\n", what);
		failures++;
	}
}

static int fires_during(pad_hold_t *h, pad_sample_t *s)
{
	int fired = 0;
	uint32_t end = s->now_ms + LONG_WAIT_MS;

	while (s->now_ms != end) {
		fired += pad_hold_step(h, s) ? 1 : 0;
		s->now_ms += POLL_MS;
	}
	return fired;
}

static void test_bridged_at_boot(void)
{
	pad_hold_t h;
	pad_sample_t s = {.bridged = true, .now_ms = 0u};

	pad_hold_init(&h);
	check(fires_during(&h, &s) == 0, "a short present at boot never resets");
	check(!pad_hold_active(&h), "no hold feedback for a short present at boot");
}

static void test_hold_and_release(uint32_t start_ms)
{
	pad_hold_t h;
	pad_sample_t s = {.bridged = false, .now_ms = start_ms};

	pad_hold_init(&h);
	pad_hold_step(&h, &s);
	s.bridged = true;
	pad_hold_step(&h, &s);
	check(pad_hold_active(&h), "hold feedback once bridged");
	s.now_ms += RECOVERY_HOLD_MS - POLL_MS;
	check(!pad_hold_step(&h, &s), "not before the hold time");
	s.now_ms += POLL_MS;
	check(pad_hold_step(&h, &s), "fires at the hold time");
	check(fires_during(&h, &s) == 0, "fires once per bridge, however long it is held");
	s.bridged = false;
	pad_hold_step(&h, &s);
	s.bridged = true;
	check(fires_during(&h, &s) == 1, "a new bridge after release fires again");
}

static void test_short_bridge(void)
{
	pad_hold_t h;
	pad_sample_t s = {.bridged = false, .now_ms = 0u};

	pad_hold_init(&h);
	pad_hold_step(&h, &s);
	s.bridged = true;
	for (uint32_t t = 0u; t < SHORT_HOLD_MS; t += POLL_MS) {
		check(!pad_hold_step(&h, &s), "short bridge does not fire");
		s.now_ms += POLL_MS;
	}
	s.bridged = false;
	pad_hold_step(&h, &s);
	check(!pad_hold_active(&h), "release clears the hold");
}

static void test_boot_loop(void)
{
	uint32_t count = 0u;

	for (uint32_t i = 1u; i < RECOVERY_BOOT_LOOP_LIMIT; i++) {
		count = boot_loop_count_next(count, true);
		check(!boot_loop_tripped(count), "not tripped before the limit");
	}
	count = boot_loop_count_next(count, true);
	check(boot_loop_tripped(count), "tripped at the limit");
	check(boot_loop_count_next(count, true) == RECOVERY_BOOT_LOOP_LIMIT, "count saturates");
	check(boot_loop_count_next(count, false) == 0u, "a clean boot clears the count");
}

int main(void)
{
	test_bridged_at_boot();
	test_hold_and_release(0u);
	test_hold_and_release(NEAR_WRAP_MS);
	test_short_bridge();
	test_boot_loop();
	printf("RECOVERY TESTS: %s\n", failures ? "FAIL" : "PASS");
	return failures ? 1 : 0;
}
