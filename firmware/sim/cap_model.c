#include <math.h>
#include <string.h>

#include "cap_harness.h"

typedef struct {
	uint32_t start;
	uint32_t end;
} span_t;

harness_want_t harness_want(const capstore_entry_t *e, uint32_t first)
{
	harness_want_t want = {e->seq, first, 0u, 0u};
	uint32_t t = e->trigger_index;

	if (e->format != SP_CAPFMT_FULL && t > CAP_ODR_HZ) {
		want.aim = t - CAP_ODR_HZ;
	}
	if (e->format == SP_CAPFMT_ULTRA && t > CAP_EXTENDED_PRE) {
		want.slow = (t - CAP_EXTENDED_PRE) / CAP_HISTORY_GROUP * CAP_HISTORY_GROUP;
	}
	return want;
}

static sp_cap_sample_t mean_of(const harness_want_t *want, const span_t *span)
{
	sp_cap_sample_t mean;

	for (uint32_t axis = 0; axis < CODEC_AXES; axis++) {
		double sum = 0;
		for (uint32_t i = span->start; i < span->end; i++) {
			sp_cap_sample_t s = harness_signal_at(want->first + i);
			sum += codec_axis_value(&s, axis);
		}
		codec_set_axis(&mean, (codec_axis_set_t){axis, (int16_t)nearbyint(sum / (span->end - span->start))});
	}
	return mean;
}

static sp_cap_sample_t expected(const harness_want_t *want, uint32_t k)
{
	if (k >= want->aim) {
		return harness_signal_at(want->first + k);
	}
	if (k < want->slow) {
		span_t group = {k - k % CAP_HISTORY_GROUP, k - k % CAP_HISTORY_GROUP + CAP_HISTORY_GROUP};
		return mean_of(want, &group);
	}
	uint32_t end = want->aim - (want->aim - 1u - k) / CAPAIM_GROUP_SAMPLES * CAPAIM_GROUP_SAMPLES;
	span_t group = {end > want->slow + CAPAIM_GROUP_SAMPLES ? end - CAPAIM_GROUP_SAMPLES : want->slow, end};

	return mean_of(want, &group);
}

static int refill(capdec_t *dec)
{
	const harness_want_t *want = dec->context;
	const capstore_entry_t *e = capstore_find(harness_store(), want->seq);
	uint32_t body = e ? capstore_body_start(e) : 0u;
	uint32_t left = e ? e->payload_len - body - dec->source_offset : 0u;
	capstore_read_t req = {want->seq, body + dec->source_offset, dec->in, left < CODEC_IN_BYTES ? left : CODEC_IN_BYTES};

	dec->in_len = req.len;
	dec->source_offset += req.len;
	return req.len ? capstore_read(harness_store(), &req) : 0;
}

int harness_matches(const harness_want_t *want)
{
	const capstore_entry_t *e = capstore_find(harness_store(), want->seq);
	capdec_t dec = {.refill = refill, .context = (void *)want};
	capaim_reader_t reader = {.dec = &dec};
	sp_cap_sample_t s;

	if (!e) {
		return 0;
	}
	capaim_shape_t shape = {e->n_samples, e->trigger_index, e->format};
	capaim_open(&reader, &shape);
	for (uint32_t k = 0; k < e->n_samples; k++) {
		sp_cap_sample_t w = expected(want, k);
		if (capaim_next(&reader, &s) != 1 || memcmp(&s, &w, sizeof(s)) != 0) {
			return 0;
		}
	}
	return capaim_next(&reader, &s) == 0;
}
