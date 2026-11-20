# Firmware IDLE power audit (nRF52840 / Zephyr 4.1 firmware, state of the working tree 2026-11-19, firmware 0.3.12)

Scope: everything the firmware does while IDLE (and when it moves into IDLE) that costs current, with a line-referenced cause, an estimated saving and a proposed change. Repository paths are relative to the repo root. Zephyr paths refer to the local Zephyr 4.1.0 tree (C:/zp/zephyr). Build facts come from the last release build's generated config (C:/zp/build/fixes/firmware/zephyr/.config, built 2026-11-13 from a slightly older prj.conf; the clock, PM, log and driver options cited here are not set in prj.conf, so the current tree gets the same defaults).

Working assumptions used in every estimate (none of them measured):
- Cell ~3.8 V. REG0 DC/DC makes 3.0 V; REG1 DC/DC makes the core supply ([app.overlay:1-3](firmware/app.overlay); HANDOVER §6 "Clocks and regulators").
- CPU running from flash at 64 MHz: about 3.3 mA at VDD with DC/DC (u-blox figure quoted in [HANDOVER.md:287](HANDOVER.md)). That is roughly 3 mA from the cell after REG0.
- 1 µA of continuous idle current = 0.162 mAh per week at 162 idle h/week. The brief's idle budget is ~17 µA, about 2.8 mAh/week.

## Q0. Ranked list: what costs current in IDLE, and what would each fix save?

### Takeaway
The firmware is already lean: release builds have no UART or logging, the kernel is tickless, the IMU wake uses the low-power GPIO SENSE path, the SAADC and TEMP are off between reads, and unused RAM is powered down. The nRF52840 accounts for about 3.5–3.8 µA of the ~17 µA idle budget. The rest is the IMU (~7 µA) and the PCM (~4 µA), which firmware tuning cannot reach. The firmware-addressable items, largest first:
1. A phone link that stays up while the puck is IDLE (conditional, ~2–6 µA while it lasts).
2. System OFF after a long still period (~3.1–3.4 µA for as long as it lasts).
3. LFRC (32 kHz RC) calibration every 4–8 s (~1.0–1.2 µA, ~0.7–0.95 µA of it recoverable with Kconfig only).
4. Everything else (2 s poll, 60 s housekeeping, 120 s advertising linger, settings write on going idle) adds up to about 0.1–0.3 µA in total.

The cost of each false wake (30 s ACTIVE) may matter more than any of these, but it is unmeasured.

### Cited Findings
Ranked audit table. Savings are estimates and need measurement with JP1 cut.

| # | Item (cause, lines) | Cost now (estimate) | Proposed change | Saving (estimate) | Risk / constraint |
|---|---|---|---|---|---|
| 1 | A BLE link kept open through IDLE: `go_idle` does nothing to the link ([main.c:210-220](firmware/src/main.c)); slow parameters are 105–210 ms with latency 4 ([prj.conf:45-48](firmware/prj.conf), [ble.c:62-64](firmware/src/ble.c)); the app is told to "stay connected through the end" ([archesk docs/shotpuck-integration.md:407](../archesk/docs/shotpuck-integration.md)) | ~2–6 µA on top of the idle budget, for as long as a phone stays connected (inference, see Q5) | After N min (e.g. 5–10) of IDLE with no transfer running, either disconnect, or request an "idle" parameter set that stays inside Apple's limit (interval_max × (latency+1) ≤ 2 s) | Most of the link cost: ~2–6 µA × connected-idle hours. 0.3–1 mAh/week if a phone stays connected ~1 day/week; more if the app holds the link for days | App must reconnect and REPLAY on the next wake (the puck already advertises in ACTIVE). Check what archesk actually does before acting |
| 2 | No deep-idle state. In IDLE the nRF stays in System ON with RTC, LFRC, calibration, WDT and 128 KB of RAM powered | nRF share ≈ 3.5–3.8 µA (Q7) | System OFF after ≥30–60 min of IDLE. Wake on IMU INT1 SENSE (and VBUS, as now) | ~3.1–3.4 µA while in deep idle. At ~150 h/week that is ≈0.47–0.5 mAh/week (~17 % of idle drain, ~5 % of the weekly total) | Boot cost ~0.5–2 mC per wake. Wall clock lost until SET_TIME. Must not be entered while the image is unconfirmed (MCUboot would revert it). Details in Q8 |
| 3 | LFRC calibration, Zephyr defaults: `CONFIG_CLOCK_CONTROL_NRF_CALIBRATION_PERIOD=4000`, `MAX_SKIP=1`, `TEMP_DIFF=2` (0.5 °C), `USES_TEMP_SENSOR=y` (build .config). Every 4 s the HFXO is started and the die temperature read; at least every 8 s a 17 ms calibration runs ([nrf_clock_calibration.c:123-177, 206-235](C:/zp/zephyr/drivers/clock_control/nrf_clock_calibration.c)) | ~1.0–1.2 µA (up to ~2 µA while the temperature moves) | prj.conf: `CONFIG_CLOCK_CONTROL_NRF_CALIBRATION_MAX_SKIP=15` (range 0–255, [Kconfig.nrf:94-97](C:/zp/zephyr/drivers/clock_control/Kconfig.nrf)). Optionally a longer PERIOD (e.g. 16000) plus `z_nrf_clock_calibration_force_start()` in `go_active` | ~0.7–0.85 µA (MAX_SKIP=15); ~0.8–0.95 µA (PERIOD 16–32 s). About 0.11–0.15 mAh/week | BLE needs ±500 ppm. Temperature-triggered calibration is kept. The longer PERIOD option also slows temperature tracking during ACTIVE |
| 4 | Main loop wakes every 2 s in IDLE (`IDLE_POLL_MS 2000`, [main.c:58, 617-632, 697](firmware/src/main.c)) to feed the WDT, poll VBUS, the claim window, the reset pads and the linger timer | ~0.05–0.12 µA | Event-driven IDLE: SENSE interrupt on the pads (P0.26), POWER USBDETECTED event for VBUS, a one-shot deadline for the linger, sleep up to the 60 s housekeeping, WDT timeout raised to ≥2× that | ~0.05–0.1 µA (~0.01–0.02 mAh/week) | Refactor touches the watchdog and recovery paths; low value on its own |
| 5 | Advertising linger: 120 s after IDLE at 500–525 ms ([main.c:48, 729-738](firmware/src/main.c); [ble.c:202-204](firmware/src/ble.c)) | ~10 µA while advertising (code comment, unmeasured), i.e. ~0.84 mC ≈ 0.23 µAh per idle entry without a link | Linger 30–60 s, or a 1–2 s interval during linger | ~0.1–0.2 µAh per idle entry; ≈0.005–0.02 mAh/week at 20–90 entries/week | A phone that arrives late finds the puck asleep until the next lift |
| 6 | Housekeeping every 60 s in IDLE ([main.c:50, 535-540, 739-742](firmware/src/main.c)): TEMP (HFXO start), SAADC ×4 oversample, 2 I2C reads, stack scans | ~0.05–0.1 µA | Leave it. 120–300 s would delay the low-battery cut-off (3 readings) to 6–15 min | ≤0.05–0.08 µA | Cut-off latency |
| 7 | `store_save_counts` on every `go_idle` ([main.c:218](firmware/src/main.c), [store.c:97-101](firmware/src/store.c)) | ≈0: NVS compares and skips identical data ([nvs.c:1092-1140](C:/zp/zephyr/subsys/fs/nvs/nvs.c)); counts are already saved at each accepted shot ([main.c:253](firmware/src/main.c)) | None needed (or drop it) | <0.01 µA | — |
| 8 | CHG_STAT P0.15 left as a connected input with no pull when VBUS is absent ([power.c:114, 159-170](firmware/src/power.c)); behind D2 it floats ([hardware/design.py:35-37](hardware/design.py)) | Unknown; probably ~0, possibly µA if it settles mid-rail | Configure it `GPIO_DISCONNECTED` (input buffer off) when VBUS is absent, and reconnect it when VBUS appears | 0 to a few µA (unverified) | None; costs a few lines |
| 9 | Already optimal, keep as is: INT1 level IRQ on SENSE/PORT; SAADC off between reads; no external divider; TEMP stopped; release has no UART or log; tickless; RAM sections 2–5 off; reset-pad pull-up conducts only when bridged; LED timer idle; WDT causes no wakeups | — | Do not change. Switching INT1 to an edge trigger would add ~15 µA (Q4) | — | — |

