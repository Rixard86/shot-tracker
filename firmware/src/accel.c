#include "accel.h"

#include <string.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>

#include "capture.h"

LOG_MODULE_REGISTER(accel, LOG_LEVEL_INF);

#define I2C_ADDR 0x6A
#define WHO_AM_I_VAL 0x6C

#define REG_FIFO_CTRL1 0x07
#define REG_FIFO_CTRL2 0x08
#define REG_FIFO_CTRL3 0x09
#define REG_FIFO_CTRL4 0x0A
#define REG_INT1_CTRL 0x0D
#define REG_WHO_AM_I 0x0F
#define REG_CTRL1_XL 0x10
#define REG_CTRL2_G 0x11
#define REG_CTRL3_C 0x12
#define REG_CTRL6_C 0x15
#define REG_CTRL7_G 0x16
#define REG_CTRL9_XL 0x18
#define REG_WAKE_UP_SRC 0x1B
#define REG_FIFO_STATUS1 0x3A
#define REG_TAP_CFG0 0x56
#define REG_TAP_CFG2 0x58
#define REG_WAKE_UP_THS 0x5B
#define REG_WAKE_UP_DUR 0x5C
#define REG_MD1_CFG 0x5E
#define REG_FIFO_DATA_OUT_TAG 0x78

#define REG_OFF 0x00
#define CTRL3_SW_RESET 0x01
#define CTRL3_IF_INC 0x04
#define CTRL3_BDU 0x40
#define CTRL9_DEN_DEFAULTS 0xE0
#define CTRL9_I3C_DISABLE 0x02
#define ODR_12HZ5 0x10
#define ODR_417HZ 0x60
#define FS_XL_8G 0x08
#define FS_XL_32G 0x04
#define FS_G_2000DPS 0x0C
#define CTRL6_XL_LOW_POWER 0x10
#define TAP_CFG0_LATCHED 0x01
#define TAP_CFG0_CLEAR_ON_READ 0x40
#define TAP_CFG2_INTERRUPTS_ENABLE 0x80
#define WAKE_DUR_THS_FS_DIV_256 0x10
#define WAKE_THS_MAX 63u
#define WAKE_THS_MG_X100 3125u
#define PERCENT 100u
#define MD1_INT1_WU 0x20
#define WAKE_UP_SRC_WU_IA 0x08
#define INT1_FIFO_TH 0x08
#define FIFO_MODE_BYPASS 0x00
#define FIFO_MODE_STREAM 0x06
#define FIFO_BDR_XL_GY_417HZ 0x66
#define FIFO_WTM_WORDS 52
#define FIFO_STATUS2_DIFF_HI 0x03
#define FIFO_STATUS2_OVERRUN 0x40
#define FIFO_WORD_BYTES 7
#define FIFO_TAG_SHIFT 3
#define FIFO_TAG_GYRO 0x01
#define FIFO_TAG_ACC 0x02
#define AXES 3
#define AXIS_BYTES 2
#define RESET_WAIT_MS 5
#define IDLE_SETTLE_MS 200
#define UG_PER_MG 1000

static const struct device *const i2c = DEVICE_DT_GET(DT_NODELABEL(i2c0));
static const struct gpio_dt_spec int1 =
	GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), accel_int1_gpios);
static struct gpio_callback int1_cb;
static accel_irq_handler_t user_handler;
static accel_mode_t cur_mode = ACCEL_OFF;
static int16_t last_gyro[AXES];

static int wr(uint8_t reg, uint8_t val)
{
	return i2c_reg_write_byte(i2c, I2C_ADDR, reg, val);
}

static int rd(uint8_t reg, uint8_t *val)
{
	return i2c_reg_read_byte(i2c, I2C_ADDR, reg, val);
}

static void int1_isr(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(cb);
	ARG_UNUSED(pins);
	gpio_pin_interrupt_configure_dt(&int1, GPIO_INT_DISABLE);
	if (user_handler) {
		user_handler();
	}
}

int accel_init(accel_irq_handler_t handler)
{
	uint8_t id = 0;
	int err;

	if (!device_is_ready(i2c) || !gpio_is_ready_dt(&int1)) {
		return -ENODEV;
	}
	err = rd(REG_WHO_AM_I, &id);
	if (err || id != WHO_AM_I_VAL) {
		LOG_ERR("LSM6DSO32 not found (err %d id 0x%02x)", err, id);
		return -ENODEV;
	}
	wr(REG_CTRL3_C, CTRL3_SW_RESET);
	k_msleep(RESET_WAIT_MS);
	wr(REG_CTRL3_C, CTRL3_BDU | CTRL3_IF_INC);
	wr(REG_CTRL9_XL, CTRL9_DEN_DEFAULTS | CTRL9_I3C_DISABLE);

	user_handler = handler;
	gpio_pin_configure_dt(&int1, GPIO_INPUT);
	gpio_init_callback(&int1_cb, int1_isr, BIT(int1.pin));
	gpio_add_callback(int1.port, &int1_cb);
	gpio_pin_interrupt_configure_dt(&int1, GPIO_INT_LEVEL_ACTIVE);
	return 0;
}

static void irq_pause(void)
{
	gpio_pin_interrupt_configure_dt(&int1, GPIO_INT_DISABLE);
}

void accel_irq_rearm(void)
{
	gpio_pin_interrupt_configure_dt(&int1, GPIO_INT_LEVEL_ACTIVE);
}

