#include "event_tx.h"

#include <errno.h>
#include <string.h>
#include <zephyr/logging/log.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/util.h>

#include "ble.h"
#include "evlog.h"

LOG_MODULE_REGISTER(event_tx, LOG_LEVEL_INF);

#define SECTOR_BYTES 4096u
#define REJECT_LOG_LEN 128u
#define LIVE_QUEUE_LEN 8u
#define BATCH_MAX 10u
#define PAYLOAD_MAX (BATCH_MAX * sizeof(sp_event_t))

typedef struct {
	uint32_t live_head;
	uint32_t live_len;
	bool replaying;
	bool flash_done;
	evlog_cursor_t cursor;
	uint32_t reject_next;
} tx_state_t;

static const struct flash_area *area;
static evlog_t shots;
static bool shots_ok;
static sp_event_t rejects[REJECT_LOG_LEN];
static uint32_t rejects_written;
static sp_event_t live[LIVE_QUEUE_LEN];
static tx_state_t state;
static uint8_t payload[PAYLOAD_MAX];

static int area_read(uint32_t offset, evlog_record_t *rec)
{
	return flash_area_read(area, offset, rec, sizeof(*rec));
}

static int area_write(uint32_t offset, const evlog_record_t *rec)
{
	return flash_area_write(area, offset, rec, sizeof(*rec));
}

static int area_erase(uint32_t offset)
{
	return flash_area_erase(area, offset, SECTOR_BYTES);
}

static evlog_flash_t shots_flash = {
	.read = area_read,
	.write = area_write,
	.erase_sector = area_erase,
	.sector_size = SECTOR_BYTES,
};

int event_tx_init(void)
{
	int err = flash_area_open(FIXED_PARTITION_ID(eventlog_partition), &area);

	if (err) {
		return err;
	}
	shots_flash.size = (uint32_t)area->fa_size;
	err = evlog_init(&shots, &shots_flash);
	shots_ok = err == 0;
	return err;
}

static uint32_t oldest_reject(void)
{
	return rejects_written > REJECT_LOG_LEN ? rejects_written - REJECT_LOG_LEN : 0u;
}

void event_tx_record(const sp_event_t *event)
{
	if (!event->accepted) {
		rejects[rejects_written % REJECT_LOG_LEN] = *event;
		rejects_written++;
	} else if (shots_ok && evlog_append(&shots, event)) {
		LOG_WRN("shot log write failed");
	}
	if (state.live_len == LIVE_QUEUE_LEN) {
		state.live_head = (state.live_head + 1u) % LIVE_QUEUE_LEN;
		state.live_len--;
	}
	live[(state.live_head + state.live_len) % LIVE_QUEUE_LEN] = *event;
	state.live_len++;
}

void event_tx_replay(uint32_t from_seq)
{
	state.replaying = true;
	state.flash_done = !shots_ok;
	state.cursor.from_seq = from_seq;
	evlog_rewind(&shots, &state.cursor);
	state.reject_next = oldest_reject();
}

bool event_tx_busy(void)
{
	return state.live_len > 0 || state.replaying;
}

static bool next_replay(tx_state_t *s, sp_event_t *out)
{
	if (!s->flash_done) {
		if (evlog_next(&shots, &s->cursor) == 1) {
			*out = s->cursor.event;
			return true;
		}
		s->flash_done = true;
	}
	s->reject_next = MAX(s->reject_next, oldest_reject());
	while (s->reject_next < rejects_written) {
		const sp_event_t *e = &rejects[s->reject_next++ % REJECT_LOG_LEN];
		if (e->seq >= s->cursor.from_seq) {
			*out = *e;
			return true;
		}
	}
	s->replaying = false;
	return false;
}

static bool next_event(tx_state_t *s, sp_event_t *out)
{
	if (s->live_len > 0) {
		*out = live[s->live_head];
		s->live_head = (s->live_head + 1u) % LIVE_QUEUE_LEN;
		s->live_len--;
		return true;
	}
	return s->replaying && next_replay(s, out);
}

static size_t fill_batch(tx_state_t *s, uint16_t room)
{
	size_t max = MIN(room / sizeof(sp_event_t), BATCH_MAX);
	size_t n = 0;
	sp_event_t e;

	while (n < max && next_event(s, &e)) {
		memcpy(&payload[n * sizeof(e)], &e, sizeof(e));
		n++;
	}
	return n * sizeof(e);
}

static void drop_all(void)
{
	state.live_len = 0;
	state.replaying = false;
}

void event_tx_pump(void)
{
	while (event_tx_busy()) {
		uint16_t room = ble_event_room();
		if (room < sizeof(sp_event_t)) {
			drop_all();
			return;
		}
		tx_state_t trial = state;
		size_t len = fill_batch(&trial, room);
		if (len == 0) {
			state = trial;
			return;
		}
		int err = ble_send_events(payload, (uint16_t)len);
		if (err == -EAGAIN || err == -ENOMEM) {
			return;
		}
		state = trial;
		if (err) {
			drop_all();
			return;
		}
	}
}
