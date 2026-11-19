# Cutting the ShotPuck's idle drain by a third

> **Status (2026-11-19, firmware 0.3.12):** done: #1 charge inhibit only with a cable, #5 two-stage IMU idle (15 Hz for 20 min, then 1.875 Hz), and the history RAM powered only while ACTIVE. Declined by Richard: #2 supervision timeout (Android phones only) and #6 LFRC calibration `MAX_SKIP` (wait for the rev 0C crystal). Open: #3, #4, #7–#10. Rev 0C hardware items are not decided. All figures below are estimates from the research, not measurements.

Firmware alone can cut the ShotPuck's estimated ~17 µA idle current by about **6 µA, a third**, on the existing rev 0B board. That is worth roughly **1 mAh a week, about 12 % of the weekly drain**, and it keeps the wake on lift or draw. A next board revision can take idle to about **2 µA**, which is below the cell's own self-discharge. The two large firmware levers are running the LSM6DSV32X at its lowest rate, 1.875 Hz, after a still period (**−3.1 µA**) and putting the nRF52840 into System OFF after 30–60 min without motion (**−2.5 to −3.5 µA**). Relaxing LFRC calibration with one Kconfig line adds −0.7 to −0.85 µA. Everything else in the firmware is worth tenths of a µA.

Three conditional costs that the 17 µA figure leaves out are bigger than any of these:

- **A 30 µA leak through R3** whenever the die is at or below 0 °C, or at or above 45 °C, without a cable. It is a one-line fix.
- **A phone left connected** to an idle puck: 7–11 µA. On an iPhone that rejects the puck's 4 s supervision timeout it could be ~137 µA. The 4 s timeout breaks Apple's current 6 s minimum.
- **False wakes**, at about 7 µAh each. Nothing counts them yet.

Do the work in this order:

1. The two zero-risk fixes: drive CHG_INH only with VBUS present, and set a 6–18 s timeout.
2. Measure with JP1 cut.
3. The 1.875 Hz two-stage idle, once recorded lifts replay cleanly through the simulator.
4. The calibration Kconfig.
5. System OFF, last, once its boot time is measured.

For the next revision, three changes are worth the board space: a sub-µA protection IC (−3.4 µA), a 32.768 kHz crystal (−1.5 µA), and a nano-power wake accelerometer that lets the LSM6DSV32X be switched off (−6.7 µA).

Every figure here is an estimate from datasheets, Nordic forum answers and code reading. None has been measured on this board. Idle is also only about a third of the weekly drain, so the whole idle budget is worth at most about 2.75 mAh a week.

## Idle costs 2.75 mAh a week, and the IMU and protector draw two-thirds of it

With three 2 h sessions a week the puck is idle about 162 h. At HANDOVER's ~17 µA estimate that is **2.75 mAh a week** ([HANDOVER](../../HANDOVER.md)). The 6 h of ACTIVE at the README's 0.8–0.9 mA add 4.8–5.4 mAh ([README](../../README.md)), so the board draws **about 8 mAh a week**. A 65 mAh cell then lasts roughly eight weeks to empty. That ignores the 3.3 V cut-off and the cell's own self-discharge. EEMB allows self-discharge of up to 3 % a month, which is **≤2.7 µA equivalent, or ≤0.45 mAh a week** ([EEMB LIR1255 specification](https://www.opensourceinstruments.com/Electronics/Data/LIR1254.pdf)).

Throughout this report, **1 µA of continuous idle current = 0.162 mAh/week = 5.9 % of the idle drain ≈ 2 % of the weekly drain**. If ACTIVE is nearer HANDOVER's 0.55 mA for the sensors alone, each µA is about 2.7 % of the weekly drain. Idle is therefore a third of the weekly drain, or up to 45 % at the low ACTIVE figure. Removing idle current entirely would stretch a charge by about half; it would not double it.

Adding up the parts gives a little under the planning figure. That suggests the 17 µA carries about 2 µA of margin, or that something unaccounted for is drawing current. A measurement will tell which.