static uint8_t wake_threshold(uint16_t wake_mg)
{
	uint32_t ths = ((uint32_t)wake_mg * PERCENT + WAKE_THS_MG_X100 - 1u) / WAKE_THS_MG_X100;

	return (uint8_t)CLAMP(ths, 1u, WAKE_THS_MAX);
}

int accel_set_idle(uint16_t wake_mg)
{
	uint8_t dummy;

	irq_pause();
	wr(REG_MD1_CFG, REG_OFF);
	wr(REG_INT1_CTRL, REG_OFF);
	wr(REG_FIFO_CTRL4, FIFO_MODE_BYPASS);
	wr(REG_CTRL2_G, REG_OFF);
	wr(REG_CTRL6_C, CTRL6_XL_LOW_POWER);
	wr(REG_CTRL1_XL, ODR_12HZ5 | FS_XL_8G);
	wr(REG_TAP_CFG0, TAP_CFG0_LATCHED | TAP_CFG0_CLEAR_ON_READ);
	wr(REG_TAP_CFG2, TAP_CFG2_INTERRUPTS_ENABLE);
	wr(REG_WAKE_UP_DUR, WAKE_DUR_THS_FS_DIV_256);
	wr(REG_WAKE_UP_THS, wake_threshold(wake_mg));
	k_msleep(IDLE_SETTLE_MS);
	rd(REG_WAKE_UP_SRC, &dummy);
	wr(REG_MD1_CFG, MD1_INT1_WU);
	cur_mode = ACCEL_IDLE;
	accel_irq_rearm();
	return 0;
}

int accel_set_active(void)
{
	uint8_t dummy;

	irq_pause();
	wr(REG_MD1_CFG, REG_OFF);
	rd(REG_WAKE_UP_SRC, &dummy);
	wr(REG_TAP_CFG2, REG_OFF);
	wr(REG_CTRL6_C, REG_OFF);
	wr(REG_CTRL7_G, REG_OFF);
	wr(REG_FIFO_CTRL4, FIFO_MODE_BYPASS);
	wr(REG_FIFO_CTRL1, FIFO_WTM_WORDS);
	wr(REG_FIFO_CTRL2, REG_OFF);
	wr(REG_FIFO_CTRL3, FIFO_BDR_XL_GY_417HZ);
	wr(REG_CTRL1_XL, ODR_417HZ | FS_XL_32G);
	wr(REG_CTRL2_G, ODR_417HZ | FS_G_2000DPS);
	wr(REG_FIFO_CTRL4, FIFO_MODE_STREAM);
	wr(REG_INT1_CTRL, INT1_FIFO_TH);
	memset(last_gyro, 0, sizeof(last_gyro));
	cur_mode = ACCEL_ACTIVE;
	accel_irq_rearm();
	return 0;
}

int accel_off(void)
{
	irq_pause();
	wr(REG_MD1_CFG, REG_OFF);
	wr(REG_INT1_CTRL, REG_OFF);
	wr(REG_FIFO_CTRL4, FIFO_MODE_BYPASS);
	wr(REG_CTRL1_XL, REG_OFF);
	wr(REG_CTRL2_G, REG_OFF);
	cur_mode = ACCEL_OFF;
	return 0;
}

accel_mode_t accel_mode(void)
{
	return cur_mode;
}

int accel_clear_wake(void)
{
	uint8_t src = 0;
	int err = rd(REG_WAKE_UP_SRC, &src);

	return err ? err : (src & WAKE_UP_SRC_WU_IA) ? 1 : 0;
}

static int fifo_level(int *overrun)
{
	uint8_t st[2];

	if (i2c_burst_read(i2c, I2C_ADDR, REG_FIFO_STATUS1, st, sizeof(st))) {
		return -EIO;
	}
	*overrun = (st[1] & FIFO_STATUS2_OVERRUN) ? 1 : 0;
	return st[0] | ((st[1] & FIFO_STATUS2_DIFF_HI) << BITS_PER_BYTE);
}

static void unpack_axes(const uint8_t *b, int16_t out[AXES])
{
	for (int i = 0; i < AXES; i++) {
		out[i] = (int16_t)sys_get_le16(&b[i * AXIS_BYTES]);
	}
}

static void store_acc(sp_cap_sample_t *s, const uint8_t *b)
{
	int16_t acc[AXES];

	unpack_axes(b, acc);
	for (int i = 0; i < AXES; i++) {
		s->acc[i] = acc[i];
		s->gyro[i] = last_gyro[i];
	}
}

int accel_read_fifo(accel_fifo_t *fifo)
{
	int words = fifo_level(&fifo->overrun);
	int n = 0;

	for (int w = 0; w < words && n < fifo->max; w++) {
		uint8_t b[FIFO_WORD_BYTES];
		if (i2c_burst_read(i2c, I2C_ADDR, REG_FIFO_DATA_OUT_TAG, b, sizeof(b))) {
			return -EIO;
		}
		uint8_t tag = b[0] >> FIFO_TAG_SHIFT;
		if (tag == FIFO_TAG_GYRO) {
			unpack_axes(&b[1], last_gyro);
		} else if (tag == FIFO_TAG_ACC) {
			store_acc(&fifo->samples[n++], &b[1]);
		}
	}
	return words < 0 ? words : n;
}

void accel_to_mg(const sp_cap_sample_t *raw, int16_t mg[3])
{
	for (int i = 0; i < AXES; i++) {
		mg[i] = (int16_t)(((int32_t)raw->acc[i] * (int32_t)CAP_ACC_UG_PER_LSB) / UG_PER_MG);
	}
}
