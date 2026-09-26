#ifndef CLAIM_H
#define CLAIM_H

#include <stdbool.h>

int claim_init(void);
void claim_poll(bool vbus);
bool claim_is_open(void);
bool claim_take_paired(void);
int claim_forget_all(void);

#endif
