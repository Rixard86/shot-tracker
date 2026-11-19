#include <stdio.h>
#include <string.h>

#include "cap_harness.h"

#define SPACING HARNESS_SHOT_SPACING
#define FULL_RATE CAPAIM_FULL_RATE_PRE
#define EXTENDED_SAMPLES (CAP_EXTENDED_PRE + CAP_EXTENDED_POST)
#define ODD_WAKE_TRIGGER 1003u
#define SHORT_WAKE_TRIGGER 300u
#define ULTRA_TRIGGER 20003u
#define ULTRA_WAKE_TRIGGER 9003u
#define ULTRA_SHORT_TRIGGER 2001u
#define GROUP8 CAP_HISTORY_GROUP
#define LATE_BY 4000u
#define STALL_SAMPLES 2000u
#define PUMPS_TO_FINISH 200u

static int failures;

static void check(int ok, const char *what)
{
	if (!ok) {
		printf("FAIL: %s\n", what);
		failures++;
	}
}

static int shot_matches(const capstore_entry_t *e, uint32_t first)
{
	harness_want_t want;

	if (!e) {
		return 0;
	}
	want = harness_want(e, first);
	return harness_matches(&want);
}

static void test_extended(void)
{
	harness_setup(HARNESS_BOW);
	const capstore_entry_t *e = harness_shoot(SPACING, SP_CAPTURE_EXTENDED);
	check(e && e->format == CAPSTORE_FORMAT_AIM_AVERAGED && e->n_samples == EXTENDED_SAMPLES &&
	      e->trigger_index == CAP_EXTENDED_PRE, "extended shot keeps its header and is marked averaged");
	check(shot_matches(e, SPACING - CAP_EXTENDED_PRE), "aiming part: rounded 4-sample means; last second and after: bit-exact");
	const capstore_entry_t *s = harness_shoot(2u * SPACING, SP_CAPTURE_STANDARD);
	check(s && s->format == CAPSTORE_FORMAT_FULL && shot_matches(s, 2u * SPACING - CAP_STANDARD_PRE),
	      "standard capture stays full rate");
}

static void test_after_wake(void)
{
	harness_setup(HARNESS_BOW);
	const capstore_entry_t *odd = harness_shoot(ODD_WAKE_TRIGGER, SP_CAPTURE_EXTENDED);
	check(odd && odd->trigger_index == ODD_WAKE_TRIGGER && shot_matches(odd, 0u),
	      "capture shortened by a wake: groups counted back from 1 s before the trigger, partial group first");
	harness_wake();
	const capstore_entry_t *short_one = harness_shoot(SHORT_WAKE_TRIGGER, SP_CAPTURE_EXTENDED);
	check(short_one && short_one->trigger_index == SHORT_WAKE_TRIGGER && shot_matches(short_one, 0u),
	      "trigger within 1 s of the wake: nothing averaged");
}

static void test_ultra(void)
{
	uint32_t first = (ULTRA_TRIGGER - CAP_ULTRA_PRE) / GROUP8 * GROUP8;
	uint32_t trigger_index = ULTRA_TRIGGER - first;

	harness_setup(HARNESS_BOW);
	const capstore_entry_t *e = harness_shoot(ULTRA_TRIGGER, SP_CAPTURE_ULTRA);
	check(e && e->format == CAPSTORE_FORMAT_ULTRA && e->trigger_index == trigger_index &&
	      e->n_samples == trigger_index + CAP_ULTRA_POST, "ultra window starts at a group of 8, 30 s or a few samples more before");
	check(shot_matches(e, first), "ultra: 60 Hz means, then 120 Hz means, then every sample from 1 s before");
	check(e && e->payload_len < EXTENDED_SAMPLES * sizeof(sp_cap_sample_t), "an ultra capture stores less than a raw extended one");
	harness_wake();
	const capstore_entry_t *woke = harness_shoot(ULTRA_WAKE_TRIGGER, SP_CAPTURE_ULTRA);
	check(woke && woke->trigger_index == ULTRA_WAKE_TRIGGER && shot_matches(woke, 0u),
	      "ultra shortened by a wake: shorter 60 Hz part, partial 120 Hz group first");
	harness_wake();
	const capstore_entry_t *brief = harness_shoot(ULTRA_SHORT_TRIGGER, SP_CAPTURE_ULTRA);
	check(brief && brief->trigger_index == ULTRA_SHORT_TRIGGER && shot_matches(brief, 0u),
	      "ultra within 6 s of the wake has no 60 Hz part");
}

