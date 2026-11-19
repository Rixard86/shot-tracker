#ifndef WAKESTAT_H
#define WAKESTAT_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
	uint32_t motion_wakes;
	uint32_t wakes_without_shot;
	bool counting;
	bool shot;
} wakestat_t;

void wakestat_wake(wakestat_t *w);
void wakestat_shot(wakestat_t *w);
void wakestat_sleep(wakestat_t *w);

#endif
