#ifndef CAPTURE_H
#define CAPTURE_H

#include <stddef.h>
#include <stdint.h>

#include "protocol.h"

#define CAP_ODR_HZ 416u
#define CAP_PRE_SAMPLES 416u
#define CAP_POST_SAMPLES 208u
#define CAP_WINDOW_SAMPLES (CAP_PRE_SAMPLES + CAP_POST_SAMPLES)
#define CAP_RING_SAMPLES 1152u
#define CAP_SLOTS 4u
#define CAP_ACC_UG_PER_LSB 976u
#define CAP_GYRO_MDPS_PER_LSB 70u
#define CAP_NO_SLOT (-1)

typedef enum { CAP_SLOT_FREE, CAP_SLOT_FILLING, CAP_SLOT_READY } cap_slot_state_t;

typedef struct {
	cap_slot_state_t state;
	uint32_t seq;
	uint32_t trigger_index;
	sp_cap_sample_t samples[CAP_WINDOW_SAMPLES];
} cap_slot_t;

typedef struct {
	sp_cap_sample_t ring[CAP_RING_SAMPLES];
	uint32_t count;
	cap_slot_t slots[CAP_SLOTS];
	uint32_t next_slot;
} cap_t;

typedef struct {
	uint32_t seq;
	uint32_t trigger_index;
} cap_request_t;

typedef struct {
	int slot;
	uint32_t seq;
	uint16_t first;
	uint8_t *buf;
	size_t capacity;
} cap_packet_t;

void cap_init(cap_t *cap);
void cap_reset(cap_t *cap);
int cap_push(cap_t *cap, const sp_cap_sample_t *sample);
int cap_request(cap_t *cap, const cap_request_t *req);
int cap_find(const cap_t *cap, uint32_t seq);
uint32_t cap_index_to_ms(uint32_t index);
uint32_t cap_ms_to_index(uint32_t ms);
size_t cap_pack_header(const cap_t *cap, const cap_packet_t *pkt);
size_t cap_pack_data(const cap_t *cap, cap_packet_t *pkt);

#endif
