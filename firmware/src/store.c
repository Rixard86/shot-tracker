/* store.c - see store.h */
#include "store.h"

#include <stddef.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>

LOG_MODULE_REGISTER(store, LOG_LEVEL_INF);

#define CFG_MAGIC_SLOPE_WAKE 0x53504331u /* "SPC1" */
#define CFG_MAGIC 0x53504332u
#define SLOPE_WAKE_DEFAULT_MG 150.0f

struct cfg_blob {
	uint32_t magic;
	sd_config_t cfg;
};

typedef struct {
	const char *key;
	size_t offset;
	size_t size;
} field_t;

#define FIELD(key, member) {key, offsetof(store_t, member), sizeof(((store_t *)0)->member)}

static const field_t fields[] = {
	FIELD("total", total),
	FIELD("seq", seq),
	FIELD("resetseq", count_reset_seq),
	FIELD("crashes", crashes),
	FIELD("capmode", capture_mode),
};

static store_t *target;

static sd_config_t migrate_slope_wake(const sd_config_t *old)
{
	sd_config_t cfg = *old;

	if (cfg.wake_mg == SLOPE_WAKE_DEFAULT_MG) {
		sd_config_t defaults;

		sd_default_config(&defaults);
		cfg.wake_mg = defaults.wake_mg;
	}
	return cfg;
}

static int set_cb(const char *key, size_t len, settings_read_cb read_cb, void *cb_arg)
{
	for (size_t i = 0; i < ARRAY_SIZE(fields); i++) {
		if (!strcmp(key, fields[i].key) && len == fields[i].size) {
			return read_cb(cb_arg, (uint8_t *)target + fields[i].offset, len) < 0 ? -EIO : 0;
		}
	}
	if (!strcmp(key, "cfg") && len == sizeof(struct cfg_blob)) {
		struct cfg_blob b;
		if (read_cb(cb_arg, &b, len) < 0) {
			return -EIO;
		}
		if (b.magic == CFG_MAGIC) {
			target->cfg = b.cfg;
		} else if (b.magic == CFG_MAGIC_SLOPE_WAKE) {
			target->cfg = migrate_slope_wake(&b.cfg);
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

int store_save_count_reset(const store_t *s)
{
	int err = store_save_counts(s);

	return err ? err : settings_save_one("sp/resetseq", &s->count_reset_seq,
					     sizeof(s->count_reset_seq));
}
