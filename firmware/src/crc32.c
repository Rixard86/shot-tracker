#include "crc32.h"

#define NIBBLE_BITS 4u
#define NIBBLE_MASK 0xFu

static const uint32_t nibble_table[] = {
	0x00000000u, 0x1DB71064u, 0x3B6E20C8u, 0x26D930ACu, 0x76DC4190u, 0x6B6B51F4u, 0x4DB26158u, 0x5005713Cu,
	0xEDB88320u, 0xF00F9344u, 0xD6D6A3E8u, 0xCB61B38Cu, 0x9B64C2B0u, 0x86D3D2D4u, 0xA00AE278u, 0xBDBDF21Cu,
};

static uint32_t fold_nibble(uint32_t crc)
{
	return (crc >> NIBBLE_BITS) ^ nibble_table[crc & NIBBLE_MASK];
}

uint32_t crc32_step(uint32_t crc, const crc32_bytes_t *bytes)
{
	const uint8_t *end = bytes->data + bytes->len;

	for (const uint8_t *byte = bytes->data; byte < end; byte++) {
		crc = fold_nibble(fold_nibble(crc ^ *byte));
	}
	return crc;
}
