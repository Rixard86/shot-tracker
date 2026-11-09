#ifndef EVQ_H
#define EVQ_H

#include <stdbool.h>
#include <stdint.h>

#include "evlog.h"
#include "protocol.h"

#define EVQ_REJECT_LOG_LEN 128u
#define EVQ_LIVE_QUEUE_LEN 8u
#define EVQ_BATCH_MAX 10u

typedef struct {
	uint16_t (*room)(void);
	int (*send)(const uint8_t *data, uint16_t len);
} evq_link_t;

typedef struct {
	evlog_t *shots;
	evq_link_t link;
} evq_deps_t;

typedef struct {
	uint32_t live_head;
	uint32_t live_len;
	bool replaying;
	bool flash_done;
	evlog_cursor_t cursor;
	uint32_t reject_next;
} evq_state_t;

typedef struct {
	evq_deps_t deps;
	sp_event_t rejects[EVQ_REJECT_LOG_LEN];
	uint32_t rejects_written;
	sp_event_t live[EVQ_LIVE_QUEUE_LEN];
	evq_state_t state;
	uint8_t payload[EVQ_BATCH_MAX * sizeof(sp_event_t)];
} evq_t;

void evq_init(evq_t *q, const evq_deps_t *deps);
int evq_record(evq_t *q, const sp_event_t *event);
void evq_replay(evq_t *q, uint32_t from_seq);
bool evq_busy(const evq_t *q);
void evq_pump(evq_t *q);

#endif
