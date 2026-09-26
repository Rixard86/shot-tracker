#ifndef EVENT_TX_H
#define EVENT_TX_H

#include <stdbool.h>
#include <stdint.h>

#include "protocol.h"

int event_tx_init(void);
void event_tx_record(const sp_event_t *event);
void event_tx_replay(uint32_t from_seq);
bool event_tx_busy(void);
void event_tx_pump(void);

#endif
