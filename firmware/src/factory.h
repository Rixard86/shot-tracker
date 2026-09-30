#ifndef FACTORY_H
#define FACTORY_H

#include <stdbool.h>

void factory_boot_guard(void);
int factory_pads_init(void);
void factory_poll(void);
bool factory_pads_held(void);

#endif
