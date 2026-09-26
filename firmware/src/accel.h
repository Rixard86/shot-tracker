#ifndef ACCEL_H
#define ACCEL_H

#include <stdint.h>

#include "protocol.h"

typedef enum { ACCEL_OFF, ACCEL_IDLE, ACCEL_ACTIVE } accel_mode_t;

typedef void (*accel_irq_handler_t)(void);

typedef struct {
	sp_cap_sample_t *samples;
	int max;
	int overrun;
} accel_fifo_t;

int accel_init(accel_irq_handler_t handler);
int accel_set_idle(uint16_t wake_mg);
int accel_set_active(void);
int accel_off(void);
accel_mode_t accel_mode(void);
void accel_irq_rearm(void);
int accel_clear_wake(void);
int accel_read_fifo(accel_fifo_t *fifo);
void accel_to_mg(const sp_cap_sample_t *raw, int16_t mg[3]);

#endif
