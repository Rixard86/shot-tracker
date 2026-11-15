#ifndef CRC32_H
#define CRC32_H

#include <stdint.h>

typedef struct {
	const uint8_t *data;
	uint32_t len;
} crc32_bytes_t;

uint32_t crc32_step(uint32_t crc, const crc32_bytes_t *bytes);

#endif
