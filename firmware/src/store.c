/* store.c - see store.h */
#include "store.h"

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>

LOG_MODULE_REGISTER(store, LOG_LEVEL_INF);

#define SEQ_MARGIN 1024u /* events between seq saves < this */
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
	err = settings_load_subtree("sp");
	s->seq += SEQ_MARGIN;
	store_save_counts(s);
	return err;
}

int store_save_counts(const store_t *s)
{
	int err = settings_save_one("sp/total", &s->total, sizeof(s->total));
	return err ? err : settings_save_one("sp/seq", &s->seq, sizeof(s->seq));
}

int store_save_cfg(const store_t *s)
{
	struct cfg_blob b = {.magic = CFG_MAGIC, .cfg = s->cfg};
	return settings_save_one("sp/cfg", &b, sizeof(b));
}
