#include "devstream.h"

#include <string.h>

uint32_t devstream_fit(uint16_t room)
{
	if (room < sizeof(sp_stream_head_t)) {
		return 0u;
	}
	uint32_t fit = (uint32_t)(room - sizeof(sp_stream_head_t)) / (uint32_t)sizeof(sp_cap_sample_t);

	return fit < DEVSTREAM_MAX_SAMPLES ? fit : DEVSTREAM_MAX_SAMPLES;
}

void devstream_start(devstream_t *s, const devstream_req_t *req)
{
	memset(s, 0, sizeof(*s));
	s->divider = req->fit > 0u ? req->divider : 0u;
	s->fit = req->fit < DEVSTREAM_MAX_SAMPLES ? req->fit : DEVSTREAM_MAX_SAMPLES;
	s->started_ms = req->started_ms;
}

void devstream_stop(devstream_t *s)
{
	s->divider = 0u;
}

bool devstream_on(const devstream_t *s)
{
	return s->divider > 0u;
}

bool devstream_expired(const devstream_t *s, uint32_t now_ms)
{
	return devstream_on(s) && now_ms - s->started_ms >= DEVSTREAM_MAX_MS;
}

bool devstream_push(devstream_t *s, const sp_cap_sample_t *sample)
{
	if (!devstream_on(s) || s->phase++ % s->divider != 0u) {
		return false;
	}
	memcpy(&s->buf[sizeof(sp_stream_head_t) + s->fill * sizeof(*sample)], sample, sizeof(*sample));
	s->fill++;
	return s->fill == s->fit;
}

devstream_packet_t devstream_take(devstream_t *s)
{
	sp_stream_head_t head = {.first = s->first, .divider = s->divider};
	devstream_packet_t packet = {s->buf, (uint16_t)(sizeof(head) + s->fill * sizeof(sp_cap_sample_t))};

	memcpy(s->buf, &head, sizeof(head));
	s->first += s->fill;
	s->fill = 0u;
	return packet;
}
