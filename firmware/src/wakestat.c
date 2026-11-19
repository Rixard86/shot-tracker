#include "wakestat.h"

void wakestat_wake(wakestat_t *w)
{
	w->motion_wakes++;
	w->counting = true;
	w->shot = false;
}

void wakestat_shot(wakestat_t *w)
{
	w->shot = true;
}

void wakestat_sleep(wakestat_t *w)
{
	if (w->counting && !w->shot) {
		w->wakes_without_shot++;
	}
	w->counting = false;
}
