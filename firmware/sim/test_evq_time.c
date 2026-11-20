#include <stdio.h>

#include "evq_harness.h"

#define PHONE_OFFSET_MS 1763600000000LL
#define FIRST_UPTIME_MS 1000u
#define UPTIME_STEP_MS 1500u
#define MEASURED_UNIX_MS 1763600123456LL
#define OLD_BOOT_SHOTS 3u
#define NEW_BOOT_UPTIME_MS 400u

static int failures;

static void check(int ok, const char *what)
{
	if (!ok) {
		printf("FAIL: %s\n", what);
		failures++;
	}
}

static sp_event_t untimed(uint32_t seq, bool accepted)
{
	sp_event_t e = event(seq, accepted);

	e.t_uptime_ms = FIRST_UPTIME_MS + (seq - 1u) * UPTIME_STEP_MS;
	return e;
}

static int64_t filled(uint32_t seq)
{
	return PHONE_OFFSET_MS + FIRST_UPTIME_MS + (seq - 1u) * UPTIME_STEP_MS;
}

static void record_offline(uint32_t last)
{
	room_now = 0u;
	for (uint32_t seq = 1u; seq <= last; seq++) {
		sp_event_t e = untimed(seq, seq % 2u == 1u);
		evq_record(&queue, &e);
		evq_pump(&queue);
	}
	room_now = ROOM_MTU_247;
}

static void set_phone_time(void)
{
	clock_offset_ms = PHONE_OFFSET_MS;
	clock_set = true;
}

static void test_fill_after_set_time(void)
{
	reset(true);
	record_offline(OLD_BOOT_SHOTS);
	set_phone_time();
	evq_replay(&queue, 1u);
	pump_all();
	check(sent_count == OLD_BOOT_SHOTS, "replay sends the shots and the reject");
	for (uint32_t i = 0; i < sent_count; i++) {
		check(sent_time[i] == filled(sent[i]), "events of this boot get the time once it is set");
	}
}

static void test_no_time_without_set_time(void)
{
	reset(true);
	record_offline(OLD_BOOT_SHOTS);
	evq_replay(&queue, 1u);
	pump_all();
	check(sent_count == OLD_BOOT_SHOTS && sent_time[0] == 0 && sent_time[OLD_BOOT_SHOTS - 1u] == 0,
	      "no SET_TIME yet: events still go out untimed");
}

static void test_measured_time_kept(void)
{
	reset(true);
	sp_event_t e = untimed(1u, true);

	e.t_unix_ms = MEASURED_UNIX_MS;
	evq_record(&queue, &e);
	set_phone_time();
	pump_all();
	check(sent_count == 1u && sent_time[0] == MEASURED_UNIX_MS, "a time taken at the trigger is kept");
}

static void test_live_event_filled(void)
{
	reset(true);
	sp_event_t e = untimed(1u, false);

	evq_record(&queue, &e);
	set_phone_time();
	pump_all();
	check(sent_count == 1u && sent_time[0] == filled(1u), "a live event queued before SET_TIME gets the time");
}

static void test_old_boot_stays_untimed(void)
{
	reset(true);
	for (uint32_t seq = 1u; seq <= OLD_BOOT_SHOTS; seq++) {
		sp_event_t e = untimed(seq, true);
		evlog_append(&shots, &e);
	}
	reboot();
	sp_event_t now = untimed(OLD_BOOT_SHOTS + 1u, true);
	now.t_uptime_ms = NEW_BOOT_UPTIME_MS;
	evq_record(&queue, &now);
	evq_pump(&queue);
	sent_count = 0;
	set_phone_time();
	evq_replay(&queue, 1u);
	pump_all();
	check(sent_count == OLD_BOOT_SHOTS + 1u, "replay sends both boots");
	check(sent_time[0] == 0 && sent_time[OLD_BOOT_SHOTS - 1u] == 0,
	      "shots of an earlier boot stay untimed: that boot's offset is unknown");
	check(sent_time[OLD_BOOT_SHOTS] == PHONE_OFFSET_MS + NEW_BOOT_UPTIME_MS, "the shot of this boot is filled");
	reboot();
	set_phone_time();
	evq_replay(&queue, OLD_BOOT_SHOTS + 1u);
	pump_all();
	check(sent_count == 1u && sent_time[0] == 0, "the log keeps the record as written: untimed after the next boot");
}

int main(void)
{
	test_fill_after_set_time();
	test_no_time_without_set_time();
	test_measured_time_kept();
	test_live_event_filled();
	test_old_boot_stays_untimed();
	printf("EVQ TIME TESTS: %s\n", failures ? "FAIL" : "PASS");
	return failures ? 1 : 0;
}
