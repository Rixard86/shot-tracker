#ifndef CHARGE_H
#define CHARGE_H

#include <stdbool.h>
#include <stdint.h>

#define CHARGE_MS_PER_MIN 60000u
#define CHARGE_TOP_MV 4150u
#define CHARGE_FULL_AFTER_MS (60u * CHARGE_MS_PER_MIN)
#define CHARGE_TIMEOUT_MS (300u * CHARGE_MS_PER_MIN)
#define CHARGE_DONE_MIN_MV 4100u
#define CHARGE_RECHARGE_MV 4050u
#define CHARGE_LIFT_MV 40u
#define CHARGE_SHOWN_MAX_PCT 99u
#define CHARGE_FULL_PCT 100u

typedef struct {
	bool vbus;
	bool charging;
	bool paused;
	uint16_t mv;
	uint32_t now_ms;
} charge_sample_t;

typedef struct {
	bool charging;
	bool cycle;
	bool at_top;
	bool full;
	bool timed_out;
	uint32_t cycle_since_ms;
	uint32_t top_since_ms;
} charge_ctl_t;

void charge_init(charge_ctl_t *c);
void charge_step(charge_ctl_t *c, const charge_sample_t *s);
bool charge_holds_off(const charge_ctl_t *c);
bool charge_inhibit_pin(const charge_ctl_t *c, const charge_sample_t *s);
uint16_t charge_cell_mv(const charge_ctl_t *c, uint16_t mv);
uint8_t charge_shown_pct(const charge_ctl_t *c, uint8_t pct);

#endif
