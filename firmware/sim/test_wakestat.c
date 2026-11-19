#include <stdio.h>

#include "../src/wakestat.h"

#define FALSE_WAKES 3u
#define SHOTS_IN_SESSION 5u

static int failures;

static void check(int ok, const char *what)
{
	if (!ok) {
		printf("FAIL: %s\n", what);
		failures++;
	}
}

static void test_counts(void)
{
	wakestat_t w = {0};

	for (uint32_t i = 0; i < FALSE_WAKES; i++) {
		wakestat_wake(&w);
		wakestat_sleep(&w);
	}
	check(w.motion_wakes == FALSE_WAKES && w.wakes_without_shot == FALSE_WAKES, "wakes with no shot are counted");
	wakestat_wake(&w);
	for (uint32_t i = 0; i < SHOTS_IN_SESSION; i++) {
		wakestat_shot(&w);
	}
	wakestat_sleep(&w);
	check(w.motion_wakes == FALSE_WAKES + 1u && w.wakes_without_shot == FALSE_WAKES, "a wake with shots is not a false wake");
}

static void test_other_wakes(void)
{
	wakestat_t w = {0};

	wakestat_sleep(&w);
	check(w.motion_wakes == 0u && w.wakes_without_shot == 0u, "going idle after boot or a recording counts nothing");
	wakestat_wake(&w);
	wakestat_sleep(&w);
	wakestat_sleep(&w);
	check(w.wakes_without_shot == 1u, "one false wake is counted once");
	wakestat_shot(&w);
	wakestat_wake(&w);
	wakestat_sleep(&w);
	check(w.wakes_without_shot == 2u, "a shot before the wake does not count for it");
}

int main(void)
{
	test_counts();
	test_other_wakes();
	printf("WAKESTAT TESTS: %s\n", failures ? "FAIL" : "PASS");
	return failures ? 1 : 0;
}
