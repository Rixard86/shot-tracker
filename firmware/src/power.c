/*
 * power.c - battery, charger and supply management.
 *
 * Supply: LIR1254 -> BMD-340 VCCH (= nRF52840 VDDH, high-voltage mode, REG0).
 *         REG0 output (VCC = VDD) is programmed to 3.0 V and powers the
 *         LSM6DSO32 and the LED.
 * Battery voltage: SAADC internal VDDH/5 input, no external divider.
 * Charger: MCP73831 (4.20 V). STAT is read through a diode (low = charging).
 *          The GPIO pull-up is only enabled while VBUS is present, so an
 *          unpowered charger can never leak pull-up current in standby.
 *          CHG_INHIBIT high turns on Q2, which pulls Q1's gate low and opens
 *          the PROG resistor -> charger disabled. Default (pin low / MCU
 *          dead) = charging allowed, so a flat cell can always recover.
 * Temperature: nRF die sensor. In a sealed puck with the MCU asleep >99 %
 *          of the time the die tracks the cell temperature closely.
 */
#include "power.h"

#include <hal/nrf_power.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/dt-bindings/adc/nrf-saadc-v3.h> /* NRF_SAADC_VDDHDIV5 */
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/poweroff.h>

LOG_MODULE_REGISTER(power, LOG_LEVEL_INF);

#define CHG_MIN_C 1    /* Li-ion: no charging at or below ~0 C  */
#define CHG_MAX_C 44   /* ... or above 45 C                      */
#define CHG_HYST_C 3
#define CUTOFF_MV 3300 /* protect cell (CP1654 has no internal PCM) */

static const struct device *const adc = DEVICE_DT_GET(DT_NODELABEL(adc));
static const struct device *const temp_dev = DEVICE_DT_GET(DT_NODELABEL(temp));
static const struct gpio_dt_spec chg_stat =
	GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), chg_stat_gpios);
static const struct gpio_dt_spec chg_inhibit =
	GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), chg_inhibit_gpios);

static power_state_t st;
static int16_t adc_buf;

static const struct adc_channel_cfg vddh_ch = {
	.gain = ADC_GAIN_1_2,
	.reference = ADC_REF_INTERNAL, /* 0.6 V -> 1.2 V full scale */
	.acquisition_time = ADC_ACQ_TIME(ADC_ACQ_TIME_MICROSECONDS, 10),
	.channel_id = 0,
	.input_positive = NRF_SAADC_VDDHDIV5,
};

/* Li-ion open-circuit-ish curve at light load */
static const uint16_t curve_mv[] = {3300, 3450, 3680, 3740, 3770, 3790, 3820,
				    3870, 3920, 3980, 4050, 4150};
static const uint8_t curve_pct[] = {0, 5, 10, 20, 30, 40, 50, 60, 70, 80, 90, 100};

static uint8_t mv_to_pct(uint16_t mv)
{
	const int n = ARRAY_SIZE(curve_mv);
	if (mv <= curve_mv[0]) {
		return 0;
	}
	for (int i = 1; i < n; i++) {
		if (mv <= curve_mv[i]) {
			return curve_pct[i - 1] + (uint8_t)((mv - curve_mv[i - 1]) *
				(curve_pct[i] - curve_pct[i - 1]) / (curve_mv[i] - curve_mv[i - 1]));
		}
	}
	return 100;
}

void power_regout0_3v0(void)
{
	/* In high-voltage mode REG0 defaults to 1.8 V: too low for the LED.
	 * Program UICR once, then reset (same approach as nRF52840 Dongle). */
	if ((nrf_power_mainregstatus_get(NRF_POWER) == NRF_POWER_MAINREGSTATUS_HIGH) &&
	    ((NRF_UICR->REGOUT0 & UICR_REGOUT0_VOUT_Msk) !=
	     (UICR_REGOUT0_VOUT_3V0 << UICR_REGOUT0_VOUT_Pos))) {
		NRF_NVMC->CONFIG = NVMC_CONFIG_WEN_Wen << NVMC_CONFIG_WEN_Pos;
		while (NRF_NVMC->READY == NVMC_READY_READY_Busy) {
		}
		NRF_UICR->REGOUT0 = (NRF_UICR->REGOUT0 & ~((uint32_t)UICR_REGOUT0_VOUT_Msk)) |
				    (UICR_REGOUT0_VOUT_3V0 << UICR_REGOUT0_VOUT_Pos);
		while (NRF_NVMC->READY == NVMC_READY_READY_Busy) {
		}
		NRF_NVMC->CONFIG = NVMC_CONFIG_WEN_Ren << NVMC_CONFIG_WEN_Pos;
		while (NRF_NVMC->READY == NVMC_READY_READY_Busy) {
		}
		NVIC_SystemReset();
	}
}

