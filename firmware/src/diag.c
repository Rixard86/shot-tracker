#include "diag.h"

#include <hal/nrf_power.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/fatal.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/logging/log_ctrl.h>
#include <zephyr/sys/reboot.h>

#include "ble.h"
#include "protocol.h"
#include "recovery.h"

LOG_MODULE_REGISTER(diag, LOG_LEVEL_INF);

#define CRASH_MARK_REG 0u
#define STACK_FREE_MAX 0xFFFFu
#define KNOWN_CAUSES (RESET_CPU_LOCKUP | RESET_WATCHDOG | RESET_PIN | RESET_LOW_POWER_WAKE | \
		      RESET_POR | RESET_SOFTWARE)

static uint8_t last_cause = SP_RESET_POWER_ON;
static uint8_t last_reason = SP_CRASH_NONE;

void k_sys_fatal_error_handler(unsigned int reason, const struct arch_esf *esf)
{
	ARG_UNUSED(esf);
	LOG_PANIC();
	nrf_power_gpregret_set(NRF_POWER, CRASH_MARK_REG, crash_mark_encode(reason));
	sys_reboot(SYS_REBOOT_COLD);
}

void diag_boot(void)
{
	uint32_t cause = 0u;
	uint8_t mark = nrf_power_gpregret_get(NRF_POWER, CRASH_MARK_REG);

	(void)hwinfo_get_reset_cause(&cause);
	(void)hwinfo_clear_reset_cause();
	nrf_power_gpregret_set(NRF_POWER, CRASH_MARK_REG, 0u);
	reset_flags_t flags = {
		.crash_mark = mark,
		.lockup = (cause & RESET_CPU_LOCKUP) != 0u,
		.watchdog = (cause & RESET_WATCHDOG) != 0u,
		.pin = (cause & RESET_PIN) != 0u,
		.wake = (cause & (RESET_LOW_POWER_WAKE | RESET_POR)) != 0u,
		.software = (cause & RESET_SOFTWARE) != 0u,
		.other = (cause & ~KNOWN_CAUSES) != 0u,
	};
	last_cause = reset_classify(&flags);
	last_reason = last_cause == SP_RESET_CRASH ? crash_mark_reason(mark) : SP_CRASH_NONE;
	if (reset_is_crash(last_cause)) {
		LOG_WRN("restarted after a crash: cause %u reason %u", last_cause, last_reason);
	}
}

bool diag_last_reset_crashed(void)
{
	return reset_is_crash(last_cause);
}

void diag_count_crash(store_t *st)
{
	uint16_t next = crash_count_next(st->crashes, last_cause);

	if (next != st->crashes) {
		st->crashes = next;
		store_save_crashes(st);
	}
}

static uint16_t stack_free(k_tid_t thread)
{
	size_t unused = 0u;

	if (k_thread_stack_space_get(thread, &unused)) {
		return 0u;
	}
	return unused > STACK_FREE_MAX ? STACK_FREE_MAX : (uint16_t)unused;
}

void diag_publish(const store_t *st)
{
	sp_diag_t d = {
		.reset_cause = last_cause,
		.crash_reason = last_reason,
		.crash_count = st->crashes,
		.main_stack_free = stack_free(k_current_get()),
		.workq_stack_free = stack_free(&k_sys_work_q.thread),
	};

	ble_update_diag(&d);
}
