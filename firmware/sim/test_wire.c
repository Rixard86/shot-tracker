#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "../src/wire.h"

#define SET_TIME_UNIX_MS 1700000000000LL
#define SET_TIME_UPTIME_MS 5000LL
#define LATER_MS 1000LL
#define LONG_UPTIME_MS 5000000000LL
#define RESYNC_SHIFT_MS 250LL
#define V3_BYTES ((uint16_t)sizeof(sp_config_t))

typedef struct {
	const char *what;
	size_t offset;
	uint32_t value;
	bool valid;
} bound_t;

#define FIELD(name) offsetof(sp_config_t, name)

static const bound_t bounds[] = {
	{"trig below min", FIELD(trig_hf_mg), WIRE_TRIG_MIN_MG - 1u, false},
	{"trig at min", FIELD(trig_hf_mg), WIRE_TRIG_MIN_MG, true},
	{"trig at max", FIELD(trig_hf_mg), WIRE_TRIG_MAX_MG, true},
	{"trig above max", FIELD(trig_hf_mg), WIRE_TRIG_MAX_MG + 1u, false},
	{"move below min", FIELD(move_mg), WIRE_MOVE_MIN_MG - 1u, false},
	{"move at max", FIELD(move_mg), WIRE_MOVE_MAX_MG, true},
	{"move above max", FIELD(move_mg), WIRE_MOVE_MAX_MG + 1u, false},
	{"pre-still at max", FIELD(pre_still_min_ms), WIRE_PRE_STILL_MAX_MS, true},
	{"pre-still above max", FIELD(pre_still_min_ms), WIRE_PRE_STILL_MAX_MS + 1u, false},
	{"post end at max", FIELD(post_end_ms), WIRE_POST_END_MAX_MS, true},
	{"post end above max", FIELD(post_end_ms), WIRE_POST_END_MAX_MS + 1u, false},
	{"post lf at max", FIELD(post_lf_min_mg), WIRE_POST_LF_MAX_MG, true},
	{"post lf above max", FIELD(post_lf_min_mg), WIRE_POST_LF_MAX_MG + 1u, false},
	{"refractory below min", FIELD(refractory_ms), WIRE_REFRACTORY_MIN_MS - 1u, false},
	{"refractory at max", FIELD(refractory_ms), WIRE_REFRACTORY_MAX_MS, true},
	{"refractory above max", FIELD(refractory_ms), WIRE_REFRACTORY_MAX_MS + 1u, false},
	{"wake below min", FIELD(wake_mg), WIRE_WAKE_MIN_MG - 1u, false},
	{"wake at min", FIELD(wake_mg), WIRE_WAKE_MIN_MG, true},
	{"wake above max", FIELD(wake_mg), WIRE_WAKE_MAX_MG + 1u, false},
	{"idle below min", FIELD(idle_timeout_s), WIRE_IDLE_MIN_S - 1u, false},
	{"idle at max", FIELD(idle_timeout_s), WIRE_IDLE_MAX_S, true},
	{"idle above max", FIELD(idle_timeout_s), WIRE_IDLE_MAX_S + 1u, false},
};

static int failures;

static void check(int ok, const char *what)
{
	if (!ok) {
		printf("FAIL: %s\n", what);
		failures++;
	}
}

static store_t default_store(void)
{
	store_t s = {.capture_mode = SP_CAPTURE_STANDARD};

	sd_default_config(&s.cfg);
	return s;
}

static sp_config_t default_wire(void)
{
	store_t s = default_store();
	sp_config_t w;

	wire_config_out(&s, &w);
	return w;
}

static void set_field(sp_config_t *w, const bound_t *b)
{
	uint16_t v = (uint16_t)b->value;

	memcpy((uint8_t *)w + b->offset, &v, sizeof(v));
}

static void test_bounds(void)
{
	sp_config_t w = default_wire();

	check(wire_config_valid(&w), "defaults are valid");
	for (size_t i = 0; i < sizeof(bounds) / sizeof(bounds[0]); i++) {
		sp_config_t edited = w;

		set_field(&edited, &bounds[i]);
		check(wire_config_valid(&edited) == bounds[i].valid, bounds[i].what);
	}
	sp_config_t window = w;

	window.post_start_ms = window.post_end_ms;
	check(!wire_config_valid(&window), "post window must not be empty");
	window.capture_mode = SP_CAPTURE_EXTENDED;
	window.post_start_ms = (uint16_t)(window.post_end_ms - 1u);
	check(wire_config_valid(&window), "extended mode, shortest post window");
	window.capture_mode = SP_CAPTURE_EXTENDED + 1u;
	check(!wire_config_valid(&window), "unknown capture mode");
}

static void test_round_trip(void)
{
	sp_config_t w = default_wire();
	store_t s = default_store();
	sp_config_t back;

	w.trig_hf_mg = WIRE_TRIG_MAX_MG;
	w.idle_timeout_s = WIRE_IDLE_MAX_S;
	w.capture_mode = SP_CAPTURE_EXTENDED;
	wire_config_in(&w, &s);
	wire_config_out(&s, &back);
	check(memcmp(&w, &back, sizeof(w)) == 0, "wire -> store -> wire keeps every field");
	check(back.version == PROTO_VERSION, "outgoing config carries the current version");
}

