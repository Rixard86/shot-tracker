#ifndef SUPPLY_H
#define SUPPLY_H

#include <stdbool.h>

#include "capture.h"

sp_cap_sample_t *supply_history_ram_on(void);
void supply_history_ram_off(void);
void supply_unused_ram_off(void);
void supply_clear_fpu_flags(void);
void supply_reg0_set_ldo(bool ldo);
bool supply_reg0_ldo(void);

#endif
