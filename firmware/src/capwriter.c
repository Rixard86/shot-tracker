#include "capwriter.h"

#include <string.h>

#define DRAIN_BYTES 64u

typedef enum { STEP_ENCODED, STEP_WAIT, STEP_FINISHED, STEP_LOST } step_t;

void capw_init(capw_t *w, const capw_deps_t *deps)
{
	memset(w, 0, sizeof(*w));
	w->deps = *deps;
}

void capw_request(capw_t *w, const capw_request_t *req)
{
	if (w->len == CAPW_QUEUE_LEN) {
		w->head = (w->head + 1u) % CAPW_QUEUE_LEN;
		w->len--;
	}
	w->queue[(w->head + w->len) % CAPW_QUEUE_LEN] = *req;
	w->len++;
}

bool capw_busy(const capw_t *w)
{
	return w->active || w->len > 0;
}

bool capw_pending(const capw_t *w, uint32_t seq)
{
	if (w->active && w->current.seq == seq) {
		return true;
	}
	for (uint32_t i = 0; i < w->len; i++) {
		if (w->queue[(w->head + i) % CAPW_QUEUE_LEN].seq == seq) {
			return true;
		}
	}
	return false;
}

static int drain(capw_t *w)
{
	uint8_t buf[DRAIN_BYTES];

	for (;;) {
		codec_bytes_t out = {buf, sizeof(buf)};
		if (capenc_take(&w->enc, &out) == 0) {
			return 0;
		}
		capstore_chunk_t chunk = {buf, out.len};
		int err = capstore_write(w->deps.store, &chunk);
		if (err) {
			return err;
		}
	}
}

static bool start(capw_t *w, const capw_request_t *req)
{
	cap_request_t window_req = cap_mode_request(req->mode, req->trigger);

	if (req->epoch != w->deps.cap->epoch) {
		return false;
	}
	w->window = cap_window(w->deps.cap, &window_req);
	if (w->window.end == w->window.first) {
		return false;
	}
	capstore_entry_t header = {
		.seq = req->seq,
		.n_samples = (uint16_t)(w->window.end - w->window.first),
		.trigger_index = (uint16_t)(w->window.trigger - w->window.first),
	};
	if (capstore_begin(w->deps.store, &header) != 0) {
		return false;
	}
	capenc_init(&w->enc);
	w->current = *req;
	w->next = w->window.first;
	w->active = true;
	return true;
}

static bool start_next(capw_t *w)
{
	while (w->len > 0) {
		capw_request_t req = w->queue[w->head];

		w->head = (w->head + 1u) % CAPW_QUEUE_LEN;
		w->len--;
		if (start(w, &req)) {
			return true;
		}
	}
	return false;
}

static step_t encode_block(capw_t *w)
{
	sp_cap_sample_t samples[CODEC_BLOCK_SAMPLES];
	const cap_t *cap = w->deps.cap;
	uint32_t left = w->window.end - w->next;
	uint32_t n = left < CODEC_BLOCK_SAMPLES ? left : CODEC_BLOCK_SAMPLES;

	if (cap->epoch != w->current.epoch) {
		return STEP_LOST;
	}
	if (cap->count < w->next + n) {
		return STEP_WAIT;
	}
	if (!cap_holds(cap, w->next)) {
		return STEP_LOST;
	}
	for (uint32_t i = 0; i < n; i++) {
		samples[i] = *cap_sample(cap, w->next + i);
	}
	codec_block_t block = {samples, n};
	if (capenc_block(&w->enc, &block) != 0 || drain(w) != 0) {
		return STEP_LOST;
	}
	w->next += n;
	return w->next == w->window.end ? STEP_FINISHED : STEP_ENCODED;
}

static int finish(capw_t *w)
{
	w->active = false;
	capenc_finish(&w->enc);
	if (drain(w) != 0) {
		capstore_abort(w->deps.store);
		return 0;
	}
	if (capstore_commit(w->deps.store) != 0) {
		return 0;
	}
	w->committed_seq = w->current.seq;
	return CAPW_COMMITTED;
}

int capw_pump(capw_t *w)
{
	if (!w->active && !start_next(w)) {
		return 0;
	}
	for (uint32_t b = 0; b < CAPW_BLOCKS_PER_PUMP; b++) {
		step_t step = encode_block(w);

		if (step == STEP_FINISHED) {
			return finish(w);
		}
		if (step == STEP_LOST) {
			w->active = false;
			capstore_abort(w->deps.store);
			return 0;
		}
		if (step == STEP_WAIT) {
			return 0;
		}
	}
	return 0;
}