static void test_parse_versions(void)
{
	sp_config_t current = default_wire();
	sp_config_t incoming = current;
	wire_bytes_t v3 = {.data = &incoming, .len = V3_BYTES};
	wire_bytes_t v2 = {.data = &incoming, .len = SP_CONFIG_V2_BYTES};

	current.capture_mode = SP_CAPTURE_EXTENDED;
	incoming.capture_mode = SP_CAPTURE_STANDARD;
	incoming.trig_hf_mg = WIRE_TRIG_MAX_MG;
	sp_config_t cfg = current;
	check(wire_config_parse(&cfg, &v3) == WIRE_CFG_OK, "v3 write accepted");
	check(cfg.trig_hf_mg == WIRE_TRIG_MAX_MG && cfg.capture_mode == SP_CAPTURE_STANDARD,
	      "v3 write sets every field");

	incoming.version = PROTO_VERSION_V2;
	cfg = current;
	check(wire_config_parse(&cfg, &v2) == WIRE_CFG_OK, "v2 write accepted");
	check(cfg.version == PROTO_VERSION, "v2 write is stored as the current version");
	check(cfg.trig_hf_mg == WIRE_TRIG_MAX_MG, "v2 write sets its fields");
	check(cfg.capture_mode == SP_CAPTURE_EXTENDED, "v2 write keeps the capture mode");
}

static void test_parse_rejects(void)
{
	sp_config_t current = default_wire();
	sp_config_t incoming = current;
	sp_config_t cfg = current;
	wire_bytes_t v3 = {.data = &incoming, .len = V3_BYTES};
	wire_bytes_t v2 = {.data = &incoming, .len = SP_CONFIG_V2_BYTES};
	wire_bytes_t short_write = {.data = &incoming, .len = SP_CONFIG_V2_BYTES - 1u};
	wire_bytes_t long_write = {.data = &incoming, .len = V3_BYTES + 1u};

	incoming.version = PROTO_VERSION_V2;
	check(wire_config_parse(&cfg, &v3) == WIRE_CFG_BAD_VALUE, "20 bytes must be v3");
	incoming.version = PROTO_VERSION;
	check(wire_config_parse(&cfg, &v2) == WIRE_CFG_BAD_VALUE, "19 bytes must be v2");
	check(wire_config_parse(&cfg, &short_write) == WIRE_CFG_BAD_LENGTH, "too short");
	check(wire_config_parse(&cfg, &long_write) == WIRE_CFG_BAD_LENGTH, "too long");
	incoming.wake_mg = WIRE_WAKE_MAX_MG + 1u;
	check(wire_config_parse(&cfg, &v3) == WIRE_CFG_BAD_VALUE, "out-of-range value");
	check(memcmp(&cfg, &current, sizeof(cfg)) == 0, "a refused write changes nothing");
}

static bool same_config(const store_t *a, const store_t *b)
{
	sp_config_t wa;
	sp_config_t wb;

	wire_config_out(a, &wa);
	wire_config_out(b, &wb);
	return memcmp(&wa, &wb, sizeof(wa)) == 0 && a->cfg.fs_hz == b->cfg.fs_hz;
}

static void test_normalize(void)
{
	store_t s = default_store();
	store_t before = s;

	check(wire_config_normalize(&s), "stored defaults are valid");
	check(same_config(&s, &before), "normalising valid defaults changes nothing");
	s.cfg.trig_hf_mg = (float)(WIRE_TRIG_MIN_MG - 1u);
	check(!wire_config_normalize(&s), "stored out-of-range trigger is refused");
	s = default_store();
	s.capture_mode = SP_CAPTURE_EXTENDED + 1u;
	check(!wire_config_normalize(&s), "stored unknown capture mode is refused");
}

static void test_clock(void)
{
	wire_clock_t clock = {0};
	wire_time_t now = {.unix_ms = SET_TIME_UNIX_MS, .uptime_ms = SET_TIME_UPTIME_MS};

	check(wire_clock_unix_ms(&clock, SET_TIME_UPTIME_MS) == 0, "no time before SET_TIME");
	wire_clock_set(&clock, &now);
	check(wire_clock_unix_ms(&clock, SET_TIME_UPTIME_MS + LATER_MS) ==
		      SET_TIME_UNIX_MS + LATER_MS, "time follows uptime after SET_TIME");
	check(wire_clock_unix_ms(&clock, LONG_UPTIME_MS) ==
		      SET_TIME_UNIX_MS - SET_TIME_UPTIME_MS + LONG_UPTIME_MS,
	      "no 32-bit wrap after 49.7 days");
	now.unix_ms += RESYNC_SHIFT_MS;
	wire_clock_set(&clock, &now);
	check(wire_clock_unix_ms(&clock, SET_TIME_UPTIME_MS) == SET_TIME_UNIX_MS + RESYNC_SHIFT_MS,
	      "a later SET_TIME moves the clock");
}

int main(void)
{
	test_bounds();
	test_round_trip();
	test_parse_versions();
	test_parse_rejects();
	test_normalize();
	test_clock();
	printf("WIRE TESTS: %s\n", failures ? "FAIL" : "PASS");
	return failures ? 1 : 0;
}
