#include "event_tx.h"

#include <zephyr/logging/log.h>
#include <zephyr/storage/flash_map.h>

#include "ble.h"
#include "evlog.h"
#include "evq.h"

LOG_MODULE_REGISTER(event_tx, LOG_LEVEL_INF);

#define SECTOR_BYTES 4096u

static const struct flash_area *area;
static evlog_t shots;
static evq_t queue;

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
	evq_deps_t deps = {.link = {.room = ble_event_room, .send = ble_send_events}};
	int err = flash_area_open(FIXED_PARTITION_ID(eventlog_partition), &area);

	if (!err) {
		shots_flash.size = (uint32_t)area->fa_size;
		err = evlog_init(&shots, &shots_flash);
	}
	deps.shots = err ? NULL : &shots;
	evq_init(&queue, &deps);
	return err;
}

void event_tx_record(const sp_event_t *event)
{
	if (evq_record(&queue, event)) {
		LOG_WRN("shot log write failed");
	}
}

void event_tx_replay(uint32_t from_seq)
{
	evq_replay(&queue, from_seq);
}

uint32_t event_tx_newest_seq(void)
{
	return shots.newest_seq;
}

bool event_tx_busy(void)
{
	return evq_busy(&queue);
}

void event_tx_pump(void)
{
	evq_pump(&queue);
}
