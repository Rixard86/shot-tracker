/* store.h - persisted state via Zephyr settings (NVS backend) */
#ifndef STORE_H
#define STORE_H

#include <stdint.h>
#include "shot_detect.h"

typedef struct {
	uint32_t total;   /* lifetime accepted shots */
	uint32_t seq;     /* last event sequence number */
	sd_config_t cfg;  /* detector thresholds */
	uint8_t capture_mode;
	uint16_t crashes;
} store_t;

/* Loads persisted values into *s (keeps caller defaults if absent). */
int store_init(store_t *s);
int store_save_counts(const store_t *s);
int store_save_cfg(const store_t *s);
int store_forget_except_seq(void);
int store_save_crashes(const store_t *s);

#endif
