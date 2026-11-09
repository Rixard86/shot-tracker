#include "capture.h"

#include <string.h>

#define MS_PER_S 1000u

void cap_init(cap_t *cap)
{
	memset(cap, 0, sizeof(*cap));
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

cap_window_t cap_window(const cap_t *cap, const cap_request_t *req)
{
	cap_window_t empty = {0, 0, 0};
	uint32_t oldest = oldest_held(cap);
	uint32_t first = req->trigger > req->pre ? req->trigger - req->pre : 0u;

	if (req->trigger < oldest || req->trigger >= cap->count) {
		return empty;
	}
	return (cap_window_t){first > oldest ? first : oldest, req->trigger, req->trigger + req->post};
}

cap_request_t cap_mode_request(uint8_t mode, uint32_t trigger)
{
	if (mode == SP_CAPTURE_EXTENDED) {
		return (cap_request_t){trigger, CAP_EXTENDED_PRE, CAP_EXTENDED_POST};
	}
	return (cap_request_t){trigger, CAP_STANDARD_PRE, CAP_STANDARD_POST};
}

uint32_t cap_index_to_ms(uint32_t index)
{
	return (uint32_t)(((uint64_t)index * MS_PER_S) / CAP_ODR_HZ);
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
