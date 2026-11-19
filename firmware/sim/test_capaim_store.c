#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../src/capaim.h"
#include "../src/capwriter.h"

#define SECTOR_BYTES 4096u
#define FLASH_BYTES 0x66000u
#define ERASED 0xFFu
#define BATCH 26u
#define DETECT_DELAY 288u
#define SHOT_SPACING 9600u
#define GROUP CAPAIM_GROUP_SAMPLES
#define FULL_RATE CAPAIM_FULL_RATE_PRE
#define EXTENDED_SAMPLES (CAP_EXTENDED_PRE + CAP_EXTENDED_POST)
#define ODD_WAKE_TRIGGER 1003u
#define SHORT_WAKE_TRIGGER 300u
#define HASH_MUL 2654435761u
#define HASH_SHIFT 16u
#define NOISE_SPAN 101u
#define NOISE_CENTER 50
#define GRAVITY_AXIS 2u
#define GRAVITY_LSB 1024
#define SWAY_STEP 7u
#define SWAY_SPAN 40u

typedef struct {
	uint32_t seq;
	uint32_t first;
	uint32_t aim;
} want_t;

static uint8_t flash[FLASH_BYTES];
static cap_t cap;
static capstore_t store;
static capw_t writer;
static uint32_t seq;
static int failures;

static void check(int ok, const char *what)
{
	if (!ok) {
		printf("FAIL: %s\n", what);
		failures++;
	}
}

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
	memset(&flash[offset], ERASED, SECTOR_BYTES);
	return 0;
}

static const capstore_flash_t dev = {fake_read, fake_write, fake_erase, FLASH_BYTES, SECTOR_BYTES};

static sp_cap_sample_t signal_at(uint32_t index)
{
	sp_cap_sample_t s;

	for (uint32_t axis = 0; axis < CODEC_AXES; axis++) {
		uint32_t h = (index * CODEC_AXES + axis) * HASH_MUL >> HASH_SHIFT;
		int32_t value = (int32_t)(h % NOISE_SPAN) - NOISE_CENTER + (int32_t)(index / SWAY_STEP % SWAY_SPAN);
		codec_set_axis(&s, (codec_axis_set_t){axis, (int16_t)(value + (axis == GRAVITY_AXIS ? GRAVITY_LSB : 0))});
	}
	return s;
}

static sp_cap_sample_t expected(const want_t *want, uint32_t k)
{
	if (k >= want->aim) {
		return signal_at(want->first + k);
	}
	uint32_t end = want->aim - (want->aim - 1u - k) / GROUP * GROUP;
	uint32_t start = end > GROUP ? end - GROUP : 0u;
	sp_cap_sample_t mean;

	for (uint32_t axis = 0; axis < CODEC_AXES; axis++) {
		double sum = 0;
		for (uint32_t i = start; i < end; i++) {
			sp_cap_sample_t s = signal_at(want->first + i);
			sum += codec_axis_value(&s, axis);
		}
		codec_set_axis(&mean, (codec_axis_set_t){axis, (int16_t)nearbyint(sum / (end - start))});
	}
	return mean;
}

static int refill(capdec_t *dec)
{
	const want_t *want = dec->context;
	const capstore_entry_t *e = capstore_find(&store, want->seq);
	uint32_t left = e ? e->payload_len - dec->source_offset : 0u;
	capstore_read_t req = {want->seq, dec->source_offset, dec->in, left < CODEC_IN_BYTES ? left : CODEC_IN_BYTES};

	dec->in_len = req.len;
	dec->source_offset += req.len;
	return req.len ? capstore_read(&store, &req) : 0;
}

static int matches(const want_t *want)
{
	const capstore_entry_t *e = capstore_find(&store, want->seq);
	capdec_t dec = {.refill = refill, .context = (void *)want};
	capaim_reader_t reader = {.dec = &dec};
	sp_cap_sample_t s;

	if (!e) {
		return 0;
	}
	capaim_shape_t shape = {e->n_samples, e->trigger_index, e->format == CAPSTORE_FORMAT_AIM_AVERAGED};
	capaim_open(&reader, &shape);
	for (uint32_t k = 0; k < e->n_samples; k++) {
		sp_cap_sample_t w = expected(want, k);
		if (capaim_next(&reader, &s) != 1 || memcmp(&s, &w, sizeof(s)) != 0) {
			return 0;
		}
	}
	return capaim_next(&reader, &s) == 0;
}

static void run_to(uint32_t count)
{
	while (cap.count < count) {
		for (uint32_t i = 0; i < BATCH; i++) {
			sp_cap_sample_t s = signal_at(cap.count);
			cap_push(&cap, &s);
		}
		while (capw_busy(&writer) && capw_pump(&writer) == CAPW_COMMITTED) {
		}
	}
}

