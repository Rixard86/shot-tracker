#ifndef DEVSTREAM_H
#define DEVSTREAM_H

#include <stdbool.h>
#include <stdint.h>

#include "protocol.h"

#define DEVSTREAM_MAX_SAMPLES 19u
#define DEVSTREAM_MAX_MS 600000u
#define DEVSTREAM_PACKET_MAX (sizeof(sp_stream_head_t) + DEVSTREAM_MAX_SAMPLES * sizeof(sp_cap_sample_t))

typedef struct {
	uint8_t divider;
	uint32_t fit;
	uint32_t started_ms;
} devstream_req_t;

typedef struct {
	const uint8_t *data;
	uint16_t len;
} devstream_packet_t;

typedef struct {
	uint8_t divider;
	uint32_t fit;
	uint32_t started_ms;
	uint32_t phase;
	uint32_t first;
	uint32_t fill;
	uint8_t buf[DEVSTREAM_PACKET_MAX];
} devstream_t;

uint32_t devstream_fit(uint16_t room);
void devstream_start(devstream_t *s, const devstream_req_t *req);
void devstream_stop(devstream_t *s);
bool devstream_on(const devstream_t *s);
bool devstream_expired(const devstream_t *s, uint32_t now_ms);
bool devstream_push(devstream_t *s, const sp_cap_sample_t *sample);
devstream_packet_t devstream_take(devstream_t *s);

#endif
