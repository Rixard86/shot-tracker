#include "capture.h"

#include <stdbool.h>
#include <string.h>

#define MS_PER_S 1000u

void cap_init(cap_t *cap)
{
	memset(cap, 0, sizeof(*cap));
}

void cap_reset(cap_t *cap)
{
	cap->count = 0;
	for (uint32_t i = 0; i < CAP_SLOTS; i++) {
		if (cap->slots[i].state == CAP_SLOT_FILLING) {
			cap->slots[i].state = CAP_SLOT_FREE;
		}
	}
}

static bool window_complete(const cap_t *cap, uint32_t slot)
{
	const cap_slot_t *s = &cap->slots[slot];

	return s->state == CAP_SLOT_FILLING && cap->count >= s->trigger_index + CAP_POST_SAMPLES;
}

static void fill_slot(cap_t *cap, uint32_t slot)
{
	cap_slot_t *s = &cap->slots[slot];
	uint32_t start = s->trigger_index - CAP_PRE_SAMPLES;

	for (uint32_t k = 0; k < CAP_WINDOW_SAMPLES; k++) {
		s->samples[k] = cap->ring[(start + k) % CAP_RING_SAMPLES];
	}
	s->state = CAP_SLOT_READY;
}

int cap_push(cap_t *cap, const sp_cap_sample_t *sample)
{
	cap->ring[cap->count % CAP_RING_SAMPLES] = *sample;
	cap->count++;
	for (uint32_t i = 0; i < CAP_SLOTS; i++) {
		if (window_complete(cap, i)) {
			fill_slot(cap, i);
			return (int)i;
		}
	}
	return CAP_NO_SLOT;
}

int cap_request(cap_t *cap, const cap_request_t *req)
{
	if (req->trigger_index < CAP_PRE_SAMPLES) {
		return CAP_NO_SLOT;
	}
	uint32_t start = req->trigger_index - CAP_PRE_SAMPLES;
	if (cap->count > start + CAP_RING_SAMPLES) {
		return CAP_NO_SLOT;
	}
	uint32_t slot = cap->next_slot;
	cap_slot_t *s = &cap->slots[slot];

	cap->next_slot = (slot + 1) % CAP_SLOTS;
	s->state = CAP_SLOT_FILLING;
	s->seq = req->seq;
	s->trigger_index = req->trigger_index;
	return (int)slot;
}

int cap_find(const cap_t *cap, uint32_t seq)
{
	for (uint32_t i = 0; i < CAP_SLOTS; i++) {
		if (cap->slots[i].state == CAP_SLOT_READY && cap->slots[i].seq == seq) {
			return (int)i;
		}
	}
	return CAP_NO_SLOT;
}

uint32_t cap_index_to_ms(uint32_t index)
{
	return (uint32_t)(((uint64_t)index * MS_PER_S) / CAP_ODR_HZ);
}

uint32_t cap_ms_to_index(uint32_t ms)
{
	return (uint32_t)(((uint64_t)ms * CAP_ODR_HZ + MS_PER_S - 1u) / MS_PER_S);
}

static bool slot_ready(const cap_t *cap, const cap_packet_t *pkt)
{
	if (pkt->slot < 0 || pkt->slot >= (int)CAP_SLOTS) {
		return false;
	}
	const cap_slot_t *s = &cap->slots[pkt->slot];

	return s->state == CAP_SLOT_READY && s->seq == pkt->seq;
}

size_t cap_pack_header(const cap_t *cap, const cap_packet_t *pkt)
{
	sp_cap_header_t h = {
		.type = SP_CAP_HEADER,
		.seq = pkt->seq,
		.n_samples = slot_ready(cap, pkt) ? CAP_WINDOW_SAMPLES : 0u,
		.trigger_index = CAP_PRE_SAMPLES,
		.odr_hz = CAP_ODR_HZ,
		.acc_ug_per_lsb = CAP_ACC_UG_PER_LSB,
		.gyro_mdps_per_lsb = CAP_GYRO_MDPS_PER_LSB,
	};

	if (pkt->capacity < sizeof(h)) {
		return 0;
	}
	memcpy(pkt->buf, &h, sizeof(h));
	return sizeof(h);
}

size_t cap_pack_data(const cap_t *cap, cap_packet_t *pkt)
{
	const size_t min_len = sizeof(sp_cap_data_t) + sizeof(sp_cap_sample_t);

	if (!slot_ready(cap, pkt) || pkt->first >= CAP_WINDOW_SAMPLES || pkt->capacity < min_len) {
		return 0;
	}
	uint32_t fit = (uint32_t)((pkt->capacity - sizeof(sp_cap_data_t)) / sizeof(sp_cap_sample_t));
	uint32_t left = CAP_WINDOW_SAMPLES - pkt->first;
	uint32_t n = fit < left ? fit : left;
	sp_cap_data_t d = {.type = SP_CAP_DATA, .seq = pkt->seq, .first = pkt->first};

	memcpy(pkt->buf, &d, sizeof(d));
	memcpy(pkt->buf + sizeof(d), &cap->slots[pkt->slot].samples[pkt->first],
	       n * sizeof(sp_cap_sample_t));
	pkt->first = (uint16_t)(pkt->first + n);
	return sizeof(d) + n * sizeof(sp_cap_sample_t);
}
