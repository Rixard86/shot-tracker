#ifndef CAPTURE_H
#define CAPTURE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "protocol.h"

#define CAP_ODR_HZ 480u
#define CAP_RING_SAMPLES 4800u
#define CAP_STANDARD_PRE 480u
#define CAP_STANDARD_POST 240u
#define CAP_EXTENDED_PRE 2880u
#define CAP_EXTENDED_POST 960u
#define CAP_ACC_UG_PER_LSB 976u
#define CAP_GYRO_MDPS_PER_LSB 70u

typedef struct {
	sp_cap_sample_t ring[CAP_RING_SAMPLES];
	uint32_t count;
	uint32_t epoch;
} cap_t;

typedef struct {
	uint32_t trigger;
	uint32_t pre;
	uint32_t post;
} cap_request_t;

typedef struct {
	uint32_t first;
	uint32_t trigger;
	uint32_t end;
} cap_window_t;

typedef struct {
	uint32_t seq;
	uint16_t n_samples;
	uint16_t trigger_index;
} cap_header_info_t;

typedef struct {
	uint32_t seq;
	uint16_t first;
	const sp_cap_sample_t *samples;
	uint32_t count;
} cap_data_info_t;

typedef struct {
	uint8_t *buf;
	size_t capacity;
} cap_buf_t;

void cap_init(cap_t *cap);
void cap_reset(cap_t *cap);
void cap_push(cap_t *cap, const sp_cap_sample_t *sample);
bool cap_holds(const cap_t *cap, uint32_t index);
const sp_cap_sample_t *cap_sample(const cap_t *cap, uint32_t index);
cap_window_t cap_window(const cap_t *cap, const cap_request_t *req);
cap_request_t cap_mode_request(uint8_t mode, uint32_t trigger);
uint32_t cap_index_to_ms(uint32_t index);
uint32_t cap_ms_to_index(uint32_t ms);
uint32_t cap_data_fit(size_t capacity);
size_t cap_pack_header(const cap_header_info_t *info, const cap_buf_t *out);
size_t cap_pack_data(const cap_data_info_t *data, const cap_buf_t *out);

#endif
