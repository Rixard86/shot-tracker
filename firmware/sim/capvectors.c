#include <stdio.h>

#include "../src/crc32.h"
#include "cap_harness.h"

#define MAX_PAYLOAD 65536u
#define CRC_INIT 0xFFFFFFFFu
#define RECORD_SAMPLES 1440u
#define RECORD_AFTER 2000u
#define SHOT_TRIGGER 9600u
#define WAKE_TRIGGER 1003u
#define ULTRA_TRIGGER 20003u
#define ULTRA_WAKE_TRIGGER 9003u
#define ULTRA_BRIEF_TRIGGER 2001u

typedef struct {
	const char *name;
	harness_signal_t signal;
	uint32_t trigger;
	uint8_t mode;
	uint32_t record;
} recipe_t;

static const recipe_t recipes[] = {
	{"standard", HARNESS_BOW, SHOT_TRIGGER, SP_CAPTURE_STANDARD, 0u},
	{"recording", HARNESS_BOW, RECORD_AFTER, SP_CAPTURE_STANDARD, RECORD_SAMPLES},
	{"extended", HARNESS_BOW, SHOT_TRIGGER, SP_CAPTURE_EXTENDED, 0u},
	{"extended-after-wake", HARNESS_BOW, WAKE_TRIGGER, SP_CAPTURE_EXTENDED, 0u},
	{"ultra", HARNESS_BOW, ULTRA_TRIGGER, SP_CAPTURE_ULTRA, 0u},
	{"ultra-after-wake", HARNESS_BOW, ULTRA_WAKE_TRIGGER, SP_CAPTURE_ULTRA, 0u},
	{"ultra-brief", HARNESS_BOW, ULTRA_BRIEF_TRIGGER, SP_CAPTURE_ULTRA, 0u},
	{"stress-standard", HARNESS_STRESS, SHOT_TRIGGER, SP_CAPTURE_STANDARD, 0u},
	{"stress-ultra", HARNESS_STRESS, ULTRA_TRIGGER, SP_CAPTURE_ULTRA, 0u},
};

static uint8_t payload[MAX_PAYLOAD];
static const capstore_entry_t *current;

static int refill(capdec_t *dec)
{
	uint32_t body = capstore_body_start(current);
	uint32_t left = current->payload_len - body - dec->source_offset;
	capstore_read_t req = {current->seq, body + dec->source_offset, dec->in, left < CODEC_IN_BYTES ? left : CODEC_IN_BYTES};

	dec->in_len = req.len;
	dec->source_offset += req.len;
	return req.len ? capstore_read(harness_store(), &req) : 0;
}

static capaim_shape_t shape_of(const capstore_entry_t *e)
{
	return (capaim_shape_t){e->n_samples, e->trigger_index, e->format};
}

static void print_stored(const capstore_entry_t *e)
{
	capdec_t dec = {.refill = refill};
	capaim_shape_t shape = shape_of(e);
	uint32_t count = capaim_stored(&shape);
	sp_cap_sample_t s;

	capdec_init(&dec, count);
	printf("      \"stored\": [");
	for (uint32_t i = 0; i < count && capdec_next(&dec, &s) == 1; i++) {
		printf("%s[%d,%d,%d,%d,%d,%d]", i ? "," : "", s.acc[0], s.acc[1], s.acc[2], s.gyro[0], s.gyro[1], s.gyro[2]);
	}
	printf("],\n");
}

static uint32_t rebuilt_crc(const capstore_entry_t *e)
{
	capdec_t dec = {.refill = refill};
	capaim_reader_t reader = {.dec = &dec};
	capaim_shape_t shape = shape_of(e);
	uint32_t crc = CRC_INIT;
	sp_cap_sample_t s;

	capaim_open(&reader, &shape);
	for (uint32_t k = 0; k < e->n_samples && capaim_next(&reader, &s) == 1; k++) {
		crc32_bytes_t bytes = {(const uint8_t *)&s, sizeof(s)};
		crc = crc32_step(crc, &bytes);
	}
	return ~crc;
}

static void print_vector(const recipe_t *r, const capstore_entry_t *e)
{
	uint32_t body = capstore_body_start(e);
	uint32_t sent_len = e->payload_len - body;
	capstore_read_t req = {e->seq, body, payload, sent_len};
	capstore_check_t check = {.seq = e->seq};

	current = e;
	capstore_read(harness_store(), &req);
	capstore_body_check(harness_store(), &check);
	printf("    {\n      \"name\": \"%s\",\n      \"seq\": %u,\n      \"n_samples\": %u,\n", r->name, e->seq, e->n_samples);
	printf("      \"trigger_index\": %u,\n      \"format\": %u,\n", e->trigger_index, e->format);
	printf("      \"payload_len\": %u,\n      \"check\": %u,\n      \"payload_hex\": \"", sent_len, check.check);
	for (uint32_t i = 0; i < sent_len; i++) {
		printf("%02x", payload[i]);
	}
	printf("\",\n");
	print_stored(e);
	printf("      \"rebuilt_crc32\": %u\n    }", rebuilt_crc(e));
}

static const capstore_entry_t *make(const recipe_t *r)
{
	harness_setup(r->signal);
	if (r->record > 0u) {
		harness_run_to(r->trigger);
		return harness_record(r->record);
	}
	return harness_shoot(r->trigger, r->mode);
}

int main(void)
{
	uint32_t n = sizeof(recipes) / sizeof(recipes[0]);

	printf("{\n  \"vectors\": [\n");
	for (uint32_t i = 0; i < n; i++) {
		const capstore_entry_t *e = make(&recipes[i]);
		if (!e) {
			fprintf(stderr, "vector %s not stored\n", recipes[i].name);
			return 1;
		}
		print_vector(&recipes[i], e);
		printf("%s\n", i + 1u < n ? "," : "");
	}
	printf("  ]\n}\n");
	return 0;
}
