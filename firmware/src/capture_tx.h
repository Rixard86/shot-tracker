#ifndef CAPTURE_TX_H
#define CAPTURE_TX_H

#include <stdbool.h>
#include <stdint.h>

#include "capstore.h"
#include "capwriter.h"

void cap_tx_init(const capstore_t *store, const capw_t *writer);
void cap_tx_queue(uint32_t seq);
bool cap_tx_busy(void);
void cap_tx_pump(void);

#endif
