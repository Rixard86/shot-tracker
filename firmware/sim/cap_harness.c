#include "cap_harness.h"

#include <string.h>

#define ERASED 0xFFu
#define BATCH 26u
#define DETECT_DELAY 288u
#define HASH_MUL 2654435761u
#define HASH_SHIFT 16u
#define NOISE_SPAN 101u
#define NOISE_CENTER 50
#define GRAVITY_AXIS 2u
#define GRAVITY_LSB 1024
#define SWAY_STEP 7u
#define SWAY_SPAN 40u
#define WORD_SHIFT 16u
#define SPIKE_EVERY 97u
#define SPIKE_LSB 20000
#define RAW_AXIS 0u
#define SPIKE_AXIS 1u
#define FLAT_AXIS 2u
#define FLAT_LSB (-321)
#define SMOOTH_AXIS 3u
#define SMOOTH_PERIOD 960
#define SMOOTH_DIVISOR 30
#define SMOOTH_OFFSET 3840

static uint8_t flash[HARNESS_FLASH_BYTES];
static sp_cap_sample_t history[CAP_HISTORY_SAMPLES];
static cap_t cap;
static capstore_t store;
static capw_t writer;
static uint32_t seq;
static harness_signal_t kind;

static int fake_read(const capstore_span_t *span)
{
	memcpy(span->data, &flash[span->offset], span->len);
	return 0;
}

static int fake_write(const capstore_wspan_t *span)
{
	for (uint32_t i = 0; i < span->len; i++) {
		flash[span->offset + i] &= span->data[i];
	}
	return 0;
}

static int fake_erase(uint32_t offset)
{
	memset(&flash[offset], ERASED, HARNESS_SECTOR_BYTES);
	return 0;
}

static const capstore_flash_t dev = {fake_read, fake_write, fake_erase, HARNESS_FLASH_BYTES, HARNESS_SECTOR_BYTES};

static int32_t bow_value(uint32_t index, uint32_t axis)
{
	uint32_t h = (index * CODEC_AXES + axis) * HASH_MUL >> HASH_SHIFT;
	int32_t value = (int32_t)(h % NOISE_SPAN) - NOISE_CENTER + (int32_t)(index / SWAY_STEP % SWAY_SPAN);

	return value + (axis == GRAVITY_AXIS ? GRAVITY_LSB : 0);
}

static int32_t stress_value(uint32_t index, uint32_t axis)
{
	if (axis == RAW_AXIS) {
		return (int16_t)((index * HASH_MUL) >> WORD_SHIFT);
	}
	if (axis == SMOOTH_AXIS) {
		int32_t x = (int32_t)(index % SMOOTH_PERIOD) - SMOOTH_PERIOD / 2;
		return x * x / SMOOTH_DIVISOR - SMOOTH_OFFSET;
	}
	if (axis == SPIKE_AXIS) {
		return index % SPIKE_EVERY == 0u ? SPIKE_LSB : bow_value(index, axis);
	}
	return axis == FLAT_AXIS ? FLAT_LSB : bow_value(index, axis);
}

sp_cap_sample_t harness_signal_at(uint32_t index)
{
	sp_cap_sample_t s;

	for (uint32_t axis = 0; axis < CODEC_AXES; axis++) {
		int32_t v = kind == HARNESS_STRESS ? stress_value(index, axis) : bow_value(index, axis);
		codec_set_axis(&s, (codec_axis_set_t){axis, (int16_t)v});
	}
	return s;
}

void harness_run_to(uint32_t count)
{
	while (cap.count < count) {
		for (uint32_t i = 0; i < BATCH; i++) {
			sp_cap_sample_t s = harness_signal_at(cap.count);
			cap_push(&cap, &s);
		}
		while (capw_busy(&writer) && capw_pump(&writer) == CAPW_COMMITTED) {
		}
	}
}

void harness_push(uint32_t samples)
{
	for (uint32_t i = 0; i < samples; i++) {
		sp_cap_sample_t s = harness_signal_at(cap.count);
		cap_push(&cap, &s);
	}
}

void harness_request(uint32_t trigger, uint8_t mode)
{
	capw_request_t req = {++seq, trigger, cap.epoch, mode, 0u, HARNESS_OFFSET};

	capw_request(&writer, &req);
}

int harness_pump(void)
{
	return capw_pump(&writer);
}

const capstore_entry_t *harness_shoot(uint32_t trigger, uint8_t mode)
{
	harness_run_to(trigger + DETECT_DELAY);
	capw_request_t req = {++seq, trigger, cap.epoch, mode, 0u, HARNESS_OFFSET};
	capw_request(&writer, &req);
	harness_run_to(trigger + HARNESS_SHOT_SPACING);
	return capstore_find(&store, seq);
}

const capstore_entry_t *harness_record(uint32_t samples)
{
	capw_request_t req = {++seq, cap.count - 1u, cap.epoch, SP_CAPTURE_STANDARD, samples, HARNESS_OFFSET};

	capw_request(&writer, &req);
	harness_run_to(cap.count + samples + HARNESS_SHOT_SPACING);
	return capstore_find(&store, seq);
}

void harness_setup(harness_signal_t signal)
{
	capw_deps_t deps = {&cap, &store};

	kind = signal;
	memset(flash, ERASED, sizeof(flash));
	cap_init(&cap, history);
	seq = 0;
	capstore_init(&store, &dev);
	capw_init(&writer, &deps);
}

void harness_wake(void)
{
	cap_reset(&cap);
}

int harness_reboot(void)
{
	return capstore_init(&store, &dev);
}

uint32_t harness_next_seq(void)
{
	return ++seq;
}

capstore_t *harness_store(void)
{
	return &store;
}
