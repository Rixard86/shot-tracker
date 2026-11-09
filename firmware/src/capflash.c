#include "capflash.h"

#include <zephyr/storage/flash_map.h>

#define SECTOR_BYTES 4096u

static const struct flash_area *area;

static int area_read(const capstore_span_t *span)
{
	return flash_area_read(area, span->offset, span->data, span->len);
}

static int area_write(const capstore_wspan_t *span)
{
	return flash_area_write(area, span->offset, span->data, span->len);
}

static int area_erase(uint32_t offset)
{
	return flash_area_erase(area, offset, SECTOR_BYTES);
}

static capstore_flash_t capture_flash = {
	.read = area_read,
	.write = area_write,
	.erase_sector = area_erase,
	.sector_size = SECTOR_BYTES,
};

int capflash_init(capstore_t *store)
{
	int err = flash_area_open(FIXED_PARTITION_ID(capture_partition), &area);

	if (err) {
		return err;
	}
	capture_flash.size = (uint32_t)area->fa_size;
	return capstore_init(store, &capture_flash);
}
