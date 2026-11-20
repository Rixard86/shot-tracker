# nRF52840 SoC idle (sleep) current: measures, estimated savings, status in this firmware

Conventions used in all estimates below (from the assignment): idle board total about 17 µA (estimated, not measured), idle about 162 h/week, weekly drain about 8.4-9.8 mAh. **1 µA saved in idle = 5.9 % of the 17 µA idle = 0.162 mAh/week = 1.65-1.93 % of weekly drain.** All µA figures are SoC-side (VDD rail) unless stated; how REG0 maps them to cell current at light load is itself a gap (see the REG0 question). Repo sources are cited by repo-relative path, Zephyr sources by path under C:/zp.

## 1. Baseline: what does the nRF52840 itself draw in System ON idle here (clock source, RAM retention, regulator mode, VDDH)?

### Takeaway
By the Product Specification, the SoC in this puck's idle state should draw roughly 3.5-4.5 µA: about 2.5 µA for System ON with RTC on the LFRC and 128 KB of RAM kept, about 0.3 µA extra for high-voltage mode, and about 1 µA for Zephyr's default LFRC calibration. That is only about 20-25 % of the 17 µA idle. The IMU (~7 µA) and the BQ29700 (~4 µA) dominate, so even removing every SoC µA saves at most about 4 µA (about 6-7 % of weekly drain).

