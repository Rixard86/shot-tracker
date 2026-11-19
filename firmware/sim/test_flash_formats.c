#include <stdio.h>
#include <string.h>

#include "../src/capstore_priv.h"
#include "../src/evlog.h"

#define SECTOR_BYTES 4096u
#define LOG_BYTES (2u * SECTOR_BYTES)
#define STORE_BYTES (4u * SECTOR_BYTES)
#define ERASED 0xFFu
#define EVENT_SEQ 0x01020304u
#define EVENT_UNIX_MS 0x0102030405060708
#define EVENT_UPTIME_MS 0x11121314u
#define EVENT_PEAK_MG 0x2122u
#define EVENT_STILL_MS 0x3132u
#define EVENT_POST_MG 0x4142u
#define EVENT_REASON 2u
#define EVENT_CHECK_0_3_8 0x36FB2B38u
#define CAPTURE_SEQ 0x0A0B0C0Du
#define CAPTURE_SAMPLES 720u
#define CAPTURE_TRIGGER 480u
#define PAYLOAD_BYTES 203u
#define PAYLOAD_STRIDE 7u
#define CAPTURE_CHECK_0_3_8 0x50085E05u
#define RECORD_MAGIC_0_3_8 0x53504352u
#define AVERAGED_MAGIC_0_3_11 0x53504341u
#define ULTRA_MAGIC_0_3_12 0x53504355u

static uint8_t log_flash[LOG_BYTES];
static uint8_t store_flash[STORE_BYTES];
static capstore_t store;
static int failures;

static void check(int ok, const char *what)
{
	if (!ok) {
		printf("FAIL: %s\n", what);
		failures++;
	}
}

static int log_read(uint32_t offset, evlog_record_t *rec)
{
	memcpy(rec, &log_flash[offset], sizeof(*rec));
	return 0;
}

static int log_write(uint32_t offset, const evlog_record_t *rec)
{
	memcpy(&log_flash[offset], rec, sizeof(*rec));
	return 0;
}

static int log_erase(uint32_t offset)
{
	memset(&log_flash[offset], ERASED, SECTOR_BYTES);
	return 0;
}

static int log_write_word(uint32_t offset, uint32_t word)
{
	memcpy(&log_flash[offset], &word, sizeof(word));
	return 0;
}

static const evlog_flash_t log_dev = {log_read, log_write, log_erase, log_write_word, LOG_BYTES, SECTOR_BYTES};

static int store_read(const capstore_span_t *span)
{
	memcpy(span->data, &store_flash[span->offset], span->len);
	return 0;
}

static int store_write(const capstore_wspan_t *span)
{
	memcpy(&store_flash[span->offset], span->data, span->len);
	return 0;
}

static int store_erase(uint32_t offset)
{
	memset(&store_flash[offset], ERASED, SECTOR_BYTES);
	return 0;
}

static const capstore_flash_t store_dev = {store_read, store_write, store_erase, STORE_BYTES, SECTOR_BYTES};

static void test_event_record(void)
{
	evlog_t log;
	evlog_record_t rec;
	sp_event_t event = {
		.seq = EVENT_SEQ,
		.t_unix_ms = EVENT_UNIX_MS,
		.t_uptime_ms = EVENT_UPTIME_MS,
		.peak_hf_mg = EVENT_PEAK_MG,
		.pre_still_ms = EVENT_STILL_MS,
		.post_lf_mg = EVENT_POST_MG,
		.accepted = 1,
		.reason = EVENT_REASON,
	};

	memset(log_flash, ERASED, sizeof(log_flash));
	check(evlog_init(&log, &log_dev) == 0, "log opens");
	check(evlog_append(&log, &event) == 0, "event logged");
	memcpy(&rec, log_flash, sizeof(rec));
	check(memcmp(&rec.event, &event, sizeof(event)) == 0 && rec.magic == EVLOG_MAGIC,
	      "a log record is the event followed by the magic");
	check(rec.check == EVENT_CHECK_0_3_8, "log record check word is the one firmware 0.3.8 writes");
}

