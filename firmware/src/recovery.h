#ifndef RECOVERY_H
#define RECOVERY_H

#include <stdbool.h>
#include <stdint.h>

#define RECOVERY_HOLD_MS 5000u
#define RECOVERY_BOOT_LOOP_LIMIT 3u

typedef struct {
	bool armed;
	bool held;
	uint32_t since_ms;
} pad_hold_t;

typedef struct {
	bool bridged;
	uint32_t now_ms;
} pad_sample_t;

void pad_hold_init(pad_hold_t *h);
bool pad_hold_step(pad_hold_t *h, const pad_sample_t *s);
bool pad_hold_active(const pad_hold_t *h);
uint32_t boot_loop_count_next(uint32_t count, bool crashed);
bool boot_loop_tripped(uint32_t count);

#endif
