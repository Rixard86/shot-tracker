#include "evq.h"

#include <errno.h>
#include <string.h>

#define TIME_UNSET 0

void evq_init(evq_t *q, const evq_deps_t *deps)
{
	memset(q, 0, sizeof(*q));
	q->deps = *deps;
	q->boot_seq = deps->shots ? deps->shots->newest_seq : 0u;
}

static uint32_t oldest_reject(const evq_t *q)
{
	return q->rejects_written > EVQ_REJECT_LOG_LEN ? q->rejects_written - EVQ_REJECT_LOG_LEN
						       : 0u;
}

int evq_record(evq_t *q, const sp_event_t *event)
{
	int err = 0;
	evq_state_t *s = &q->state;

	if (!event->accepted) {
		q->rejects[q->rejects_written % EVQ_REJECT_LOG_LEN] = *event;
		q->rejects_written++;
	} else if (q->deps.shots) {
		err = evlog_append(q->deps.shots, event);
	}
	if (s->live_len == EVQ_LIVE_QUEUE_LEN) {
		s->live_head = (s->live_head + 1u) % EVQ_LIVE_QUEUE_LEN;
		s->live_len--;
	}
	q->live[(s->live_head + s->live_len) % EVQ_LIVE_QUEUE_LEN] = *event;
	s->live_len++;
	return err;
}

void evq_replay(evq_t *q, uint32_t from_seq)
{
	evq_state_t *s = &q->state;

	s->replaying = true;
	s->flash_done = q->deps.shots == NULL;
	s->cursor.from_seq = from_seq;
	if (q->deps.shots) {
		evlog_rewind(q->deps.shots, &s->cursor);
	}
	s->reject_next = oldest_reject(q);
}

bool evq_busy(const evq_t *q)
{
	return q->state.live_len > 0 || q->state.replaying || q->state.end_due;
}

typedef struct {
	evq_t *q;
	evq_state_t s;
} batch_t;

static bool next_flash(batch_t *b, sp_event_t *out)
{
	if (!b->s.flash_done && evlog_next(b->q->deps.shots, &b->s.cursor) == 1) {
		*out = b->s.cursor.event;
		return true;
	}
	b->s.flash_done = true;
	return false;
}

static bool next_reject(batch_t *b, sp_event_t *out)
{
	const evq_t *q = b->q;
	evq_state_t *s = &b->s;

	if (s->reject_next < oldest_reject(q)) {
		s->reject_next = oldest_reject(q);
	}
	while (s->reject_next < q->rejects_written) {
		const sp_event_t *e = &q->rejects[s->reject_next++ % EVQ_REJECT_LOG_LEN];

		if (e->seq >= s->cursor.from_seq) {
			*out = *e;
			return true;
		}
	}
	s->replaying = false;
	s->end_due = true;
	return false;
}

static bool next_event(batch_t *b, sp_event_t *out)
{
	evq_state_t *s = &b->s;

	if (s->live_len > 0) {
		*out = b->q->live[s->live_head];
		s->live_head = (s->live_head + 1u) % EVQ_LIVE_QUEUE_LEN;
		s->live_len--;
		return true;
	}
	return s->replaying && (next_flash(b, out) || next_reject(b, out));
}

static void stamp(const evq_t *q, sp_event_t *e)
{
	if (e->t_unix_ms == TIME_UNSET && e->seq > q->boot_seq) {
		e->t_unix_ms = q->deps.unix_ms(e->t_uptime_ms);
	}
}

static uint16_t fill_batch(batch_t *b, uint16_t room)
{
	uint32_t max = room / sizeof(sp_event_t);
	uint32_t n = 0;
	sp_event_t e;

	if (max > EVQ_BATCH_MAX) {
		max = EVQ_BATCH_MAX;
	}
	while (n < max && next_event(b, &e)) {
		stamp(b->q, &e);
		memcpy(&b->q->payload[n * sizeof(e)], &e, sizeof(e));
		n++;
	}
	return (uint16_t)(n * sizeof(e));
}

static void drop_all(evq_state_t *s)
{
	s->live_len = 0;
	s->replaying = false;
	s->end_due = false;
}

static bool send_end(evq_t *q)
{
	sp_replay_end_t end = {q->state.cursor.from_seq};

	memcpy(q->payload, &end, sizeof(end));
	int err = q->deps.link.send(q->payload, sizeof(end));

	if (err == -EAGAIN || err == -ENOMEM) {
		return false;
	}
	q->state.end_due = false;
	if (err) {
		drop_all(&q->state);
	}
	return err == 0;
}

void evq_pump(evq_t *q)
{
	while (evq_busy(q)) {
		uint16_t room = q->deps.link.room();

		if (room < sizeof(sp_event_t)) {
			drop_all(&q->state);
			return;
		}
		batch_t trial = {.q = q, .s = q->state};
		uint16_t len = fill_batch(&trial, room);

		if (len == 0) {
			q->state = trial.s;
			if (!q->state.end_due || !send_end(q)) {
				return;
			}
			continue;
		}
		int err = q->deps.link.send(q->payload, len);

		if (err == -EAGAIN || err == -ENOMEM) {
			return;
		}
		q->state = trial.s;
		if (err) {
			drop_all(&q->state);
			return;
		}
	}
}
