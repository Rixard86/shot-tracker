#include "accel.h"

#include <string.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>

#include "accel_regs.h"
#include "capture.h"
#include "shot_detect.h"

LOG_MODULE_REGISTER(accel, LOG_LEVEL_INF);

#define FIFO_BURST_WORDS 32
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

static int probe(void)
{
	uint8_t id = 0;
	int err = rd(REG_WHO_AM_I, &id);

	if (err || id != WHO_AM_I_VAL) {
		LOG_ERR("LSM6DSV32X not found (err %d id 0x%02x)", err, id);
		return -ENODEV;
	}
	return 0;
}

static int configure(void)
{
	int err = wr(REG_CTRL3, CTRL3_SW_RESET);

	k_msleep(RESET_WAIT_MS);
	err = err ? err : wr(REG_CTRL3, CTRL3_BDU | CTRL3_IF_INC);
	err = err ? err : wr(REG_IF_CFG, IF_CFG_ANTISPIKE_ALWAYS_ON);
	err = err ? err : wr(REG_CTRL8, CTRL8_RESERVED_SET | FS_XL_32G);
	return err ? err : wr(REG_CTRL6, FS_G_2000DPS);
}

int accel_init(accel_irq_handler_t handler)
{
	if (!device_is_ready(i2c) || !gpio_is_ready_dt(&int1)) {
		return -ENODEV;
	}
	if (probe() != 0) {
		(void)i2c_recover_bus(i2c);
		if (probe() != 0) {
			return -ENODEV;
		}
	}
	int err = configure();
	if (err) {
		return err;
	}

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

int accel_set_idle(uint16_t wake_mg)
{
	uint8_t dummy;

	irq_pause();
	wr(REG_MD1_CFG, REG_OFF);
	wr(REG_INT1_CTRL, REG_OFF);
	wr(REG_FIFO_CTRL4, FIFO_MODE_BYPASS);
	wr(REG_CTRL2, REG_OFF);
	wr(REG_CTRL1, OP_MODE_XL_LOW_POWER_2_AVG | ODR_15HZ);
	wr(REG_TAP_CFG0, TAP_CFG0_LATCHED);
	wr(REG_FUNCTIONS_ENABLE, FUNCTIONS_INTERRUPTS_ENABLE);
	wr(REG_INACTIVITY_DUR, WAKE_THS_WEIGHT_31MG | INACT_ODR_DEFAULT);
	wr(REG_WAKE_UP_THS, sd_wake_ths_lsb((float)wake_mg));
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
	wr(REG_FUNCTIONS_ENABLE, REG_OFF);
	wr(REG_FIFO_CTRL4, FIFO_MODE_BYPASS);
	wr(REG_FIFO_CTRL1, FIFO_WTM_WORDS);
	wr(REG_FIFO_CTRL2, REG_OFF);
	wr(REG_FIFO_CTRL3, FIFO_BDR_XL_GY_480HZ);
	wr(REG_CTRL1, OP_MODE_HIGH_PERF | ODR_480HZ);
	wr(REG_CTRL2, OP_MODE_HIGH_PERF | ODR_480HZ);
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
	wr(REG_CTRL1, REG_OFF);
	wr(REG_CTRL2, REG_OFF);
	cur_mode = ACCEL_OFF;
	return 0;
}

accel_mode_t accel_mode(void)
{
	return cur_mode;
}

bool accel_idle_ok(void)
{
	uint8_t ctrl1 = 0;
	uint8_t md1 = 0;

	if (rd(REG_CTRL1, &ctrl1) || rd(REG_MD1_CFG, &md1)) {
		return false;
	}
	return ctrl1 == (OP_MODE_XL_LOW_POWER_2_AVG | ODR_15HZ) && md1 == MD1_INT1_WU;
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

static uint8_t fifo_burst[FIFO_BURST_WORDS * FIFO_WORD_BYTES];

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

static int take_word(const uint8_t *word, sp_cap_sample_t *slot)
{
	uint8_t tag = word[0] >> FIFO_TAG_SHIFT;

	if (tag == FIFO_TAG_GYRO) {
		unpack_axes(&word[1], last_gyro);
	} else if (tag == FIFO_TAG_ACC) {
		store_acc(slot, &word[1]);
		return 1;
	}
	return 0;
}

int accel_read_fifo(accel_fifo_t *fifo)
{
	int words = fifo_level(&fifo->overrun);
	int n = 0;

	if (words < 0) {
		return words;
	}
	while (words > 0 && n < fifo->max) {
		int count = MIN(MIN(words, fifo->max - n), FIFO_BURST_WORDS);

		if (i2c_burst_read(i2c, I2C_ADDR, REG_FIFO_DATA_OUT_TAG, fifo_burst,
				   (uint32_t)count * FIFO_WORD_BYTES)) {
			return -EIO;
		}
		for (int w = 0; w < count; w++) {
			n += take_word(&fifo_burst[w * FIFO_WORD_BYTES], &fifo->samples[n]);
		}
		words -= count;
	}
	return n;
}

void accel_to_mg(const sp_cap_sample_t *raw, int16_t mg[3])
{
	for (int i = 0; i < AXES; i++) {
		mg[i] = (int16_t)(((int32_t)raw->acc[i] * (int32_t)CAP_ACC_UG_PER_LSB) / UG_PER_MG);
	}
}
