#include "evq_harness.h"

#include <errno.h>
#include <string.h>

#define FLASH_BYTES 16384u
#define SECTOR_BYTES 4096u
#define ERASED 0xFFu
#define MAX_PUMPS 100u

static uint8_t flash[FLASH_BYTES];
evlog_t shots;
evq_t queue;
uint16_t room_now;
int send_result;
uint32_t sent[MAX_SENT];
uint32_t sent_count;
uint32_t send_calls;
uint32_t batch_sizes[MAX_SENT];
uint32_t sends_left;

static int fake_read(uint32_t offset, evlog_record_t *rec)
{
	memcpy(rec, &flash[offset], sizeof(*rec));
	return 0;
}

static int fake_write(uint32_t offset, const evlog_record_t *rec)
{
	memcpy(&flash[offset], rec, sizeof(*rec));
	return 0;
}

static int fake_erase(uint32_t offset)
{
	memset(&flash[offset], ERASED, SECTOR_BYTES);
	return 0;
}

static const evlog_flash_t dev = {
	.read = fake_read,
	.write = fake_write,
	.erase_sector = fake_erase,
	.size = FLASH_BYTES,
	.sector_size = SECTOR_BYTES,
};

static uint16_t fake_room(void)
{
	return room_now;
}

static int fake_send(const uint8_t *data, uint16_t len)
{
	send_calls++;
	if (send_result) {
		return send_result;
	}
	if (sends_left == 0u) {
		return -EAGAIN;
	}
	sends_left--;
	batch_sizes[send_calls - 1u] = len / sizeof(sp_event_t);
	for (uint16_t off = 0; off + sizeof(sp_event_t) <= len; off += sizeof(sp_event_t)) {
		memcpy(&sent[sent_count++], &data[off], sizeof(uint32_t));
	}
	return 0;
}

void reset(bool with_log)
{
	evq_deps_t deps = {.shots = with_log ? &shots : NULL, .link = {fake_room, fake_send}};

	memset(flash, ERASED, sizeof(flash));
	evlog_init(&shots, &dev);
	evq_init(&queue, &deps);
	room_now = ROOM_MTU_247;
	send_result = 0;
	sends_left = SENDS_UNLIMITED;
	sent_count = 0;
	send_calls = 0;
}

sp_event_t event(uint32_t seq, bool accepted)
{
	return (sp_event_t){.seq = seq, .accepted = accepted ? 1u : 0u};
}

void pump_all(void)
{
	for (uint32_t i = 0; i < MAX_PUMPS && evq_busy(&queue); i++) {
		evq_pump(&queue);
	}
}

bool sent_in_order(uint32_t first, uint32_t count)
{
	if (sent_count != count) {
		return false;
	}
	for (uint32_t i = 0; i < count; i++) {
		if (sent[i] != first + i) {
			return false;
		}
	}
	return true;
}

bool rising_from(uint32_t index, uint32_t first_seq)
{
	for (uint32_t i = index; i < sent_count; i++) {
		if (sent[i] != first_seq + (i - index)) {
			return false;
		}
	}
	return true;
}

void record_rejects(uint32_t first, uint32_t last)
{
	for (uint32_t seq = first; seq <= last; seq++) {
		sp_event_t e = event(seq, false);
		evq_record(&queue, &e);
	}
}
