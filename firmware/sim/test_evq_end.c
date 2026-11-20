#include <errno.h>
#include <stdio.h>

#include "evq_harness.h"

#define LOGGED_SHOTS 25u
#define REJECTS_FROM 26u
#define REJECTS_TO 30u
#define REPLAY_FROM 3u
#define LATER_FROM 20u
#define SENT_FROM_3 (LOGGED_SHOTS - REPLAY_FROM + 1u + REJECTS_TO - REJECTS_FROM + 1u)
#define SENT_FROM_20 (LOGGED_SHOTS - LATER_FROM + 1u + REJECTS_TO - REJECTS_FROM + 1u)
#define LIVE_SEQ 31u
#define SENDS_FOR_20 2u

static int failures;

static void check(int ok, const char *what)
{
	if (!ok) {
		printf("FAIL: %s\n", what);
		failures++;
	}
}

static void fill_log(void)
{
	reset(true);
	for (uint32_t seq = 1u; seq <= LOGGED_SHOTS; seq++) {
		sp_event_t e = event(seq, true);
		evlog_append(&shots, &e);
	}
	room_now = 0u;
	record_rejects(REJECTS_FROM, REJECTS_TO);
	evq_pump(&queue);
	room_now = ROOM_MTU_247;
}

static void test_marker_after_last_event(void)
{
	fill_log();
	evq_replay(&queue, REPLAY_FROM);
	pump_all();
	check(sent_count == SENT_FROM_3, "replay sends its shots and rejects");
	check(end_count == 1u && end_from == REPLAY_FROM && end_after == SENT_FROM_3,
	      "one end marker, with the replay's from_seq, after its last event");
	check(!evq_busy(&queue), "idle after the marker");
}

static void test_marker_waits_for_room(void)
{
	fill_log();
	evq_replay(&queue, LATER_FROM);
	sends_left = 1u;
	pump_all();
	check(end_count == 0u && evq_busy(&queue), "a full link holds the marker");
	sends_left = SENDS_UNLIMITED;
	pump_all();
	check(end_count == 1u && end_after == SENT_FROM_20, "the marker goes out once the link has room");
}

static void test_no_marker_when_cut(void)
{
	fill_log();
	evq_replay(&queue, REPLAY_FROM);
	sends_left = 1u;
	evq_pump(&queue);
	room_now = 0u;
	evq_pump(&queue);
	room_now = ROOM_MTU_247;
	sends_left = SENDS_UNLIMITED;
	pump_all();
	check(end_count == 0u, "a replay cut by an unsubscribed or lost link sends no marker");
	fill_log();
	evq_replay(&queue, REPLAY_FROM);
	sends_left = 1u;
	evq_pump(&queue);
	send_result = -ENOTCONN;
	evq_pump(&queue);
	send_result = 0;
	sends_left = SENDS_UNLIMITED;
	pump_all();
	check(end_count == 0u, "a replay cut by a send error sends no marker");
	fill_log();
	evq_replay(&queue, LATER_FROM);
	sends_left = SENDS_FOR_20;
	pump_all();
	check(sent_count == SENT_FROM_20 && end_count == 0u && evq_busy(&queue), "the marker is held");
	room_now = 0u;
	evq_pump(&queue);
	check(!evq_busy(&queue), "a held marker is dropped with the link, so the puck can sleep");
	room_now = ROOM_MTU_247;
	sends_left = SENDS_UNLIMITED;
	pump_all();
	check(end_count == 0u, "a dropped marker is not sent on the next link");
}

static void test_newer_replay_wins(void)
{
	fill_log();
	evq_replay(&queue, REPLAY_FROM);
	sends_left = 1u;
	evq_pump(&queue);
	evq_replay(&queue, LATER_FROM);
	sends_left = SENDS_UNLIMITED;
	pump_all();
	check(end_count == 1u && end_from == LATER_FROM, "only the newest REPLAY gets a marker");
}

static void test_live_only_no_marker(void)
{
	reset(true);
	sp_event_t e = event(LIVE_SEQ, true);

	evq_record(&queue, &e);
	pump_all();
	check(sent_count == 1u && end_count == 0u, "live events alone send no marker");
}

int main(void)
{
	test_marker_after_last_event();
	test_marker_waits_for_room();
	test_no_marker_when_cut();
	test_newer_replay_wins();
	test_live_only_no_marker();
	printf("EVQ END TESTS: %s\n", failures ? "FAIL" : "PASS");
	return failures ? 1 : 0;
}
