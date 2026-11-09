#include <stdio.h>

#include "../src/protocol.h"
#include "../src/recovery.h"

#define POLL_MS 250u
#define SHORT_HOLD_MS 2000u
#define LONG_WAIT_MS 60000u
#define NEAR_WRAP_MS 0xFFFFF000u
#define SAVED_SEQ 5000u
#define OLDER_LOGGED_SEQ 4200u
#define NEWER_LOGGED_SEQ 5007u
#define STACK_OVERFLOW_REASON 2u
#define HUGE_REASON 1000u
#define SOME_CRASHES 41u

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

static void test_seq_resume(void)
{
	check(seq_resume(SAVED_SEQ, OLDER_LOGGED_SEQ) == SAVED_SEQ + RECOVERY_SEQ_MARGIN, "resumes past the saved seq");
	check(seq_resume(SAVED_SEQ, NEWER_LOGGED_SEQ) == NEWER_LOGGED_SEQ + RECOVERY_SEQ_MARGIN,
	      "resumes past a logged shot newer than the saved seq");
	check(seq_resume(0u, OLDER_LOGGED_SEQ) == OLDER_LOGGED_SEQ + RECOVERY_SEQ_MARGIN,
	      "wiped settings resume past the newest logged shot");
	check(seq_resume(0u, 0u) == RECOVERY_SEQ_MARGIN, "a new puck starts at the margin");
}

static void test_low_battery(void)
{
	uint32_t count = 0u;

	for (uint32_t i = 1u; i < RECOVERY_LOW_BATTERY_READINGS; i++) {
		count = low_battery_count_next(count, true);
		check(!low_battery_confirmed(count), "too few low readings for a cut-off");
	}
	uint32_t after_good = low_battery_count_next(low_battery_count_next(count, false), true);
	check(!low_battery_confirmed(after_good), "one good reading starts the count over");
	count = low_battery_count_next(count, true);
	check(low_battery_confirmed(count), "enough low readings in a row cut off");
	check(low_battery_count_next(count, true) == RECOVERY_LOW_BATTERY_READINGS, "count saturates");
}

static void test_crash_mark(void)
{
	check(crash_mark_reason(0u) == SP_CRASH_NONE, "no mark: no crash reason");
	check(crash_mark_encode(0u) != 0u, "reason 0 still leaves a mark");
	check(crash_mark_reason(crash_mark_encode(STACK_OVERFLOW_REASON)) == STACK_OVERFLOW_REASON,
	      "the reason survives the mark");
	check(crash_mark_reason(crash_mark_encode(HUGE_REASON)) == RECOVERY_CRASH_REASON_MAX,
	      "large reasons saturate below the no-crash value");
}

static void test_reset_classify(void)
{
	reset_flags_t none = {0};
	reset_flags_t all = {.crash_mark = 1u, .lockup = true, .watchdog = true, .pin = true,
			     .wake = true, .software = true, .other = true};
	reset_flags_t f = all;

	check(reset_classify(&none) == SP_RESET_POWER_ON, "no flags: power-on");
	check(reset_classify(&f) == SP_RESET_CRASH, "a crash mark wins over the software reset");
	f.crash_mark = 0u;
	check(reset_classify(&f) == SP_RESET_LOCKUP, "lockup next");
	f.lockup = false;
	check(reset_classify(&f) == SP_RESET_WATCHDOG, "then watchdog");
	f.watchdog = false;
	check(reset_classify(&f) == SP_RESET_PIN, "then pin");
	f.pin = false;
	check(reset_classify(&f) == SP_RESET_WAKE, "then wake from System OFF");
	f.wake = false;
	check(reset_classify(&f) == SP_RESET_SOFTWARE, "then software");
	f.software = false;
	check(reset_classify(&f) == SP_RESET_OTHER, "anything else: other");
}

static void test_crash_count(void)
{
	check(reset_is_crash(SP_RESET_CRASH) && reset_is_crash(SP_RESET_WATCHDOG) &&
		      reset_is_crash(SP_RESET_LOCKUP), "crash, watchdog and lockup count");
	check(!reset_is_crash(SP_RESET_SOFTWARE) && !reset_is_crash(SP_RESET_POWER_ON) &&
		      !reset_is_crash(SP_RESET_PIN) && !reset_is_crash(SP_RESET_WAKE),
	      "normal restarts do not count");
	check(crash_count_next(SOME_CRASHES, SP_RESET_WATCHDOG) == SOME_CRASHES + 1u, "counts up");
	check(crash_count_next(SOME_CRASHES, SP_RESET_SOFTWARE) == SOME_CRASHES, "unchanged");
	check(crash_count_next(RECOVERY_CRASH_COUNT_MAX, SP_RESET_CRASH) == RECOVERY_CRASH_COUNT_MAX,
	      "saturates");
}

int main(void)
{
	test_bridged_at_boot();
	test_hold_and_release(0u);
	test_hold_and_release(NEAR_WRAP_MS);
	test_short_bridge();
	test_boot_loop();
	test_seq_resume();
	test_low_battery();
	test_crash_mark();
	test_reset_classify();
	test_crash_count();
	printf("RECOVERY TESTS: %s\n", failures ? "FAIL" : "PASS");
	return failures ? 1 : 0;
}
