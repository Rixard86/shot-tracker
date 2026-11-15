#include "capflash.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/atomic.h>

#include "capjoin.h"
#include "flash_sector.h"

LOG_MODULE_REGISTER(capflash, LOG_LEVEL_INF);

#define SPARE_TRAILER_SECTORS 1u
#define BYTES_PER_KB 1024u

typedef struct {
	capjoin_piece_t piece;
	uint8_t *data;
	bool write;
} piece_io_t;

static const struct flash_area *area;
static const struct flash_area *spare;
static bool spare_in_use;
static atomic_t spare_claimed;
K_MUTEX_DEFINE(spare_lock);

static int piece_io(const piece_io_t *io)
{
	const struct flash_area *fa = io->piece.spare ? spare : area;
	int err;

	if (io->piece.spare) {
		k_mutex_lock(&spare_lock, K_FOREVER);
		if (atomic_get(&spare_claimed)) {
			k_mutex_unlock(&spare_lock);
			return -EBUSY;
		}
	}
	err = io->write ? flash_area_write(fa, io->piece.local, io->data, io->piece.len)
			: flash_area_read(fa, io->piece.local, io->data, io->piece.len);
	if (io->piece.spare) {
		k_mutex_unlock(&spare_lock);
	}
	return err;
}

static int joined_io(capstore_span_t span, bool write)
{
	while (span.len > 0u) {
		capjoin_req_t req = {(uint32_t)area->fa_size, span.offset, span.len};
		piece_io_t io = {capjoin_piece(&req), span.data, write};
		int err = piece_io(&io);

		if (err) {
			return err;
		}
		span.offset += io.piece.len;
		span.data += io.piece.len;
		span.len -= io.piece.len;
	}
	return 0;
}

static int joined_read(const capstore_span_t *span)
{
	return joined_io(*span, false);
}

static int joined_write(const capstore_wspan_t *span)
{
	capstore_span_t io = {span->offset, (uint8_t *)span->data, span->len};

	return joined_io(io, true);
}

static int joined_erase(uint32_t offset)
{
	capjoin_req_t req = {(uint32_t)area->fa_size, offset, FLASH_SECTOR_BYTES};
	capjoin_piece_t piece = capjoin_piece(&req);
	int err;

	if (!piece.spare) {
		return flash_area_erase(area, piece.local, FLASH_SECTOR_BYTES);
	}
	k_mutex_lock(&spare_lock, K_FOREVER);
	err = atomic_get(&spare_claimed) ? -EBUSY : flash_area_erase(spare, piece.local, FLASH_SECTOR_BYTES);
	k_mutex_unlock(&spare_lock);
	return err;
}

static capstore_flash_t joined_flash = {
	.read = joined_read,
	.write = joined_write,
	.erase_sector = joined_erase,
	.sector_size = FLASH_SECTOR_BYTES,
};

static int open_areas(void)
{
	int err = area ? 0 : flash_area_open(FIXED_PARTITION_ID(capture_partition), &area);

	return err || spare ? err : flash_area_open(FIXED_PARTITION_ID(slot1_partition), &spare);
}

int capflash_open(capstore_t *store, bool with_spare)
{
	int err = open_areas();

	if (err) {
		return err;
	}
	spare_in_use = with_spare && !atomic_get(&spare_claimed);
	joined_flash.size = (uint32_t)area->fa_size;
	if (spare_in_use) {
		joined_flash.size += (uint32_t)spare->fa_size - SPARE_TRAILER_SECTORS * FLASH_SECTOR_BYTES;
	}
	LOG_INF("captures: %u KB%s", joined_flash.size / BYTES_PER_KB, spare_in_use ? " incl. slot 1" : "");
	return capstore_init(store, &joined_flash);
}

bool capflash_spare_in_use(void)
{
	return spare_in_use;
}

void capflash_spare_claim(void)
{
	if (atomic_get(&spare_claimed)) {
		return;
	}
	k_mutex_lock(&spare_lock, K_FOREVER);
	atomic_set(&spare_claimed, 1);
	k_mutex_unlock(&spare_lock);
	LOG_INF("slot 1 handed to the firmware upload");
}

bool capflash_spare_claimed(void)
{
	return atomic_get(&spare_claimed) != 0;
}
