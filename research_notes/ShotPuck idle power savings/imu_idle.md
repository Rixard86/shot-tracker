# LSM6DSV32X IMU current while the puck is IDLE: options, wake behaviour and savings

Source key (used throughout):
- **DS** = LSM6DSV32X datasheet DS13511 Rev 2, Feb 2024 (local copy `\\lovegood\shared\Cursor\DS_lsm6dsv32x.pdf`; public copy https://www.st.com/resource/en/datasheet/lsm6dsv32x.pdf). Page numbers are the PDF's own "page N/198".
- **AN5763** = ST AN5763 Rev 2, Feb 2023, "LSM6DSV16X: 6-axis IMU with embedded sensor fusion, AI, Qvar" (https://cdn.sparkfun.com/assets/0/a/c/c/9/an5763-lsm6dsv16x-6axis-imu-with-embedded-sensor-fusion-ai-qvar-for-highend-applications-stmicroelectronics.pdf). It covers the **LSM6DSV16X**, the ±16 g sibling. The DSV32X's own application note is **AN6016** (named on DS p.1). I could not retrieve AN6016: st.com timed out twice. Where the DSV32X datasheet gives a value, it matches AN5763 (see Q1), except the power-down floor.
- Repository files are cited by path (read-only).

---

## Q1. Supply current per accelerometer mode and ODR. Which mode and ODR give the lowest current with a usable high-pass wake?

### Takeaway
LPM1 is the cheapest accelerometer mode at every ODR. The IMU draws **7.1 µA at 15 Hz** (today's IDLE) and **4.0 µA at 1.875 Hz**, the lowest ODR the part has, so 1.875 Hz saves 3.1 µA. Power-down still costs **2.6–3 µA**, so above that floor the accelerometer itself costs only 4.5 µA at 15 Hz and 1.4 µA at 1.875 Hz. There is no 7.5 Hz low-power rate: 7.5 Hz exists only in normal mode (100 µA) and high-performance mode (190 µA). The lowest current that still has a wake is **LPM1 at 1.875 Hz**, and wake-up is supported in every mode. Whether its high-pass wake still catches a slow lift has to be checked against the recorded data (Q2).

### Cited Findings
- AN5763 Table 16, "Power consumption (@ Vdd = 1.8 V, T = 25 °C)", accelerometer only, in µA ([AN5763 p.23](https://cdn.sparkfun.com/assets/0/a/c/c/9/an5763-lsm6dsv16x-6axis-imu-with-embedded-sensor-fusion-ai-qvar-for-highend-applications-stmicroelectronics.pdf)):

  | ODR | LPM1 (2 mean) | LPM2 (4 mean) | LPM3 (8 mean) |
  |---|---|---|---|
  | 1.875 Hz | **4.0** | 4.2 | 4.4 |
  | 7.5 Hz | n/a | n/a (row exists for the gyro only) | n/a |
  | 15 Hz | **7.1** | 7.8 | 9.2 |
  | 30 Hz | 10.5 | 12.0 | 14.6 |
  | 60 Hz | 17.0 | 20.0 | 25.6 |
  | 120 Hz | 29.5 | 35.1 | 46.6 |
  | 240 Hz | 54.5 | 66.1 | 88.6 |
  | Normal mode (all ODRs) | 100.0 | | |
  | High-performance (all ODRs) | 190.0 | | |
  | High-accuracy ODR (all ODRs) | 210.0 | | |
  | **Power-down (accel + gyro)** | **2.6** | | |

  The same table gives, for the gyroscope only: low-power mode 257 µA (7.5 Hz), 262 (15), 272 (30), 289 (60), 325 (120), 397 (240); high-performance 535; high-accuracy ODR 555. For both sensors together: high-performance 650 µA, high-accuracy ODR 670 µA. The gyroscope's power-down row is the same 2.6 µA.
- DSV32X datasheet Table 4 (@ Vdd = 1.8 V, T = 25 °C): LPM2 20 µA at 60 Hz and 4.2 µA at 1.875 Hz; LPM1 17 µA at 60 Hz and 4.0 µA at 1.875 Hz; normal mode 100 µA; high-performance 190 µA; accel + gyro high-performance 0.65 mA; **"Gyroscope and accelerometer current consumption during power-down" 3 µA** ([DS p.14, Table 4](file://lovegood/shared/Cursor/DS_lsm6dsv32x.pdf)). These match AN5763 Table 16 point for point, except power-down (3 µA here, 2.6 µA in AN5763).
- "Low-power mode is available for lower ODRs (1.875 Hz, 15 Hz, 30 Hz, 60 Hz, 120 Hz, 240 Hz)", and high-performance mode runs from 7.5 Hz up to 7.68 kHz. Footnote 10 of the ODR table: 1.875 Hz "is available when the accelerometer is in low-power mode" ([DS §6.2, p.31; p.13](file://lovegood/shared/Cursor/DS_lsm6dsv32x.pdf)).
- "The embedded functions based on accelerometer data (free-fall, 6D/4D, tap/double-tap, wake-up, activity/inactivity, stationary/motion, step counter, step detection, significant motion, tilt) and the FIFO batching functionality are supported in all modes" ([DS §6.2, p.31](file://lovegood/shared/Cursor/DS_lsm6dsv32x.pdf)).
- RMS noise in the low-power modes: LPM1 3.2 mg, LPM2 2.3 mg, LPM3 1.6 mg ([DS Table 3, p.12](file://lovegood/shared/Cursor/DS_lsm6dsv32x.pdf)). The low-power modes have no anti-aliasing filter: the LPF1 cutoff is 2300 Hz in LPM1, 912 Hz in LPM2 and 431 Hz in LPM3 ([DS Table 69 note, p.71](file://lovegood/shared/Cursor/DS_lsm6dsv32x.pdf); [AN5763 §3.1, p.16](https://cdn.sparkfun.com/assets/0/a/c/c/9/an5763-lsm6dsv16x-6axis-imu-with-embedded-sensor-fusion-ai-qvar-for-highend-applications-stmicroelectronics.pdf)).
- "In the low-power modes, the anti-aliasing filter is disabled … Increasing the number of averaged measurements allows reducing the noise, while decreasing them allows reducing the current consumption" ([AN5763 §3.1, p.16](https://cdn.sparkfun.com/assets/0/a/c/c/9/an5763-lsm6dsv16x-6axis-imu-with-embedded-sensor-fusion-ai-qvar-for-highend-applications-stmicroelectronics.pdf)).
- Current IDLE register values: CTRL1 = 0x43 (LPM1, 15 Hz); CTRL2 = 0 (gyro power-down); TAP_CFG0 = 0x11 (latched, SLOPE_FDS = 1); CTRL8 = 0x67 (HP_LPF2_XL_BW = 011, ±32 g); CTRL9 = 0x20 (XL_FASTSETTL_MODE); INACTIVITY_DUR = 0x24 (31.25 mg/LSB; XL_INACT_ODR left at its default 01); FUNCTIONS_ENABLE = 0x80 (INACT_EN = 00) ([accel.c `accel_set_idle`, `configure`](file:///C:/Users/rixar/SynologyDrive/Documents/Cursor/shot-tracker/firmware/src/accel.c); [accel_regs.h](file:///C:/Users/rixar/SynologyDrive/Documents/Cursor/shot-tracker/firmware/src/accel_regs.h)).
- An ST community user moving from the LSM6DSO32 found that LPM1 at the lowest ODR draws about the same as the DSO32's ULP mode, and about twice as much at the highest ODR (60 Hz against 52 Hz). Nobody from ST answered ([ST community, "LSM6DSV32X power modes"](https://community.st.com/mems-sensors-48/lsm6dsv32x-power-modes-137913)).

### Inferences
- The table is close to linear in ODR: about 0.21–0.24 µA per Hz on top of a ~3.6 µA intercept (LPM1, 1.875→15 Hz: 3.1 µA over 13.1 Hz; 1.875→240 Hz: 50.5 µA over 238 Hz). Each LPM1 sample costs roughly 0.22 µC at 1.8 V.
- Over the 162 h idle week:

  | IDLE setting | IMU current | Charge per week | Change against today |
  |---|---|---|---|
  | LPM1 15 Hz (today) | 7.1 µA | 1.15 mAh | — |
  | LPM1 1.875 Hz | 4.0 µA | 0.65 mAh | **−0.50 mAh** (−3.1 µA) |
  | LPM1 30 Hz | 10.5 µA | 1.70 mAh | +0.55 mAh |
  | LPM2 15 Hz | 7.8 µA | 1.26 mAh | +0.11 mAh |
  | Power-down (no wake) | 2.6–3 µA | 0.42–0.49 mAh | −0.66 to −0.73 mAh, **but it cannot wake** |

- Against the ~17 µA whole-board estimate, 1.875 Hz removes about 18 % of the idle current. For the full week (6 h ACTIVE at 0.55–0.9 mA, so 3.3–5.4 mAh, plus 2.75 mAh idle, 6.1–8.2 mAh in total), the saving is about 6–8 % of the weekly charge.
- LPM2 and LPM3 cost more and buy only lower noise. Even LPM1 noise (3.2 mg RMS) sits about 29σ below the 93.75 mg threshold, so LPM1 is the right mode. Normal and high-performance mode (100/190 µA) are out of the question for IDLE.
- About 2.6–3 µA of the IMU's idle current is a floor that no register setting removes. Only cutting the IMU's VDD would, and the board has no switch: VDD is fed straight from the nRF REG0 ([design.py header](file:///C:/Users/rixar/SynologyDrive/Documents/Cursor/shot-tracker/hardware/design.py)). The same floor makes VALIDATION's "System OFF (current under 1 µA)" target unreachable while the IMU stays powered, if VDD stays up in System OFF. That is an MCU question to confirm.
- VALIDATION's "about 7 µA, interpolated from the datasheet" for LPM1 at 15 Hz ([VALIDATION.md, Standby current](file:///C:/Users/rixar/SynologyDrive/Documents/Cursor/shot-tracker/docs/VALIDATION.md)) agrees with the 7.1 µA in AN5763. That figure already includes the power-down floor.

### Gaps
- ST gives no current at Vdd = 3.0 V (the board's VDD) or Vdd_IO = 3.0 V. Every table is at 1.8 V, and how much the current changes with Vdd is not published. Board current should be measured with JP1 cut.
- The full current table for the DSV32X itself (AN6016) could not be fetched. The DSV16X table is used on the evidence that the DSV32X datasheet's six published points match it. The power-down floor differs (3 against 2.6 µA).
- No LPM1 or LPM2 figure for 7.5 Hz exists: the ODR is not available in the low-power modes.

---

## Q2. How does the ODR change the high-pass wake (cutoff = ODR/N, threshold, settling, latency)?

### Takeaway
The high-pass cutoff scales with ODR (ODR/N, N = 10…800). At 1.875 Hz no setting reproduces today's 0.33 Hz: the nearest is ODR/10 = 0.19 Hz. A lower cutoff makes the wake **more** sensitive to slow tilts, not less. What 1.875 Hz costs is latency: one sample every 533 ms instead of every 67 ms. A cleaner alternative is the part's **"absolute" wake-up** (low-pass data minus a stored rest position, USR_OFF_ON_WU), which does not depend on speed or ODR at all.

### Cited Findings
- High-pass cutoffs per HP_LPF2_XL_BW: 000 = slope (ODR/4), 001 = ODR/10, 010 = ODR/20, 011 = ODR/45, 100 = ODR/100, 101 = ODR/200, 110 = ODR/400, 111 = ODR/800 ([DS Table 69, p.70–71](file://lovegood/shared/Cursor/DS_lsm6dsv32x.pdf)).
- Worst-case settling at 99 % on the high-pass path, in samples: ODR/10 and ODR/20 take 27, ODR/45 39, ODR/100 78, ODR/200 156, ODR/400 313, ODR/800 626 ([AN5763 Table 17, p.25](https://cdn.sparkfun.com/assets/0/a/c/c/9/an5763-lsm6dsv16x-6axis-imu-with-embedded-sensor-fusion-ai-qvar-for-highend-applications-stmicroelectronics.pdf)).
- "The XL_FASTSETTL_MODE bit of the CTRL9 register enables the accelerometer LPF2 or HPF fast-settling mode: the selected filter sets the first sample after writing this bit. **This feature applies only upon device exit from power-down mode**" ([AN5763 §3.6, p.25](https://cdn.sparkfun.com/assets/0/a/c/c/9/an5763-lsm6dsv16x-6axis-imu-with-embedded-sensor-fusion-ai-qvar-for-highend-applications-stmicroelectronics.pdf)). AN5763 also recommends XL_FASTSETTL_MODE together with TAP_CFG0 HW_FUNC_MASK_XL_SETTL, which masks the basic interrupts while the filter settles ([AN5763 §5.3, p.48](https://cdn.sparkfun.com/assets/0/a/c/c/9/an5763-lsm6dsv16x-6axis-imu-with-embedded-sensor-fusion-ai-qvar-for-highend-applications-stmicroelectronics.pdf)).
- After a change between two low-power ODRs, the first sample is already correct when LPF2 and HP are off; with the filter at ODR/4, one sample is discarded ([AN5763 Tables 18–19, p.27](https://cdn.sparkfun.com/assets/0/a/c/c/9/an5763-lsm6dsv16x-6axis-imu-with-embedded-sensor-fusion-ai-qvar-for-highend-applications-stmicroelectronics.pdf)).
- Wake-up threshold resolution (WU_INACT_THS_W): 7.8125, 15.625, 31.25, 62.5, 125 or 250 mg/LSB, with a 6-bit WK_THS. The duration WAKE_DUR is counted in units of 1/ODR_XL ([DS §9.55 Table 153, p.93](file://lovegood/shared/Cursor/DS_lsm6dsv32x.pdf); [AN5763 §5.3, p.47](https://cdn.sparkfun.com/assets/0/a/c/c/9/an5763-lsm6dsv16x-6axis-imu-with-embedded-sensor-fusion-ai-qvar-for-highend-applications-stmicroelectronics.pdf)).
- **Absolute wake-up:** "it is possible to configure the wake-up feature as an absolute wake-up with respect to a programmable position … by setting both the SLOPE_FDS bit of the TAP_CFG0 register and the USR_OFF_ON_WU bit of the WAKE_UP_THS register to 1. Using this configuration, the input data for the wake-up function comes from the low-pass filter path and the programmable position is subtracted as an offset", taken from X/Y/Z_OFS_USR ([AN5763 §5.3, p.47](https://cdn.sparkfun.com/assets/0/a/c/c/9/an5763-lsm6dsv16x-6axis-imu-with-embedded-sensor-fusion-ai-qvar-for-highend-applications-stmicroelectronics.pdf)). DS: USR_OFF_ON_WU "Drives the low-pass filtered data with user offset correction (instead of high-pass filtered data) to the wake-up and the activity/inactivity functions" ([DS WAKE_UP_THS §9.62, p.97](file://lovegood/shared/Cursor/DS_lsm6dsv32x.pdf)).
- Each offset register is 8-bit two's complement, limited to [−127, 127]. Its weight does not depend on the full scale: 2⁻¹⁰ g/LSB (USR_OFF_W = 0) or 2⁻⁶ g/LSB = 15.625 mg (USR_OFF_W = 1) ([AN5763 §4.6](https://cdn.sparkfun.com/assets/0/a/c/c/9/an5763-lsm6dsv16x-6axis-imu-with-embedded-sensor-fusion-ai-qvar-for-highend-applications-stmicroelectronics.pdf)).
- Field evidence, 2026-11-15: the slope filter never woke on a smooth lift or draw. The high-pass model (ODR/45 at 15 Hz) woke on the lift about 4 s before the 100° turn, and on the draw at least 1.4 s before release in 34 of 37 shots. The simulator's high-pass model is first-order with `alpha = 1 / (1 + 2π/45)` ([HANDOVER.md, 0.3.10 row and "Sim wake model"](file:///C:/Users/rixar/SynologyDrive/Documents/Cursor/shot-tracker/HANDOVER.md)).

### Inferences
- First-order view, with τ = N / (2π·ODR). A ramp in one axis at r g/s drives the high-pass output towards r·τ, so the 93.75 mg threshold trips only once r > 0.09375/τ:

  | ODR / N | Cutoff | τ | Slowest ramp that wakes | Sample period | 99 % settling (if not fast-settled) |
  |---|---|---|---|---|---|
  | 15 Hz / 45 (today) | 0.333 Hz | 0.48 s | 0.20 g/s | 67 ms | 2.6 s |
  | 1.875 Hz / 10 | 0.188 Hz | 0.85 s | 0.11 g/s | 533 ms | 14.4 s |
  | 1.875 Hz / 20 | 0.094 Hz | 1.70 s | 0.055 g/s | 533 ms | 14.4 s |
  | 1.875 Hz / 45 | 0.042 Hz | 3.82 s | 0.025 g/s | 533 ms | 20.8 s |
  | 30 Hz / 100 | 0.30 Hz | 0.53 s | 0.18 g/s | 33 ms | 2.6 s |
  | 60 Hz / 200 | 0.30 Hz | 0.53 s | 0.18 g/s | 17 ms | 2.6 s |

  At 1.875 Hz, a slow lift crosses the threshold at least as early in filter terms. Detection then waits for the next sample, which adds at most about 0.47 s against 15 Hz. With ODR/45 kept as is, the cutoff falls to 0.042 Hz. That is very sensitive to slow tilts and still safe against noise: LPM1 noise is 3.2 mg RMS, and the offset drift is 0.08 mg/°C ([DS Table 3](file://lovegood/shared/Cursor/DS_lsm6dsv32x.pdf)).
- The 1.875 Hz settling times (14–21 s) only matter if the filter is not fast-settled. The firmware already passes through power-down before IDLE, so a firmware-driven switch to 1.875 Hz can use the same reseeding. The sensor's own automatic ODR switch (Q3) does not pass through power-down, so fast-settle does not apply there.
- With no anti-aliasing in LPM1, every 1.875 Hz sample is close to a snapshot. Vibration in a bag or car aliases into the wake band at either ODR, so false wakes from transport are probably similar. At 1.875 Hz there are 8 times fewer chances to catch a short knock on the bow stand.
- The absolute wake (store the rest gravity vector at 15.625 mg/LSB, compare the low-pass data) wakes after about 5.4° of tilt (asin 0.094) on any axis, however slowly the bow is lifted. That removes the cutoff trade-off at 1.875 Hz. The rounding error of the offset (±7.8 mg) and LPM1 noise are small against 93.75 mg.
  - Cost: the firmware has to read the rest acceleration when it goes idle and write three offset registers.
  - Limit: it reacts to orientation, not to vibration. A draw from a bow that is already up changes orientation little, but the puck would have woken on the lift before it.
  - SLOPE_FDS and USR_OFF_ON_WU feed both wake-up and activity/inactivity. One wake input is chosen at a time: high-pass or absolute, not both.
- The quickest check needs no hardware. Run the 37 recorded captures and the 24 s lift stream through the simulator's high-pass and absolute models, decimated 256:1 (480 Hz → 1.875 Hz), with N = 10, 20 and 45, and compare the wake lead time with 15 Hz/45.

### Gaps
- ST does not publish the digital high-pass filter's exact equation. Whether the first-order model fits at N = 10 and 20 is unverified.
- No ST document says whether the high-pass state is kept or reset across an automatic ODR change (inactivity ↔ activity).
- The absolute wake has not been tried on this hardware, and AN5763 gives no example with USR_OFF_W = 1.

---

## Q3. Does the LSM6DSV32X have an ultra-low-power (ULP) accelerometer mode, and does wake-up work in it?

### Takeaway
No. The LSM6DSV32X has no ULP mode. Its accelerometer modes are power-down, LPM1/2/3, normal, high-performance, high-accuracy ODR and ODR-triggered. LPM1 at 1.875 Hz (4.0 µA) is the lowest active setting. HANDOVER's pointer to a ULP mode (datasheet §6.2.1) appears to date from the LSM6DSO32.

### Cited Findings
- "In the LSM6DSV32X, the accelerometer can be configured in five different operating modes: power-down mode, low-power mode (1, 2, 3), normal mode, high-performance mode, and high-accuracy ODR mode" ([DS §6.2, p.31](file://lovegood/shared/Cursor/DS_lsm6dsv32x.pdf)).
- ST's driver lists OP_MODE_XL values 0 (high-performance), 1 (high-accuracy ODR), 3 (ODR-triggered), 4/5/6 (low-power with 2/4/8 averages) and 7 (normal). There is no ULP value ([lsm6dsv32x_reg.h lines 3986–3992](file:///C:/zp/modules/hal/st/sensor/stmemsc/lsm6dsv32x_STdC/driver/lsm6dsv32x_reg.h)). AN5763 adds the ODR-triggered mode to the list of power modes ([AN5763 §3, p.16](https://cdn.sparkfun.com/assets/0/a/c/c/9/an5763-lsm6dsv16x-6axis-imu-with-embedded-sensor-fusion-ai-qvar-for-highend-applications-stmicroelectronics.pdf)).
- An ST community user confirms: "The accelerometer has no 'Ultra Low Power (ULP)' mode, and the lowest option appears to be LPM1" ([ST community](https://community.st.com/mems-sensors-48/lsm6dsv32x-power-modes-137913)).
- HANDOVER §9 says: "A further idle saving would be the accelerometer's ultra-low-power mode (datasheet §6.2.1; check wake-up support first)" ([HANDOVER.md, battery-life bullet](file:///C:/Users/rixar/SynologyDrive/Documents/Cursor/shot-tracker/HANDOVER.md)).

### Inferences
- The HANDOVER note should be corrected. The DSV32X's equivalent of ULP is LPM1 at 1.875 Hz, and wake-up works there because the embedded functions are "supported in all modes" (Q1).
- ODR-triggered mode takes its sample timing from an external signal. On this part that signal would come in on INT2, which is unconnected on the board ([design.py](file:///C:/Users/rixar/SynologyDrive/Documents/Cursor/shot-tracker/hardware/design.py)). The mode is therefore unusable without a board change, and it would cost MCU wake-ups.

### Gaps
- The ODR-triggered mode's current and pin details were not looked up (AN6016 not retrieved).

---

## Q4. Can the activity/inactivity function hold 1.875 Hz while the puck is still and return to 15 Hz on motion, by itself? How fast, and does it work with the high-pass wake?

### Takeaway
Yes. With INACT_EN = 01 the sensor drops to LPM1 at XL_INACT_ODR (1.875, 15, 30 or 60 Hz) once the puck has been still for SLEEP_DUR. It returns to the CTRL1 setting "immediately" after 1–4 samples over the threshold. The wake-up interrupt keeps running at whatever ODR is in force. Two limits matter: the inactivity timer is short (at most 512 s at CTRL1 = 15 Hz), and the high-pass state across the ODR switch is undocumented. A timer in the firmware does the same job with any delay and with fast-settle.

### Cited Findings
- INACT_EN_[1:0] in FUNCTIONS_ENABLE ([DS §9.53, p.91](file://lovegood/shared/Cursor/DS_lsm6dsv32x.pdf)):
  - 00: stationary/motion interrupts only, no configuration change.
  - 01: accelerometer to LPM1 at XL_INACT_ODR, gyro unchanged.
  - 10: the same, with the gyro in sleep mode.
  - 11: the same, with the gyro in power-down.
- XL_INACT_ODR_[1:0] (INACTIVITY_DUR): 00 = 1.875 Hz, 01 = 15 Hz (default), 10 = 30 Hz, 11 = 60 Hz. INACT_DUR_[1:0] sets activity after 1, 2, 3 or 4 consecutive over-threshold events ([DS §9.55 Table 153, p.93](file://lovegood/shared/Cursor/DS_lsm6dsv32x.pdf)).
- "When a certain number of consecutive X, Y, Z filtered data is smaller than the configured threshold, the OP_MODE_XL_[2:0] and the ODR_XL_[3:0] bits of the CTRL1 register are bypassed (inactivity) and the accelerometer is internally set in low-power mode 1 at the sampling rate configured through the XL_INACT_ODR_[1:0] bits … although the content of the CTRL1 register is left untouched" ([AN5763 §5.6, p.59](https://cdn.sparkfun.com/assets/0/a/c/c/9/an5763-lsm6dsv16x-6axis-imu-with-embedded-sensor-fusion-ai-qvar-for-highend-applications-stmicroelectronics.pdf)).
- How long stillness must last: SLEEP_DUR_[3:0], with "1 LSB corresponds to 512 / ODR_XL time"; 0000 means 16 / ODR_XL ([AN5763 §5.6, p.59](https://cdn.sparkfun.com/assets/0/a/c/c/9/an5763-lsm6dsv16x-6axis-imu-with-embedded-sensor-fusion-ai-qvar-for-highend-applications-stmicroelectronics.pdf); [DS WAKE_UP_DUR Table 171, p.98](file://lovegood/shared/Cursor/DS_lsm6dsv32x.pdf)). ST's example works out SLEEP_DUR = 0101 as 5 × 512 / 480 Hz = 5.33 s, so ODR_XL is the CTRL1 ODR ([AN5763 §5.6.1, p.60](https://cdn.sparkfun.com/assets/0/a/c/c/9/an5763-lsm6dsv16x-6axis-imu-with-embedded-sensor-fusion-ai-qvar-for-highend-applications-stmicroelectronics.pdf)).
- Return to activity: "When filtered data on one axis becomes bigger than the threshold for a configurable time, the CTRL1 register settings are immediately restored (activity)"; INACT_DUR is counted in 1/ODR_XL ([AN5763 §5.6, p.59](https://cdn.sparkfun.com/assets/0/a/c/c/9/an5763-lsm6dsv16x-6axis-imu-with-embedded-sensor-fusion-ai-qvar-for-highend-applications-stmicroelectronics.pdf)).
- Threshold: activity/inactivity uses its own INACT_THS_[5:0] (INACTIVITY_THS, 55h), separate from the wake-up WK_THS, with the same WU_INACT_THS_W weight. Its input is chosen by the same SLOPE_FDS bit (slope or high-pass) ([AN5763 §5.6, p.58](https://cdn.sparkfun.com/assets/0/a/c/c/9/an5763-lsm6dsv16x-6axis-imu-with-embedded-sensor-fusion-ai-qvar-for-highend-applications-stmicroelectronics.pdf); [DS §9.56, p.93](file://lovegood/shared/Cursor/DS_lsm6dsv32x.pdf)).
- "HAODR mode is not compatible with the activity/inactivity functionality" ([AN5763 §3.1, p.17](https://cdn.sparkfun.com/assets/0/a/c/c/9/an5763-lsm6dsv16x-6axis-imu-with-embedded-sensor-fusion-ai-qvar-for-highend-applications-stmicroelectronics.pdf)). This does not affect IDLE, which uses LPM1.
- Fast-settle applies only on leaving power-down (Q2, [AN5763 §3.6](https://cdn.sparkfun.com/assets/0/a/c/c/9/an5763-lsm6dsv16x-6axis-imu-with-embedded-sensor-fusion-ai-qvar-for-highend-applications-stmicroelectronics.pdf)).
- The IDLE main loop already wakes every 2 s (IDLE_POLL_MS 2000), and housekeeping runs every 60 s ([main.c](file:///C:/Users/rixar/SynologyDrive/Documents/Cursor/shot-tracker/firmware/src/main.c)).

### Inferences
- A sensor-only setup would be: CTRL1 = LPM1 15 Hz as today, FUNCTIONS_ENABLE = 0x81 (INACT_EN = 01; the gyro is already off), XL_INACT_ODR = 00, INACT_THS set to e.g. 31–62 mg, SLEEP_DUR = 15. At CTRL1 = 15 Hz the longest stillness timer is 15 × 512 / 15 = **512 s (8.5 min)**. A firmware timer would be needed for anything longer.
- Waking from the 1.875 Hz state:
  - The wake-up function runs on the same filtered data at the current ODR, so an over-threshold sample at 1.875 Hz can raise WU_IA on INT1 directly, within ≤ 533 ms plus WAKE_DUR. The automatic return to 15 Hz then hardly matters, because the MCU switches to ACTIVE at 480 Hz on the interrupt.
  - With INACT_THS < WK_THS, a small motion could first bring the sensor back to 15 Hz without waking the MCU. The 15 Hz high-pass wake would then catch the lift. This works only if the high-pass state across the switch behaves (undocumented).
  - With SLOPE_FDS = 1 and N = 45, the cutoff during inactivity becomes 0.042 Hz (see Q2), which makes small slow tilts count as activity more easily.
- A firmware-driven second stage is simpler and better documented than INACT_EN: after N minutes of IDLE, `accel_set_idle` is redone at 1.875 Hz through power-down (fast-settle reseeds the filter). The cost is a few I²C writes, once per idle period. It also allows N longer than 8.5 min and a high-pass N chosen for 1.875 Hz (e.g., 10 or 20).

### Gaps
- What the high-pass and slope filter outputs do at the automatic ODR switch is not documented.
- Whether the wake-up interrupt keeps exactly the same behaviour while the device is in the internal inactivity state is implied ("supported in all modes"), not stated in an example.
- The DSV32X application note (AN6016) was not available to confirm that the DSV16X text applies word for word.

---

## Q5. Two-stage idle: 15 Hz for the first N minutes after the last shot, then 1.875 Hz. Trade-offs for the first shot of the next session, and the FIFO "option 2"

### Takeaway
The saving hardly depends on N. With three sessions a week, about 160 of the 162 idle hours fall in the long gaps, so any N from 8.5 min to 2 h saves **≈0.48–0.50 mAh/week**: the IMU's idle average falls from 7.1 to about 4.0 µA. The price is up to +0.47 s of wake latency in the deep stage only. FIFO look-back ("option 2") works in the 15 Hz stage and is useless at 1.875 Hz.

### Cited Findings
- Currents: LPM1 15 Hz 7.1 µA, 1.875 Hz 4.0 µA ([AN5763 Table 16](https://cdn.sparkfun.com/assets/0/a/c/c/9/an5763-lsm6dsv16x-6axis-imu-with-embedded-sensor-fusion-ai-qvar-for-highend-applications-stmicroelectronics.pdf)).
- FIFO: "The LSM6DSV32X embeds 1.5 KB of data in FIFO (up to 4.5 KB with the compression feature enabled)" ([DS FIFO section, p.44](file://lovegood/shared/Cursor/DS_lsm6dsv32x.pdf)). FIFO batching is supported in all modes ([DS §6.2](file://lovegood/shared/Cursor/DS_lsm6dsv32x.pdf)). One FIFO word is 7 bytes (tag + 6 data bytes; FIFO_WORD_BYTES = 7 in [accel_regs.h](file:///C:/Users/rixar/SynologyDrive/Documents/Cursor/shot-tracker/firmware/src/accel_regs.h)).
- Captures start at the wake when the wake comes after the window start: standard −480…+240 samples, extended −2880…+960 ([HANDOVER.md, Capture bullet](file:///C:/Users/rixar/SynologyDrive/Documents/Cursor/shot-tracker/HANDOVER.md)). The high-pass wake comes about 4 s before the 100° lift turn and ≥ 1.4 s before release in 34 of 37 draws ([HANDOVER.md, 0.3.10](file:///C:/Users/rixar/SynologyDrive/Documents/Cursor/shot-tracker/HANDOVER.md)).

### Inferences
- Weekly saving against all-15 Hz, for a stage 1 of N after each of 3 sessions: N = 8.5 min → 0.50 mAh; 30 min → 0.50 mAh; 1 h → 0.49 mAh; 2 h → 0.48 mAh. Moving all idle time to 1.875 Hz gives 0.50 mAh. So N can be chosen for behaviour, e.g., to cover a long break at the range, not for power.
- First shot of a new session from the deep stage: the archer has to lift the bow first, and the lift is the event that wakes the puck. At 1.875 Hz the lift is detected ≤ 0.47 s later than at 15 Hz. Against a lift-to-release time of several seconds, that should still catch the shot. A capture shortened by the wake can lose ≤ 0.47 s more of its pre-trigger part.
  - The real risk is a lift so smooth that the high-pass output stays under the threshold. At 1.875 Hz that is less likely (Q2), and the absolute wake removes it.
  - Verify by replaying the recorded data, decimated, before choosing.
- FIFO "option 2": uncompressed, the 1.5 KB holds ~219 accelerometer words, i.e., **14.6 s at 15 Hz**. With compression, up to 3× that is possible (≤ 4.5 KB). In the 15 Hz stage the shot that woke the puck can be rebuilt from history. In the 1.875 Hz stage the history is too coarse for capture or detection. If option 2 matters, keep stage 1 long enough to cover the typical breaks between ends.

### Gaps
- No ST figure for the extra current of FIFO batching while idle. FIFO logic is digital and presumably small, but no number was found.
- The real time distribution of idle periods (between ends vs. between sessions) is not known. The estimate assumes 3 sessions a week, with the 2 h sessions counted as ACTIVE.

---

## Q6. What do the embedded functions (FSM, MLC, significant motion, tilt, SFLP, 6D) cost, and could one of them detect a lift or draw better at a lower ODR?

### Takeaway
None of them runs below 15 Hz, so none can supervise a 1.875 Hz deep stage. Significant motion counts steps, so it is useless for a bow. Tilt needs ≥ 30 Hz (+3.4 µA) and a 35° change. SFLP needs the gyroscope (~260 µA). FSM and MLC can run at 15 Hz alongside today's IDLE ODR, but ST publishes no current figure for them on this part. In a sibling part MLC adds ~1–4 µA, more than 1.875 Hz would save on its own.

### Cited Findings
- Significant motion "works at 30 Hz, so the accelerometer ODR must be set at a value of 30 Hz or higher. It generates an interrupt when the difference between the number of steps counted … is higher than 10 steps", and it enables the step counter ([AN5763 §6.2, p.65](https://cdn.sparkfun.com/assets/0/a/c/c/9/an5763-lsm6dsv16x-6axis-imu-with-embedded-sensor-fusion-ai-qvar-for-highend-applications-stmicroelectronics.pdf)).
- Relative tilt "works at 30 Hz, so the accelerometer ODR must be set at a value of 30 Hz or higher … an interrupt is generated when the device is tilted by an angle greater than 35 degrees from the start position … the tilt logic typically requires a 2-second settling time" ([AN5763 §6.3, p.66](https://cdn.sparkfun.com/assets/0/a/c/c/9/an5763-lsm6dsv16x-6axis-imu-with-embedded-sensor-fusion-ai-qvar-for-highend-applications-stmicroelectronics.pdf)).
- SFLP block current alone: 3.5 µA at 15 Hz, 7 at 30, 14 at 60, 28 at 120, 56 at 240, 112 at 480 Hz ([AN5763 Table 47, p.69](https://cdn.sparkfun.com/assets/0/a/c/c/9/an5763-lsm6dsv16x-6axis-imu-with-embedded-sensor-fusion-ai-qvar-for-highend-applications-stmicroelectronics.pdf)). SFLP produces a game rotation vector from accelerometer and gyroscope ([DS §2.8, p.8](file://lovegood/shared/Cursor/DS_lsm6dsv32x.pdf)). The gyro in low-power mode at 15 Hz draws 262 µA ([AN5763 Table 16](https://cdn.sparkfun.com/assets/0/a/c/c/9/an5763-lsm6dsv16x-6axis-imu-with-embedded-sensor-fusion-ai-qvar-for-highend-applications-stmicroelectronics.pdf)).
- FSM_ODR: 15, 30 (default), 60, 120, 240, 480 or 960 Hz ([DS FSM_ODR Table 326, p.139](file://lovegood/shared/Cursor/DS_lsm6dsv32x.pdf)). MLC ODR in ST's driver starts at 15 Hz (`LSM6DSV32X_MLC_15Hz = 0x0`); SFLP likewise (`LSM6DSV32X_SFLP_15Hz`) ([lsm6dsv32x_reg.h lines 4765, 4889, 5201](file:///C:/zp/modules/hal/st/sensor/stmemsc/lsm6dsv32x_STdC/driver/lsm6dsv32x_reg.h)).
- Adaptive self-configuration (ASC) "allows the FSM to automatically reconfigure the device in real time … The FSM can write a subset of the device registers using the SETR command" ([DS §2.7, p.7](file://lovegood/shared/Cursor/DS_lsm6dsv32x.pdf)).
- An ST community article on the LSM6DSV16X uses ASC to switch from a moving configuration (accel and gyro at 120 Hz high-performance, MLC, pedometer, SFLP; "around 700 µA") to a still one: "70 µA when stationary", gyro off, accel in low-power mode at an unstated ODR ([ST community, "How the edge features of LSM6DSV16X augment your smart applications"](https://community.st.com/mems-and-sensors-62/how-the-edge-features-of-lsm6dsv16x-augment-your-smart-applications-124368)).
- MLC extra current, **LSM6DSOX (another part), seen only in a search summary, not verified**: +1 µA (vibration monitoring, 26 Hz), +1 µA (motion intensity, 12.5 Hz), +2 µA (6D position, 26 Hz), +4 µA (activity recognition, 26 Hz) ([ST LSM6DSOX presentation](https://www.st.com/content/dam/AME/2019/technology-tour-2019/toronto/presentations/T2S8_Toronto_LSM6DSOX_T.Reis.pdf)). ST's MLC note for that part is AN5259 ([st.com](https://www.st.com/resource/zh/application_note/an5259-lsm6dsox-machine-learning-core-stmicroelectronics.pdf)).
- The 6D/4D orientation and free-fall functions are basic interrupts and are supported in all modes ([DS §6.2](file://lovegood/shared/Cursor/DS_lsm6dsv32x.pdf)).

### Inferences
- Tilt would catch a 100° lift but not a draw. At 30 Hz the IMU draws 10.5 µA against 7.1 µA today, so tilt costs current instead of saving it.
- An FSM at 15 Hz could encode a better "lift" rule, for example a change in the gravity direction held for a few samples, or a norm or angle condition with a timer. The accelerometer cost would stay as today; the FSM adds an unknown compute current. ASC could even let the FSM change the ODR itself.
  - The FSM cannot run while the accelerometer is at 1.875 Hz (minimum FSM ODR 15 Hz), so it does not combine with the deep stage.
  - The same lift rule (orientation change from rest) comes free with the absolute wake (Q2), at any ODR.
- Free-fall is irrelevant. 6D orientation would fire when a bow lying flat is stood up, but not on a lift from a stand or on a draw. It is at most a supplementary trigger.

### Gaps
- No published FSM or MLC current for the LSM6DSV32X. AN6070 (FSM) and AN6071 (MLC) are named on DS p.1 but were not retrieved. The LSM6DSOX figures are a different part and came secondhand.

---

## Q7. Interface settings: do anti-spike, INT1 push-pull or pad pull-ups add idle current?

### Takeaway
The present configuration enables **no internal pull-up that can draw current**. SDO_PU_EN = 0 with SA0 on GND; SHUB_PU_EN = 0 with SDx/SCx on GND; SDA_PU_EN = 0. INT1 is push-pull, active high, into an nRF input without a pull, so it has no DC path. The anti-spike filter ("always on") has no published current. There are two traps worth knowing (below). The one real host-side risk is letting SDA/SCL float.

### Cited Findings
- IF_CFG written once as 0x20: ASF_CTRL = 1, SDA_PU_EN = 0, SHUB_PU_EN = 0, H_LACTIVE = 0 (active high), PP_OD = 0 (push-pull), I2C_I3C_disable = 0. PIN_CTRL is never written ([accel.c `configure`](file:///C:/Users/rixar/SynologyDrive/Documents/Cursor/shot-tracker/firmware/src/accel.c); bit layout in [lsm6dsv32x_reg.h lines 210–248](file:///C:/zp/modules/hal/st/sensor/stmemsc/lsm6dsv32x_STdC/driver/lsm6dsv32x_reg.h)).
- IF_CFG defaults: SDA_PU_EN 0, SHUB_PU_EN 0, ASF_CTRL 0 ("0: antispike filters are managed by the protocol and turned off after the broadcast address; 1: … always enabled") ([DS §9.3 Table 30, p.58](file://lovegood/shared/Cursor/DS_lsm6dsv32x.pdf)). IF_CFG and PIN_CTRL are **not reset by the software reset** ([DS §9.2–9.3](file://lovegood/shared/Cursor/DS_lsm6dsv32x.pdf)).
- PIN_CTRL defaults ([DS §9.2 Table 28, p.56–57](file://lovegood/shared/Cursor/DS_lsm6dsv32x.pdf)):
  - SDO_PU_EN = 0: "SDO pin pull-up disconnected".
  - OIS_PU_DIS = 0: OCS_Aux and SDO_Aux keep their pull-ups.
- Internal pull-ups are "from 30 k to 50 k, depending on Vdd_IO" ([DS Table 23 note, p.50–51](file://lovegood/shared/Cursor/DS_lsm6dsv32x.pdf)).
- Board wiring:
  - SA0 = GND and CS = VDD (I²C selected).
  - Aux master pins SDx/SCx to GND.
  - INT2, OCS_Aux and SDO_Aux unconnected.
  - VDD = VDDIO = 3.0 V from REG0.
  ([design.py header](file:///C:/Users/rixar/SynologyDrive/Documents/Cursor/shot-tracker/hardware/design.py)).
- INT1 is configured `GPIO_ACTIVE_HIGH` with no pull flag ([shotpuck_nrf52840.dts line 37](file:///C:/Users/rixar/SynologyDrive/Documents/Cursor/shot-tracker/firmware/boards/shotpuck/shotpuck/shotpuck_nrf52840.dts)). INT1 is push-pull and driven low by default ([HANDOVER.md, IMU configuration](file:///C:/Users/rixar/SynologyDrive/Documents/Cursor/shot-tracker/HANDOVER.md)). INT2 defaults to "output forced to ground" ([DS Table 23](file://lovegood/shared/Cursor/DS_lsm6dsv32x.pdf)).
- Anti-spike management: the device turns its I²C anti-spike filter off after it sees the I3C broadcast address; "for the case in which the host decides to keep the device as I²C with an antispike filter … writing the ASF_CTRL bit to 1 … forces the antispike filter to always be turned on" ([DS §5.2.3, p.29](file://lovegood/shared/Cursor/DS_lsm6dsv32x.pdf)). The only interface-disable bit, I2C_I3C_disable, turns off both I²C and I3C, so it is for SPI-only use ([DS Table 23 / §9.3](file://lovegood/shared/Cursor/DS_lsm6dsv32x.pdf)).
- "In order to avoid an unexpected increase in current consumption, the input pins that are not pulled-up/pulled-down must be polarized by the host" ([AN5763 §3, p.16](https://cdn.sparkfun.com/assets/0/a/c/c/9/an5763-lsm6dsv16x-6axis-imu-with-embedded-sensor-fusion-ai-qvar-for-highend-applications-stmicroelectronics.pdf)). The IMU's SCL and SDA are "input without pull-up" by default ([DS Table 23, p.51](file://lovegood/shared/Cursor/DS_lsm6dsv32x.pdf)).
- The board's `i2c0_sleep` pinctrl state is `low-power-enable` without pull-ups; the default state uses `bias-pull-up` ([shotpuck_nrf52840-pinctrl.dtsi](file:///C:/Users/rixar/SynologyDrive/Documents/Cursor/shot-tracker/firmware/boards/shotpuck/shotpuck/shotpuck_nrf52840-pinctrl.dtsi)). No `CONFIG_PM_DEVICE` appears in `firmware/prj.conf` (grep).
- ST's reply on a "higher than expected current" LSM6DSV16X thread: the board-level excess "is more likely due to leakage or back-powering through one of the MCU-connected pins"; "ensure pull-ups are tied to the always-on rail". The poster later found the cause elsewhere on the board ([ST community](https://community.st.com/mems-sensors-48/lsm6dsv16x-higher-than-expected-current-consumption-168478)).

### Inferences
- **Trap 1:** setting SDO_PU_EN, or SHUB_PU_EN, would put a 30–50 kΩ pull-up against a pin tied to GND. That costs about **60–100 µA per pin** at 3.0 V (two pins for SHUB_PU_EN, so 120–200 µA), several times the whole idle budget. PIN_CTRL and IF_CFG survive a software reset, so a stray write would persist until power-on. A health check could read both back (IF_CFG = 0x20, PIN_CTRL at its default).
- The OCS_Aux/SDO_Aux pull-ups are on (default), but the pins are unconnected, so no current flows. CS has its default pull-up and is tied to VDD, so no current flows there either.
- **Trap 2:** if Zephyr device power management is ever enabled and the TWIM is suspended, the `i2c0_sleep` state removes the nRF's pull-ups. SDA and SCL would then float at the IMU's unpulled inputs, against AN5763's advice, and could draw extra current. Today the default state with pull-ups stays in force.
- The anti-spike filter is a small input glitch filter. Its current is unpublished and presumably negligible. Turning it off (ASF_CTRL = 0) would rely on the I3C broadcast logic and gains no documented saving, so it is not worth changing.
- The push-pull INT1 into an nRF input without a pull draws nothing, whether low or latched high. Open-drain would need a pull resistor, which costs current while INT1 is latched active. Push-pull is the low-current choice.

### Gaps
- No ST figure for the anti-spike filter's current, nor for a difference between push-pull and open-drain beyond the pull resistor.

---

## Q8. Does the I²C pull-up draw current while idle?

### Takeaway
Only while a line is low. The nRF internal pull-ups (~13 kΩ) cost ≈ 0.23 mA per line, but idle I²C traffic is a few register reads a minute, about 1 nA on average. A stuck-low SDA, or an INT line held against a pull, would draw 0.23 mA continuously.

### Cited Findings
- I²C runs at 400 kHz on the nRF's internal pull-ups (~13 kΩ); "there are no external pull-up pads" ([HANDOVER.md §9 and load-bearing assumptions](file:///C:/Users/rixar/SynologyDrive/Documents/Cursor/shot-tracker/HANDOVER.md); [pinctrl `bias-pull-up`](file:///C:/Users/rixar/SynologyDrive/Documents/Cursor/shot-tracker/firmware/boards/shotpuck/shotpuck/shotpuck_nrf52840-pinctrl.dtsi)).
- I²C bus lines are pulled up, and "when the bus is free, both the lines are high" ([DS I²C interface section, p.21](file://lovegood/shared/Cursor/DS_lsm6dsv32x.pdf)).
- I²C traffic while idle:
  - every 60 s, housekeeping runs `accel_idle_ok` (reads CTRL1 and MD1_CFG);
  - each wake reads WAKE_UP_SRC once;
  - the 2 s idle loop does not touch the IMU unless the wake event is pending.
  ([accel.c](file:///C:/Users/rixar/SynologyDrive/Documents/Cursor/shot-tracker/firmware/src/accel.c); [main.c lines 341–349, 492, HOUSEKEEP_IDLE_MS 60000](file:///C:/Users/rixar/SynologyDrive/Documents/Cursor/shot-tracker/firmware/src/main.c))
- The firmware recovers a stuck bus (`i2c_recover_bus`) at init ([accel.c `accel_init`](file:///C:/Users/rixar/SynologyDrive/Documents/Cursor/shot-tracker/firmware/src/accel.c)).

### Inferences
- A line held low draws 3.0 V / 13 kΩ ≈ 0.23 mA. Two single-register reads take about 0.2 ms of bus time a minute, with each line low for some fraction of that. On average that is well under 1 nA. Pull-up current is irrelevant to the idle budget.
- A FIFO read-out after a wake (option 2: about 219 words × 7 B ≈ 1.5 KB) takes ≈ 35 ms at 400 kHz, once per wake. The pull-up charge is a few µC per wake, negligible at a few wakes a day. The CPU and TWIM time are the larger cost; that is an MCU-side question.
- The real risks are fault states: SDA held low by a hung transfer, or any pull resistor fighting a driven pin. Each costs ≥ 0.23 mA, tens of times the IMU's idle current. A periodic bus-level check is cheap insurance.

### Gaps
- The nRF52840 internal pull-up value (~13 kΩ) is a nominal figure from HANDOVER, not measured on the board. The nRF-side TWIM idle current is outside this note and belongs to the MCU research.

---

## Summary ranking of IMU idle options (for the report writer; every figure traces to the sections above)

| Option | IMU idle current | Saving vs today (µA / mAh per 162 h week) | Keeps lift/draw wake? | Main risk |
|---|---|---|---|---|
| Today: LPM1 15 Hz, high-pass ODR/45 | 7.1 µA | — | Yes (field-proven, 34/37 draws ≥ 1.4 s early) | — |
| LPM1 1.875 Hz all idle, high-pass ODR/10 or /20, through power-down + fast-settle | 4.0 µA | −3.1 µA / −0.50 mAh | Probably; verify on the recorded data | +≤ 0.47 s latency; filter-model uncertainty |
| Two-stage, firmware timer (15 Hz for N, then 1.875 Hz) | ≈ 4.0 µA average | ≈ −3.1 µA / −0.48 to −0.50 mAh (any N ≤ 2 h) | Yes in stage 1; as above in stage 2 | Extra mode logic; option 2 only in stage 1 |
| Two-stage, sensor's own INACT_EN = 01 (XL_INACT_ODR 1.875 Hz) | ≈ 4.0 µA | ≈ −3.1 µA; stage 1 ≤ 8.5 min | Probably | High-pass state at the automatic ODR switch undocumented |
| Absolute wake (USR_OFF_ON_WU) at 1.875 Hz | 4.0 µA | −3.1 µA / −0.50 mAh | Lift yes, at any speed; draw only via orientation | New mode, untested; rest offset written at idle entry |
| LPM2 or LPM3 instead of LPM1 | 7.8 / 9.2 µA at 15 Hz | +0.7 / +2.1 µA (worse) | Yes | No benefit; noise is already far below the threshold |
| Tilt / SMD / SFLP | ≥ 10.5 µA (30 Hz), SFLP + gyro ≥ 265 µA | Worse | Tilt: lift only; SMD: no | Not suitable |
| FSM/MLC at 15 Hz | 7.1 µA + unknown (sibling part: +1–4 µA) | Worse or equal | Possibly smarter | No DSV32X current data |
| Power-down | 2.6–3 µA | −4.1 to −4.5 µA | **No** | Breaks the wake requirement; storage only |
