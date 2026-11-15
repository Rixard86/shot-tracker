#include "lifetime.h"

#include <stdbool.h>
#include <zephyr/logging/log.h>
#include <zephyr/storage/flash_map.h>

#include "flash_sector.h"
#include "lifecount.h"

LOG_MODULE_REGISTER(lifetime, LOG_LEVEL_INF);


static const struct flash_area *area;
static lifecount_t life;
static bool ready;

static int area_read(uint32_t offset, lifecount_record_t *rec)
{
	return flash_area_read(area, offset, rec, sizeof(*rec));
}

static int area_write(uint32_t offset, const lifecount_record_t *rec)
{
	return flash_area_write(area, offset, rec, sizeof(*rec));
}

static int area_erase(uint32_t offset)
{
	return flash_area_erase(area, offset, FLASH_SECTOR_BYTES);
}

static const lifecount_flash_t life_flash = {
	.read = area_read,
	.write = area_write,
	.erase_sector = area_erase,
	.sector_size = FLASH_SECTOR_BYTES,
};

int lifetime_init(uint32_t start_seq)
{
	int err = flash_area_open(FIXED_PARTITION_ID(lifetime_partition), &area);

	if (!err && area->fa_size != LIFECOUNT_SECTORS * FLASH_SECTOR_BYTES) {
		err = LIFECOUNT_ERR_LAYOUT;
	}
	err = err ? err : lifecount_init(&life, &life_flash);
	err = err ? err : lifecount_start(&life, start_seq);
	ready = err == 0;
	if (!ready) {
		LOG_ERR("lifetime counter unavailable (%d)", err);
	}
	return err;
}

void lifetime_shot(void)
{
	if (ready && lifecount_shot(&life) != 0) {
		LOG_WRN("lifetime counter write failed");
	}
}

void lifetime_unshot(uint32_t seq)
{
	if (ready && lifecount_unshot(&life, seq) < 0) {
		LOG_WRN("lifetime counter write failed");
	}
}

uint32_t lifetime_count(void)
{
	return lifecount_value(&life);
}
