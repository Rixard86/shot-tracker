# Request from archesk: store the aiming part of extended captures at 120 Hz

Written 2026-11-19 on the archesk side, for whoever works on the ShotPuck firmware next. Richard asked for this option only; it changes what an extended capture keeps, so treat it as his decision already made, not a proposal to re-litigate.

## The request

In **extended** capture mode (`capture_mode` 1), store the aiming part of each shot capture as **means of 4 samples (120 Hz)** instead of every sample (480 Hz):

| Part of the capture | Samples | Stored as |
|---|---|---|
| Aiming: window start until 1 s before the trigger | `index < trigger_index - 480` | means of 4 consecutive samples, 120 Hz |
| Last second before the trigger, and everything after it | `index >= trigger_index - 480` | every sample, bit-exact, as today |

When a capture is sent (the automatic stream and `GET_CAPTURE`), **rebuild 480 Hz** so the CAPTURE protocol stays exactly as it is: same header (`n_samples`, `trigger_index`, `odr_hz` 480), same sample format, same count. No app change is then needed.

Standard mode, dev recordings (`RECORD`, `trigger_index` 0) and everything else stay as they are.

## Why

With today's codec an extended capture takes about 13.8 KB of flash. Richard syncs late: the bow stays on its stand at the line while he scores, so the phone usually only catches up when he packs up. In extended mode the puck keeps about 48 shots in the 660 KB area (29 in the 408 KB area). A 60-arrow round therefore loses the recordings of its first 12 or so shots before the phone gets them. The shot events themselves survive, only the motion is lost.

With the aiming part at 120 Hz, an extended capture drops to about 9.7 KB, and about 69 fit in 660 KB (42 in 408 KB): enough for a 60-arrow round.

## Measurements (real data)

28 real extended captures from board 1 (3840 samples, 480 Hz, `trigger_index` 2880), taken on 2026-11-15 and 2026-11-18. Method: a Python replay of `capcodec.c`'s exact bit cost (32-sample blocks, delta or slope per axis per block, Rice `k` 0–17, escape at q ≥ 24, raw fallback), plus the 20-byte record header and word padding. For capacity, each 4096-byte sector loses its 8-byte header and one sector is kept free.

| | Median per capture | Of raw (46,080 B) | Fit in 660 KB | Fit in 408 KB |
|---|---|---|---|---|
| Today | 13,802 B (13,080–14,484) | 30.0% | ~48 | ~29 |
| Aiming averaged to 120 Hz | 9,728 B | 21.1% (−29.5%) | ~69 | ~42 |

The averaged part goes through the unchanged codec as its own run of samples; nothing in `capcodec.c` needs to change. For reference, lossless codec tweaks were also measured (block size, extra predictors, finer or per-sample Rice parameters): none saved more than 2%, so the codec itself is not worth touching.

Standard captures are not affected: their whole window (1 s before to 0.5 s after) is inside the full-rate part.

## Why archesk loses nothing it uses

- **Steadiness** is the RMS of the gyro rotation speed over the **last 1 s** before release, and **"Last ¼ second"** compares the last 250 ms with the 750 ms before it. Both sit entirely in the full-rate part, so they come out bit-identical.
- **The Pointing view, sight picture and 3D replay add up (integrate) the gyro over the aiming part.** They tolerate 120 Hz, since bow sway while aiming is far slower than 60 Hz. But they turn any constant bias into drift, which leads to the constraints below.

## Constraints

1. **Round, never truncate.** Store each 4-sample mean rounded to nearest, so it carries no bias. `sum >> 2` would bias every value by about −0.4 LSB on average, and the gyro integration in the app would turn that into steady drift.
2. **Keep the integral.** The rebuilt 480 Hz signal must add up, over each group of 4, to 4 × the stored mean. Repeating the mean 4 times does this exactly. Linear interpolation between group means (each placed at the centre of its group) is smoother in plots and keeps it to within rounding. Either is fine; never a scheme that shifts or scales the signal.
3. **The boundary is fixed by `trigger_index`.** Count groups backwards from `trigger_index - 480`, so the full-rate part always starts exactly 1 s before the trigger. A capture shortened by a recent wake (fewer samples, smaller `trigger_index`) follows the same rule. A partial group of 1–3 samples at the very start is stored as its own mean. If there is no aiming part (`trigger_index` ≤ 480), nothing is averaged.
4. **Old captures must still decode after the update.** Captures in the 408 KB area survive firmware updates, so records written by the old firmware (all 480 Hz) and by the new one must be distinguishable in flash: a header flag, a new record magic or a format version. The header already holds `n_samples` and `trigger_index`, so the boundary itself needs no new field.
5. **Only shot captures in extended mode.** Leave standard mode and `RECORD` dev captures at full rate.

## What would confirm it

- Host test: encode and decode round trip. The last 1 s and the post-trigger part are bit-identical. Every rebuilt group adds up to 4 × its stored mean (or within rounding for interpolation). Each stored mean is within ±0.5 LSB of the true 4-sample mean.
- Host test: a capture written by the old format still decodes after the change (mixed ring).
- Capacity: on realistic data, about 69 extended captures fit in 660 KB, against about 48 today. The 28 real captures can be exported as a fixture if wanted (ask on the archesk side).
- On the puck: an extended capture fetched over BLE still has `n_samples` 3840, `odr_hz` 480 and `trigger_index` 2880, and archesk shows it normally.
- `docs/PROTOCOL.md`: add a sentence saying the aiming part of an extended capture (until 1 s before the trigger) holds no detail above 60 Hz.