static void test_history_lost(void)
{
	harness_setup(HARNESS_BOW);
	harness_run_to(ULTRA_TRIGGER + LATE_BY);
	harness_request(ULTRA_TRIGGER, SP_CAPTURE_ULTRA);
	harness_pump();
	harness_push(STALL_SAMPLES);
	for (uint32_t i = 0; i < PUMPS_TO_FINISH; i++) {
		harness_pump();
	}
	check(capstore_find(harness_store(), 1u) == NULL, "a capture whose 60 Hz history was overwritten mid-write is dropped");
}

static uint32_t store_full_rate_extended(uint32_t first)
{
	capstore_entry_t h = {.seq = harness_next_seq(), .n_samples = EXTENDED_SAMPLES, .trigger_index = CAP_EXTENDED_PRE};
	static capenc_t enc;
	sp_cap_sample_t block[CODEC_BLOCK_SAMPLES];
	capstore_t *store = harness_store();

	capenc_init(&enc);
	check(capstore_begin(store, &h) == 0, "old-format record opened");
	for (uint32_t at = 0; at < EXTENDED_SAMPLES; at += CODEC_BLOCK_SAMPLES) {
		for (uint32_t i = 0; i < CODEC_BLOCK_SAMPLES; i++) {
			block[i] = harness_signal_at(first + at + i);
		}
		codec_block_t b = {block, CODEC_BLOCK_SAMPLES};
		capenc_block(&enc, &b);
		capstore_chunk_t chunk = {enc.out, capenc_ready(&enc)};
		capstore_write(store, &chunk);
		capenc_drop(&enc, chunk.len);
	}
	capenc_finish(&enc);
	capstore_chunk_t rest = {enc.out, capenc_ready(&enc)};
	check(capstore_write(store, &rest) == 0 && capstore_commit(store) == 0, "old-format record stored");
	return h.seq;
}

static void test_mixed_ring(void)
{
	uint32_t old_first = 2u * SPACING - CAP_EXTENDED_PRE;
	uint32_t ultra_first = (ULTRA_TRIGGER - CAP_ULTRA_PRE) / GROUP8 * GROUP8;

	harness_setup(HARNESS_BOW);
	uint32_t old_seq = store_full_rate_extended(old_first);
	const capstore_entry_t *u = harness_shoot(ULTRA_TRIGGER, SP_CAPTURE_ULTRA);
	uint32_t ultra_seq = u ? u->seq : 0u;
	const capstore_entry_t *e = harness_shoot(ULTRA_TRIGGER + 2u * SPACING, SP_CAPTURE_EXTENDED);
	uint32_t ext_seq = e ? e->seq : 0u;
	check(harness_reboot() == 0 && capstore_count(harness_store()) == 3u, "all three formats found after a reboot");
	const capstore_entry_t *old = capstore_find(harness_store(), old_seq);
	check(old && old->format == CAPSTORE_FORMAT_FULL && shot_matches(old, old_first),
	      "a capture stored by 0.3.10 still decodes exactly");
	check(shot_matches(capstore_find(harness_store(), ultra_seq), ultra_first), "the ultra capture decodes after the reboot");
	check(shot_matches(capstore_find(harness_store(), ext_seq), ULTRA_TRIGGER + 2u * SPACING - CAP_EXTENDED_PRE),
	      "the extended capture decodes after the reboot");
}

int main(void)
{
	test_extended();
	test_after_wake();
	test_ultra();
	test_history_lost();
	test_mixed_ring();
	printf("CAPAIM STORE TESTS: %s\n", failures ? "FAIL" : "PASS");
	return failures ? 1 : 0;
}
