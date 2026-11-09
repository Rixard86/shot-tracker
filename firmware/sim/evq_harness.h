#ifndef EVQ_HARNESS_H
#define EVQ_HARNESS_H

#include <stdbool.h>
#include <stdint.h>

#include "../src/evq.h"

#define MAX_SENT 512u
#define ROOM_MTU_247 244u
#define SENDS_UNLIMITED 0xFFFFFFFFu

extern evlog_t shots;
extern evq_t queue;
extern uint16_t room_now;
extern int send_result;
extern uint32_t sent[MAX_SENT];
extern uint32_t sent_count;
extern uint32_t send_calls;
extern uint32_t batch_sizes[MAX_SENT];
extern uint32_t sends_left;

void reset(bool with_log);
sp_event_t event(uint32_t seq, bool accepted);
void pump_all(void);
bool sent_in_order(uint32_t first, uint32_t count);
bool rising_from(uint32_t index, uint32_t first_seq);
void record_rejects(uint32_t first, uint32_t last);

#endif
