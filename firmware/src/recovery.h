#ifndef RECOVERY_H
#define RECOVERY_H

#include <stdbool.h>
#include <stdint.h>

#define RECOVERY_HOLD_MS 5000u
#define RECOVERY_BOOT_LOOP_LIMIT 3u
#define RECOVERY_SEQ_MARGIN 1024u
#define RECOVERY_LOW_BATTERY_READINGS 3u
#define RECOVERY_CRASH_REASON_MAX 0xFDu
#define RECOVERY_CRASH_COUNT_MAX 0xFFFFu

typedef struct {
	bool armed;
	bool held;
	uint32_t since_ms;
} pad_hold_t;

typedef struct {
	bool bridged;
	uint32_t now_ms;
} pad_sample_t;

typedef struct {
	uint8_t crash_mark;
	bool lockup;
	bool watchdog;
	bool pin;
	bool wake;
	bool software;
	bool other;
} reset_flags_t;

void pad_hold_init(pad_hold_t *h);
bool pad_hold_step(pad_hold_t *h, const pad_sample_t *s);
bool pad_hold_active(const pad_hold_t *h);
uint32_t boot_loop_count_next(uint32_t count, bool crashed);
bool boot_loop_tripped(uint32_t count);
uint32_t seq_resume(uint32_t saved_seq, uint32_t newest_logged_seq);
uint32_t low_battery_count_next(uint32_t count, bool low);
bool low_battery_confirmed(uint32_t count);
uint8_t crash_mark_encode(uint32_t reason);
uint8_t crash_mark_reason(uint8_t mark);
uint8_t reset_classify(const reset_flags_t *f);
bool reset_is_crash(uint8_t cause);
uint16_t crash_count_next(uint16_t count, uint8_t cause);

#endif
