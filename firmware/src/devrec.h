#ifndef DEVREC_H
#define DEVREC_H

#include <stdbool.h>
#include <stdint.h>

#include "capwriter.h"

typedef struct {
	const uint8_t *data;
	uint16_t len;
} devcmd_req_t;

typedef struct {
	uint32_t samples;
	uint32_t seq;
	bool requested;
} devrec_t;

typedef struct {
	uint32_t seq;
	uint32_t sampled;
	uint32_t epoch;
} devrec_at_t;

bool devcmd_valid(const devcmd_req_t *req);
void devrec_start(devrec_t *rec, uint8_t seconds);
bool devrec_due(const devrec_t *rec, uint32_t sampled);
capw_request_t devrec_take(devrec_t *rec, const devrec_at_t *at);
bool devrec_running(const devrec_t *rec, const capw_t *writer);

#endif
