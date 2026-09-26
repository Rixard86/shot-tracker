#ifndef CAPTURE_TX_H
#define CAPTURE_TX_H

#include <stdbool.h>
#include <stdint.h>

#include "capture.h"

void cap_tx_init(const cap_t *cap);
void cap_tx_queue(uint32_t seq);
bool cap_tx_busy(void);
void cap_tx_pump(void);

#endif
