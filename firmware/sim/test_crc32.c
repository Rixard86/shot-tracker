#include <stdio.h>

#include "../src/crc32.h"

#define CRC_INIT 0xFFFFFFFFu
#define CRC_POLY 0xEDB88320u
#define BITS_PER_BYTE 8u
#define CHECK_VALUE 0xCBF43926u
#define BYTE_VALUES 256u
#define BUFFER_BYTES 1024u
#define ROUNDS 500u
#define PIECE_MAX 97u
#define LCG_MUL 1103515245u
#define LCG_ADD 12345u
#define LCG_SHIFT 8u

static const uint8_t check_text[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
static uint8_t buffer[BUFFER_BYTES];
static uint32_t rng = 1;
static int failures;

static void check(int ok, const char *what)
{
	if (!ok) {
		printf("FAIL: %s\n", what);
		failures++;
	}
}

static uint32_t next_random(void)
{
	rng = rng * LCG_MUL + LCG_ADD;
	return rng >> LCG_SHIFT;
}

static uint32_t bit_by_bit(uint32_t crc, const crc32_bytes_t *bytes)
{
	for (uint32_t i = 0; i < bytes->len; i++) {
		crc ^= bytes->data[i];
		for (uint32_t b = 0; b < BITS_PER_BYTE; b++) {
			crc = (crc >> 1) ^ (CRC_POLY & (0u - (crc & 1u)));
		}
	}
	return crc;
}

static uint32_t in_pieces(const crc32_bytes_t *whole)
{
	uint32_t crc = CRC_INIT;
	uint32_t done = 0;

	while (done < whole->len) {
		uint32_t left = whole->len - done;
		uint32_t want = 1u + next_random() % PIECE_MAX;
		crc32_bytes_t piece = {whole->data + done, want < left ? want : left};

		crc = crc32_step(crc, &piece);
		done += piece.len;
	}
	return crc;
}

static void test_check_value(void)
{
	crc32_bytes_t text = {check_text, sizeof(check_text)};

	check(~crc32_step(CRC_INIT, &text) == CHECK_VALUE, "CRC-32 of 123456789 is CBF43926");
}

static void test_every_byte(void)
{
	int ok = 1;

	for (uint32_t value = 0; value < BYTE_VALUES; value++) {
		uint8_t byte = (uint8_t)value;
		crc32_bytes_t one = {&byte, sizeof(byte)};
		uint32_t state = next_random() * LCG_MUL;

		ok &= crc32_step(state, &one) == bit_by_bit(state, &one);
	}
	check(ok, "every byte value steps like the bit-by-bit CRC");
}

static void test_random_buffers(void)
{
	int whole_ok = 1;
	int pieces_ok = 1;

	for (uint32_t round = 0; round < ROUNDS; round++) {
		crc32_bytes_t whole = {buffer, next_random() % BUFFER_BYTES};

		for (uint32_t i = 0; i < whole.len; i++) {
			buffer[i] = (uint8_t)next_random();
		}
		uint32_t expected = bit_by_bit(CRC_INIT, &whole);
		whole_ok &= crc32_step(CRC_INIT, &whole) == expected;
		pieces_ok &= in_pieces(&whole) == expected;
	}
	check(whole_ok, "random buffers match the bit-by-bit CRC");
	check(pieces_ok, "feeding a buffer in pieces gives the same CRC");
}

int main(void)
{
	test_check_value();
	test_every_byte();
	test_random_buffers();
	printf("CRC32 TESTS: %s\n", failures ? "FAIL" : "PASS");
	return failures ? 1 : 0;
}
