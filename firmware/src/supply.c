#include "supply.h"

#include <hal/nrf_power.h>
#include <zephyr/toolchain.h>

#define UNUSED_RAM_BLOCK 8u
#define HISTORY_RAM_SECTION NRF_POWER_RAMPOWER_S2POWER_MASK
#define HISTORY_RAM_ADDR 0x20020000u
#define HISTORY_RAM_BYTES 0x8000u
#define UNUSED_RAM_SECTIONS                                                                        \
	(NRF_POWER_RAMPOWER_S3POWER_MASK | NRF_POWER_RAMPOWER_S4POWER_MASK | NRF_POWER_RAMPOWER_S5POWER_MASK)

BUILD_ASSERT(CAP_HISTORY_SAMPLES * sizeof(sp_cap_sample_t) <= HISTORY_RAM_BYTES,
	     "the 60 Hz history must fit RAM8 section 2");

sp_cap_sample_t *supply_history_ram_on(void)
{
	nrf_power_rampower_mask_on(NRF_POWER, UNUSED_RAM_BLOCK, HISTORY_RAM_SECTION);
	(void)nrf_power_rampower_mask_get(NRF_POWER, UNUSED_RAM_BLOCK);
	return (sp_cap_sample_t *)HISTORY_RAM_ADDR;
}

void supply_history_ram_off(void)
{
	nrf_power_rampower_mask_off(NRF_POWER, UNUSED_RAM_BLOCK, HISTORY_RAM_SECTION);
}

void supply_unused_ram_off(void)
{
	nrf_power_rampower_mask_off(NRF_POWER, UNUSED_RAM_BLOCK, UNUSED_RAM_SECTIONS);
}

void supply_reg0_set_ldo(bool ldo)
{
	nrf_power_dcdcen_vddh_set(NRF_POWER, !ldo);
}

bool supply_reg0_ldo(void)
{
	return !nrf_power_dcdcen_vddh_get(NRF_POWER);
}
