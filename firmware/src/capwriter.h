#ifndef CAPWRITER_H
#define CAPWRITER_H

#include <stdbool.h>
#include <stdint.h>

#include "capcodec.h"
#include "capstore.h"
#include "capture.h"

#define CAPW_QUEUE_LEN 4u
#define CAPW_BLOCKS_PER_PUMP 4u
#define CAPW_COMMITTED 1

typedef struct {
	uint32_t seq;
	uint32_t trigger;
	uint32_t epoch;
	uint8_t mode;
} capw_request_t;

typedef struct {
	const cap_t *cap;
	capstore_t *store;
} capw_deps_t;

typedef struct {
	capw_deps_t deps;
	capw_request_t queue[CAPW_QUEUE_LEN];
	uint32_t head;
	uint32_t len;
	bool active;
	capw_request_t current;
	cap_window_t window;
	uint32_t next;
	capenc_t enc;
	uint32_t committed_seq;
} capw_t;

void capw_init(capw_t *w, const capw_deps_t *deps);
void capw_request(capw_t *w, const capw_request_t *req);
int capw_pump(capw_t *w);
bool capw_busy(const capw_t *w);
bool capw_pending(const capw_t *w, uint32_t seq);

#endif
