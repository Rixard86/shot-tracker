#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "evq_harness.h"

#define EVENTS_PER_SMALL_BATCH 2u
#define ROOM_SLACK 10u
#define ROOM_TWO_EVENTS ((uint16_t)(EVENTS_PER_SMALL_BATCH * sizeof(sp_event_t) + ROOM_SLACK))
#define LIVE_EVENTS 5u
#define MIXED_EVENTS 6u
#define ROOM_LARGE 512u
#define ROOM_NONE 0u
#define ROOM_MTU_23 20u
#define FLASH_SHOTS 25u
#define LIVE_BURST 10u
#define REJECT_BURST (EVQ_REJECT_LOG_LEN + 2u)
#define REPLAY_FROM 3u
#define LOGGED_SEQ 7u
#define OVERTAKE 20u
#define PAST_EVERYTHING 1000u

static int failures;

static void check(int ok, const char *what)
{
	if (!ok) {
		printf("FAIL: %s\n", what);
		failures++;
	}
}

static void test_live_and_batches(void)
{
	reset(true);
	room_now = ROOM_TWO_EVENTS;
	for (uint32_t seq = 1u; seq <= LIVE_EVENTS; seq++) {
		sp_event_t e = event(seq, seq % 2u == 0u);
		evq_record(&queue, &e);
	}
	pump_all();
	check(sent_in_order(1u, LIVE_EVENTS), "live events go out in order");
	check(send_calls == 3u && batch_sizes[0] == EVENTS_PER_SMALL_BATCH && batch_sizes[2] == 1u,
	      "a batch holds as many events as the MTU allows");
	check(!evq_busy(&queue), "idle after the live queue is empty");
}

static void test_batch_max(void)
{
	reset(true);
	for (uint32_t seq = 1u; seq <= FLASH_SHOTS; seq++) {
		sp_event_t e = event(seq, true);
		evlog_append(&shots, &e);
	}
	room_now = ROOM_LARGE;
	evq_replay(&queue, 1u);
	pump_all();
	check(sent_in_order(1u, FLASH_SHOTS), "replay sends every logged shot");
	check(batch_sizes[0] == EVQ_BATCH_MAX, "at most 10 events per notification");
}

static void test_replay_order(void)
{
	reset(true);
	for (uint32_t seq = 1u; seq <= MIXED_EVENTS; seq++) {
		sp_event_t e = event(seq, seq % 2u == 0u);
		if (e.accepted) {
			evlog_append(&shots, &e);
		} else {
			evq_record(&queue, &e);
		}
	}
	pump_all();
	sent_count = 0;
	evq_replay(&queue, REPLAY_FROM);
	pump_all();
	check(sent_count == 4u && sent[0] == 4u && sent[1] == 6u && sent[2] == 3u && sent[3] == 5u,
	      "replay: logged shots from seq, then rejects from seq");
	check(!evq_busy(&queue), "replay ends");
}

static void test_busy_link(void)
{
	reset(true);
	sp_event_t a = event(1u, false);
	sp_event_t b = event(2u, false);

	evq_record(&queue, &a);
	evq_record(&queue, &b);
	send_result = -EAGAIN;
	evq_pump(&queue);
	check(evq_busy(&queue) && sent_count == 0u, "a busy link keeps the batch");
	send_result = -ENOMEM;
	evq_pump(&queue);
	send_result = 0;
	pump_all();
	check(sent_in_order(1u, 2u), "the kept batch goes out once the link has room");
}

static void test_drops(void)
{
	reset(true);
	sp_event_t e = event(1u, false);

	evq_record(&queue, &e);
	evq_replay(&queue, 0u);
	room_now = ROOM_NONE;
	evq_pump(&queue);
	check(!evq_busy(&queue) && send_calls == 0u, "nothing is queued for an unsubscribed phone");
	evq_record(&queue, &e);
	room_now = ROOM_MTU_23;
	evq_pump(&queue);
	check(!evq_busy(&queue) && send_calls == 0u, "MTU 23 cannot carry an event: dropped");
	evq_record(&queue, &e);
	room_now = ROOM_MTU_247;
	send_result = -ENOTCONN;
	evq_pump(&queue);
	check(!evq_busy(&queue), "a send error drops the queue");
}

static void test_overflow(void)
{
	reset(true);
	for (uint32_t seq = 1u; seq <= LIVE_BURST; seq++) {
		sp_event_t e = event(seq, false);
		evq_record(&queue, &e);
	}
	pump_all();
	check(sent_in_order(LIVE_BURST - EVQ_LIVE_QUEUE_LEN + 1u, EVQ_LIVE_QUEUE_LEN),
	      "a full live queue keeps the newest events");

	reset(false);
	for (uint32_t seq = 1u; seq <= REJECT_BURST; seq++) {
		sp_event_t e = event(seq, false);
		evq_record(&queue, &e);
		pump_all();
	}
	sent_count = 0;
	evq_replay(&queue, 0u);
	pump_all();
	check(sent_in_order(REJECT_BURST - EVQ_REJECT_LOG_LEN + 1u, EVQ_REJECT_LOG_LEN),
	      "replay sends the newest 128 rejects");
}

static void test_ring_overtakes_replay(void)
{
	reset(false);
	for (uint32_t seq = 1u; seq <= EVQ_REJECT_LOG_LEN; seq++) {
		record_rejects(seq, seq);
		pump_all();
	}
	sent_count = 0;
	evq_replay(&queue, 0u);
	sends_left = 1u;
	evq_pump(&queue);
	record_rejects(EVQ_REJECT_LOG_LEN + 1u, EVQ_REJECT_LOG_LEN + OVERTAKE);
	sends_left = SENDS_UNLIMITED;
	pump_all();
	uint32_t resume = EVQ_BATCH_MAX + EVQ_LIVE_QUEUE_LEN;

	check(sent_count == resume + EVQ_REJECT_LOG_LEN && rising_from(resume, OVERTAKE + 1u),
	      "replay skips rejects the ring overwrote");
}

static void test_empty_replay(void)
{
	reset(true);
	evq_replay(&queue, PAST_EVERYTHING);
	evq_pump(&queue);
	check(!evq_busy(&queue) && send_calls == 0u, "a replay with nothing to send ends at once");
}

static void test_shot_log(void)
{
	reset(true);
	sp_event_t shot = event(LOGGED_SEQ, true);

	check(evq_record(&queue, &shot) == 0, "accepted shot logged");
	check(shots.newest_seq == LOGGED_SEQ, "the log holds the accepted shot");
	reset(false);
	check(evq_record(&queue, &shot) == 0, "no shot log: recording still works");
	evq_replay(&queue, 0u);
	pump_all();
	check(sent_in_order(LOGGED_SEQ, 1u), "no shot log: live events still go out");
}

int main(void)
{
	test_live_and_batches();
	test_batch_max();
	test_replay_order();
	test_busy_link();
	test_drops();
	test_overflow();
	test_ring_overtakes_replay();
	test_empty_replay();
	test_shot_log();
	printf("EVQ TESTS: %s\n", failures ? "FAIL" : "PASS");
	return failures ? 1 : 0;
}
