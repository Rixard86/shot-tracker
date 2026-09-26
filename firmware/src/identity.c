#include "identity.h"

#include <string.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#define DEVICE_ID_BYTES 8
#define SERIAL_CHARS (DEVICE_ID_BYTES * 2)
#define NAME_SUFFIX_CHARS 4
#define NAME_MAX_CHARS 24
#define DIS_SERIAL_KEY "bt/dis/serial"

static char serial[SERIAL_CHARS + 1];

static void hex_to_upper(char *s)
{
	for (; *s; s++) {
		if (*s >= 'a' && *s <= 'f') {
			*s = (char)(*s - 'a' + 'A');
		}
	}
}

int identity_apply(void)
{
	uint8_t id[DEVICE_ID_BYTES] = {0};
	char name[NAME_MAX_CHARS];
	ssize_t got = hwinfo_get_device_id(id, sizeof(id));

	if (got < 0) {
		return (int)got;
	}
	bin2hex(id, sizeof(id), serial, sizeof(serial));
	hex_to_upper(serial);
	snprintk(name, sizeof(name), "%s-%.*s", CONFIG_BT_DEVICE_NAME, NAME_SUFFIX_CHARS,
		 &serial[SERIAL_CHARS - NAME_SUFFIX_CHARS]);
	int err = bt_set_name(name);

	return err ? err : settings_runtime_set(DIS_SERIAL_KEY, serial, strlen(serial));
}
