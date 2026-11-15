#include "supply.h"

#include <hal/nrf_power.h>

#define UNUSED_RAM_BLOCK 8u
#define UNUSED_RAM_SECTIONS                                                                        \
	(NRF_POWER_RAMPOWER_S2POWER_MASK | NRF_POWER_RAMPOWER_S3POWER_MASK |                       \
	 NRF_POWER_RAMPOWER_S4POWER_MASK | NRF_POWER_RAMPOWER_S5POWER_MASK)

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