| Contributor (IDLE, no cable, 25 °C) | Typical | Upper bound | Basis |
|---|---|---|---|
| LSM6DSV32X accelerometer, LPM1 at 15 Hz, wake-up on INT1 | 7.1 µA | none published | [AN5763 Table 16](https://cdn.sparkfun.com/assets/0/a/c/c/9/an5763-lsm6dsv16x-6axis-imu-with-embedded-sensor-fusion-ai-qvar-for-highend-applications-stmicroelectronics.pdf), at 1.8 V; includes a 2.6–3 µA power-down floor |
| BQ29700 cell protector | 4.0 µA | 5.5 µA | [TI BQ2970x](https://www.ti.com/lit/ds/symlink/bq2970.pdf) INORMAL |
| nRF52840 System ON, RTC on LFRC, 128 KB retained, high-voltage mode | 2.5–2.8 µA | — | [nRF52840 PS sleep table](https://docs.nordicsemi.com/r/bundle/ps_nrf52840/page/_tmp/nrf52840/autodita/current/parameters.i_sleep.html) |
| LFRC calibration (HFXO every 4 s, a 17 ms calibration at least every 8 s) | ~1.0 µA | ~2 µA while the temperature moves | [Nordic DevZone 14214](https://devzone.nordicsemi.com/f/nordic-q-a/14214/how-much-current-consumption-when-we-use-internal-32k-rc), nRF52832 figure |
| CPU wakes (2 s poll, 60 s housekeeping) | 0.1–0.3 µA | — | code reading, [main.c](../../firmware/src/main.c) |
| MCP73831 reverse leakage (D1, D3 and the VBUS pin are in series with it) | 0.15–0.25 µA | 2 µA | [Microchip MCP73831](https://ww1.microchip.com/downloads/en/DeviceDoc/MCP73831-Family-Data-Sheet-DS20001984H.pdf) IDISCHARGE |
| Q2 gate leakage, diodes, LED, resistors, I²C pull-ups | ~0 | 2 µA (DMN2004DWK IGSS max) | topology, [design.py](../../hardware/design.py) |
| **Board total, bottom-up** | **≈15 µA** | **≈20 µA** | against the 17 µA planning figure |
| Cell self-discharge (not board current) | — | ≤2.7 µA equivalent | EEMB, ≤3 %/month |

The nRF52840 accounts for under a quarter of the idle current, and much of the firmware is already as lean as it can be:

- The battery is read on the SAADC's internal VDDH/5 input, so no divider drains the cell ([power.c](../../firmware/src/power.c)).
- INT1 is a level interrupt. Zephyr maps level interrupts to the pin's SENSE/PORT mechanism, not to a GPIOTE IN channel ([gpio_nrfx.c](file:///C:/zp/zephyr/drivers/gpio/gpio_nrfx.c)). That avoids the PS's **17.37 µA versus 2.36 µA** penalty for waking on a GPIOTE IN event ([nRF52840 PS sleep table](https://docs.nordicsemi.com/r/bundle/ps_nrf52840/page/_tmp/nrf52840/autodita/current/parameters.i_sleep.html)).
- Release builds have no UART, console or log ([prj.conf](../../firmware/prj.conf)).
- The SAADC is enabled only around each read.
- The kernel is tickless.
- Both regulators run DC/DC.
- The top 96 KB of RAM are switched off once the image is confirmed ([supply.c](../../firmware/src/supply.c)).

Each of these must survive future changes. An edge-triggered INT1 would add about 15 µA, nearly doubling idle.

The conditional costs are a bigger risk than the steady-state figure. The R3 leak is confirmed in the code. `update_temperature_gate()` runs on every `power_update()` whether or not a cable is present, and `update_charge_control()` then drives CHG_INH high whenever the die is at or below 0 °C or at or above 45 °C ([power.c](../../firmware/src/power.c)). CHG_INH drives 3.0 V into R3, a 100 kΩ resistor to ground ([design.py](../../hardware/design.py)). That is **30 µA, nearly double today's whole idle current, so the idle drain roughly triples**, for as long as a winter session or a hot car lasts.

## Ranked: what each measure saves, costs and risks

The tables below are ranked by the order I recommend doing the work, which weighs saving against risk and effort. They are not ranked by size. Savings are estimates. Weekly shares assume 8 mAh a week.

**Firmware on rev 0B**

| # | Measure | Saving (estimate) | Share of 17 µA | mAh/week | Share of weekly | Cost and risk | Wake and shot safety |
|---|---|---|---|---|---|---|---|
| 1 | Drive CHG_INH only while VBUS is present | 0 at 1–44 °C; removes **30 µA** at die ≤0 °C or ≥45 °C | up to 176 % while it lasts | 0.72 per day spent cold or hot | ~9 % per such day | One condition in `power.c`; none | Not affected |
| 2 | Supervision timeout 4 s → 6–18 s in both parameter sets | 0 if iOS accepts 4 s today; avoids a possible ~137 µA connected-idle fallback | — | up to 3.3 per connected day if it bites | — | One Kconfig value and one constant; a lost link is noticed later | Not affected |
| 3 | CHG_STAT set to `GPIO_DISCONNECTED` without VBUS; erratum 87 idle hook | 0 expected; guards against µA (floating input) and mA (CPU never sleeps) faults | — | — | — | A few lines each | Not affected |
| 4 | Wake counter and negotiated connection parameters logged in DIAG | 0 itself; sizes false wakes (10/day ≈ 3 µA equivalent) and the phone-link cost | — | 0.5 at 10 false wakes/day | 6 % | Small | Not affected |
| 5 | Two-stage IMU idle: 15 Hz for N min, then LPM1 1.875 Hz through power-down and fast-settle | **3.1 µA** | 18 % | 0.50 | 6 % | Register writes and a timer; up to 0.47 s more wake latency in the deep stage; filter model at ODR/10–20 unverified | Changes the wake: replay the recordings first |
| 6 | LFRC calibration `MAX_SKIP` 1 → 15 | 0.7–0.85 µA | 4–5 % | 0.11–0.14 | 1.4–1.7 % | One Kconfig line; ±500 ppm margin under fast temperature swings | Not affected |
| 7 | Idle-phone policy: disconnect after 5–10 min of IDLE with nothing pending, or an Apple-legal low-duty idle tier | **7–11 µA** while a phone stays connected (tier: 3–7 µA) | 41–65 % | 1.1–1.8 if a phone sits in range all week | 14–22 % | App must treat the drop as routine; check what archesk does first | Not affected: shots are logged and replayed |
| 8 | Advertising linger at a 2 s interval instead of 0.5 s (or a 30–60 s linger) | 0.2–0.3 µA weekly average | 1–2 % | 0.03–0.05 | <1 % | One constant; a phone opened late finds the puck asleep sooner | Not affected |
| 9 | System OFF after 30–60 min still; only with a confirmed image, no VBUS, no link, no pending write | **2.5–3.5 µA** (1.7–2.7 µA on top of #6) | 15–21 % | 0.4–0.55 | 5–7 % | Moderate code; 0.3–1 s boot per wake; wall clock lost until SET_TIME; no 60 s battery guard while off; loses energy above ~5 spurious wakes per idle hour | Changes the wake: boot time must be measured |
| 10 | Event-driven idle loop (2 s poll → up to 60 s, watchdog timeout ≥2× the longest sleep) | 0.05–0.3 µA | 0.3–1.8 % | 0.01–0.05 | ≤0.6 % | Refactor of the watchdog and recovery paths; reset-pad hold reacts later | Not affected |

Items 1–3, 5, 6 and 8 together save **about 4 µA**: 24 % of idle, 0.65 mAh/week, about 8 % of the weekly drain. They also remove two conditional leaks. Adding System OFF brings the total to **about 6 µA**: 35 % of idle, ~1 mAh/week, ~12 % of the weekly drain. A charge then lasts about 9 weeks instead of 8. A phone policy (#7) adds 1–2 mAh/week on top, if phones really do stay connected.

**Hardware for the next revision**

| # | Change | Saving (estimate) | Share of 17 µA | mAh/week | Share of weekly | Cost and risk | Wake and shot safety |
|---|---|---|---|---|---|---|---|
| H1 | BQ29700 → ABLIC S-82M1A/S-82N1x protector | **3.4 µA** | 20 % | 0.55 | 7 % | New SNT-6A footprint; threshold variant and JLC stock unchecked; no firmware | Not affected |
| H2 | 32.768 kHz crystal on BMD-340 pads 13/14 | **1.4–1.5 µA**, plus 2.8 µA less while connected | 8–9 % | 0.23–0.24 | 3 % | Crystal and two capacitors on a crowded edge; `K32SRC_XTAL` in the app and MCUboot; replaces #6 | Not affected |
| H3 | Nano-power wake accelerometer (ADXL367 class), with the LSM6DSV32X on a load switch | **≈6.7 µA** (replaces #5) | 39 % | 1.1 | 14 % | New sensor, switch and driver; the LSM6DSV32X is re-initialised on every wake; I²C must not back-power it | Replaces the wake path: full re-validation on bows |
| H4 | R3 100 kΩ → ≥1 MΩ | Hardware backup for #1 (3 µA when asserted) | — | — | — | 0402 value change; Q1 gate leakage unchecked | Not affected |
| H5 | Ship mode (S-82N1B PS pin, or a latched load switch) | Storage only: ~8 µA → 0.05–4 µA | — | — | — | One GPIO and passives; how the PS pin releases is not yet read | Storage only |
| H6 | JP2 reset pads moved to the bottom side | Avoids ~230 µA if the pads bridge | — | — | — | Already rev 0C item 1 | Not affected |

**Measures to skip or reject**

| Measure | Estimated effect | Why not |
|---|---|---|
| Housekeeping 60 s → 120–300 s | ≤0.05 µA | Slows the three-reading low-battery cut-off |
| Switch off RAM8 section 1 | ~0.2 µA | Needs ~19 KB less RAM |
| LFRC ultra-low-power mode | ≤0.4 µA | No Zephyr support; accuracy undocumented |
| Lower REGOUT0 below 3.0 V | 0–2 µA, unknown | LED fails at 1.8 V; 2.1–2.7 V need a UICR erase |
| TWIM device power management | ~0 | The sleep pin state leaves SDA/SCL floating at the IMU |
| Lower TX power, fewer advertising channels, smaller payload | ≤4.7, ~13 and ~5 µA, only while advertising | Idle advertising is only the linger; costs link margin, discovery speed or the UUID scan filter |
| Declare 250 ppm with the RC oscillator | ~1.6 µA while connected | Nordic advises against it: missed anchors |
| SoftDevice Controller (nRF Connect SDK) | perhaps 1–1.5 µA while connected | SDK migration; MCUboot and OTA retest |
| Disable LFRC calibration | ~1 µA | BLE ±500 ppm budget and timestamp drift |
| LPM2/LPM3, tilt, significant motion, SFLP, FSM/MLC | worse or unknown | More current; tilt and significant motion need ≥30 Hz |
| IMU power-down in idle | −4.1 to −4.5 µA | No wake: breaks the requirement |
| Edge-triggered INT1; SDO_PU_EN or SHUB_PU_EN set; synthesized LF clock | +15 µA; +60–200 µA; ~+200 µA | Regressions to guard against |
| MCP73831 swap; external LDO for VDD | ≤0.25 µA; ≈0 or worse | Not worth a part |

The order follows from the risk to the wake, not from the size of the saving.

- **Items 1–3 first.** They are zero-risk and cost a few lines. Item 1 closes a leak bigger than the whole budget. Item 2 is a compliance fix that may also close one.
- **Item 4 and the JP1 measurements next.** False wakes and connected phones are the two biggest unknowns. Without data, a policy for either is guesswork.
- **The 1.875 Hz stage (#5).** It is the largest sure saving, but it changes the wake that 0.3.10 fixed only days ago. It should follow a simulator replay and the first field data from the 15 Hz high-pass wake.
- **System OFF (#9) last.** It stacks a cold boot onto that wake, so it waits for a measured boot time.

## Slowing the IMU to 1.875 Hz is the largest safe firmware saving

ST's current table for the LSM6DSV16X sibling puts LPM1 at **7.1 µA at 15 Hz and 4.0 µA at 1.875 Hz**, with power-down at 2.6 µA ([AN5763 Table 16](https://cdn.sparkfun.com/assets/0/a/c/c/9/an5763-lsm6dsv16x-6axis-imu-with-embedded-sensor-fusion-ai-qvar-for-highend-applications-stmicroelectronics.pdf)). That table matches every one of the six points the DSV32X datasheet publishes, except power-down, which the DSV32X datasheet gives as 3 µA ([ST DS13511 Table 4](https://www.st.com/resource/en/datasheet/lsm6dsv32x.pdf)).

There is no low-power 7.5 Hz rate. There is also **no ultra-low-power mode** on this part. Its accelerometer modes are power-down, LPM1–3, normal, high-performance and high-accuracy ODR, and wake-up is supported in all of them ([ST DS13511 §6.2](https://www.st.com/resource/en/datasheet/lsm6dsv32x.pdf)). An ST community user found the same ([ST community](https://community.st.com/mems-sensors-48/lsm6dsv32x-power-modes-137913)). HANDOVER's pointer to an "ultra-low-power mode (datasheet §6.2.1)" looks like a leftover from the LSM6DSO32. LPM1 at 1.875 Hz is the equivalent.

About 3 µA of the 7.1 µA is a floor that no register setting removes. LPM2 and LPM3 only buy lower noise, which the wake does not need: LPM1's 3.2 mg RMS noise is already about 29σ below the 93.75 mg threshold.

The concern at 1.875 Hz is the high-pass wake that fixed the bow-1 misses in 0.3.10. Its cutoff scales with ODR, as ODR/N with N from 10 to 800 ([ST DS13511 Table 69](https://www.st.com/resource/en/datasheet/lsm6dsv32x.pdf)). A first-order model says the slower rate makes the wake *more* sensitive to a slow lift, not less:

| Setting | Slowest ramp that trips 93.75 mg |
|---|---|
| 15 Hz, ODR/45 (today) | about 0.20 g/s |
| 1.875 Hz, ODR/10 | about 0.11 g/s |
| 1.875 Hz, ODR/20 | about 0.055 g/s |

The real costs are time and settling:

- **Time.** One sample arrives every 533 ms instead of every 67 ms, so the wake can come **up to 0.47 s later**.
- **Settling.** At 1.875 Hz the filter needs 27–39 samples (14–21 s) to settle unless it is fast-settled. Fast-settle applies only on leaving power-down ([AN5763 §3.6 and Table 17](https://cdn.sparkfun.com/assets/0/a/c/c/9/an5763-lsm6dsv16x-6axis-imu-with-embedded-sensor-fusion-ai-qvar-for-highend-applications-stmicroelectronics.pdf)). The firmware already passes through power-down on the way into IDLE, so a switch made by the firmware gets fast-settle at no cost.

ST does not publish the filter's equation, so the first-order model is itself an assumption.

The design that protects shots is a **two-stage idle**: 15 Hz for N minutes after the last motion, then 1.875 Hz. About 160 of the 162 idle hours fall in the long gaps between sessions. The saving is therefore **0.48–0.50 mAh/week for any N from 8.5 min to 2 h**, and N can be chosen for behaviour rather than power.

N should be long enough to cover breaks between ends at the range. Every mid-session wake then stays at today's 15 Hz. It also keeps the planned FIFO look-back ("fix 2") useful: the 1.5 KB FIFO holds about 14.6 s of history at 15 Hz, but only coarse history at 1.875 Hz ([ST DS13511, FIFO section](https://www.st.com/resource/en/datasheet/lsm6dsv32x.pdf)).

Only the first lift of a session then comes from the slow stage, and the archer nocks and draws seconds after it. The 0.3.10 evidence has the high-pass wake about 4 s before the 100° lift turn, and at least 1.4 s before release on 34 of 37 draws ([HANDOVER](../../HANDOVER.md)). That evidence comes from replaying 37 captures and a 24 s lift stream through the simulator. 0.3.10 itself reached two pucks only on 2026-11-19. The cheapest check needs no hardware: decimate the same recordings 256:1 to 1.875 Hz, replay them with N = 10, 20 and 45, and compare the wake lead time with today's 15 Hz/45.

The sensor can also step down by itself: with INACT_EN = 01 it drops to XL_INACT_ODR (1.875 Hz) when still and restores the CTRL1 rate "immediately" on activity ([AN5763 §5.6](https://cdn.sparkfun.com/assets/0/a/c/c/9/an5763-lsm6dsv16x-6axis-imu-with-embedded-sensor-fusion-ai-qvar-for-highend-applications-stmicroelectronics.pdf)). Two things count against it: its stillness timer tops out at 512 s at 15 Hz, and ST does not document what the high-pass filter does across that automatic switch. A firmware timer is the better choice.

If the replay shows slow lifts slipping under the threshold, the fallback is the "absolute" wake (SLOPE_FDS plus USR_OFF_ON_WU). It compares low-pass data with a stored rest vector, so it trips after about 5.4° of tilt however slowly the bow moves ([AN5763 §5.3](https://cdn.sparkfun.com/assets/0/a/c/c/9/an5763-lsm6dsv16x-6axis-imu-with-embedded-sensor-fusion-ai-qvar-for-highend-applications-stmicroelectronics.pdf)). It reacts to orientation, not vibration, so a draw from a bow that is already raised relies on the lift having woken the puck. It is untested here.

None of the embedded engines helps ([AN5763 §6.2–6.3 and Table 47](https://cdn.sparkfun.com/assets/0/a/c/c/9/an5763-lsm6dsv16x-6axis-imu-with-embedded-sensor-fusion-ai-qvar-for-highend-applications-stmicroelectronics.pdf)):

- Tilt and significant motion need at least 30 Hz (10.5 µA).
- Significant motion counts steps.
- SFLP needs the 262 µA gyroscope.
- FSM and MLC cannot run below 15 Hz and have no published current for this part.

Two register traps are worth a cheap health check. Setting SDO_PU_EN or SHUB_PU_EN would put a 30–50 kΩ internal pull-up against a grounded pin, which costs **60–200 µA**. IF_CFG and PIN_CTRL also survive a software reset ([ST DS13511 §9.2–9.3 and Table 23](https://www.st.com/resource/en/datasheet/lsm6dsv32x.pdf)). Reading back IF_CFG = 0x20 and PIN_CTRL at its default in the 60 s housekeeping costs nothing.

## System OFF and one Kconfig line shrink the nRF's 4 µA share

The nRF52840's idle share is about **3.5–4.5 µA** ([nRF52840 PS sleep table](https://docs.nordicsemi.com/r/bundle/ps_nrf52840/page/_tmp/nrf52840/autodita/current/parameters.i_sleep.html)):

| Item | Current |
|---|---|
| System ON, full RAM, RTC on LFRC | 3.16 µA |
| RAM already switched off (96 KB) | −0.7 µA |
| High-voltage mode | +0.3 µA |
| LFRC calibration | +1 µA |
| CPU wakes | +0.1–0.3 µA |

### LFRC calibration

Calibration is the cheapest µA to recover. With Zephyr's defaults (4000 ms period, MAX_SKIP 1, TEMP_DIFF 0.5 °C), the driver starts the HFXO and reads the die temperature every 4 s, and runs a full calibration at least every 8 s ([nrf_clock_calibration.c](file:///C:/zp/zephyr/drivers/clock_control/nrf_clock_calibration.c); [Kconfig.nrf](file:///C:/zp/zephyr/drivers/clock_control/Kconfig.nrf)). Nordic puts one calibration at about 17 ms at 460 µA, which is 7.82 µC, or **0.98 µA at one per 8 s** ([Nordic DevZone 14214](https://devzone.nordicsemi.com/f/nordic-q-a/14214/how-much-current-consumption-when-we-use-internal-32k-rc)). In a field report, the 4 s and 8 s spikes raised System ON sleep from about 2 to 3.5 µA ([Nordic DevZone 45361](https://devzone.nordicsemi.com/f/nordic-q-a/45361/sdk15-2-impact-of-nrf_sdh_clock_lf_rc_-setup-on-performance/180388)).

Setting `CONFIG_CLOCK_CONTROL_NRF_CALIBRATION_MAX_SKIP=15` keeps the 4 s temperature check and the 0.5 °C trigger, so the RC's main source of drift stays covered. At stable temperature it calibrates only every 64 s, which saves about **0.7–0.85 µA**. The alternative, a 30 s period with MAX_SKIP 0, saves about the same but stops reacting to temperature, so it is the worse trade.

The risk is the ±500 ppm the BLE controller is told to expect ([nRF52840 PS, CLOCK](https://docs.nordicsemi.com/r/bundle/ps_nrf52840/page/clock.html)). A fast temperature swing between calibrations, such as a puck taken from a car into the sun, is the case to test. Nordic's older guidance is to calibrate at least every 8 s. Before shipping the change, check it three ways: the `CONFIG_CLOCK_CONTROL_NRF_CALIBRATION_DEBUG` counters, a JP1 trace, and a link-stability test through a temperature swing. Turning calibration off altogether is not an option: shot timestamps are derived from uptime, and the uncalibrated RC is only ±5 % ([u-blox BMD-340 data sheet](https://content.u-blox.com/sites/default/files/BMD-340_DataSheet_UBX-19033353.pdf)).

### System OFF

System OFF is the structural saving. In high-voltage mode the PS gives **0.95 µA**, not the 0.40 µA normal-voltage figure ([nRF52840 PS sleep table](https://docs.nordicsemi.com/r/bundle/ps_nrf52840/page/_tmp/nrf52840/autodita/current/parameters.i_sleep.html)), so the saving over System ON is about 2.5–3.5 µA. The BMD-340's REG0 output can still supply up to 1 mA in System OFF ([u-blox BMD-340 data sheet](https://content.u-blox.com/sites/default/files/BMD-340_DataSheet_UBX-19033353.pdf)), so the IMU stays powered and armed.

The firmware already has the pieces. The low-battery cut-off calls `sys_poweroff()` through `power_off_until_charger()` ([power.c](../../firmware/src/power.c)). INT1's level configuration is what Zephyr's own Nordic System OFF sample arms before powering off ([system_off sample](file:///C:/zp/zephyr/samples/boards/nordic/system_off/src/main.c)).

Entry would require all of these:

- at least 30–60 min of IDLE;
- no VBUS;
- no link;
- no pending capture write;
- **a confirmed image**.

The last one is critical. A wake from System OFF is a reset, and a reset while the image is unconfirmed makes MCUboot revert it ([ota.c](../../firmware/src/ota.c)). Requiring `ota_image_confirmed()` keeps MCUboot's automatic revert for the case it is meant for: a new image that never gets confirmed. Before powering off, the firmware must also read WAKE_UP_SRC so that INT1 is low, and clear RESETREAS. Otherwise the puck can wake at once ([Nordic DevZone 60238](https://devzone.nordicsemi.com/f/nordic-q-a/60238/unexpected-reset-reason-power_resetreas_off/245017)).

The price is paid on every wake. MCUboot validates slot 0 with ECDSA on every boot. Measured startup times elsewhere:

- about 120 ms with ECDSA on an nRF52840 DK ([Nordic DevZone 118608](https://devzone.nordicsemi.com/f/nordic-q-a/118608/reducing-startup-time-with-mcuboot-in-nrf-desktop/521626));
- 225 ms for application plus MCUboot on an nRF52833, against about 10 ms without slot validation ([Nordic DevZone 94929](https://devzone.nordicsemi.com/f/nordic-q-a/94929/reduce-mcuboot-startup-time/401268)).

This app's own init is unmeasured; my guess is 0.2–0.5 s. It covers the capture-ring CRC walk, the BT settings, three battery readings and the IMU reset.

One extra boot costs roughly 1–2.5 mC, and System OFF saves about 10.8 mC per idle hour. So **more than about five spurious wakes per idle hour cancel the gain**. A 30–60 min entry delay keeps the breaks between ends out of System OFF.

System OFF also loses three things:

- **The wall clock.** Shots get `t_unix_ms = 0` until the phone sends SET_TIME ([PROTOCOL](../../docs/PROTOCOL.md)).
- **The RAM-only ring of rejected candidates.**
- **The 60 s battery guard.** The nRF52 has no timer wake from System OFF, so only the BQ29700's 2.8 V trip would remain.

Combined with the 1.875 Hz stage, the first lift of a session could wait up to ~0.5 s for a sample, and then for the boot, before ACTIVE starts. That fits inside the ~4 s between the lift wake and the 100° turn. It is still enough to shorten the aiming part of that session's first extended capture. This is why System OFF comes last: it needs a measured boot time and the same field validation as the 0.3.10 wake.

### Small items and insurance

The remaining firmware items are small.

- **The 2 s IDLE poll** costs about 0.05–0.3 µA. An event-driven loop could save most of it, but it rewires the watchdog and recovery paths.
- **The watchdog** causes no wakeups of its own. Pausing it in sleep would blind it to a deadlocked thread for no measurable gain ([wdt_nrfx.c](file:///C:/zp/zephyr/drivers/watchdog/wdt_nrfx.c)). If the poll is ever slowed, raise the timeout instead.
- **The 60 s housekeeping** (≤0.1 µA) sets the 3-minute low-battery cut-off and should stay as it is.
- **A synthesized LF clock** drew about 200 µA in one user's measurement ([Nordic DevZone 110984](https://devzone.nordicsemi.com/f/nordic-q-a/110984/ncs-system-on-sleep-without-xo/482753)), so it is rejected outright.

Three cheap insurance items guard against faults far larger than the budget:

- **CHG_STAT (P0.15)** is a connected input with no pull, and nothing drives it when VBUS is absent ([power.c](../../firmware/src/power.c)). Switching it to `GPIO_DISCONNECTED` until VBUS returns removes any mid-rail cross-conduction current. Floating pins are a common cause of unexplained nRF52840 sleep current ([Nordic DevZone 127498](https://devzone.nordicsemi.com/f/nordic-q-a/127498/current-draw-jumps-187-ua-after-10-min-for-no-apparent-reason/563972)).
- **Erratum 87** (unexpected wake from System ON idle when the FPU is used) applies to every nRF52840 revision ([Nordic erratum 87](https://infocenter.nordicsemi.com/topic/errata_nRF52840_EngD/ERR/nRF52840/EngineeringD/latest/anomaly_840_87.html)). The shot detector uses floats, and Zephyr 4.1's idle path does no FPSCR handling ([cpu_idle.c](file:///C:/zp/zephyr/arch/arm/core/cortex_m/cpu_idle.c)). A `z_arm_on_enter_cpu_idle()` hook with Nordic's workaround costs a few lines.
- **Erratum 246** (an intermittent extra ~350 µA after CPU and DMA access the same RAM block) also applies to every revision and has no workaround in Zephyr ([Nordic erratum 246](https://infocenter.nordicsemi.com/topic/errata_nRF52840_Rev1/ERR/nRF52840/Rev1/latest/anomaly_840_246.html)). In idle, the 60 s TWIM and SAADC transfers are the DMA users. The first JP1 trace should be searched for 350 µA plateaus lasting up to a minute.

## A phone left connected outweighs every firmware tweak

Once the 120 s linger ends, the radio costs nothing. The Zephyr controller requests the HFXO only around scheduled radio events, about 1.5 ms ahead of each ([lll_vendor.h](file:///C:/zp/zephyr/subsys/bluetooth/controller/ll_sw/nordic/lll/lll_vendor.h)). With no advertising and no link, nothing is scheduled. The radio's idle share is therefore the linger, plus any phone that stays connected.

### The advertising linger

Nordic's Online Power Profiler puts one connectable advertising event on the nRF52840, at 0 dBm with DC/DC, at **13.3 µC**. A DK measurement of the same kind of event with DC/DC off came to 20.4 µC ([Nordic DevZone 80728](https://devzone.nordicsemi.com/f/nordic-q-a/80728/apparent-discrepancy-between-online-power-profiler-measurements)). At the puck's 500–525 ms interval that is **about 26 µA**. That is two to three times the "~10 uA" in the ble.c comment, and above VALIDATION's 10–15 µA expectation ([ble.c](../../firmware/src/ble.c); [VALIDATION](../../docs/VALIDATION.md)). Another user measured about 20 µA on an nRF52840 kit where the profiler predicted 11 µA ([Nordic DevZone 104405](https://devzone.nordicsemi.com/f/nordic-q-a/104405/optimizing-advertising-power-nrf52840/449039)), so the profiler is no upper bound.

A 120 s linger costs about 0.86 µAh per idle entry. At three sessions of about 20 rests each with no phone connected, that is about 52 µAh a week, or roughly 0.3 µA averaged. A 2 s linger interval saves three-quarters of it and keeps the "open the app after the session" window. The phone then takes about one interval longer to find the puck.

### A phone that stays connected

A connected phone costs on a different scale. Each connection event the puck listens to costs about 4 µC. That figure is derived from profiler numbers, ±50 %. On top of it sits a **window-widening floor** that does not depend on interval or latency. The receiver opens early by (SCA_local + SCA_central) × the time since the last anchor, so the average cost is that ppm sum times the 6.26 mA RX current ([Nordic Academy](https://academy.nordicsemi.com/?p=46652); [nRF52840 PS radio currents](https://docs.nordicsemi.com/r/bundle/ps_nrf52840/page/_tmp/nrf52840/autodita/current/parameters.i_radio.html)). With the LFRC declared at ±500 ppm and a 50 ppm phone, the floor is **≈3.4 µA**. Nordic's own worked example gives +1.52 µA for 270 ppm ([Nordic DevZone 14214](https://devzone.nordicsemi.com/f/nordic-q-a/14214/how-much-current-consumption-when-we-use-internal-32k-rc)).

| Link parameters | Extra current while connected and idle (estimate) |
|---|---|
| 105–210 ms, latency 4 (today) | ≈7–11 µA |
| 495–510 ms, latency 10, timeout 18 s (Apple-legal idle tier) | ≈4.1 µA |
| 990–1005 ms, latency 4, with a 50 ppm crystal | ≈1.4 µA |

At today's parameters a connected idle puck draws **≈7–11 µA extra**, another 41–65 % of the idle budget. A simpler estimate without the widening floor gives 2–6 µA. A phone left in range all week would add 1.1–1.8 mAh. The firmware never drops an encrypted link ([claim.c](../../firmware/src/claim.c)), and the app guidance tells the app to stay connected through the end of a session ([archesk integration doc](../../../archesk/docs/shotpuck-integration.md)).

### The iPhone timeout risk

The bigger iPhone risk is compliance. An Apple DTS engineer, quoting the current Accessory Design Guidelines, gives these rules:

- supervision timeout of 6–18 s;
- interval max × (latency + 1) ≤ 6 s;
- timeout greater than three times that product.

He also warns that "iOS may still reject them and instead suggest another set" ([Apple Developer Forums 775811](https://developer.apple.com/forums/thread/775811)). Both of the puck's parameter sets use a **4 s timeout** (`CONFIG_BT_PERIPHERAL_PREF_TIMEOUT=400` in [prj.conf](../../firmware/prj.conf), and the same in `fast_param` in [ble.c](../../firmware/src/ble.c)). That passes only the older 2–6 s rule set that Punch Through still cites ([Punch Through](https://punchthrough.com/?p=15988)).

If iOS rejects the request and keeps a short default near 30 ms, a connected idle puck would draw on the order of **137 µA**, about 3.3 mAh a day. That figure is illustrative; Apple does not document the fallback interval. Raising the timeout to 6 s or more in both sets makes the request compliant at no power cost.

### Idle-link policy and the other radio settings

Either of two policies removes most of the remaining link cost:

- **Disconnect after 5–10 min of IDLE with nothing pending.** This removes the cost entirely. Advertising already restarts in `go_active()` on the next lift ([main.c](../../firmware/src/main.c)). Shots are logged in flash and the app resyncs with REPLAY ([README](../../README.md)), so no shot is at risk.
- **Add a third, idle-only parameter tier** at the Apple limit, for example 495–510 ms, latency 10, timeout 18 s. Notifications from the puck still go out within about 0.5 s, because Zephyr cancels latency when data is queued. Connected idle falls to about 4.1 µA, but writes from the phone can wait up to ~5.6 s.

Either policy needs a matching note in the archesk integration doc. Neither should be chosen before checking how archesk actually holds the link.

The other radio settings matter only during the linger:

- −40 dBm TX power saves at most ~18 % of an advertising event.
- One advertising channel instead of three saves about half.
- Dropping the 128-bit UUID saves ~5 µA while advertising, but breaks the app's scan filter.
- Declaring 250 ppm saves ~1.6 µA while connected, against Nordic's advice.
- Moving to Nordic's SoftDevice Controller means migrating to the nRF Connect SDK, for perhaps 1–1.5 µA while connected ([Nordic DevZone 114717](https://devzone.nordicsemi.com/f/nordic-q-a/114717/nordic-softdevice-controller-versus-zephyr-ble-controller-config_bt_ll_sw_split-y)).

## Three part swaps on the next board reach about 2 µA

On rev 0B, about 11 µA of idle sits in two parts that firmware cannot push below their floors: the BQ29700 at 4 µA typical and 5.5 µA max ([TI BQ2970x](https://www.ti.com/lit/ds/symlink/bq2970.pdf)), and the IMU with its ~3 µA power-down floor plus sampling. The next revision has three swaps worth their board space.

**1. A sub-µA protector.** ABLIC's S-82M1A/S-82N1x family draws **0.6 µA typical and 0.99 µA max** in operation, and 50 nA in power-down. Overcharge, overdischarge and overcurrent thresholds are selectable across the BQ29700's range ([ABLIC S-82M1/S-82N1](https://www.ablic.com/en/semicon/products/power-management-ic/lithium-ion-battery-protection-ic/s-82m1-82n1/)). That is **−3.4 µA, 20 % of idle and 0.55 mAh/week, with no firmware change**. The open costs:

- a new SNT-6A footprint;
- choosing the threshold code nearest 4.275 V / 2.8 V / 0.1 V;
- JLC stock;
- confirming the external-FET topology, which the product page does not state.

None of these is checked yet. The S-82N1B variant adds a power-saving pin (50 nA) that would also give a true ship mode.

**2. A 32.768 kHz crystal.** BMD-340 pads 13 and 14 (P0.00/P0.01) are the module's crystal pins, and they are unconnected on rev 0B ([u-blox BMD-340 data sheet](https://content.u-blox.com/sites/default/files/BMD-340_DataSheet_UBX-19033353.pdf); [design.py](../../hardware/design.py)). A crystal (CL ≤12.5 pF, ESR ≤100 kΩ) and two load capacitors remove the calibration (~1 µA) and the RC's higher run current (0.7 µA against 0.23 µA). That is **≈−1.5 µA always**, and it cuts the window-widening floor of any connection from ≈3.4 to ≈0.6 µA. The firmware needs `CLOCK_CONTROL_NRF_K32SRC_XTAL` in both the app and MCUboot. The crystal also takes about 0.4 s longer to start at boot ([Nordic Academy](https://academy.nordicsemi.com/?p=46652)).

**3. A nano-power wake sensor.** The largest hardware saving comes from no longer using a 7 µA IMU as the wake sensor. An ADXL367-class accelerometer is listed at 180 nA in motion-activated wake-up mode and 40 nA in standby ([Analog Devices ADXL367](https://www.analog.com/en/products/adxl367.html)). A TPS22916 load switch draws 10 nA off and 0.5 µA on ([TI TPS22916](https://ti.com/lit/pdf/SLVSDO5)). Together they would let the LSM6DSV32X be unpowered in idle: **≈−6.7 µA, 39 % of idle, 1.1 mAh/week**. The IMU's 3 µA floor would also disappear in System OFF.

This is also the riskiest change against "never miss a shot":

- The wake path that the 0.3.10 fix just tuned would be replaced by a different sensor and filter.
- The LSM6DSV32X would need power-up and full re-initialisation on every wake; gyro turn-on alone takes 40 ms.
- The I²C lines would have to be held so they cannot back-power the unpowered IMU.

The ADXL367 figures come from search summaries that give no test conditions, and no ST alternative was researched.

### Smaller hardware items

Raising R3 from 100 kΩ to ≥1 MΩ backs up the firmware CHG_INH fix in hardware, at 3 µA when asserted. Q1's gate leakage is unchecked: R3 must still hold Q1B off when the MCU is dead.

A ship mode can come from either of two designs. The first is the S-82N1B's PS pin, which turns the whole board off at ~50 nA. The second is a load switch between VBAT and VCCH, held on by the nRF and switched on by VBUS. It costs its 0.5 µA on-current in normal idle.

### Rev 0C items that would raise idle current

Several planned rev 0C items would raise idle current and need care:

- **A permanent cell divider.** 2 MΩ adds 2.1 µA. A 10 MΩ divider breaks the SAADC's 800 kΩ source limit unless a hold capacitor is added ([nRF52840 PS, SAADC](https://docs.nordicsemi.com/r/bundle/ps_nrf52840/page/saadc.html)).
- **The load-sharing Schottky.** Its reverse leakage at about 4 V adds directly to idle.
- **A MAX17048 or MAX17260 fuel gauge.** It adds 3–5 µA ([HANDOVER](../../HANDOVER.md)).
- **A BQ25180-class charger.** It adds about 3 µA of battery-side quiescent current unless held in ship mode ([TI BQ25180](https://www.ti.com/product/BQ25180)).

Replacing the MCP73831 (0.15–0.25 µA typical reverse leakage) or adding an external LDO for VDD is not worth a part.

### Combined result and storage

With the protector, the crystal, the wake sensor and firmware System OFF, idle falls to about **2 µA**:

| Part | Current |
|---|---|
| nRF52840 in System OFF | ≈0.95 µA |
| Protector | 0.6 µA |
| Charger reverse leakage | 0.2 µA |
| Wake sensor | 0.18 µA |

Idle drain drops to about 0.3 mAh/week and the weekly total to about 5.5 mAh, roughly 11–12 weeks per charge against about 8 today. At that point the cell's own ≤2.7 µA self-discharge is larger than the board's draw, so more board work has little left to gain.

Rev 0B can already hold a storage state without new hardware. The low-battery path's System OFF with the IMU in power-down draws about **8 µA**: nRF 0.95 µA, IMU 3 µA, BQ29700 4 µA, charger 0.2 µA. A full cell would last about eight months on the shelf, against about 4.5–5 months for an unused puck left in normal idle (15–17 µA plus self-discharge). That state cannot wake on a lift, only on VBUS, and the VBUS wake is itself still unverified on hardware. A true disconnect is impossible on rev 0B because the BQ29700 has no enable pin.

## Ten JP1 measurements decide which estimates hold

Every number above is an estimate:

- ST's current tables are at 1.8 V; the board runs the IMU at 3.0 V.
- The calibration and advertising charges are nRF52832-era or SoftDevice figures, not the Zephyr link layer on this module.
- HANDOVER's 17 µA has never been measured.

JP1 sits in series with the cell, so a power profiler across a cut JP1 sees the whole board. Two rules apply to every measurement:

- Use the release build. The debug build wakes every second to flush RTT logs.
- Detach the probe and power-cycle the board after any SWD session.

| # | Measurement | Setup | What it confirms | Expected (estimate) |
|---|---|---|---|---|
| 1 | Baseline idle | Release build, no phone, ≥3 min after IDLE (past the linger), 25 °C, averaged over ≥5 min | The 17 µA budget and the 15 µA bottom-up sum | 15–17 µA |
| 2 | Trace structure | Same, high sample rate | Calibration spikes at 4 s and 8 s (~1 µA), 2 s poll, 60 s housekeeping; erratum-246 plateaus at ~350 µA; any steady offset (CHG_STAT) | Spikes every 4 s and 8 s |
| 3 | IMU share | Test build with the IMU idle at 15 Hz, at 1.875 Hz, and in power-down | 7.1 / 4.0 / 2.6–3 µA at 3.0 V | ≈3.1 µA between 15 Hz and 1.875 Hz |
| 4 | REG0 DC/DC vs LDO | Dev-page regulator switch | Regulator behaviour at µA load | ≈0 difference |
| 5 | Advertising linger | First 120 s after IDLE, no phone | 10 µA (code comment) vs 26 µA (profiler) | ~26 µA above baseline |
| 6 | Connected idle | Android phone and iPhone; negotiated parameters logged in DIAG | 7–11 µA; whether iOS accepts the 4 s timeout | 7–11 µA, far more if rejected |
| 7 | Cold soak without cable | Freezer at ≤0 °C, before and after fix #1 | The 30 µA R3 leak | +30 µA before the fix |
| 8 | System OFF current and wake | Test command; IMU in wake configuration, then in power-down | 0.95 µA high-voltage figure; VDD stays up; INT1 and VBUS wake from OFF | ≈12 µA (15 Hz wake), ≈8 µA (IMU off) |
| 9 | Wake-to-ACTIVE time from System OFF | GPIO toggle or trace from the INT1 edge | MCUboot plus app init; break-even wake rate | 0.3–1 s |
| 10 | False wakes per day | Wake counter in DIAG over a normal week | Size of the false-wake cost | Unknown |

Rows 1–4 need no new firmware beyond a test build. They should come before any change, because they show whether the budget, the IMU share and the calibration spikes match this report's assumptions. Rows 6, 9 and 10 are the gates for the phone policy and for System OFF.

Row 8 also corrects a test target. VALIDATION expects "current under 1 µA" in the low-battery System OFF state ([VALIDATION](../../docs/VALIDATION.md)). The BQ29700 alone draws 4 µA, and the IMU's power-down floor adds about 3 µA, so about 8 µA is a pass on rev 0B, not a fault.

## Conclusion

This changes where the effort should go. The steady-state idle current is already close to what this board's parts allow. The nRF draws under a quarter of it, and the firmware is lean, so tuning the main loop, the watchdog or the housekeeping is not worth the risk.

The larger threats are conditional states that the 17 µA figure never included:

- a winter session or a hot car switching on R3;
- an iPhone rejecting the 4 s timeout;
- a phone left in range;
- an unknown false-wake rate;
- bridged JP2 pads;
- an erratum-246 plateau.

Any one of these can cost more than the whole optimisation package saves. Three of them are found only by instrumentation — a wake counter, logged connection parameters and a long JP1 trace — not by tuning. That makes the zero-risk fixes and the measurements the real first step, and the 1.875 Hz stage the first real saving.

There is also a ceiling. Idle is about a third of the weekly drain, so even the full rev 0C hardware plus System OFF stretches a charge only from about 8 weeks to 11–12. The 6 h of ACTIVE, dominated by the gyroscope, remain the larger term, and HANDOVER rules out duty-cycling the gyro unless asked.

Three project documents carry figures that this research contradicts, and they should be corrected when the work is done:

- HANDOVER's pointer to an IMU ultra-low-power mode: the LSM6DSV32X has none.
- The advertising figures in ble.c and VALIDATION, which are probably two to three times too low.
- VALIDATION's System OFF target of under 1 µA, which the protector and the IMU make unreachable on rev 0B.
