/* accel.h - LIS2DE12 driver, see accel.c */
#ifndef ACCEL_H
#define ACCEL_H

#include <stdint.h>

typedef enum { ACCEL_OFF, ACCEL_IDLE, ACCEL_ACTIVE } accel_mode_t;

/* Called from ISR context. INT1 is masked until accel_irq_rearm(). */
typedef void (*accel_irq_handler_t)(void);

int accel_init(accel_irq_handler_t handler);
int accel_set_idle(uint16_t wake_mg);
int accel_set_active(void);
int accel_off(void);
accel_mode_t accel_mode(void);
void accel_irq_rearm(void);

/* IDLE: read/clear latched wake source. Returns 1 if a wake event fired. */
int accel_clear_wake(void);

/* ACTIVE: drain up to max samples (mg). Returns count or -errno. */
int accel_read_fifo(int16_t (*out)[3], int max, int *overrun);

#endif
