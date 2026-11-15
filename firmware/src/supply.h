#ifndef SUPPLY_H
#define SUPPLY_H

#include <stdbool.h>

void supply_unused_ram_off(void);
void supply_reg0_set_ldo(bool ldo);
bool supply_reg0_ldo(void);

#endif