static const capstore_entry_t *shoot(uint32_t trigger, uint8_t mode)
{
	run_to(trigger + DETECT_DELAY);
	capw_request_t req = {++seq, trigger, cap.epoch, mode, 0u};
	capw_request(&writer, &req);
	run_to(trigger + SHOT_SPACING);
	return capstore_find(&store, seq);
}

static void setup(void)
{
	capw_deps_t deps = {&cap, &store};

	memset(flash, ERASED, sizeof(flash));
	cap_init(&cap);
	seq = 0;
	check(capstore_init(&store, &dev) == 0, "store init");
	capw_init(&writer, &deps);
}

static void test_extended(void)
{
	setup();
	const capstore_entry_t *e = shoot(SHOT_SPACING, SP_CAPTURE_EXTENDED);
	want_t want = {seq, SHOT_SPACING - CAP_EXTENDED_PRE, CAP_EXTENDED_PRE - FULL_RATE};
	check(e && e->format == CAPSTORE_FORMAT_AIM_AVERAGED && e->n_samples == EXTENDED_SAMPLES &&
	      e->trigger_index == CAP_EXTENDED_PRE, "extended shot keeps its header and is marked averaged");
	check(matches(&want), "aiming part: rounded 4-sample means; last second and after: bit-exact");
	const capstore_entry_t *s = shoot(2u * SHOT_SPACING, SP_CAPTURE_STANDARD);
	want_t standard = {seq, 2u * SHOT_SPACING - CAP_STANDARD_PRE, 0u};
	check(s && s->format == CAPSTORE_FORMAT_FULL && matches(&standard), "standard capture stays full rate");
}

static void test_after_wake(void)
{
	setup();
	const capstore_entry_t *odd = shoot(ODD_WAKE_TRIGGER, SP_CAPTURE_EXTENDED);
	want_t odd_want = {seq, 0u, ODD_WAKE_TRIGGER - FULL_RATE};
	check(odd && odd->trigger_index == ODD_WAKE_TRIGGER && matches(&odd_want),
	      "capture shortened by a wake: groups counted back from 1 s before the trigger, partial group first");
	cap_reset(&cap);
	const capstore_entry_t *short_one = shoot(SHORT_WAKE_TRIGGER, SP_CAPTURE_EXTENDED);
	want_t short_want = {seq, 0u, 0u};
	check(short_one && short_one->trigger_index == SHORT_WAKE_TRIGGER && matches(&short_want),
	      "trigger within 1 s of the wake: nothing averaged");
}

static void store_full_rate_extended(uint32_t first)
{
	capstore_entry_t h = {.seq = ++seq, .n_samples = EXTENDED_SAMPLES, .trigger_index = CAP_EXTENDED_PRE};
	static capenc_t enc;
	sp_cap_sample_t block[CODEC_BLOCK_SAMPLES];

	capenc_init(&enc);
	check(capstore_begin(&store, &h) == 0, "old-format record opened");
	for (uint32_t at = 0; at < EXTENDED_SAMPLES; at += CODEC_BLOCK_SAMPLES) {
		for (uint32_t i = 0; i < CODEC_BLOCK_SAMPLES; i++) {
			block[i] = signal_at(first + at + i);
		}
		codec_block_t b = {block, CODEC_BLOCK_SAMPLES};
		capenc_block(&enc, &b);
		capstore_chunk_t chunk = {enc.out, capenc_ready(&enc)};
		capstore_write(&store, &chunk);
		capenc_drop(&enc, chunk.len);
	}
	capenc_finish(&enc);
	capstore_chunk_t rest = {enc.out, capenc_ready(&enc)};
	check(capstore_write(&store, &rest) == 0 && capstore_commit(&store) == 0, "old-format record stored");
}

static void test_mixed_ring(void)
{
	setup();
	uint32_t old_first = 2u * SHOT_SPACING - CAP_EXTENDED_PRE;
	store_full_rate_extended(old_first);
	want_t old = {seq, old_first, 0u};
	shoot(3u * SHOT_SPACING, SP_CAPTURE_EXTENDED);
	want_t fresh = {seq, 3u * SHOT_SPACING - CAP_EXTENDED_PRE, CAP_EXTENDED_PRE - FULL_RATE};
	check(capstore_init(&store, &dev) == 0 && capstore_count(&store) == 2u, "both formats found after a reboot");
	const capstore_entry_t *e = capstore_find(&store, old.seq);
	check(e && e->format == CAPSTORE_FORMAT_FULL && matches(&old), "a capture stored by 0.3.10 still decodes exactly");
	check(matches(&fresh), "the averaged capture next to it decodes after the reboot");
}

int main(void)
{
	test_extended();
	test_after_wake();
	test_mixed_ring();
	printf("CAPAIM STORE TESTS: %s\n", failures ? "FAIL" : "PASS");
	return failures ? 1 : 0;
}