Supporting figures:
- nRF52840 sleep currents at 3 V: System ON, no RAM retention, RTC on LFRC: 1.50 µA. Full 256 kB retention with RTC on LFRC: 3.16 µA. System OFF, no RAM: 0.40 µA. Wake on GPIOTE IN event: 17.37 µA. Wake on GPIOTE PORT event: 2.36 µA — [nRF52840 PS, sleep current parameters](https://docs.nordicsemi.com/r/bundle/ps_nrf52840/page/_tmp/nrf52840/autodita/current/parameters.i_sleep.html)
- LFRC calibration "takes about 17ms and consumes 460µA in average", i.e. 7.82 µC. At one calibration per 8 s that is ≈0.98 µA. LFRC run current 0.6 µA vs 0.25 µA for the crystal (nRF52832, SoftDevice rc_ctiv=16 / rc_temp_ctiv=2) — [Nordic DevZone, Stian Røed Hafskjold](https://devzone.nordicsemi.com/f/nordic-q-a/14214/how-much-current-consumption-when-we-use-internal-32k-rc/54293)
- README idle budget: ~14 µA = LSM6DSV32X LP1 at 15 Hz ~7 µA (interpolated) + BQ29700 4 µA + nRF System ON with RC — [README.md:114](README.md). HANDOVER uses ~17 µA — [HANDOVER.md:423](HANDOVER.md)

### Inferences
- nRF share of IDLE today: 1.50 µA base + ~0.83 µA for the 128 KB still powered (derived in Q3) + ~1.0–1.2 µA calibration + ~0.1–0.25 µA periodic wakes ≈ 3.5–3.8 µA. The cell sits at ~3.8 V on VDDH (high-voltage mode), and the PS lists 1.29 µA vs 0.97 µA for the "any event" case at 5 V vs 3 V. So the real base is likely a little higher than the 3 V figures.
- Doing items 3–5 without System OFF saves ~0.9–1.1 µA, about 0.15–0.18 mAh/week (≈5–6 % of the idle drain). System OFF (item 2) includes those savings for the time spent in it.
- False wakes are a transition cost that probably outweighs items 3–7. Every IDLE→ACTIVE wake runs `idle_timeout_ms` = 30 s ([shot_detect.c:30](firmware/src/shot_detect.c)) at ~0.85 mA ≈ 25.5 mC ≈ 7.1 µAh. At 10 false wakes/day that is ≈0.5 mAh/week, as much as ~3 µA of continuous idle. 0.3.10 made the wake more sensitive (high-pass at 90 mg, [shot_detect.c:29](firmware/src/shot_detect.c)). The wake count is not recorded anywhere, so this cannot be estimated yet.

### Gaps
- None of the µA figures is measured on the board. JP1 + PPK2 is needed, with the release build only (Q7: the debug build wakes at 1 Hz for logging).
- Number of false wakes per day: unknown. Suggest a wake counter in DIAG or the event log before any tuning.
- How long archesk keeps a link open while the puck is IDLE: not checked in the app code (read-only rule; only the integration doc was read).

## Q1. How often does the CPU wake while IDLE, why, and what does each wake cost?

### Takeaway
With no charger, no link and the linger over, the CPU wakes about every 2 s for the main loop and every 4 s for the LFRC calibration cycle. The calibration cycle also starts the HFXO and runs the system work queue. A 17 ms calibration runs at least every 8 s. There are smaller wakes every 60 s (housekeeping) and ≤256 s (kernel timer span). The calibration cycle is the expensive one (~1 µA). The main-loop poll is cheap (~0.05–0.12 µA).

### Cited Findings
Wake sources while IDLE (no VBUS, not connected, linger over):

| Source | Period | Evidence |
|---|---|---|
| Main loop `k_sem_take(&wake_sem, K_MSEC(poll_period(active)))` | 2000 ms idle; 1000 ms active; 500 ms with VBUS; 250 ms with the claim window open or pads held; 20 ms while a TX or capture write is busy | [main.c:56-61, 617-632, 697](firmware/src/main.c) |
| Per wake: `ota_feed`, `ota_confirm_when_healthy`, `handle_ctrl`, `claim_poll`, `ble_adv_set_pairing`, `claim_feedback`, `factory_poll` (GPIO read), `charge_feedback` (returns early without VBUS), `update_capture_area`, `check_live_stream`, `led_fast_blink`, `cap_tx_hold`, `ble_set_bulk` (returns at once without a link), `apply_config`, linger check | each wake | [main.c:697-742](firmware/src/main.c); [ble.c:311-326](firmware/src/ble.c); [factory.c:113-125](firmware/src/factory.c) |
| Housekeeping (`guard_battery` → `power_update`, `check_sensor`, `publish_status`) | 60 s idle, 10 s active | [main.c:49-50, 535-540, 739-742](firmware/src/main.c) |
| LFRC calibration k_timer → HFXO request → work-queue temperature read → calibrate or skip → HFXO release | 4 s cycle; calibration at least every 2nd cycle (MAX_SKIP=1) or when ΔT ≥ 0.5 °C | [nrf_clock_calibration.c:116-177, 206-235](C:/zp/zephyr/drivers/clock_control/nrf_clock_calibration.c); build .config: `CONFIG_CLOCK_CONTROL_NRF_CALIBRATION_PERIOD=4000`, `MAX_SKIP=1`, `TEMP_DIFF=2`, `LF_ALWAYS_ON=y` |
| Temperature read requests the HF clock (HFXO) for every sample | each calibration cycle + each housekeeping | [temp_nrf5.c:41-73](C:/zp/zephyr/drivers/sensor/nordic/temp/temp_nrf5.c) (`onoff_request` at 59-60, release at 65) |
| Kernel timer span: the RTC compare is set at most half the 24-bit counter ahead (256 s) | ≤256 s | [nrf_rtc_timer.c:37-43](C:/zp/zephyr/drivers/timer/nrf_rtc_timer.c) |
| LED k_timer | only while a pattern plays | [led.c:61-69](firmware/src/led.c) |
| claim.c unencrypted-link deadline (30 s) | only after a connection | [claim.c:13, 41-47](firmware/src/claim.c) |
| Advertising events | every 500–525 ms for 120 s after going idle, if not connected | [ble.c:202-204](firmware/src/ble.c); [main.c:48, 729-738](firmware/src/main.c) |

- LFRC calibration needs the HFXO running ("The HFCLK crystal oscillator has to be started"). A DONE event marks the end. HFXO startup time is the sum of the power-up time and the debounce time, with debounce = HFXODEBOUNCE × 16 µs (reset value 256 µs) — [nRF52840 PS, CLOCK](https://docs.nordicsemi.com/r/bundle/ps_nrf52840/page/clock.html)
- HFXO startup typ. 0.36 ms; LFRC uncalibrated ±2 %, calibrated ±500 ppm (sibling-chip figures from the nRF52811 PS) — [nRF52811 PS, CLOCK](https://infocenter.nordicsemi.com/topic/ps_nrf52811/clock.html?cp=4_3_0_4_3_1)
- One calibration ≈ 17 ms at 460 µA average = 7.82 µC (nRF52832 figure) — [Nordic DevZone](https://devzone.nordicsemi.com/f/nordic-q-a/14214/how-much-current-consumption-when-we-use-internal-32k-rc/54293)

### Inferences
- Main-loop wake: roughly 2–5 k CPU cycles (30–80 µs) of work plus the RTC ISR and context switches. At ~3 mA that is ~0.1–0.25 µC per wake, ≈0.05–0.12 µA at 0.5 Hz. Moving IDLE to an 8 s poll (still under the 10 s WDT) saves ~75 % of that. A fully event-driven loop sleeping up to 60 s saves ~95 %.
- Calibration cycle: (7.82 µC for the calibration + ~0.5–1.1 µC for the skipped cycle's HFXO start, temperature read and work-queue switch) per 8 s ≈ 1.0–1.1 µA. Continuous temperature drift forces a calibration every 4 s, ≈2 µA. With `MAX_SKIP=15`: 4 s temperature checks (~0.13–0.28 µA) + one calibration per 64 s (~0.12 µA) ≈ 0.25–0.4 µA, saving ~0.7–0.85 µA. With PERIOD=16000 and MAX_SKIP=1 it is ≈0.27 µA (saving ~0.8 µA). PERIOD=32000 gives ≈0.13 µA.
- Why `MAX_SKIP` is the safer knob: the driver still checks the temperature every 4 s and recalibrates on a 0.5 °C change ([nrf_clock_calibration.c:206-235](C:/zp/zephyr/drivers/clock_control/nrf_clock_calibration.c)). The RC's main drift source, temperature, stays covered in ACTIVE too. PERIOD is a compile-time `K_MSEC` ([nrf_clock_calibration.c:125-127](C:/zp/zephyr/drivers/clock_control/nrf_clock_calibration.c)), so an IDLE-only change needs a Zephyr patch. The force API exists for "calibrate now on going ACTIVE": `z_nrf_clock_calibration_force_start()` ([nrf_clock_control.h:101-102](C:/zp/zephyr/include/zephyr/drivers/clock_control/nrf_clock_control.h)).
- Uptime accuracy also rides on calibration: `t_unix_ms` is uptime mapped through SET_TIME (PROTOCOL.md:132, 160). If calibration stopped altogether, the ±2 % uncalibrated tolerance could be ~72 s/h of drift (sibling-PS figure). With temperature-triggered calibration kept, drift stays near ±500 ppm, ~1.8 s/h.
- If the IDLE poll is made slower without a pad interrupt, the reset-pad hold gets longer to trigger. The first bridged sample starts the 5 s hold timer ([recovery.c:15-36](firmware/src/recovery.c), [recovery.h:7](firmware/src/recovery.h)), so detection can lag by up to one IDLE poll period.

### Gaps
- No nRF52840-specific figures found for: calibration duration and current, HFXO run and startup current, TEMP conversion time and current. The 17 ms / 460 µA figure is for the nRF52832 with the SoftDevice, and the Zephyr driver's cycle differs: HFXO is started every 4 s even when the calibration is skipped.
- Actual CPU time per main-loop wake was not measured (a GPIO toggle or PPK2 trace would settle it).

## Q2. Does the watchdog force wakeups? Could it pause in sleep or be fed less often?

### Takeaway
The WDT itself causes no wakeups. It counts on the 32 kHz clock and runs through CPU sleep. It only forces the main loop to feed it within 10 s, and the 2 s IDLE poll sits well inside that. Pausing it in sleep would save nothing measurable on the nRF52840, and it would blind the watchdog to a deadlock where the main thread blocks forever while the CPU sleeps. If IDLE polling is ever slowed, raise the timeout (e.g. ≥2× the longest IDLE sleep) instead of pausing.

### Cited Findings
- Timeout 10 s, reset-SoC flag; `wdt_setup(wdt, WDT_OPT_PAUSE_HALTED_BY_DBG)` only, so the WDT runs in sleep. Fed once per main-loop pass — [ota.c:17, 64-89](firmware/src/ota.c); [main.c:698](firmware/src/main.c)
- The Zephyr driver sets `RUN_SLEEP` unless `WDT_OPT_PAUSE_IN_SLEEP` is passed, and `RUN_HALT` unless `WDT_OPT_PAUSE_HALTED_BY_DBG` is passed — [wdt_nrfx.c:46-54](C:/zp/zephyr/drivers/watchdog/wdt_nrfx.c)
- nRF52 WDT: pausing while the CPU sleeps or is halted by the debugger is optional. CRV/RREN/CONFIG lock once it starts and become configurable again "after a reset, or waking up from OFF mode" — [nRF52832 PS, WDT](https://infocenter.nordicsemi.com/topic/com.nordic.infocenter.nrf52832.ps.v1.1/wdt.html)
- Erratum 88 ("WDT: increased current consumption when configured to pause in System ON idle", ~3 µA → ~400 µA) — [nRF52805 errata 88](https://docs.nordicsemi.com/r/bundle/errata_nrf52805_rev2/page/err/nrf52805/rev2/latest/anomaly_805_88.html). nrfx lists it as present only on nRF52805/810/811/832, not on the nRF52840 — [nrf52_erratas.h:4703-4711](C:/zp/modules/hal/nordic/nrfx/mdk/nrf52_erratas.h)
- Entering System OFF stops and disables the watchdog (nRF5340 PS) — [nRF5340 PS, WDT](https://docs.nordicsemi.com/r/bundle/ps_nrf5340/page/wdt.html). An unresolved DevZone thread reports an nRF52832 resetting ~20 s after entering System OFF with the WDT enabled — [DevZone](https://devzone.nordicsemi.com/f/nordic-q-a/127374/nrf52832-resets-20-s-after-entering-system-off-watchdog/563528)
- The image confirmation that makes MCUboot keep an OTA image needs a secured link and 10 s of uptime — [ota.c:96-103](firmware/src/ota.c); README D15 ([README.md:78](README.md))

### Inferences
- Saving from WDT changes on their own: ≈0 µA. The WDT has no periodic interrupt, and the feed costs nothing extra because it rides on a wake that happens anyway.
- With an event-driven IDLE (Q1), the WDT timeout sets the minimum wake rate. A 120–150 s timeout with a 60 s IDLE sleep keeps hang recovery (a stuck image resets within ~2.5 min instead of 10 s) and still allows MCUboot rollback. The rollback of an unconfirmed image already depends on a reset, which the longer timeout still delivers.

### Gaps
- No WDT run current found in the nRF52840 PS pages fetched. Expected to be small, but unverified.
- No nRF52840-specific statement found that System OFF stops the WDT (nRF5340 doc and nRF52832 forum only). The existing low-battery cut-off already enters System OFF with the WDT running ([power.c:226-235](firmware/src/power.c)) and should be checked on hardware (it is in the "load-bearing but unverified" list, [HANDOVER.md:426](HANDOVER.md)).

## Q3. Which peripherals and pins stay powered in IDLE (TWIM, SAADC, GPIO pulls, UART/RTT, logging, RAM)?

### Takeaway
None of them is a meaningful drain in the release build:
- The SAADC is enabled only around each read.
- The TEMP peripheral is stopped after each read.
- UART, console, printk and logging are compiled out.
- The TWIM stays enabled (no `CONFIG_PM_DEVICE`), but it is idle with its lines high.
- RAM sections 2–5 of RAM8 are off in IDLE.

The open questions are small: the floating CHG_STAT input, and the TWIM's enabled-idle current (expected ~0, no figure found). The debug build is a different story: it wakes every 1 s for deferred logging, so it must never be used for current measurements.

### Cited Findings
- Release config: `CONFIG_SERIAL=n`, `CONFIG_CONSOLE=n`, `CONFIG_UART_CONSOLE=n`, `CONFIG_LOG=n`, `CONFIG_PRINTK=n`, `CONFIG_BOOT_BANNER=n` — [prj.conf:97-102](firmware/prj.conf). The build .config confirms SERIAL, LOG, CONSOLE and PRINTK are not set.
- Debug overlay adds LOG over SEGGER RTT — [debug.conf:5-8](firmware/debug.conf). The debug build gets `CONFIG_LOG_MODE_DEFERRED=y`, `CONFIG_LOG_PROCESS_THREAD=y`, `CONFIG_LOG_PROCESS_THREAD_SLEEP_MS=1000` (C:/zp/build/fixesdbg/firmware/zephyr/.config).
- SAADC: the driver calls `nrf_saadc_enable` at the start of a sequence and `STOP` + `nrf_saadc_disable` at the end, even without PM runtime — [adc_nrfx_saadc.c:345-348, 651-656](C:/zp/zephyr/drivers/adc/adc_nrfx_saadc.c). The battery is read on the internal `NRF_SAADC_VDDHDIV5` input with no external divider — [power.c:7, 59-65](firmware/src/power.c)
- TWIM: `pm_device_driver_init` runs the resume action, which calls `nrfx_twim_enable`. Suspend (`nrfx_twim_disable`) only happens through PM device actions — [i2c_nrfx_twim_common.c:105-139](C:/zp/zephyr/drivers/i2c/i2c_nrfx_twim_common.c). `CONFIG_PM_DEVICE` is not set (build .config). The board's `i2c0_sleep` state (low-power-enable) is therefore never applied, and the internal pull-ups stay on — [shotpuck_nrf52840-pinctrl.dtsi:3-16](firmware/boards/shotpuck/shotpuck/shotpuck_nrf52840-pinctrl.dtsi)
- CHG_STAT P0.15: configured as `GPIO_INPUT`; a pull-up is enabled only while VBUS is present, otherwise it is a plain input — [power.c:114, 159-170](firmware/src/power.c). Hardware: STAT → D2 cathode, D2 anode → CHG_STAT — [design.py:35-37](hardware/design.py)
- Reset pads P0.26: DT flags `GPIO_PULL_UP | GPIO_ACTIVE_LOW`, so the pull-up is always on — [shotpuck_nrf52840.dts:40](firmware/boards/shotpuck/shotpuck/shotpuck_nrf52840.dts), [factory.c:69-73](firmware/src/factory.c). The pads are bare and reached only with the cap off — [design.py:32-34](hardware/design.py)
- LED P0.13 and CHG_INHIBIT P0.11 are driven low when inactive (R3 pull-down on Q1B gate) — [led.c:91-96](firmware/src/led.c), [power.c:115](firmware/src/power.c), [design.py:20-23](hardware/design.py)
- RAM: once the image is confirmed, sections 3–5 of RAM8 (top 96 KB) are switched off; section 2 (history) is on only in ACTIVE — [supply.c:23-31](firmware/src/supply.c), [ota.c:55-62](firmware/src/ota.c), [main.c:210-216](firmware/src/main.c), [HANDOVER.md:434-439](HANDOVER.md). HANDOVER estimates the saving at ~0.7 µA by the data sheet.
- PS: full 256 kB RAM retention with RTC 3.16 µA vs no RAM 1.50 µA — [nRF52840 PS sleep](https://docs.nordicsemi.com/r/bundle/ps_nrf52840/page/_tmp/nrf52840/autodita/current/parameters.i_sleep.html)

### Inferences
- RAM: (3.16 − 1.50) µA / 256 KB ≈ 6.5 nA/KB, so the 128 KB still powered (RAM0–7 plus RAM8 sections 0–1) costs ≈0.83 µA. The image ends at 0x2001CDB0 ([HANDOVER.md:437](HANDOVER.md)), inside RAM8 section 1, so nothing more can be switched off. Only shrinking below 0x20018000 would free section 1 (~0.2 µA), which is not worth it.
- TWIM: SCL and SDA idle high, so the ~13 kΩ internal pull-ups carry no current and an idle-enabled TWIM should draw ~0. Adding `CONFIG_PM_DEVICE` to suspend it in `go_idle` would save ≈0 at a code-size cost; not recommended.
- CHG_STAT: without VBUS the pin is held only by D2's reverse leakage and the unpowered MCP73831's STAT pin, so its voltage is undefined. A CMOS input buffer near mid-rail can draw cross-conduction current. Setting `GPIO_DISCONNECTED` while VBUS is absent (and back to input + pull-up when VBUS is seen, as now) removes the question at no cost. The reset-pad pull-up is fine: it carries current only when the pads are bridged.
- Debug build: its 1 Hz log thread adds a wake every second (≈0.1–0.25 µA by the Q1 per-wake estimate). The SWD/RTT link also keeps parts of the debug interface powered while a probe is attached. Idle measurements are valid only on the release build with the probe disconnected.

### Gaps
- No TWIM enabled-but-idle current found in the PS pages fetched.
- No PS figure for the leakage of a floating input with the buffer connected. Measure, or simply apply `GPIO_DISCONNECTED`.

## Q4. How is the IMU INT1 wake line configured, and does it use GPIOTE IN (high current) or PORT/SENSE?

### Takeaway
INT1 (P0.03) uses a level interrupt (`GPIO_INT_LEVEL_ACTIVE`). In Zephyr 4.1, `gpio_nrfx` maps level interrupts to the pin's SENSE mechanism and the GPIOTE PORT event, and allocates an IN channel only for edge interrupts. This is the low-power path (2.36 µA vs 17.37 µA chip current in the PS). Keep it, and never switch this pin to an edge trigger without `sense-edge-mask`.

### Cited Findings
- `gpio_pin_configure_dt(&int1, GPIO_INPUT)` and `gpio_pin_interrupt_configure_dt(&int1, GPIO_INT_LEVEL_ACTIVE)`. The ISR disables the interrupt, and the main loop re-arms it after reading WAKE_UP_SRC — [accel.c:44-53, 95-111](firmware/src/accel.c), [main.c:341-347](firmware/src/main.c). DT: `accel-int1-gpios = <&gpio0 3 GPIO_ACTIVE_HIGH>` — [shotpuck_nrf52840.dts:37](firmware/boards/shotpuck/shotpuck/shotpuck_nrf52840.dts)
- `get_trigger` maps `GPIO_INT_MODE_LEVEL` to `NRFX_GPIOTE_TRIGGER_HIGH/LOW` (sense). An IN channel is allocated only when `mode == GPIO_INT_MODE_EDGE` and the pin is not in `sense-edge-mask` — [gpio_nrfx.c:328-339, 368-383](C:/zp/zephyr/drivers/gpio/gpio_nrfx.c)
- ION_RAMON_GPIOTE (wake on GPIOTE input, event mode) 17.37 µA vs ION_RAMON_GPIOTEPORT (wake on PORT event) 2.36 µA — [nRF52840 PS sleep](https://docs.nordicsemi.com/r/bundle/ps_nrf52840/page/_tmp/nrf52840/autodita/current/parameters.i_sleep.html)
- IMU in IDLE: LP1 (2-sample mean) at 15 Hz with high-pass wake, latched INT1 — [accel.c:113-134](firmware/src/accel.c), [accel_regs.h:34-35](firmware/src/accel_regs.h), [HANDOVER.md:285](HANDOVER.md)

### Inferences
- There is no saving to find here. The risk is a regression: an edge-triggered INT1 would cost ~15 µA, almost doubling the idle budget.
- The same SENSE configuration is what a System OFF wake needs (Q8), so the existing pin setup can be reused before `sys_poweroff()`.

### Gaps
- The IMU's own idle current (~7 µA, the largest single idle consumer) is out of this audit's scope. The firmware choices that set it are ODR and mode: [accel.c:123](firmware/src/accel.c). HANDOVER mentions the IMU's ultra-low-power mode as a possible further saving ([HANDOVER.md:423](HANDOVER.md)); it is tied to the wake reliability fixed in 0.3.10.

## Q5. Advertising after going idle, and a link left open in IDLE

### Takeaway
The 120 s advertising linger costs ~0.23 µAh per idle entry, which is negligible per week (≤0.02 mAh) and can be cut to 30–60 s at little risk. The larger, conditional cost is a phone that stays connected while the puck is IDLE. The firmware never drops or slows a link on going idle, so a link in range can persist for hours or days at a ~0.5–1 s effective event rate.

### Cited Findings
- Linger: `in_linger = ACTIVE || now − idle_since_ms < ADV_LINGER_MS (120000) || claim_is_open()`. Advertising stops when `!in_linger`. After a disconnect in IDLE the puck does not re-advertise — [main.c:48, 729-738](firmware/src/main.c)
- Advertising parameters: connectable, 0x0320–0x0348 (500–525 ms). Code comment: "~10 uA average while advertising". AD = flags + 128-bit UUID (+ pairing marker); scan response = name — [ble.c:191-204, 272-289](firmware/src/ble.c)
- Link parameters while not bulk: 84–168 units (105–210 ms), latency 4, timeout 4 s; fast 15–30 ms, latency 0, only while a transfer runs plus 5 s — [prj.conf:45-48](firmware/prj.conf), [ble.c:25-28, 59-64, 311-326](firmware/src/ble.c). `go_idle` touches only the IMU, RAM and settings — [main.c:210-220](firmware/src/main.c)
- App guidance: "Stay connected through the end" of a session — [archesk docs/shotpuck-integration.md:407](../archesk/docs/shotpuck-integration.md)
- Unencrypted links are dropped after 30 s; encrypted ones are kept indefinitely — [claim.c:13, 19-36](firmware/src/claim.c)
- LFRC adds window widening during a connection: Nordic estimated 2.85 µA extra vs a crystal during a connection, of which 1.52 µA is window widening (nRF52832, SoftDevice) — [Nordic DevZone](https://devzone.nordicsemi.com/f/nordic-q-a/14214/how-much-current-consumption-when-we-use-internal-32k-rc/54293)

### Inferences
- Linger cost: (10 − ~3 µA base) × 120 s ≈ 0.84 mC ≈ 0.23 µAh per idle entry without a link. At 20–90 idle entries per week (several per session if ends are >30 s apart) that is ≈0.005–0.02 mAh/week. Cutting to 30 s saves ~75 % of that. Raising the linger interval to 1–2 s saves 50–75 % while keeping the window. Either is safe for shot capture: advertising restarts the moment the puck wakes (`go_active` → `ble_adv_start`, [main.c:200-208](firmware/src/main.c)).
- Link left open in IDLE: an empty connection event (one RX plus one TX, with LFRC window widening) is cheaper than a three-channel advertising event (~3.5 µC by the code comment's own 10 µA at 500 ms). A rough ~1.5–3 µC per event at an effective 0.525–1.05 s gives ≈2–6 µA. If the app keeps the link while the bow sits in its case next to the phone, that cost lasts the whole time.
- Two options for a link in IDLE:
  - (a) After 5–10 min in IDLE with no transfer, call `bt_conn_disconnect`. The app reconnects on the next lift, because ACTIVE advertises, and it already resyncs with COUNT/REPLAY.
  - (b) On `go_idle`, request an "idle" parameter set at the Apple limit, e.g. interval 400 ms with latency 4 (2 s effective). That roughly halves the event rate.

  (a) removes the cost entirely and is simpler; (b) keeps live status.

### Gaps
- No measured nRF52840 figure found for the average current of a connection at a ~1 s effective interval. The 2–6 µA range is an inference, and so is the assumption that the Zephyr link layer really skips events under peripheral latency when idle (not verified in the controller source).
- Whether archesk disconnects at session end or holds the link in the background: unknown.
- Whether the "~10 µA while advertising" comment was ever measured: no source in the repo.

## Q6. Battery measurement and other housekeeping: is there a continuous divider, and how often and how long does it run?

### Takeaway
There is no external divider, so there is no continuous drain. The battery is read on the SAADC's internal VDDH/5 input every 60 s in IDLE, with 4× oversampling, 10 µs acquisition, and offset calibration only when the die temperature moved ≥10 °C. Charger state is a register read, and the STAT pull-up is on only with VBUS. In total housekeeping costs ≈0.05–0.1 µA. The 60 s interval is also what sets the 3-minute low-battery cut-off, so it should stay.

### Cited Findings
- `NRF_SAADC_VDDHDIV5`, gain 1/2, internal reference, 10 µs acquisition, 12-bit, oversampling 4; calibrate when `!calibrated || |ΔT| ≥ 10 °C` — [power.c:38-41, 59-65, 119-146](firmware/src/power.c). No external divider — [power.c:7](firmware/src/power.c)
- The SAADC is enabled only during a sequence — [adc_nrfx_saadc.c:345-348, 651-656](C:/zp/zephyr/drivers/adc/adc_nrfx_saadc.c)
- Die temperature on each `power_update` (HF clock requested per sample) — [power.c:148-157, 197-214](firmware/src/power.c), [temp_nrf5.c:41-73](C:/zp/zephyr/drivers/sensor/nordic/temp/temp_nrf5.c)
- Charger: `nrf_power_usbregstatus_vbusdet_get`; with VBUS: pull-up, 50 µs settle, read; without: plain input — [power.c:159-170](firmware/src/power.c). `charge_feedback` returns at once without VBUS — [main.c:590-598](firmware/src/main.c)
- `check_sensor` in IDLE reads CTRL1 and MD1_CFG over I2C — [main.c:487-509](firmware/src/main.c), [accel.c:174-183](firmware/src/accel.c)
- `publish_status` → `ble_update_status` (BAS level; notify only if changed and connected) and `diag_publish` (two stack-usage scans with `CONFIG_INIT_STACKS`) — [main.c:108-124](firmware/src/main.c), [ble.c:346-354](firmware/src/ble.c), [diag.c:72-93](firmware/src/diag.c), [prj.conf:66](firmware/prj.conf)
- Low-battery cut-off: 3 consecutive readings below 3300 mV, about 3 min in IDLE — [recovery.h:10](firmware/src/recovery.h), [power.c:37, 221-224](firmware/src/power.c), [README.md:114](README.md)
- Settings write on going idle: NVS returns without writing when the new data equals the latest entry — [nvs.c:1092-1140](C:/zp/zephyr/subsys/fs/nvs/nvs.c); the lookup cache is enabled — [prj.conf:21-22](firmware/prj.conf)

### Inferences
- A housekeeping pass costs about 1–2 ms of CPU, an HFXO start for TEMP and ~50 µs of SAADC, roughly 3–7 µC per minute ≈ 0.05–0.1 µA.
- Two micro-savings exist (reusing the calibration driver's temperature, or a 120 s IDLE period), but each is worth ≤0.05 µA and the second doubles cut-off latency. Not recommended.
- `store_save_counts` in `go_idle` costs only an NVS lookup and compare, usually no flash write. It has no power impact worth changing.

### Gaps
- SAADC and TEMP conversion currents for the nRF52840 not retrieved (PS electrical tables not fetched).

## Q7. Kernel and power-management configuration: what is on and what does it cost in IDLE?

### Takeaway
The kernel is tickless at 32768 ticks/s, and there is no `CONFIG_PM`; the nRF52 idle thread sleeps (System ON) without it. There is no `CONFIG_PM_DEVICE` or `PM_DEVICE_RUNTIME`; that matters only for the TWIM, at ~0. Release has no serial or log. The only clock-related cost is the LFRC calibration (Q1). The board uses the RC because the module has no 32 kHz crystal.

### Cited Findings
- From the build .config: `CONFIG_TICKLESS_KERNEL=y`, `CONFIG_SYS_CLOCK_TICKS_PER_SEC=32768`, `CONFIG_NRF_RTC_TIMER=y`, `# CONFIG_PM_DEVICE is not set`, `CONFIG_CLOCK_CONTROL_NRF_K32SRC_RC=y`, `..._RC_CALIBRATION=y`, `..._CALIBRATION_PERIOD=4000`, `..._MAX_SKIP=1`, `..._TEMP_DIFF=2`, `..._USES_TEMP_SENSOR=y`, `..._K32SRC_500PPM=y`, `CONFIG_BT_LL_SW_SPLIT=y` (open-source link layer), `CONFIG_BT_CTLR_TX_PWR_0=y`, `# CONFIG_BT_PRIVACY is not set` (C:/zp/build/fixes/firmware/zephyr/.config)
- Board defconfig chooses the RC: "the BMD-340 has no 32.768 kHz crystal" — [shotpuck_nrf52840_defconfig:4-5](firmware/boards/shotpuck/shotpuck/shotpuck_nrf52840_defconfig)
- MCUboot also runs the RC with calibration (`CONFIG_CLOCK_CONTROL_NRF_K32SRC_RC_CALIBRATION=y`), validates slot 0 on every boot (`CONFIG_BOOT_VALIDATE_SLOT0=y`, `CONFIG_BOOT_ECDSA_TINYCRYPT=y`) and has minimal logging (C:/zp/build/fixes/mcuboot/zephyr/.config)
- Kconfig help for PERIOD: each period runs a temperature measurement followed by a calibration, which is skipped while ΔT < TEMP_DIFF until MAX_SKIP consecutive skips. MAX_SKIP range 0–255 — [Kconfig.nrf:83-101](C:/zp/zephyr/drivers/clock_control/Kconfig.nrf)
- Code that nothing uses is already off: `CONFIG_TIMESLICING=n`, `CONFIG_ENTROPY_BT_HCI=n` — [prj.conf:27-30](firmware/prj.conf)

### Inferences
- Kconfig-only change with the best value-to-risk ratio: `CONFIG_CLOCK_CONTROL_NRF_CALIBRATION_MAX_SKIP=15` in prj.conf, saving ~0.7–0.85 µA (Q1). It changes no application code. BLE accuracy is still guarded by the 0.5 °C trigger. Verify with `CONFIG_CLOCK_CONTROL_NRF_CALIBRATION_DEBUG` in a debug build (counts calibrations and skips, [nrf_clock_calibration.c:280-296](C:/zp/zephyr/drivers/clock_control/nrf_clock_calibration.c)) and with a PPK2 trace.
- `CONFIG_PM` and `CONFIG_PM_DEVICE_RUNTIME` would add nothing for IDLE on the nRF52. The CPU already sleeps in WFE/WFI, and the only runtime-PM-capable peripherals in use (SAADC, TWIM) either self-disable or draw ~0 when idle.

### Gaps
- No PS figure retrieved for the extra current of the LFRC itself vs LFXO on the nRF52840 (the 0.6 vs 0.25 µA figure is from the nRF52832 thread). It does not matter here, because no crystal is fitted.

## Q8. System OFF as a deeper idle after a long still period: what is needed, and what is lost?

### Takeaway
System OFF after ≥30–60 min of IDLE would cut the nRF's share from ~3.5–3.8 µA to ~0.4 µA. Idle would drop from ~17 to ~14 µA, about 0.47–0.5 mAh/week if ~150 of the 162 idle hours are spent there. Each wake then costs a cold boot through MCUboot (~120 ms with ECDSA on an nRF52840 DK) plus app init, roughly 0.5–2 mC, so the break-even time is about 3–11 min. The firmware already has the pieces (System OFF + VBUS wake in the cut-off path; a level/SENSE INT1). It must not enter System OFF with an unconfirmed image or a pending capture write. It loses the wall clock until the next SET_TIME, plus the RAM-only rejected-candidate ring.

### Cited Findings
- Existing System OFF path: `power_off_until_charger()` → `sys_poweroff()`, wake on VBUS (USBDETECTED) — [power.c:226-235](firmware/src/power.c); used by the low-battery cut-off — [main.c:524-533](firmware/src/main.c). `CONFIG_POWEROFF=y` — [prj.conf:9](firmware/prj.conf)
- Zephyr's Nordic System OFF sample arms a wake pin with `gpio_pin_interrupt_configure_dt(&sw0, GPIO_INT_LEVEL_ACTIVE)` and then calls `sys_poweroff()` — [samples/boards/nordic/system_off/src/main.c:75, 101](C:/zp/zephyr/samples/boards/nordic/system_off/src/main.c). INT1 is already configured that way ([accel.c:99, 108-111](firmware/src/accel.c))
- System OFF, no RAM retention, wake on reset: 0.40 µA; System ON, no RAM, RTC on LFRC: 1.50 µA; full RAM + RTC: 3.16 µA — [nRF52840 PS sleep](https://docs.nordicsemi.com/r/bundle/ps_nrf52840/page/_tmp/nrf52840/autodita/current/parameters.i_sleep.html)
- Boot handling: a wake from System OFF is classified as a wake, not a crash, so it does not feed the boot-loop guard — [diag.c:44-55](firmware/src/diag.c); the app starts ACTIVE — [main.c:687-689](firmware/src/main.c). The RAM8 power-off setting survives a wake from System OFF, and the code already switches section 2 on before use — [HANDOVER.md:434-439](HANDOVER.md)
- MCUboot validates slot 0 with ECDSA P-256 (tinycrypt) on every boot (MCUboot .config). Measured startup on an nRF52840 DK: ~120 ms with ECDSA vs ~1 s with RSA (nRF Desktop, NCS 2.9.0) — [Nordic DevZone](https://devzone.nordicsemi.com/f/nordic-q-a/118608/reducing-startup-time-with-mcuboot-in-nrf-desktop/521626)
- An unconfirmed (trial) image is confirmed only after a secured link and 10 s of uptime. Until then a reset makes MCUboot revert it — [ota.c:96-103](firmware/src/ota.c), [README.md:78](README.md)
- `t_unix_ms` is 0 until the phone sends SET_TIME after a boot — [docs/PROTOCOL.md:132, 160](docs/PROTOCOL.md). Rejected candidates are RAM-only (128) — [HANDOVER.md:432](HANDOVER.md)
- The detector needs ≥500 ms of stillness before a trigger, and IDLE wakes at 90 mg with a high-pass filter — [archesk docs/shotpuck-integration.md:298](../archesk/docs/shotpuck-integration.md)
- WDT: entering System OFF stops and disables it (nRF5340 PS); WDT configuration is free again after waking from OFF (nRF52832 PS) — [nRF5340 WDT](https://docs.nordicsemi.com/r/bundle/ps_nrf5340/page/wdt.html), [nRF52832 WDT](https://infocenter.nordicsemi.com/topic/com.nordic.infocenter.nrf52832.ps.v1.1/wdt.html)

### Inferences
- Entry conditions:
  - IDLE for ≥T_deep (30–60 min).
  - No VBUS, no link, linger over.
  - `ota_image_confirmed()` true.
  - `!capw_busy()`; no event or capture transfer pending.
  - Battery not in cut-off (that path already exists).
- Entry steps:
  - `store_save_counts`.
  - `ble_adv_stop`.
  - IMU stays in its IDLE wake configuration. Read WAKE_UP_SRC to clear the latch, and check INT1 is low before arming; a pin already at its sensed level wakes immediately.
  - Arm INT1 with `GPIO_INT_LEVEL_ACTIVE`, as now. Optionally arm P0.26 low-level SENSE so the reset pads also wake the board.
  - Leave CHG_INHIBIT low and STAT disconnected, as in `power_off_until_charger`.
  - Call `sys_poweroff()`.
- Wake path: INT1 SENSE → reset → MCUboot (~0.12 s validate + jump) → app init (UICR check, diag, boot guard, WDT, 3 battery readings, settings, event-log and capture-store open, `bt_enable` + settings, IMU reset + configure) → `go_active`. App init time is unmeasured; ~0.2–0.5 s is a guess. That is far shorter than lift-to-shot, and the ≥500 ms pre-shot stillness rule makes a shot during boot implausible.
- Boot charge: ~0.3–0.7 s at ~3–4 mA ≈ 1–2.5 mC. On top of that comes the 30 s ACTIVE every wake already costs in IDLE too. Per-wake penalty vs System ON IDLE: ≈0.3–0.7 µAh. Saving rate: ~3.1–3.4 µA. Break-even ≈ 5–13 min of deep idle, so T_deep = 30–60 min keeps the risk of losing on short gaps low.
- What is lost:
  - Wall-clock mapping, so shots get `t_unix_ms = 0` until the app reconnects and sends SET_TIME. The app could fall back on its own receive time.
  - Uptime continuity.
  - The RAM-only rejected-candidate ring.
  - A one-time LFRC start and calibration on each boot.

  Nothing persistent is lost: settings, bonds, the shot log and captures are all in flash.
- Interaction with "no downgrades" and OTA: deep idle must be refused while an image is on trial. Otherwise the first System OFF wake silently rolls back an OTA that a phone never confirmed.
- This is the largest firmware-only structural saving. It also makes items 3–5 of Q0 moot for the time spent in it. Items 3–5 still matter for short idle gaps within sessions.

### Gaps
- No nRF52840-specific PS statement retrieved for the System OFF current with GPIO DETECT wake armed (only the "wake on reset" 0.40 µA and LPCOMP 0.86 µA rows), or for whether the WDT stops in System OFF on the nRF52840.
- Whether REG0/VDD (which powers the IMU) keeps 3.0 V in System OFF in high-voltage mode was not confirmed from an nRF52840 source. The IMU must keep running for the wake to work, so this needs a bench check. The existing VBUS-wake path is also listed as unverified ([HANDOVER.md:426](HANDOVER.md)).
- App init time after a System OFF wake on this firmware is unmeasured: capture-store scan, BT settings load, three battery readings.