static void test_capture_record(void)
{
	capstore_entry_t entry = {.seq = CAPTURE_SEQ, .n_samples = CAPTURE_SAMPLES, .trigger_index = CAPTURE_TRIGGER};
	uint8_t payload[PAYLOAD_BYTES];
	capstore_chunk_t chunk = {payload, sizeof(payload)};
	capstore_header_t header;

	for (uint32_t i = 0; i < sizeof(payload); i++) {
		payload[i] = (uint8_t)(i * PAYLOAD_STRIDE);
	}
	memset(store_flash, ERASED, sizeof(store_flash));
	check(capstore_init(&store, &store_dev) == 0 && capstore_begin(&store, &entry) == 0, "capture opened");
	check(capstore_write(&store, &chunk) == 0 && capstore_commit(&store) == 0, "capture stored");
	memcpy(&header, &store_flash[CAPSTORE_SECTOR_HEADER_BYTES], sizeof(header));
	check(header.magic == CAPSTORE_RECORD_MAGIC && header.seq == CAPTURE_SEQ &&
	      header.n_samples == CAPTURE_SAMPLES && header.trigger_index == CAPTURE_TRIGGER &&
	      header.payload_len == PAYLOAD_BYTES, "capture header follows the sector header");
	check(memcmp(&store_flash[CAPSTORE_SECTOR_HEADER_BYTES + CAPSTORE_HEADER_BYTES], payload, sizeof(payload)) == 0,
	      "capture payload follows its header");
	check(header.check == CAPTURE_CHECK_0_3_8, "capture check word is the one firmware 0.3.8 writes");
}

static void check_format_record(uint8_t format, uint32_t magic)
{
	capstore_entry_t entry = {.seq = CAPTURE_SEQ, .n_samples = CAPTURE_SAMPLES, .trigger_index = CAPTURE_TRIGGER,
				  .format = format};
	uint8_t payload[PAYLOAD_BYTES];
	capstore_chunk_t chunk = {payload, sizeof(payload)};
	capstore_header_t header;

	for (uint32_t i = 0; i < sizeof(payload); i++) {
		payload[i] = (uint8_t)(i * PAYLOAD_STRIDE);
	}
	memset(store_flash, ERASED, sizeof(store_flash));
	check(capstore_init(&store, &store_dev) == 0 && capstore_begin(&store, &entry) == 0 &&
	      capstore_write(&store, &chunk) == 0 && capstore_commit(&store) == 0, "averaged capture stored");
	memcpy(&header, &store_flash[CAPSTORE_SECTOR_HEADER_BYTES], sizeof(header));
	check(header.magic == magic && header.check == CAPTURE_CHECK_0_3_8,
	      "an averaged capture differs only in its record magic");
	capstore_check_t stored_check = {.seq = CAPTURE_SEQ};
	check(capstore_init(&store, &store_dev) == 0 && capstore_find(&store, CAPTURE_SEQ) &&
	      capstore_find(&store, CAPTURE_SEQ)->format == format &&
	      capstore_read_check(&store, &stored_check) == 0 && stored_check.check == CAPTURE_CHECK_0_3_8,
	      "the boot scan reads the format back from the magic, and the check word is readable");
}

static void test_averaged_records(void)
{
	check(CAPSTORE_RECORD_MAGIC == RECORD_MAGIC_0_3_8, "full-rate captures keep the record magic 0.3.8 writes");
	check_format_record(CAPSTORE_FORMAT_AIM_AVERAGED, AVERAGED_MAGIC_0_3_11);
	check_format_record(CAPSTORE_FORMAT_ULTRA, ULTRA_MAGIC_0_3_12);
}

int main(void)
{
	test_event_record();
	test_capture_record();
	test_averaged_records();
	printf("FLASH FORMAT TESTS: %s\n", failures ? "FAIL" : "PASS");
	return failures ? 1 : 0;
}
