#include "capture.h"

#include <string.h>

#include "capaim.h"

#define MS_PER_S 1000u

_Static_assert(CAP_RING_SAMPLES % CAP_HISTORY_GROUP == 0, "history groups must not wrap the sample ring");

void cap_init(cap_t *cap, sp_cap_sample_t *history)
{
	memset(cap, 0, sizeof(*cap));
	cap->history = history;
}

void cap_reset(cap_t *cap)
{
	cap->count = 0;
	cap->epoch++;
}

void cap_push(cap_t *cap, const sp_cap_sample_t *sample)
{
	cap->ring[cap->count % CAP_RING_SAMPLES] = *sample;
	cap->count++;
	if (cap->count % CAP_HISTORY_GROUP == 0u) {
		uint32_t group = cap->count / CAP_HISTORY_GROUP - 1u;
		const sp_cap_sample_t *first = &cap->ring[(cap->count - CAP_HISTORY_GROUP) % CAP_RING_SAMPLES];

		cap->history[group % CAP_HISTORY_SAMPLES] = capaim_mean(first, CAP_HISTORY_GROUP);
	}
}

static uint32_t oldest_held(const cap_t *cap)
{
	return cap->count > CAP_RING_SAMPLES ? cap->count - CAP_RING_SAMPLES : 0u;
}

bool cap_holds(const cap_t *cap, uint32_t index)
{
	return index < cap->count && index >= oldest_held(cap);
}

const sp_cap_sample_t *cap_sample(const cap_t *cap, uint32_t index)
{
	return &cap->ring[index % CAP_RING_SAMPLES];
}

static uint32_t oldest_history(const cap_t *cap)
{
	uint32_t groups = cap->count / CAP_HISTORY_GROUP;

	return groups > CAP_HISTORY_SAMPLES ? (groups - CAP_HISTORY_SAMPLES) * CAP_HISTORY_GROUP : 0u;
}

bool cap_history_holds(const cap_t *cap, uint32_t index)
{
	return index - index % CAP_HISTORY_GROUP + CAP_HISTORY_GROUP <= cap->count && index >= oldest_history(cap);
}

const sp_cap_sample_t *cap_history_sample(const cap_t *cap, uint32_t index)
{
	return &cap->history[index / CAP_HISTORY_GROUP % CAP_HISTORY_SAMPLES];
}

cap_window_t cap_window(const cap_t *cap, const cap_request_t *req)
{
	cap_window_t empty = {0, 0, 0};
	uint32_t oldest = oldest_held(cap);
	uint32_t first = req->trigger > req->pre ? req->trigger - req->pre : 0u;

	if (req->trigger < oldest || req->trigger >= cap->count) {
		return empty;
	}
	if (req->history) {
		first -= first % CAP_HISTORY_GROUP;
		oldest = oldest_history(cap);
	}
	return (cap_window_t){first > oldest ? first : oldest, req->trigger, req->trigger + req->post};
}

cap_request_t cap_mode_request(uint8_t mode, uint32_t trigger)
{
	if (mode == SP_CAPTURE_ULTRA) {
		return (cap_request_t){trigger, CAP_ULTRA_PRE, CAP_ULTRA_POST, true};
	}
	if (mode == SP_CAPTURE_EXTENDED) {
		return (cap_request_t){trigger, CAP_EXTENDED_PRE, CAP_EXTENDED_POST, false};
	}
	return (cap_request_t){trigger, CAP_STANDARD_PRE, CAP_STANDARD_POST, false};
}

uint32_t cap_index_to_ms(uint32_t index)
{
	return (index / CAP_ODR_HZ) * MS_PER_S + (index % CAP_ODR_HZ) * MS_PER_S / CAP_ODR_HZ;
}

uint32_t cap_ms_to_index(uint32_t ms)
{
	return (uint32_t)(((uint64_t)ms * CAP_ODR_HZ + MS_PER_S - 1u) / MS_PER_S);
}

uint32_t cap_data_fit(size_t capacity)
{
	if (capacity < sizeof(sp_cap_data_t) + sizeof(sp_cap_sample_t)) {
		return 0;
	}
	return (uint32_t)((capacity - sizeof(sp_cap_data_t)) / sizeof(sp_cap_sample_t));
}

size_t cap_pack_header(const cap_header_info_t *info, const cap_buf_t *out)
{
	sp_cap_header_t h = {
		.type = SP_CAP_HEADER,
		.seq = info->seq,
		.n_samples = info->n_samples,
		.trigger_index = info->trigger_index,
		.odr_hz = CAP_ODR_HZ,
		.acc_ug_per_lsb = CAP_ACC_UG_PER_LSB,
		.gyro_mdps_per_lsb = CAP_GYRO_MDPS_PER_LSB,
		.gyro_offset = info->gyro_offset,
	};

	if (out->capacity < sizeof(h)) {
		return 0;
	}
	memcpy(out->buf, &h, sizeof(h));
	return sizeof(h);
}

size_t cap_pack_data(const cap_data_info_t *data, const cap_buf_t *out)
{
	sp_cap_data_t d = {.type = SP_CAP_DATA, .seq = data->seq, .first = data->first};
	size_t samples_len = data->count * sizeof(sp_cap_sample_t);

	if (data->count == 0 || data->count > cap_data_fit(out->capacity)) {
		return 0;
	}
	memcpy(out->buf, &d, sizeof(d));
	memcpy(out->buf + sizeof(d), data->samples, samples_len);
	return sizeof(d) + samples_len;
}

uint32_t cap_rec_data_fit(size_t capacity)
{
	return capacity > sizeof(sp_caprec_data_t) ? (uint32_t)(capacity - sizeof(sp_caprec_data_t)) : 0u;
}

size_t cap_pack_rec_header(const cap_rec_info_t *info, const cap_buf_t *out)
{
	sp_caprec_header_t h = {
		.type = SP_CAPREC_HEADER,
		.seq = info->seq,
		.n_samples = info->n_samples,
		.trigger_index = info->trigger_index,
		.format = info->format,
		.payload_len = info->payload_len,
		.check = info->check,
		.odr_hz = CAP_ODR_HZ,
		.acc_ug_per_lsb = CAP_ACC_UG_PER_LSB,
		.gyro_mdps_per_lsb = CAP_GYRO_MDPS_PER_LSB,
		.gyro_offset = info->gyro_offset,
	};

	if (out->capacity < sizeof(h)) {
		return 0;
	}
	memcpy(out->buf, &h, sizeof(h));
	return sizeof(h);
}

size_t cap_pack_rec_data(const cap_rec_data_t *data, const cap_buf_t *out)
{
	sp_caprec_data_t d = {.type = SP_CAPREC_DATA, .seq = data->seq, .offset = data->offset};

	if (data->len == 0u || data->len > cap_rec_data_fit(out->capacity)) {
		return 0;
	}
	memcpy(out->buf, &d, sizeof(d));
	memcpy(out->buf + sizeof(d), data->bytes, data->len);
	return sizeof(d) + data->len;
}
