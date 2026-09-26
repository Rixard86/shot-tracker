/*
 * accel.c - minimal LIS2DE12 driver (I2C, SA0 = GND -> 0x18).
 *
 * IDLE:   10 Hz, +-2 g, high-pass filtered wake-up interrupt on INT1
 *         (latched), ~3 uA. Equivalent of sd_idle_sample() in simulation.
 * ACTIVE: 400 Hz, +-16 g (187.5 mg/LSB, 8-bit), FIFO stream mode,
 *         watermark interrupt on INT1.
 *
 * Register values follow the LIS2DE12 datasheet (DocID 029222). The LIS2DE12
 * is register-compatible with the LIS2DH12 for everything used here.
 */
#include "accel.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(accel, LOG_LEVEL_INF);

#define ADDR 0x18

#define R_WHO_AM_I 0x0F
#define WHO_AM_I_VAL 0x33
#define R_CTRL1 0x20
#define R_CTRL2 0x21
#define R_CTRL3 0x22
#define R_CTRL4 0x23
#define R_CTRL5 0x24
#define R_CTRL6 0x25
#define R_REFERENCE 0x26
#define R_OUT_X_L 0x28
#define R_FIFO_CTRL 0x2E
#define R_FIFO_SRC 0x2F
#define R_INT1_CFG 0x30
#define R_INT1_SRC 0x31
#define R_INT1_THS 0x32
#define R_INT1_DUR 0x33
#define AUTO_INC 0x80

#define FIFO_WTM 25 /* 62.5 ms of samples per interrupt at 400 Hz */

static const struct device *const i2c = DEVICE_DT_GET(DT_NODELABEL(i2c0));
static const struct gpio_dt_spec int1 =
	GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), accel_int1_gpios);
static struct gpio_callback int1_cb;
static accel_irq_handler_t user_handler;
static accel_mode_t cur_mode = ACCEL_OFF;

static int wr(uint8_t reg, uint8_t val)
{
	return i2c_reg_write_byte(i2c, ADDR, reg, val);
}

static int rd(uint8_t reg, uint8_t *val)
{
	return i2c_reg_read_byte(i2c, ADDR, reg, val);
}

static void int1_isr(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(cb);
	ARG_UNUSED(pins);
	/* level-triggered: mask until the handler's work item has serviced the
	 * sensor and calls accel_irq_rearm() */
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
	err = rd(R_WHO_AM_I, &id);
	if (err || id != WHO_AM_I_VAL) {
		LOG_ERR("LIS2DE12 not found (err %d id 0x%02x)", err, id);
		return -ENODEV;
	}
	/* reboot memory content, then power down */
	wr(R_CTRL5, 0x80);
	k_msleep(5);
	wr(R_CTRL1, 0x08); /* power-down, LPen */

	user_handler = handler;
	gpio_pin_configure_dt(&int1, GPIO_INPUT);
	gpio_init_callback(&int1_cb, int1_isr, BIT(int1.pin));
	gpio_add_callback(int1.port, &int1_cb);
	/* level interrupt: never miss a latched wake or a full FIFO */
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
	uint16_t ths = (wake_mg + 15) / 16; /* 16 mg/LSB at +-2 g */

	irq_pause();
	if (ths < 1) {
		ths = 1;
	}
	if (ths > 127) {
		ths = 127;
	}
	wr(R_CTRL1, 0x08);           /* power-down while reconfiguring */
	wr(R_FIFO_CTRL, 0x00);       /* bypass: clears FIFO            */
	wr(R_CTRL3, 0x00);
	wr(R_CTRL2, 0x01);           /* HP normal mode, HP on INT1     */
	wr(R_CTRL4, 0x00);           /* +-2 g                          */
	wr(R_CTRL5, 0x08);           /* latch INT1                     */
	wr(R_INT1_THS, (uint8_t)ths);
	wr(R_INT1_DUR, 0x00);
	wr(R_INT1_CFG, 0x2A);        /* OR of X/Y/Z high events        */
	wr(R_CTRL1, 0x2F);           /* 10 Hz, low-power, XYZ          */
	k_msleep(110);               /* let HP filter settle (~1 sample) */
	rd(R_REFERENCE, &dummy);     /* reset HP filter reference      */
	rd(R_INT1_SRC, &dummy);      /* clear any latched event        */
	wr(R_CTRL3, 0x40);           /* IA1 -> INT1                    */
	cur_mode = ACCEL_IDLE;
	accel_irq_rearm();
	return 0;
}

int accel_set_active(void)
{
	uint8_t dummy;

	irq_pause();
	wr(R_CTRL3, 0x00);
	wr(R_INT1_CFG, 0x00);
	rd(R_INT1_SRC, &dummy);
	wr(R_CTRL2, 0x00);           /* no HP filtering on data        */
	wr(R_CTRL4, 0x30);           /* +-16 g                         */
	wr(R_CTRL5, 0x40);           /* FIFO enable                    */
	wr(R_FIFO_CTRL, 0x00);       /* bypass first to reset FIFO     */
	wr(R_FIFO_CTRL, 0x80 | FIFO_WTM); /* stream mode + watermark   */
	wr(R_CTRL1, 0x7F);           /* 400 Hz, low-power, XYZ         */
	wr(R_CTRL3, 0x04);           /* FIFO watermark -> INT1         */
	cur_mode = ACCEL_ACTIVE;
	accel_irq_rearm();
	return 0;
}

int accel_off(void)
{
	irq_pause();
	wr(R_CTRL3, 0x00);
	wr(R_CTRL1, 0x08);
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
	int err = rd(R_INT1_SRC, &src);
	return err ? err : (src & 0x40) ? 1 : 0; /* IA bit */
}

int accel_read_fifo(int16_t (*out)[3], int max, int *overrun)
{
	uint8_t src = 0;
	int n;

	if (rd(R_FIFO_SRC, &src)) {
		return -EIO;
	}
	*overrun = (src & 0x40) ? 1 : 0;
	n = src & 0x1F;
	if ((src & 0x20) && n == 0) {
		n = 0; /* empty */
	}
	if (n > max) {
		n = max;
	}
	for (int i = 0; i < n; i++) {
		uint8_t b[6];
		if (i2c_burst_read(i2c, ADDR, R_OUT_X_L | AUTO_INC, b, sizeof(b))) {
			return -EIO;
		}
		/* 8-bit data lives in the high bytes (LIS2DE12) */
		out[i][0] = (int16_t)((int8_t)b[1] * 1875 / 10);
		out[i][1] = (int16_t)((int8_t)b[3] * 1875 / 10);
		out[i][2] = (int16_t)((int8_t)b[5] * 1875 / 10);
	}
	return n;
}
