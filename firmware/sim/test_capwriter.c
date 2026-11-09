#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../src/capwriter.h"

#define SECTOR_BYTES 4096u
#define FLASH_BYTES 0x66000u
#define ERASED 0xFFu
#define BATCH 26u
#define DETECT_DELAY 288u
#define SHOT_SPACING 9600u
#define WAKE_TRIGGER 100u
#define MAX_SHOTS 400u
#define FILL_SHOTS 250u
#define HASH_MUL 2654435761u
#define HASH_SHIFT 16u
#define NOISE_SPAN 5u
#define NOISE_CENTER 2
#define GRAVITY_LSB 1024
#define SWAY_LSB 20.0
#define SWAY_PERIOD 960.0
#define SHOCK_LSB 25000.0
#define SHOCK_DECAY 24.0
#define SHOCK_PERIOD 3.2
#define JUMP_LSB 300
#define TURN_LSB 2800.0
#define TURN_DECAY 120.0
#define TWO_PI 6.283185307179586
#define INT16_LIMIT 32767.0
#define AXES_PER_SENSOR 3u

typedef struct {
	const capstore_t *store;
	uint32_t seq;
} source_t;

static uint8_t flash[FLASH_BYTES];
static uint32_t triggers[MAX_SHOTS];
static uint32_t shots;
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

static int16_t clamp16(double v)
{
	return (int16_t)(v > INT16_LIMIT ? INT16_LIMIT : (v < -INT16_LIMIT ? -INT16_LIMIT : v));
}

static double since_shot(uint32_t index)
{
	for (uint32_t i = shots; i > 0; i--) {
		if (index >= triggers[i - 1u]) {
			return (double)(index - triggers[i - 1u]);
		}
	}
	return -1.0;
}

static sp_cap_sample_t signal_at(uint32_t index)
{
	sp_cap_sample_t s;
	double t = since_shot(index);
	double shock = t >= 0 ? SHOCK_LSB * exp(-t / SHOCK_DECAY) * sin(TWO_PI * t / SHOCK_PERIOD) : 0;
	double turn = t >= 0 ? TURN_LSB * exp(-t / TURN_DECAY) : 0;
	double sway = SWAY_LSB * sin(TWO_PI * index / SWAY_PERIOD);

	for (uint32_t a = 0; a < AXES_PER_SENSOR; a++) {
		int32_t noise = (int32_t)(((index * AXES_PER_SENSOR + a) * HASH_MUL >> HASH_SHIFT) % NOISE_SPAN) - NOISE_CENTER;
		s.acc[a] = clamp16((a == 2u ? GRAVITY_LSB : 0) + sway + noise + shock + (t >= 0 ? JUMP_LSB : 0));
		s.gyro[a] = clamp16(sway + noise + turn);
	}
	return s;
}

static int refill(capdec_t *dec)
{
	const source_t *src = dec->context;
	const capstore_entry_t *e = capstore_find(src->store, src->seq);
	uint32_t left = e ? e->payload_len - dec->source_offset : 0u;
	capstore_read_t req = {src->seq, dec->source_offset, dec->in, left < CODEC_IN_BYTES ? left : CODEC_IN_BYTES};

	dec->in_len = req.len;
	dec->source_offset += req.len;
	return req.len ? capstore_read(src->store, &req) : 0;
}

static int capture_matches(uint32_t shot_seq, uint32_t first)
{
	source_t src = {&store, shot_seq};
	const capstore_entry_t *e = capstore_find(&store, shot_seq);
	capdec_t dec = {.refill = refill, .context = &src};
	sp_cap_sample_t s;

	if (!e) {
		return 0;
	}
	capdec_init(&dec, e->n_samples);
	for (uint32_t k = 0; k < e->n_samples; k++) {
		sp_cap_sample_t want = signal_at(first + k);
		if (capdec_next(&dec, &s) != 1 || memcmp(&s, &want, sizeof(s)) != 0) {
			return 0;
		}
	}
	return capdec_next(&dec, &s) == 0;
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

static void shoot(uint32_t trigger, uint8_t mode)
{
	triggers[shots++] = trigger;
	run_to(trigger + DETECT_DELAY);
	capw_request_t req = {++seq, trigger, cap.epoch, mode};
	capw_request(&writer, &req);
}

static void setup(void)
{
	capw_deps_t deps = {&cap, &store};

	memset(flash, ERASED, sizeof(flash));
	cap_init(&cap);
	shots = 0;
	seq = 0;
	check(capstore_init(&store, &dev) == 0, "store init");
	capw_init(&writer, &deps);
}

static void test_modes(void)
{
	setup();
	shoot(WAKE_TRIGGER, SP_CAPTURE_STANDARD);
	shoot(SHOT_SPACING, SP_CAPTURE_STANDARD);
	shoot(2u * SHOT_SPACING, SP_CAPTURE_EXTENDED);
	run_to(3u * SHOT_SPACING);
	const capstore_entry_t *e = capstore_find(&store, 1);
	check(e && e->n_samples == WAKE_TRIGGER + CAP_STANDARD_POST && e->trigger_index == WAKE_TRIGGER,
	      "shot after wake keeps the samples since wake");
	check(capture_matches(1, 0), "short capture decodes exactly");
	check(capture_matches(2, SHOT_SPACING - CAP_STANDARD_PRE), "standard capture decodes exactly");
	check(capture_matches(3, 2u * SHOT_SPACING - CAP_EXTENDED_PRE), "extended capture decodes exactly");
	check(capstore_init(&store, &dev) == 0 && capture_matches(3, 2u * SHOT_SPACING - CAP_EXTENDED_PRE),
	      "captures survive a reboot");
}

static void test_reset_aborts(void)
{
	setup();
	shoot(SHOT_SPACING, SP_CAPTURE_EXTENDED);
	cap_reset(&cap);
	shots = 0;
	run_to(BATCH);
	check(capstore_find(&store, 1) == NULL && !capw_busy(&writer), "ring reset drops the capture");
	shoot(SHOT_SPACING / 2u, SP_CAPTURE_STANDARD);
	run_to(SHOT_SPACING);
	check(capture_matches(2, SHOT_SPACING / 2u - CAP_STANDARD_PRE), "next capture after a reset");
}

static void report_capacity(uint8_t mode, const char *name)
{
	setup();
	for (uint32_t i = 1; i <= FILL_SHOTS; i++) {
		shoot(i * SHOT_SPACING, mode);
	}
	run_to((FILL_SHOTS + 1u) * SHOT_SPACING);
	const capstore_entry_t *e = capstore_find(&store, seq);
	double raw = e ? (double)e->n_samples * sizeof(sp_cap_sample_t) : 0;
	check(e != NULL, "newest capture stored");
	printf("info: %s captures held %u, ratio %.2f (synthetic signal)\n", name, capstore_count(&store),
	       e ? raw / e->payload_len : 0.0);
}

int main(void)
{
	test_modes();
	test_reset_aborts();
	report_capacity(SP_CAPTURE_STANDARD, "standard");
	report_capacity(SP_CAPTURE_EXTENDED, "extended");
	printf("CAPWRITER TESTS: %s\n", failures ? "FAIL" : "PASS");
	return failures ? 1 : 0;
}
