#ifndef DIAG_H
#define DIAG_H

#include <stdbool.h>

#include "store.h"

void diag_boot(void);
bool diag_last_reset_crashed(void);
void diag_count_crash(store_t *st);
void diag_publish(const store_t *st);

#endif
