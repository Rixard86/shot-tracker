#include "capwriter.h"

#include <string.h>

#include "capaim.h"

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

static int store_bytes(capw_t *w, uint32_t len)
{
	capstore_chunk_t chunk = {w->enc.out, len};
	int err = capstore_write(w->deps.store, &chunk);

	capenc_drop(&w->enc, len);
	return err;
}

static int store_words(capw_t *w)
{
	uint32_t ready = capenc_ready(&w->enc);

	return store_bytes(w, ready - ready % CAPSTORE_WORD_BYTES);
}

static cap_request_t window_request(const capw_request_t *req)
{
	if (req->samples > 0u) {
		return (cap_request_t){req->trigger, 0u, req->samples};
	}
	return cap_mode_request(req->mode, req->trigger);
}

static bool start(capw_t *w, const capw_request_t *req)
{
	cap_request_t window_req = window_request(req);

	if (req->epoch != w->deps.cap->epoch) {
		return false;
	}
	w->window = cap_window(w->deps.cap, &window_req);
	if (w->window.end == w->window.first) {
		return false;
	}
	capaim_shape_t shape = {
		.n_samples = (uint16_t)(w->window.end - w->window.first),
		.trigger_index = (uint16_t)(w->window.trigger - w->window.first),
		.averaged = req->mode == SP_CAPTURE_EXTENDED,
	};
	capstore_entry_t header = {
		.seq = req->seq,
		.n_samples = shape.n_samples,
		.trigger_index = shape.trigger_index,
		.format = shape.averaged ? CAPSTORE_FORMAT_AIM_AVERAGED : CAPSTORE_FORMAT_FULL,
	};
	if (capstore_begin(w->deps.store, &header) != 0) {
		return false;
	}
	capenc_init(&w->enc);
	w->current = *req;
	w->aim_end = w->window.first + capaim_aim_samples(&shape);
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

static uint32_t group_at(const capw_t *w, uint32_t index)
{
	return index < w->aim_end ? capaim_group(w->aim_end - index) : 1u;
}

static uint32_t block_end(const capw_t *w, uint32_t *count)
{
	uint32_t end = w->next;
	uint32_t n = 0;

	while (n < CODEC_BLOCK_SAMPLES && end < w->window.end) {
		end += group_at(w, end);
		n++;
	}
	*count = n;
	return end;
}

static sp_cap_sample_t stored_sample(const capw_t *w, uint32_t index)
{
	sp_cap_sample_t group[CAPAIM_GROUP_SAMPLES];
	uint32_t count = group_at(w, index);

	for (uint32_t i = 0; i < count; i++) {
		group[i] = *cap_sample(w->deps.cap, index + i);
	}
	return capaim_mean(group, count);
}

static step_t encode_block(capw_t *w)
{
	sp_cap_sample_t samples[CODEC_BLOCK_SAMPLES];
	const cap_t *cap = w->deps.cap;
	uint32_t n;
	uint32_t end = block_end(w, &n);

	if (cap->epoch != w->current.epoch) {
		return STEP_LOST;
	}
	if (cap->count < end) {
		return STEP_WAIT;
	}
	if (!cap_holds(cap, w->next)) {
		return STEP_LOST;
	}
	uint32_t at = w->next;
	for (uint32_t i = 0; i < n; i++) {
		samples[i] = stored_sample(w, at);
		at += group_at(w, at);
	}
	codec_block_t block = {samples, n};
	if (capenc_block(&w->enc, &block) != 0 || store_words(w) != 0) {
		return STEP_LOST;
	}
	w->next = end;
	return w->next == w->window.end ? STEP_FINISHED : STEP_ENCODED;
}

static int finish(capw_t *w)
{
	w->active = false;
	capenc_finish(&w->enc);
	if (store_bytes(w, capenc_ready(&w->enc)) != 0) {
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