### Cited Findings
- PS sleep table (all Typ): System ON, no RAM retention, wake on any event 0.97 µA; System ON, full 256 kB retention, wake on any event 2.35 µA; same with power-fail comparator 2.35 µA; System ON, full retention, wake on GPIOTE input (event mode) 17.37 µA; same, wake on GPIOTE PORT event 2.36 µA; System ON, no retention, wake on RTC (LFRC) 1.50 µA; System ON, full retention, wake on RTC (LFRC) 3.16 µA; System OFF, no retention 0.40 µA; System OFF, wake on LPCOMP 0.86 µA; System OFF, full retention 1.86 µA; **System ON, no retention, 5 V on VDDH, REG0 = 3.3 V: 1.29 µA; System OFF, no retention, 5 V on VDDH, REG0 = 3.3 V: 0.95 µA**. No row states a DC/DC or LDO setting. — [nRF52840 PS, Sleep parameters](https://docs.nordicsemi.com/r/bundle/ps_nrf52840/page/_tmp/nrf52840/autodita/current/parameters.i_sleep.html)
- LFRC run current 0.7 µA (normal mode); ULP mode 0.3 µA in PS v1.1 (0.2 µA in the OPS v0.5 draft). — [nRF52840 PS v1.1 (SparkFun copy)](https://cdn.sparkfun.com/assets/e/c/3/1/7/Nano_BLE_MCU-nRF52840_PS_v1.1.pdf); [nRF52840 OPS v0.5 (Mouser)](https://www.mouser.com/datasheet/2/297/nRF52840_OPS_v0.5-1074816.pdf) (both via search-result extracts; the PDFs were too large to fetch whole)
- Nordic answer (nRF52832 context): LFRC 0.6 µA vs LFXO 0.25 µA run current, +0.35 µA for LFRC. — [DevZone 14214](https://devzone.nordicsemi.com/f/nordic-q-a/14214/how-much-current-consumption-when-we-use-internal-32k-rc)
- Nordic staff: unused RAM must be powered down manually to reach the no-retention figures; RAM is retained by default in System ON. — [DevZone 39396](https://devzone.nordicsemi.com/f/nordic-q-a/39396/nrf52840-ram-retention-in-system-on-mode)
- In this firmware: RAM8 sections 3-5 (top 96 KB) are switched off once the image is confirmed (`supply_unused_ram_off` from `note_image_state`), and section 2 (0x20020000, 32 KB) is powered only for the ACTIVE history. — [firmware/src/supply.c](firmware/src/supply.c), [firmware/src/ota.c](firmware/src/ota.c)
- The repo's own budget: idle about 14 µA = IMU low-power at 15 Hz about 7 µA + BQ29700 4 µA + "nRF System ON + RC"; HANDOVER uses 14-17 µA. — [README.md](README.md), [HANDOVER.md](HANDOVER.md)
- 32 kHz source is the RC (`CONFIG_CLOCK_CONTROL_NRF_K32SRC_RC=y`), and prj.conf sets no calibration options, so Zephyr defaults apply. — [firmware/boards/shotpuck/shotpuck/shotpuck_nrf52840_defconfig](firmware/boards/shotpuck/shotpuck/shotpuck_nrf52840_defconfig), [firmware/prj.conf](firmware/prj.conf)

### Inferences
- RAM retention cost from the PS: (2.35 − 0.97) µA / 256 KB ≈ 5.4 nA per KB (≈ 0.17 µA per 32 KB RAM8 section). Idle keeps RAM0-7 (64 KB) plus RAM8 sections 0-1 (64 KB) = 128 KB ≈ 0.69 µA. That matches the HANDOVER's "about 0.7 µA saved" for the switched-off part.
- Estimated SoC idle: 3.16 µA (full retention + RTC on LFRC) − 0.69 µA (128 KB off) ≈ 2.5 µA, + about 0.3 µA high-voltage-mode adder (1.29 − 0.97 µA from the 5 V rows), + about 1 µA LFRC calibration (question 2), + about 0.1-0.2 µA periodic CPU wakes (question 9) ≈ **3.9-4.0 µA (range 3.5-4.5 µA)**, all unmeasured.
- The GPIOTE rows show why the IMU interrupt type matters: IN-event wake costs about 15 µA more than PORT/SENSE wake (question 5; already handled here).

### Gaps
- The PS sleep table gives no DC/DC-vs-LDO split and no VDDH current at 3.7-4.2 V with REGOUT0 = 3.0 V; only the 5 V / 3.3 V rows exist. Nordic's Online Power Profiler was not consulted (interactive tool).
- The u-blox BMD-340 data sheet was not fetched; module-level sleep figures were not checked against the bare-chip PS.
- No measurement of this board's idle current exists yet. JP1 measurement is still pending per HANDOVER.

## 2. LFRC calibration: cost, Zephyr 4.1 Kconfig names, and can it be relaxed while idle?

### Takeaway
With Zephyr defaults the HFXO starts every 4 s for a temperature check, and a full LFRC calibration (about 17 ms at about 460 µA) runs at least every 8 s. That costs about 1 µA average, about 6 % of idle. Tuning `CONFIG_CLOCK_CONTROL_NRF_CALIBRATION_PERIOD` / `_MAX_SKIP` can save about 0.5-0.85 µA (3-5 % of idle, about 0.8-1.6 % of weekly drain). Zephyr has no "idle-only" interval, so the BLE clock accuracy needs to be checked.

### Cited Findings
- Zephyr 4.1 Kconfig (`drivers/clock_control/Kconfig.nrf`): `CONFIG_CLOCK_CONTROL_NRF_K32SRC_RC_CALIBRATION` (default y), `CONFIG_CLOCK_CONTROL_NRF_CALIBRATION_PERIOD` (default 4000 ms, "Calibration opportunity period"), `CONFIG_CLOCK_CONTROL_NRF_CALIBRATION_MAX_SKIP` (default 1; 0 = calibrate every period regardless of temperature), `CONFIG_CLOCK_CONTROL_NRF_CALIBRATION_TEMP_DIFF` (default 2 = 0.5 °C), `CONFIG_CLOCK_CONTROL_NRF_CALIBRATION_LF_ALWAYS_ON` (default y with the RTC system timer). — [C:/zp/zephyr/drivers/clock_control/Kconfig.nrf](C:/zp/zephyr/drivers/clock_control/Kconfig.nrf)
- Zephyr's cycle (`nrf_clock_calibration.c`): the backoff timer fires every PERIOD → `hf_request()` (HFXO on) → in the HF callback, a temperature work item is submitted on the system workqueue → if the change is < TEMP_DIFF and skips remain, `start_cycle()` releases the HFXO; otherwise `nrfx_clock_calibration_start()`. **So the HFXO is started every period even when calibration is skipped.** With MAX_SKIP = 0 the temperature sensor is not used, and every period calibrates. — [C:/zp/zephyr/drivers/clock_control/nrf_clock_calibration.c](C:/zp/zephyr/drivers/clock_control/nrf_clock_calibration.c)
- Runtime API available: `z_nrf_clock_calibration_force_start()`, `z_nrf_clock_calibration_count()`, `z_nrf_clock_calibration_skips_count()`. There is no runtime period setter. — [C:/zp/zephyr/include/zephyr/drivers/clock_control/nrf_clock_control.h](C:/zp/zephyr/include/zephyr/drivers/clock_control/nrf_clock_control.h)
- The TEMP driver itself requests the HFCLK through the onoff manager for each reading (power.c also reads TEMP every 60 s housekeeping). — [C:/zp/zephyr/drivers/sensor/nordic/temp/temp_nrf5.c](C:/zp/zephyr/drivers/sensor/nordic/temp/temp_nrf5.c), [firmware/src/power.c](firmware/src/power.c)
- Nordic (nRF52832 thread): a calibration takes about 17 ms at about 460 µA average = 7.82 µC; at an 8 s interval that is 0.98 µA. The recommended setting (nRF5 SDK, for 250 ppm) is a 4 s timer with calibration at least every 8 s, or every 4 s if the temperature changed by more than 0.5 °C. — [DevZone 14214](https://devzone.nordicsemi.com/f/nordic-q-a/14214/how-much-current-consumption-when-we-use-internal-32k-rc)
- Field report (nRF52805, RC-only): current spikes every 4 and 8 s raise the System ON sleep average from about 2 µA to about 3.5 µA. — [DevZone 45361 / related search extract](https://devzone.nordicsemi.com/f/nordic-q-a/45361/sdk15-2-impact-of-nrf_sdh_clock_lf_rc_-setup-on-performance/180388)
- LFRC calibration uses the HFXO as reference (HFCLKSTART needed). CTIV range 0.25-31.75 s. The calibrated LFRC is specified as "32.768 kHz +/-500 ppm". — [nRF52840 PS, CLOCK](https://docs.nordicsemi.com/r/bundle/ps_nrf52840/page/clock.html)
- Zephyr's accuracy choice for nRF52 with the RC defaults to `CLOCK_CONTROL_NRF_K32SRC_500PPM` (the value given to the BLE controller). — [C:/zp/zephyr/drivers/clock_control/Kconfig.nrf](C:/zp/zephyr/drivers/clock_control/Kconfig.nrf)
- An NCS/Zephyr user found that longer calibration periods did not remove an unrelated 180 µA offset on an nRF52805, and that disabling calibration made the device misbehave. — [DevZone 76049](https://devzone.nordicsemi.com/f/nordic-q-a/76049/high-current-consumption-offset-when-using-internal-rc-oscillator-as-compared-to-crystal-oscillator-for-low-frequency-clock)

### Inferences
- Current cost with defaults: about 0.98 µA (calibration ≥ every 8 s) + an HFXO start and TEMP read on the skipped 4 s cycles (HFXO debounce alone is 256 µs by default per the CLOCK chapter; estimated ≤ 0.1 µA) ≈ **1.0-1.1 µA**.
- Option A, `CALIBRATION_PERIOD=30000` + `MAX_SKIP=0`: 7.82 µC / 30 s ≈ 0.26 µA, with no 4 s HFXO starts and no temperature reaction for 30 s → **saves about 0.75-0.85 µA (4.4-5 % of idle; 0.12-0.14 mAh/week; 1.2-1.6 % of weekly)**.
- Option B, `PERIOD=8000-16000` + `MAX_SKIP=1`: calibration every 16-32 s, temperature checked every 8-16 s → saves about 0.5-0.75 µA.
- Option C (most "idle-only"): a long Kconfig period plus `z_nrf_clock_calibration_force_start()` from the main loop when going ACTIVE / starting advertising, and when power.c's 60 s die-temperature reading shows a change. This keeps BLE timing tight when the radio is used. It is unsupported use of a `z_` API, so it needs a careful review that it cannot race the driver's own cycle.
- Disabling calibration entirely saves about 1 µA but risks BLE failures (500 ppm budget) and uptime/wall-clock drift, because the shot timestamps come from uptime (`wire_clock_unix_ms(&wall_clock, uptime_ms)` in [firmware/src/main.c](firmware/src/main.c)). Not recommended.
- Risk for all options: a fast temperature swing (car to sun) between calibrations could take the LFRC beyond 500 ppm when BLE starts. The WDT (10 s) and the 2 s / 60 s timers only stretch by ppm, which is harmless.
- Status: **not done**. The defaults are in effect.

### Gaps
- No nRF52840-specific calibration duration or current found. The 17 ms / 460 µA figure is from a Nordic nRF52832-era answer.
- No data on LFRC drift versus temperature to size a safe period for 500 ppm. The PS temperature coefficient was not retrieved.

## 3. Clock-source alternatives: LFXO crystal, synthesized LFCLK, LFRC ULP mode

### Takeaway
A 32.768 kHz crystal would save about 1.4 µA in idle (about 8 %; 2.3-2.7 % of weekly drain) plus 1.3-2.9 µA whenever BLE is active, but it needs a hardware change. The synthesized clock is far worse (HFXO always on). LFRC ULP mode (0.7 → 0.3 µA) exists in the nRF52840 register map but Zephyr does not support it, and its accuracy is undocumented here.

### Cited Findings
- The calibrated LFRC is ±500 ppm. When better accuracy is needed, the LFXO must be used. LFSYNT avoids the crystal "but increases average power consumption". — [nRF52840 PS, CLOCK](https://docs.nordicsemi.com/r/bundle/ps_nrf52840/page/clock.html)
- LFRC 0.6 µA vs LFXO 0.25 µA (+0.35 µA). Window widening with LFRC (250+20 ppm) vs LFXO (20+20 ppm): +1.52 µA in a connection. Total LFRC penalty +2.85 µA connected, +1.33 µA advertising (Nordic answer, nRF52832). — [DevZone 14214](https://devzone.nordicsemi.com/f/nordic-q-a/14214/how-much-current-consumption-when-we-use-internal-32k-rc)
- With `CLOCK_CONTROL_NRF_K32SRC_SYNTH` a user measured about 200 µA idle (nRF52805, NCS 2.6.1), against about 2 µA expected. — [DevZone 110984](https://devzone.nordicsemi.com/f/nordic-q-a/110984/ncs-system-on-sleep-without-xo/482753)
- LFRC ULP mode run current 0.3 µA (PS v1.1) / 0.2 µA (OPS v0.5) vs 0.7 µA normal. — [nRF52840 PS v1.1](https://cdn.sparkfun.com/assets/e/c/3/1/7/Nano_BLE_MCU-nRF52840_PS_v1.1.pdf); [OPS v0.5](https://www.mouser.com/datasheet/2/297/nRF52840_OPS_v0.5-1074816.pdf)
- The nRF52840 MDK defines `CLOCK->LFRCMODE` (offset 0x5B4, Normal/ULP). No Zephyr driver or SoC file references LFRCMODE. Nordic staff: the LFRC must be stopped before the mode is changed. — [C:/zp/modules/hal/nordic/nrfx/mdk/nrf52840.h](C:/zp/modules/hal/nordic/nrfx/mdk/nrf52840.h); [DevZone 110984](https://devzone.nordicsemi.com/f/nordic-q-a/110984/ncs-system-on-sleep-without-xo/482753)
- The board file notes the BMD-340 has no 32.768 kHz crystal and P0.00/P0.01 are free. — [firmware/boards/shotpuck/shotpuck/shotpuck_nrf52840_defconfig](firmware/boards/shotpuck/shotpuck/shotpuck_nrf52840_defconfig)

### Inferences
- LFXO saving in idle: (0.7 − 0.23-0.25) µA run current + about 1.0 µA calibration ≈ **1.4-1.5 µA (≈ 8 % of idle; 0.23-0.24 mAh/week; 2.3-2.9 % of weekly)**. On top of that comes the advertising/connection saving while ACTIVE (outside idle). It needs a PCB revision (crystal + 2 load capacitors on P0.00/P0.01) and `CONFIG_CLOCK_CONTROL_NRF_K32SRC_XTAL`. It is not a firmware-only change, and MCUboot would need the same clock setting.
- LFSYNT: reject (about 100× worse).
- ULP: about 0.4 µA (2.4 % of idle, about 0.7 % of weekly) at best. It is speculative: Zephyr's LFCLK is always on, there is no driver support, the ULP accuracy and calibration behaviour are unknown, and it may break the 500 ppm BLE budget.

### Gaps
- LFXO run current for the nRF52840 itself was not confirmed (0.25 µA is the nRF52832 figure; 0.16 µA appeared in an unidentified Nordic table).
- LFRC ULP-mode accuracy, and whether ULP is available on the BMD-340's silicon revision, were not found.
- Whether the BMD-340 routes P0.00/P0.01 suitably for an external crystal (trace length, load capacitance) was not checked in the u-blox data sheet.

## 4. System OFF instead of System ON idle (wake on IMU INT1 via GPIO SENSE)

### Takeaway
System OFF in high-voltage mode is 0.95 µA (not 0.4 µA). Against the estimated 3.5-4.5 µA System ON SoC idle, that saves about 2.5-3.5 µA (15-21 % of idle; 0.41-0.57 mAh/week; 4.2-6.8 % of weekly drain). This is the largest SoC-only saving. The costs: every wake is a full reset through MCUboot and the app's init (likely 0.3-1 s, unmeasured), the uptime-based wall clock is lost, the 60 s battery guard stops, and an unconfirmed image would be reverted by the wake reset. It pays off only if spurious motion wakes are rare (roughly fewer than 2-10 per idle hour).

### Cited Findings
- System OFF 0.40 µA at normal supply; **0.95 µA with 5 V on VDDH and REG0 = 3.3 V**; 1.86 µA with full RAM retention. — [nRF52840 PS, Sleep](https://docs.nordicsemi.com/r/bundle/ps_nrf52840/page/_tmp/nrf52840/autodita/current/parameters.i_sleep.html)
- A GPIO DETECT signal wakes the device from System OFF, and the device then performs a system reset. RESETREAS must be cleared before System OFF or the device can wake immediately. After a GPIO wake RESETREAS shows the OFF bit (bit 16, 0x10000). One nRF52840 user saw RESETREAS read 0 after such a wake. — [Nordic System OFF doc (search extract)](https://docs.nordicsemi.com/r/xtYqc8FBbjZ9zOaVr~LAaw/lNZyWTRMCWsAlpB6lmiZvA); [DevZone 60238](https://devzone.nordicsemi.com/f/nordic-q-a/60238/unexpected-reset-reason-power_resetreas_off/245017); [DevZone 70270](https://devzone.nordicsemi.com/f/nordic-q-a/70270/reset-reason-register-resetreas-is-always-0-after-wakeup-from-system-off-via-gpio)
- MCUboot `CONFIG_BOOT_VALIDATE_SLOT0` defaults to y (the signature is checked on every boot), and the ECDSA implementation defaults to tinycrypt (CC310 only with nrfxlib). This project uses swap-using-move + ECDSA-P256. — [C:/zp/bootloader/mcuboot/boot/zephyr/Kconfig](C:/zp/bootloader/mcuboot/boot/zephyr/Kconfig); [firmware/sysbuild.conf](firmware/sysbuild.conf)
- Measured on an nRF52833: application + MCUboot 225 ms vs 8 ms for the application alone, and about 10 ms with `CONFIG_BOOT_VALIDATE_SLOT0=n`. nRF Desktop: about 980 ms MCUboot start with Fast Pair, 55 ms base. — [DevZone 94929](https://devzone.nordicsemi.com/f/nordic-q-a/94929/reduce-mcuboot-startup-time/401268); [DevZone 118608](https://devzone.nordicsemi.com/f/nordic-q-a/118608/reducing-startup-time-with-mcuboot-in-nrf-desktop/521626)
- App boot in this firmware also opens the 660 KB capture ring with a CRC walk (0.4-0.7 s before 0.3.9; CRC about 3.5× faster since). It loads settings/NVS, initialises BLE and the IMU, and starts in ACTIVE (`go_active()` at boot). — [HANDOVER.md](HANDOVER.md), [firmware/src/main.c](firmware/src/main.c)
- The RAM8 power-off setting survives a soft reset and a wake from System OFF. Touching switched-off RAM is a bus fault. — [HANDOVER.md](HANDOVER.md)
- `CONFIG_POWEROFF=y` is already enabled, and `power_off_until_charger()` already uses `sys_poweroff()` (VBUS wake) for the low-battery cut-off. — [firmware/prj.conf](firmware/prj.conf), [firmware/src/power.c](firmware/src/power.c)
- The wall clock is kept as an offset to uptime (`wire_clock_unix_ms(&wall_clock, uptime_ms)`) and is set from the phone. — [firmware/src/main.c](firmware/src/main.c)

### Inferences
- Saving: 3.5-4.5 µA → 0.95 µA ≈ **2.5-3.5 µA**. The IMU (still on VDD, which REG0 keeps up in System OFF) and the PCM are unchanged.
- Implementation sketch (Zephyr 4.1): after the 120 s advertising linger, with the image confirmed, no VBUS and no pending capture or flash work: configure the IMU wake (latched), read WAKE_UP_SRC so INT1 is low, `nrf_gpio_cfg_sense_input(3, NRF_GPIO_PIN_NOPULL, NRF_GPIO_PIN_SENSE_HIGH)`, clear RESETREAS, then `sys_poweroff()`. On boot, read the reset cause (OFF/DETECT) and go straight to ACTIVE.
- Shot risk: an archery shot normally follows the lift/draw by seconds, so a 0.3-1 s boot is probably tolerable. But the 2026-11-15 missed-shot problem (wake not firing) shows the wake path is fragile. Until the app is up, no FIFO data is read, and the pre-trigger aiming history starts only after boot. This would need the same field validation as fix 1 (high-pass wake).
- Energy break-even: one extra boot ≈ 0.5 s × about 4 mA ≈ 2 mC (estimate), and the saving is about 3 µA × 3600 s ≈ 10.8 mC per idle hour. So more than about 5 spurious wakes per idle hour (bow carried, case moved) cancels the gain. The ACTIVE period after a wake is the same in both designs.
- Functional losses: the wall clock (shots after a wake have no absolute time until a phone reconnects, unless the protocol logs relative times); the 60 s battery guard and charge control (no timer wake from System OFF on nRF52, so the cell would rely on the BQ29700's 2.8 V trip during long storage at the 3.3 V edge); the 2 s main-loop housekeeping.
- MCUboot interplay: entering System OFF before `boot_write_img_confirmed()` would make the wake reset revert a new image. System OFF must be gated on `ota_image_confirmed()`, which is the rule that "MCUboot's automatic revert must keep working" requires anyway. Keeping BOOT_VALIDATE_SLOT0 is the safer choice (about 0.2 s per wake by the nRF52833 analogue).
- Bonds, counts and captures are in flash, so they survive. The 0.4 µA vs 0.95 µA difference is the price of high-voltage mode.
- Status: **not done** (System OFF is used only for the low-battery cut-off with VBUS wake).

### Gaps
- No measured MCUboot or app boot time for this image (about 170 KB, tinycrypt P-256) on the nRF52840. It should be measured with a GPIO toggle.
- No nRF52840 figure for the wake latency from System OFF to reset vector (not found).
- Whether the LSM6DSV32X keeps its wake configuration and latched INT1 correctly across the nRF reset, given that the firmware does a SW_RESET in `accel_init`, is an IMU-side question.

## 5. GPIO interrupt configuration: GPIOTE IN event vs PORT/SENSE

### Takeaway
Already optimal. INT1 is configured as a level interrupt, which Zephyr's gpio_nrfx implements with SENSE/PORT, not a GPIOTE IN channel. That avoids the about 15 µA IN-event penalty. Any future change to an edge interrupt must keep SENSE (via `sense-edge-mask`).

### Cited Findings
- PS: full retention, wake on GPIOTE IN (event mode) 17.37 µA vs wake on GPIOTE PORT event 2.36 µA. — [nRF52840 PS, Sleep](https://docs.nordicsemi.com/r/bundle/ps_nrf52840/page/_tmp/nrf52840/autodita/current/parameters.i_sleep.html)
- Zephyr 4.1 `gpio_nrfx_pin_interrupt_configure`: a GPIOTE IN channel is allocated only if the mode is EDGE and the pin is not in the port's `sense-edge-mask` DT property. Level modes map to `NRFX_GPIOTE_TRIGGER_HIGH/LOW` (SENSE). — [C:/zp/zephyr/drivers/gpio/gpio_nrfx.c](C:/zp/zephyr/drivers/gpio/gpio_nrfx.c)
- This firmware: `gpio_pin_interrupt_configure_dt(&int1, GPIO_INT_LEVEL_ACTIVE)`. The ISR disables it, and the main loop re-arms. No `sense-edge-mask` is set in the DTS. — [firmware/src/accel.c](firmware/src/accel.c), [firmware/boards/shotpuck/shotpuck/shotpuck_nrf52840.dts](firmware/boards/shotpuck/shotpuck/shotpuck_nrf52840.dts)

### Inferences
- Saving from here: 0 µA (done). Avoided penalty: about 15 µA (would be about 88 % of the idle budget).
- Note: the INT1 pin is driven push-pull by the IMU, so there is no pull current.

### Gaps
- None material.

## 6. Other peripherals and pins that may draw in idle (TWIM, SAADC, TEMP, UART/RTT, RTC, WDT, QSPI, USB, NFC, floating pins)

### Takeaway
Most are already off or self-gating (UART and logging off, SAADC disabled after each read, no QSPI or USB stack, NFCT idle). Remaining items: the TWIM stays enabled (expected about 0 µA on production silicon), the watchdog runs in sleep (it forces the 2 s wake), and CHG_STAT is a connected-buffer input with no pull while VBUS is absent (possible leakage if it floats, unquantified). Debugger attachment must be excluded from any measurement.

### Cited Findings
- Release build: `CONFIG_SERIAL=n`, `CONFIG_CONSOLE=n`, `CONFIG_UART_CONSOLE=n`, `CONFIG_LOG=n`, `CONFIG_PRINTK=n`. The debug overlay adds RTT logging. — [firmware/prj.conf](firmware/prj.conf), [firmware/debug.conf](firmware/debug.conf)
- The Zephyr SAADC driver enables the SAADC for a read and calls `nrf_saadc_disable()` afterwards. power.c recalibrates the SAADC only on a 10 °C change. — [C:/zp/zephyr/drivers/adc/adc_nrfx_saadc.c](C:/zp/zephyr/drivers/adc/adc_nrfx_saadc.c), [firmware/src/power.c](firmware/src/power.c)
- TWIM: without `CONFIG_PM_DEVICE`, `pm_device_driver_init` runs RESUME → `nrfx_twim_enable()` + default pinctrl (internal pull-ups). The SUSPEND action (disable + sleep state with `low-power-enable`) is used only under device PM. — [C:/zp/zephyr/drivers/i2c/i2c_nrfx_twim_common.c](C:/zp/zephyr/drivers/i2c/i2c_nrfx_twim_common.c), [C:/zp/zephyr/subsys/pm/device.c](C:/zp/zephyr/subsys/pm/device.c), [firmware/boards/shotpuck/shotpuck/shotpuck_nrf52840-pinctrl.dtsi](firmware/boards/shotpuck/shotpuck/shotpuck_nrf52840-pinctrl.dtsi)
- TWIM static-current reports (0.4-0.66 mA) on DevZone trace to erratum 89 ("static current consumption of 0.4mA-0.5mA ... when using TWIM with GPIOTE", nRF52832), or to the HF clock left running. — [DevZone 61157](https://devzone.nordicsemi.com/f/nordic-q-a/61157/power-consumption-related-to-twim/248826); [DevZone 71398](https://devzone.nordicsemi.com/f/nordic-q-a/71398/power-consumption-rises-after-i2c-data-transfer/293460)
- In the nRF52840 MDK, erratum 89 applies only to the first nRF52840 silicon variant (variant 0x00), not to later revisions. — [C:/zp/modules/hal/nordic/nrfx/mdk/nrf52_erratas.h](C:/zp/modules/hal/nordic/nrfx/mdk/nrf52_erratas.h)
- An nRF52840 user saw about 50 µA leakage after disabling TWIM unless SDA/SCL were disconnected (v1 silicon), and different behaviour on v2. — [DevZone 81399](https://devzone.nordicsemi.com/f/nordic-q-a/81399/nrf52840-v1-vs-v2-twim-pins-leak-consumption-migitation)
- Watchdog: `wdt_setup(wdt, WDT_OPT_PAUSE_HALTED_BY_DBG)`. Without `WDT_OPT_PAUSE_IN_SLEEP` the nrfx driver sets `NRF_WDT_BEHAVIOUR_RUN_SLEEP_MASK`, so the WDT counts in sleep. The timeout is 10 s, fed by the 2 s idle poll. — [firmware/src/ota.c](firmware/src/ota.c), [C:/zp/zephyr/drivers/watchdog/wdt_nrfx.c](C:/zp/zephyr/drivers/watchdog/wdt_nrfx.c), [firmware/src/main.c](firmware/src/main.c)
- CHG_STAT: `GPIO_INPUT` without a pull while VBUS is absent (the pull-up only with VBUS). It reads the MCP73831 STAT through a BAT54J. — [firmware/src/power.c](firmware/src/power.c), [firmware/boards/shotpuck/shotpuck/shotpuck_nrf52840.dts](firmware/boards/shotpuck/shotpuck/shotpuck_nrf52840.dts)
- DevZone responders point to floating or pulled-up GPIOs as a common cause of unexplained nRF52840 sleep-current jumps (e.g. 187 µA). — [DevZone 127498](https://devzone.nordicsemi.com/f/nordic-q-a/127498/current-draw-jumps-187-ua-after-10-min-for-no-apparent-reason/563972)
- The factory reset pads (P0.26) are configured with `GPIO_PULL_UP`. gpio1 is disabled in app.overlay. — [firmware/boards/shotpuck/shotpuck/shotpuck_nrf52840.dts](firmware/boards/shotpuck/shotpuck/shotpuck_nrf52840.dts), [firmware/app.overlay](firmware/app.overlay)

### Inferences
- **CHG_STAT → `GPIO_DISCONNECTED` (or input + pull-down if the diode orientation allows) while VBUS is absent, and reconnect it when VBUS appears.** It costs nothing and removes any floating-input crowbar risk. Expected saving is 0 µA if the pin sits at a rail, and could be µA-scale if it floats mid-rail. It needs a measurement to quantify. Must keep charge detection working: VBUS is read from the POWER register, not this pin.
- TWIM: enabling `CONFIG_PM_DEVICE_RUNTIME` and suspending the bus in IDLE saves an expected ~0 µA on production nRF52840s (no erratum 89). The sleep pinctrl state also removes the internal pull-ups, which leaves SDA/SCL floating at the IMU → leakage risk. **Not recommended** unless a measurement shows an enabled-TWIM cost.
- WDT: its own current is unknown. It runs on the LFCLK, which is on anyway. The real cost is the 2 s wake it forces (question 9). `WDT_OPT_PAUSE_IN_SLEEP` would allow an unbounded idle wait, but then a thread stuck in a wait would never be reset. Raising the WDT timeout (e.g. 30-60 s) and the idle poll (e.g. 10-20 s) keeps the protection.
- RTC0 (BLE controller) and RTC1 (system timer) both run from the LFCLK. Their cost is inside the PS "wake on RTC" figure. No action.
- QSPI: not enabled in DTS (no external flash), so 0. USB: no USB stack (POWER VBUS detection only), so 0 beyond what the PS figures include. NFCT: P0.09/P0.10 unused, and NFCT stays idle unless its SENSE task is triggered, so 0.
- P0.26 pull-up: 0 µA normally. If moisture bridges the pads, about 3.0 V / 13 kΩ ≈ 0.23 mA (assuming the nominal internal pull-up of about 13 kΩ cited in HANDOVER). This is a robustness note, not a saving.
- Measurement hygiene: power-cycle after any SWD/RTT session before measuring. An attached or recently used debug interface keeps debug power domains on (general Nordic guidance, not sourced here).

### Gaps
- No PS or Nordic figure for the nRF52840 WDT run current, or for TWIM enabled-but-idle current.
- The CHG_STAT voltage with the charger unpowered was not measured or simulated (depends on the BAT54J leakage and the STAT pin's state).
- No source fetched for the debug-interface current after an SWD session on the nRF52840.

## 7. REG0 output voltage and regulator modes (DC/DC vs LDO; lowering VDD from 3.0 V)

### Takeaway
DC/DC vs LDO makes no documented difference in sleep. Both REG0 and REG1 already use DC/DC in the app, so no idle action is needed there. Lowering REGOUT0 (3.0 V → 1.8-2.4 V) has an unproven idle benefit: the IMU and the nRF quiescent currents are largely supply-independent, and a light-load regulator may not convert efficiently. It also breaks the LED at 1.8 V, and most target voltages need a UICR erase. **Not recommended without a measurement.**

### Cited Findings
- REG0 DC/DC is enabled since the first commit because `&reg0 { status = "okay"; }` makes Zephyr set DCDCEN0 (`nrf_power_dcdcen_vddh_set`) at boot, in MCUboot and in the app. REG1 runs DC/DC in the app via `app.overlay` (`regulator-initial-mode = <NRF5X_REG_MODE_DCDC>`) and LDO in MCUboot. A dev-page command switches REG0 at run time (not saved). — [C:/zp/zephyr/soc/nordic/nrf52/soc.c](C:/zp/zephyr/soc/nordic/nrf52/soc.c), [firmware/app.overlay](firmware/app.overlay), [firmware/src/supply.c](firmware/src/supply.c), [HANDOVER.md](HANDOVER.md), [docs/PROTOCOL.md](docs/PROTOCOL.md)
- REGOUT0 is programmed to 3.0 V at first boot, because the high-voltage default of 1.8 V is "too low for the LED". The LED is on P0.13 via 1 kΩ from VDD. — [firmware/src/power.c](firmware/src/power.c), [firmware/boards/shotpuck/shotpuck/shotpuck_nrf52840.dts](firmware/boards/shotpuck/shotpuck/shotpuck_nrf52840.dts)
- The PS sleep table has no regulator-mode column. The only high-voltage rows are 1.29 µA System ON and 0.95 µA System OFF at VDDH 5 V / REG0 3.3 V. — [nRF52840 PS, Sleep](https://docs.nordicsemi.com/r/bundle/ps_nrf52840/page/_tmp/nrf52840/autodita/current/parameters.i_sleep.html)
- u-blox (via HANDOVER): CPU 3.3 mA vs 6.3 mA, RX 4.6 vs 9.9 mA with DC/DC on. HANDOVER's estimate: the REG0 DC/DC gain is about 15-20 % on a full cell and about nothing near 3.3 V. — [HANDOVER.md](HANDOVER.md)
- The battery is measured via the SAADC VDDHDIV5 input against the internal 0.6 V reference, independent of VDD. — [firmware/src/power.c](firmware/src/power.c)

### Inferences
- DC/DC vs LDO in idle: expected about 0 µA difference (µA loads). DC/DC matters for ACTIVE. Keep both on DC/DC.
- Lower REGOUT0: for supply-independent loads (IMU at about 7 µA, nRF quiescent current) the cell current drops only if REG0 actually converts at µA load. If REG0 behaves linearly in sleep, cell current ≈ load current at any VDD. Upper bound if REG0 converted ideally: 3.0 → 2.4 V scales the VDD-side power by 0.8, i.e. ≤ about 2 µA on 10-11 µA of VDD loads; realistic is likely much less. Classed as **unknown, 0-2 µA**.
- Constraints: the LED needs VDD above its forward voltage plus drop (1.8 V likely fails; 2.4-2.7 V might work for red/green, check the LED). UICR bits can only go 1→0 without an erase. 3.0 V is VOUT=100b, so only 1.8 V (000b) is reachable in place; 2.1/2.4/2.7 V need ERASEUICR (which erases all UICR words) → risky in the field. I2C levels are fine (IMU and nRF share VDD). Flash writes and the SAADC battery reading are unaffected.
- The high-voltage-mode adder (about 0.3 µA System ON) is inherent: the 4.2 V cell cannot feed VDD directly.

### Gaps
- No Nordic data on REG0 quiescent current and light-load behaviour versus REGOUT0 setting and VDDH (3.3-4.2 V).
- No measurement of the board's idle current in REG0 DC/DC vs LDO mode (the dev-page switch makes this measurable without a new build).

## 8. nRF52840 errata relevant to sleep current

### Takeaway
Two errata apply to every nRF52840 revision and are not worked around in Zephyr 4.1. Erratum 87 (FPU: unexpected wake from System ON idle, and this firmware uses the FPU) is probably harmless with Zephyr's WFI idle. Erratum 246 (intermittent about 350 µA extra in idle after CPU/DMA RAM contention) is a real intermittent risk with TWIM/SAADC DMA. Erratum 89 (TWIM 400 µA) does not apply to production silicon.

### Cited Findings
- Erratum 87, "CPU: Unexpected wake from System ON Idle when using FPU". The trigger is FPU use. Workaround: clear the FPSCR exception bits (mask 0x9F), read FPSCR back, `NVIC_ClearPendingIRQ(FPU_IRQn)`, then WFE. A DevZone case traced high current to sprintf's float rounding without the workaround. — [Nordic errata (nRF52840 Eng D / Rev1)](https://infocenter.nordicsemi.com/topic/errata_nRF52840_EngD/ERR/nRF52840/EngineeringD/latest/anomaly_840_87.html); [DevZone 71502](https://devzone.nordicsemi.com/f/nordic-q-a/71502/high-current-when-using-ble-nus/295491)
- In the MDK, erratum 87 returns true for all nRF52840 variants (0x00-0x05 and default). Erratum 246 also applies to all. Erratum 89 applies only to variant 0x00. Erratum 197 applies only to variant 0x02. — [C:/zp/modules/hal/nordic/nrfx/mdk/nrf52_erratas.h](C:/zp/modules/hal/nordic/nrfx/mdk/nrf52_erratas.h)
- `CONFIG_FPU=y` in this firmware (the shot detector uses float). — [firmware/prj.conf](firmware/prj.conf)
- Zephyr 4.1 Cortex-M idle: SEVONPEND is set once at init, `arch_cpu_idle` sleeps with WFI, and `arch_cpu_atomic_idle` with WFE. There is no FPSCR/FPU_IRQn handling in the idle path. The optional `CONFIG_ARM_ON_ENTER_CPU_IDLE_HOOK` calls `z_arm_on_enter_cpu_idle()` before the wait instruction. — [C:/zp/zephyr/arch/arm/core/cortex_m/cpu_idle.c](C:/zp/zephyr/arch/arm/core/cortex_m/cpu_idle.c)
- Erratum 246, "System: Intermittent extra current consumption when going to sleep": about 350 µA extra in System ON idle when a high-speed master (CPU, CRYPTOCELL, USB, CTRL-AP) and a low-speed peripheral using DMA access the same RAM block with specific timing. Nordic explains the 16 MHz clock is not released until the next DMA transaction (example: SAADC sampling occasionally). Nordic also suggests keeping peripheral buffers in a separate RAM area. — [Nordic errata 246 (nRF52840 Rev1)](https://infocenter.nordicsemi.com/topic/errata_nRF52840_Rev1/ERR/nRF52840/Rev1/latest/anomaly_840_246.html); [DevZone 76911](https://devzone.nordicsemi.com/f/nordic-q-a/76911/when-how-does-nrf52805-erratum-246-system-intermittent-extra-current-consumption-when-going-to-sleep-exactly-apply/317968)
- No erratum-246 workaround appears in the nRF52840 MDK system file or in Zephyr's nordic SoC code (grep found none). — [C:/zp/modules/hal/nordic/nrfx/mdk](C:/zp/modules/hal/nordic/nrfx/mdk), [C:/zp/zephyr/soc/nordic](C:/zp/zephyr/soc/nordic)

### Inferences
- Erratum 87: with WFI, a pending but NVIC-disabled FPU IRQ should not wake the core (ARMv7-M WFI semantics), but WFE paths would wake with SEVONPEND set. Expected saving from the workaround is 0, but the downside is mA-level (a CPU that never sleeps) if it does trigger. Cheap insurance: implement `z_arm_on_enter_cpu_idle()` with the Nordic workaround (`CONFIG_ARM_ON_ENTER_CPU_IDLE_HOOK=y`). Verify on hardware by checking `NVIC_GetPendingIRQ(FPU_IRQn)` and the idle current after float-heavy shot processing.
- Erratum 246: the idle-time DMA users are TWIM (`accel_idle_ok` register reads and the WAKE_UP_SRC read in housekeeping every 60 s) and SAADC (every 60 s). If triggered, it could add about 350 µA for up to 60 s, until the next DMA: about 5.8 µA average per occurrence per hour of idle. That is larger than most items here, but intermittent. Look for 350 µA plateaus in the first current measurements. Apply the Rev3 errata workaround if Nordic publishes one for the nRF52840.
- Erratum 89 (TWIM 400 µA with GPIOTE): not applicable to production BMD-340 silicon by the MDK table.

### Gaps
- The nRF52840 Rev3 text and workaround for erratum 246 were not retrieved (only the Rev1 description via search). The nRF52805 workaround address must not be reused on the nRF52840.
- Erratum 197's title and content were not retrieved (MDK says one variant only).
- The BMD-340-A-R's exact nRF52840 variant/revision code was not checked (read FICR INFO.VARIANT/PART on a board).

## 9. Wake frequency, tickless kernel, timer coalescing, constant-latency vs low-power sub-mode

### Takeaway
The kernel is tickless, and the chip stays in the low-power sub-mode (Zephyr triggers CONSTLAT only on nRF54L). The idle wake sources are the 2 s main-loop poll (forced by the 10 s watchdog), the 4 s calibration timer and the 60 s housekeeping; motion wakes only via INT1. Lengthening the idle poll and coalescing timers saves an estimated 0.05-0.3 µA (≤ 2 % of idle), so it is low priority.

### Cited Findings
- The main loop waits `k_sem_take(&wake_sem, K_MSEC(poll_period(active)))` with `IDLE_POLL_MS 2000`. Housekeeping runs every `HOUSEKEEP_IDLE_MS 60000` (battery guard: TEMP + SAADC, IMU check via I2C, status). Advertising stops `ADV_LINGER_MS 120000` after idle. — [firmware/src/main.c](firmware/src/main.c)
- The IMU INT1 in idle is routed only to wake-up (`MD1_INT1_WU`), not data-ready, so the 15 Hz ODR does not wake the CPU. — [firmware/src/accel.c](firmware/src/accel.c)
- Zephyr nRF defconfig: `SYS_CLOCK_TICKS_PER_SEC` defaults to 128 only if not tickless (31250 for GRTC). The nRF52 uses the RTC1 timer, tickless. — [C:/zp/zephyr/soc/nordic/Kconfig.defconfig](C:/zp/zephyr/soc/nordic/Kconfig.defconfig)
- The BT controller's radio code sets `TASKS_CONSTLAT` only under `CONFIG_SOC_COMPATIBLE_NRF54LX` and otherwise triggers `TASKS_LOWPWR`. — [C:/zp/zephyr/subsys/bluetooth/controller/ll_sw/nordic/hal/nrf5/radio/radio.c](C:/zp/zephyr/subsys/bluetooth/controller/ll_sw/nordic/hal/nrf5/radio/radio.c)
- u-blox CPU run current with DC/DC: 3.3 mA (via HANDOVER). — [HANDOVER.md](HANDOVER.md)

### Inferences
- 2 s poll cost (estimate): wake + loop ≈ 50-200 µs at about 3.3 mA ≈ 0.17-0.66 µC per wake ≈ 0.08-0.33 µA. Going to 8 s (WDT 10 s) or 20 s (WDT raised to 30-60 s) saves **about 0.05-0.3 µA (0.3-1.8 % of idle; ≤ 0.6 % of weekly)**. The loop must still notice VBUS (charger polling already uses its own period), claim/pairing and the advertising linger. Those are idle-state-only checks, and INT1/BLE events post `wake_sem` anyway.
- Coalescing: make the calibration period a multiple of the idle poll, or drive calibration from the loop (question 2 option C), to merge wakes. The gain is a few tenths of a µA at most.
- Housekeeping 60 s → 120-300 s: < 0.05 µA (estimate), but it slows the low-battery cut-off (3 readings) and the charge-control reaction. Not worth it.
- Constant latency: already low-power. No action.

### Gaps
- The idle wake duration of the main loop was not measured (DWT cycle counter or GPIO toggle recommended).
- No source for the nRF52840 per-wake overhead (regulator/HFINT ramp-up) at µs scale.

## 10. Ranked summary of SoC-level idle measures (estimates; 1 µA = 5.9 % of idle = 1.65-1.93 % of weekly drain)

### Takeaway
Firmware-only options in System ON together save about 1 µA (about 6 % of idle, about 2 % of weekly drain), mostly from LFRC calibration tuning. System OFF idle saves about 2.5-3.5 µA (15-21 % of idle, 4-7 % of weekly) but changes wake and boot behaviour. A 32 kHz crystal saves about 1.4 µA (hardware revision). Several zero-cost "insurance" items (CHG_STAT disconnect, erratum 87 hook, erratum 246 check) protect against large unexpected losses.

### Cited Findings
- Per-item sources are given under questions 1-9. Key anchors: PS sleep table ([nRF52840 PS, Sleep](https://docs.nordicsemi.com/r/bundle/ps_nrf52840/page/_tmp/nrf52840/autodita/current/parameters.i_sleep.html)), calibration cost ([DevZone 14214](https://devzone.nordicsemi.com/f/nordic-q-a/14214/how-much-current-consumption-when-we-use-internal-32k-rc)), Zephyr calibration cycle ([nrf_clock_calibration.c](C:/zp/zephyr/drivers/clock_control/nrf_clock_calibration.c)), erratum 246 ([Nordic errata 246](https://infocenter.nordicsemi.com/topic/errata_nRF52840_Rev1/ERR/nRF52840/Rev1/latest/anomaly_840_246.html)).

### Inferences
| # | Measure | Est. idle saving | % of 17 µA | % weekly | Effort / risk | Status here |
|---|---|---|---|---|---|---|
| 1 | System OFF after the advertising linger, INT1 SENSE wake (gate on confirmed image, no VBUS, no pending work) | 2.5-3.5 µA | 15-21 % | 4.2-6.8 % | High: boot 0.3-1 s per wake (unmeasured), wall clock and 60 s battery guard lost, revert risk if unconfirmed, spurious-wake break-even about 5/h | Not done (System OFF only at the low-battery cut-off) |
| 2 | 32.768 kHz crystal on P0.00/P0.01 (`K32SRC_XTAL`), no calibration | about 1.4-1.5 µA (+1.3-2.9 µA during BLE) | about 8 % | 2.3-2.9 % | PCB revision. Best accuracy for BLE and timestamps | Not possible on current PCB |
| 3 | LFRC calibration: `CALIBRATION_PERIOD=30000`, `MAX_SKIP=0` (or 8-16 s, skip 1; or force-start from app) | 0.5-0.85 µA | 3-5 % | 0.8-1.6 % | Low code effort. BLE 500 ppm margin under fast temperature change to verify | Not done (defaults 4 s / skip 1 / 0.5 °C) |
| 4 | LFRC ULP mode (LFRCMODE) | ≤ 0.4 µA | ≤ 2.4 % | ≤ 0.8 % | No Zephyr support, accuracy unknown | Not done; speculative |
| 5 | Idle poll 2 s → 8-20 s (raise WDT timeout accordingly) | 0.05-0.3 µA | 0.3-1.8 % | ≤ 0.6 % | Low. Keep WDT running in sleep | Not done |
| 6 | Shrink app RAM below 0x20018000 and switch off RAM8 section 1 | about 0.17 µA | about 1 % | about 0.3 % | Needs about 19 KB of RAM cut. Low ROI | Sections 2 (idle) and 3-5 already off |
| 7 | CHG_STAT `GPIO_DISCONNECTED` without VBUS | 0 to unknown (µA if floating) | ? | ? | Trivial | Not done |
| 8 | Erratum 87 workaround via `CONFIG_ARM_ON_ENTER_CPU_IDLE_HOOK` | 0 expected (protects against mA-level fault) | - | - | Trivial | Not done |
| 9 | Erratum 246 check (350 µA plateaus after TWIM/SAADC DMA) and workaround if confirmed | 0 expected (protects against about 6 µA/h per event) | - | - | Measurement first | Not done |
| 10 | GPIO INT1 via SENSE (level), not GPIOTE IN | (avoids about 15 µA) | - | - | - | **Done** |
| 11 | RAM8 sections 3-5 off when confirmed, section 2 only when ACTIVE | (about 0.5-0.7 µA already saved) | - | - | - | **Done** |
| 12 | UART/console/log off; SAADC disabled after reads; no QSPI/USB; low-power sub-mode; tickless | (already 0) | - | - | - | **Done** |
| 13 | REG0/REG1 DC/DC | about 0 in idle | - | - | - | **Done** (both DC/DC in the app) |
| 14 | Lower REGOUT0 (3.0 → 1.8-2.7 V) | unknown, 0-2 µA | 0-12 % | 0-4 % | LED fails at 1.8 V; UICR erase needed for 2.1-2.7 V | Not done; not recommended without a measurement |
| 15 | TWIM runtime PM (suspend in idle) | about 0 expected | - | - | Floating bus risk | Not done; not recommended |
| 16 | LFSYNT clock | negative (about +100 µA or more) | - | - | - | Rejected |

- Combined firmware-only System ON package (items 3 + 5 + 7 + 8 + 9): about 0.6-1.2 µA (3.5-7 % of idle; 1-2.3 % of weekly), plus insurance against intermittent large losses.
- Upper bound for any SoC measure: the SoC's whole idle share is estimated at about 3.5-4.5 µA of the 17 µA. Bigger idle gains must come from the IMU (about 7 µA) and the PCM (4 µA), which are outside this SoC scope.

### Gaps
- Every number in this table is an estimate from data sheets, DevZone answers and code reading. No idle-current measurement of the board exists yet. The first measurement (JP1 cut, debugger detached and power-cycled) should split: REG0 DC/DC vs LDO (dev-page switch), the calibration spikes (period 4 s / 8 s visible on a PPK2 trace), the 2 s poll wakes, and possible erratum 246 plateaus.
