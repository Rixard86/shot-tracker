/* power.h - see power.c */
#ifndef POWER_H
#define POWER_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
	uint16_t battery_mv;
	uint8_t battery_pct;
	int8_t temp_c;
	bool vbus;
	bool charging;
	bool chg_inhibit;
} power_state_t;

void power_regout0_3v0(void); /* call first thing in main(); may reset */
int power_init(void);
const power_state_t *power_update(void);
const power_state_t *power_state(void);
bool power_should_cutoff(void);
bool power_vbus_present(void);
void power_off_until_charger(void); /* does not return */

#endif
