#ifndef CAPFLASH_H
#define CAPFLASH_H

#include <stdbool.h>

#include "capstore.h"

int capflash_open(capstore_t *store, bool with_spare);
bool capflash_spare_in_use(void);
void capflash_spare_claim(void);
bool capflash_spare_claimed(void);

#endif