int power_init(void)
{
	if (!device_is_ready(adc) || !gpio_is_ready_dt(&chg_stat) ||
	    !gpio_is_ready_dt(&chg_inhibit)) {
		return -ENODEV;
	}
	gpio_pin_configure_dt(&chg_stat, GPIO_INPUT);
	gpio_pin_configure_dt(&chg_inhibit, GPIO_OUTPUT_INACTIVE);
	return adc_channel_setup(adc, &vddh_ch);
}

static int read_vbat_mv(uint16_t *mv)
{
	struct adc_sequence seq = {
		.channels = BIT(0),
		.buffer = &adc_buf,
		.buffer_size = sizeof(adc_buf),
		.resolution = 12,
		.oversampling = 4,
	};
	int err = adc_read(adc, &seq);
	if (err) {
		return err;
	}
	int32_t v = adc_buf < 0 ? 0 : adc_buf;
	*mv = (uint16_t)(v * 1200 * 5 / 4096);
	return 0;
}

static int read_temp_c(int8_t *c)
{
	struct sensor_value v;
	if (!device_is_ready(temp_dev) || sensor_sample_fetch(temp_dev) ||
	    sensor_channel_get(temp_dev, SENSOR_CHAN_DIE_TEMP, &v)) {
		return -EIO;
	}
	*c = (int8_t)v.val1;
	return 0;
}

const power_state_t *power_update(void)
{
	uint16_t mv;
	int8_t tc;

	if (read_vbat_mv(&mv) == 0) {
		st.battery_mv = mv;
		st.battery_pct = mv_to_pct(mv);
	}
	if (read_temp_c(&tc) == 0) {
		st.temp_c = tc;
	}
	st.vbus = nrf_power_usbregstatus_vbusdet_get(NRF_POWER);
	if (st.vbus) {
		gpio_pin_configure_dt(&chg_stat, GPIO_INPUT | GPIO_PULL_UP);
		k_busy_wait(50);
		st.charging = gpio_pin_get_dt(&chg_stat) == 1; /* active-low in DT */
	} else {
		gpio_pin_configure_dt(&chg_stat, GPIO_INPUT);
		st.charging = false;
	}

	/* temperature window with hysteresis */
	if (!st.chg_inhibit) {
		if (st.temp_c <= CHG_MIN_C - 1 || st.temp_c >= CHG_MAX_C + 1) {
			st.chg_inhibit = true;
		}
	} else if (st.temp_c >= CHG_MIN_C + CHG_HYST_C &&
		   st.temp_c <= CHG_MAX_C - CHG_HYST_C) {
		st.chg_inhibit = false;
	}
	gpio_pin_set_dt(&chg_inhibit, st.chg_inhibit ? 1 : 0);
	return &st;
}

const power_state_t *power_state(void)
{
	return &st;
}

bool power_should_cutoff(void)
{
	return !st.vbus && st.battery_mv > 0 && st.battery_mv < CUTOFF_MV;
}

void power_off_until_charger(void)
{
	LOG_WRN("battery %u mV: system off until charger connected", st.battery_mv);
	/* nRF52840 wakes from System OFF on VBUS detect (USBDETECTED), i.e. when
	 * the charging cable is attached. Leave the inhibit pin low so charging
	 * is allowed. */
	gpio_pin_configure_dt(&chg_stat, GPIO_INPUT);
	gpio_pin_set_dt(&chg_inhibit, 0);
	sys_poweroff();
}

bool power_vbus_present(void)
{
	return nrf_power_usbregstatus_vbusdet_get(NRF_POWER);
}
