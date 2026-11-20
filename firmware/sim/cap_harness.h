#ifndef CAP_HARNESS_H
#define CAP_HARNESS_H

#include "../src/capaim.h"
#include "../src/capwriter.h"

#define HARNESS_FLASH_BYTES 0x66000u
#define HARNESS_SECTOR_BYTES 4096u
#define HARNESS_SHOT_SPACING 9600u
#define HARNESS_OFFSET_X_MDPS (-18)
#define HARNESS_OFFSET_Y_MDPS (-181)
#define HARNESS_OFFSET_Z_MDPS (-15)
#define HARNESS_OFFSET_AGE_S 42u
#define HARNESS_OFFSET {{HARNESS_OFFSET_X_MDPS, HARNESS_OFFSET_Y_MDPS, HARNESS_OFFSET_Z_MDPS}, HARNESS_OFFSET_AGE_S}

typedef enum { HARNESS_BOW, HARNESS_STRESS } harness_signal_t;

typedef struct {
	uint32_t seq;
	uint32_t first;
	uint32_t slow;
	uint32_t aim;
} harness_want_t;

void harness_setup(harness_signal_t signal);
sp_cap_sample_t harness_signal_at(uint32_t index);
void harness_run_to(uint32_t count);
void harness_push(uint32_t samples);
void harness_request(uint32_t trigger, uint8_t mode);
int harness_pump(void);
const capstore_entry_t *harness_shoot(uint32_t trigger, uint8_t mode);
const capstore_entry_t *harness_record(uint32_t samples);
void harness_wake(void);
int harness_reboot(void);
uint32_t harness_next_seq(void);
capstore_t *harness_store(void);
harness_want_t harness_want(const capstore_entry_t *e, uint32_t first);
int harness_matches(const harness_want_t *want);

#endif
