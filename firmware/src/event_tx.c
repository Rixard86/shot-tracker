#include "event_tx.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/storage/flash_map.h>

#include "ble.h"
#include "evlog.h"
#include "evq.h"
#include "flash_sector.h"

LOG_MODULE_REGISTER(event_tx, LOG_LEVEL_INF);


static const struct flash_area *area;
static const wire_clock_t *wall_clock;
static evlog_t shots;
static bool shots_ready;
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
	return flash_area_erase(area, offset, FLASH_SECTOR_BYTES);
}

static int area_write_word(uint32_t offset, uint32_t word)
{
	return flash_area_write(area, offset, &word, sizeof(word));
}

static evlog_flash_t shots_flash = {
	.read = area_read,
	.write = area_write,
	.erase_sector = area_erase,
	.write_word = area_write_word,
	.sector_size = FLASH_SECTOR_BYTES,
};

static int64_t event_unix_ms(uint32_t uptime_ms)
{
	return wire_clock_unix_ms(wall_clock, wire_uptime_unwrap(k_uptime_get(), uptime_ms));
}

int event_tx_init(const wire_clock_t *clock)
{
	evq_deps_t deps = {.link = {.room = ble_event_room, .send = ble_send_events}, .unix_ms = event_unix_ms};
	int err = flash_area_open(FIXED_PARTITION_ID(eventlog_partition), &area);

	wall_clock = clock;

	if (!err) {
		shots_flash.size = (uint32_t)area->fa_size;
		err = evlog_init(&shots, &shots_flash);
	}
	shots_ready = err == 0;
	deps.shots = shots_ready ? &shots : NULL;
	evq_init(&queue, &deps);
	return err;
}

int event_tx_delete(uint32_t seq)
{
	return shots_ready ? evlog_delete(&shots, seq) : 0;
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
