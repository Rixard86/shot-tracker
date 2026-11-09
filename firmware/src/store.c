/* store.c - see store.h */
#include "store.h"

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>

LOG_MODULE_REGISTER(store, LOG_LEVEL_INF);

#define CFG_MAGIC 0x53504331u /* "SPC1" */

struct cfg_blob {
	uint32_t magic;
	sd_config_t cfg;
};

static store_t *target;

static int set_cb(const char *key, size_t len, settings_read_cb read_cb, void *cb_arg)
{
	if (!strcmp(key, "total") && len == sizeof(uint32_t)) {
		return read_cb(cb_arg, &target->total, len) < 0 ? -EIO : 0;
	}
	if (!strcmp(key, "seq") && len == sizeof(uint32_t)) {
		return read_cb(cb_arg, &target->seq, len) < 0 ? -EIO : 0;
	}
	if (!strcmp(key, "crashes") && len == sizeof(target->crashes)) {
		return read_cb(cb_arg, &target->crashes, len) < 0 ? -EIO : 0;
	}
	if (!strcmp(key, "capmode") && len == sizeof(target->capture_mode)) {
		return read_cb(cb_arg, &target->capture_mode, len) < 0 ? -EIO : 0;
	}
	if (!strcmp(key, "cfg") && len == sizeof(struct cfg_blob)) {
		struct cfg_blob b;
		if (read_cb(cb_arg, &b, len) < 0) {
			return -EIO;
		}
		if (b.magic == CFG_MAGIC) {
			target->cfg = b.cfg;
		}
		return 0;
	}
	return -ENOENT;
}

SETTINGS_STATIC_HANDLER_DEFINE(sp, "sp", NULL, set_cb, NULL, NULL);

int store_init(store_t *s)
{
	int err;

	target = s;
	err = settings_subsys_init();
	if (err) {
		LOG_ERR("settings init %d", err);
		return err;
	}
	return settings_load_subtree("sp");
}

int store_forget_except_seq(void)
{
	int err = settings_delete("sp/total");

	err = err ? err : settings_delete("sp/cfg");
	return err ? err : settings_delete("sp/capmode");
}

int store_save_counts(const store_t *s)
{
	int err = settings_save_one("sp/total", &s->total, sizeof(s->total));
	return err ? err : settings_save_one("sp/seq", &s->seq, sizeof(s->seq));
}

int store_save_cfg(const store_t *s)
{
	struct cfg_blob b = {.magic = CFG_MAGIC, .cfg = s->cfg};
	int err = settings_save_one("sp/cfg", &b, sizeof(b));

	return err ? err : settings_save_one("sp/capmode", &s->capture_mode, sizeof(s->capture_mode));
}

int store_save_crashes(const store_t *s)
{
	return settings_save_one("sp/crashes", &s->crashes, sizeof(s->crashes));
}
