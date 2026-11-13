#ifndef LIFETIME_H
#define LIFETIME_H

#include <stdint.h>

int lifetime_init(uint32_t start_seq);
void lifetime_shot(void);
void lifetime_unshot(uint32_t seq);
uint32_t lifetime_count(void);

#endif
