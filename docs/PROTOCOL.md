# ShotPuck BLE protocol (v3)

Firmware 0.3.1 (still v3; the existing layouts are unchanged):
- While the pairing window is open, the advertising data carries a **pairing marker** (manufacturer data), so an app can pair as soon as the charger goes in (see *Discovery and identity*).
- A link that is **not encrypted 30 s after it connects is dropped**. Connect and pair in one go; never hold an unpaired link open while the user does something.
- A CONTROL write that finds the command queue full fails with ATT error 0x11 (see *CONTROL*).
- `seq` never goes backwards, also across a factory reset.
- New read-only **DIAG** characteristic (`…0008…`): why the puck last restarted, a crash count and stack headroom (see *DIAG*).
- While events or captures are streaming, the puck asks for a 15–30 ms connection interval, and goes back to 105–210 ms 5 s after the transfer ends.

v3 (compared with v2, firmware 0.3.0):
- Captures are stored compressed in flash and survive resets and updates. GET_CAPTURE works for any stored shot, not just the last 4.
- CONFIG gains `capture_mode` (standard or extended) and is 20 bytes. 19-byte v2 writes are still accepted.
- CAPTURE `n_samples` and `trigger_index` vary with the mode: always read them from the header.
- A shot soon after the puck woke up gets a shorter capture that starts at the wake (v2 sent none).
- The packet layouts are unchanged.

v2 (compared with v1):
- CAPTURE characteristic (raw 6-axis bow motion around each shot) and the GET_CAPTURE opcode.
- **Pairing required** for everything in the ShotPuck service, with new pairings accepted only while the charger is attached (see *Pairing*). FORGET_BONDS opcode.
- Unique identity: the name carries a suffix, and the Device Information Service holds a serial number.
- EVENT notifications can carry several events.
- Accepted shots are stored in flash, so REPLAY works across resets.
- CONFIG carries `version = 2`. The COUNT, STATUS and single-event layouts are unchanged.

Source of truth: `firmware/src/protocol.h`. All multi-byte values are **little-endian**, structs are packed.

## Discovery and identity

- Advertised name: **`ShotPuck-XXXX`**, where `XXXX` is the last 4 hex digits of the serial number. The advertising data includes the service UUID below, so scan with a service filter.
- **Device Information Service** (0x180A), readable **without pairing**:
  - Serial Number String (0x2A25): 16 hex characters, the nRF52840's factory-unique device ID. **Use it as `device_id`** in the app and in PowerSync. It is identical on every phone; the iOS peripheral identifier is not.
  - Model Number String (0x2A24) `ShotPuck`, Firmware Revision String (0x2A26), e.g. `0.2.0+0` (from `firmware/VERSION`).
- The puck advertises while it is **ACTIVE** (from motion wake until 30 s of stillness, plus a 120 s linger) and while its **pairing window** is open. A puck lying still in a bag is invisible, so pick up the bow to make it connectable.
- **Pairing marker** (firmware 0.3.1): while the pairing window is open, the advertising data also carries manufacturer-specific data, company ID `0xFFFF` (`SP_ADV_COMPANY_ID`), payload one flags byte with bit 0 set (`SP_ADV_PAIRING_OPEN`). Outside the window the field is absent. The app matches it only on devices that also advertise the ShotPuck service UUID. `0xFFFF` is the Bluetooth SIG's ID for tests and prototypes; a product release needs an assigned company ID, and then both firmware and app change this constant together.
- One connection at a time.
- **Unencrypted links are dropped after 30 s** (firmware 0.3.1): a connection that has not reached an encrypted link 30 s after it was made is disconnected by the puck. Paired phones encrypt within a second or two (at their first subscribe, or by re-encrypting on connect). Tools such as nRF Connect can still read the open services; they are disconnected 30 s later unless they pair.
- **MTU:** a single event is 24 bytes, so the ATT MTU must be at least 27. The puck requests 247 on connect (and supports LE data length extension). A larger MTU makes transfers faster: up to 10 events or 19 capture samples per packet at MTU 247. flutter_blue_plus on Android requests 512 by default. On iOS the MTU is negotiated automatically (185+).

## Pairing

Everything in the ShotPuck service needs an **encrypted link**, which a phone only gets by pairing (bonding) with the puck. Reading COUNT, CONFIG or STATUS, subscribing to any notification, and writing CONTROL or CONFIG all fail without it (ATT "insufficient encryption/authentication"). The Device Information Service and the standard Battery Service stay open.

- **Pairing window:** the puck accepts a *new* pairing only for **60 s after the charger is attached** (also after a boot with the charger attached). The LED blinks once a second while the window is open, and blinks twice when a phone has paired. Outside the window, pairing requests are rejected. Phones that are already paired reconnect and encrypt at any time.
- **Charger LED** (after the pairing window, while the charger stays attached; `firmware/src/chg_led.c`): one short blink every 2 s while charging; **steady on** when the charger reports done and the cell reads ≥ 4.10 V; three quick blinks every 2 s when charging is paused by the temperature gate; dark if the charger is attached but neither charging nor full (no input power or a fault). The steady LED starts only after the MCP73831 has terminated, so its ~1 mA never holds the charge current above the termination threshold. It slowly drains the cell; when the charger tops up (below its recharge threshold), the LED blinks again until done. STATUS carries the same state (`SP_ST_VBUS`, `SP_ST_CHARGING`, `SP_ST_CHG_INHIBIT`).
- **Factory reset (reset pads JP2, cap off):** bridge the two round pads beside the module with tweezers. The LED lights while they are bridged (within about 2 s); held for 5 s, the puck erases its settings, pairings, shot count, shot log and captures (the LED stays on for about 10 s while it erases), blinks 5 times fast and reboots. `seq` carries on from where it was, so later events never reuse an earlier seq. Only a bridge made while the puck runs counts: pads already shorted at boot do nothing until released. After 3 crash or watchdog resets in a row, each within a minute of boot, the firmware erases its settings and pairings by itself; the shot log and captures stay. Phones must forget the puck and pair again (through the charger window).
- Method: LE Secure Connections, "Just Works" (no PIN). iOS and Android show their standard pairing prompt.
- Up to **4 phones** can be paired. Pairing a 5th replaces the one paired longest ago.
- **FORGET_BONDS** (CONTROL `0x07`, from a paired phone) removes all pairings, including the caller's, and disconnects. Use it when giving the puck away.

App flow for adding a puck (firmware 0.3.1; no unencrypted link is held while the user waits):
1. Scan for the service and list pucks (`ShotPuck-XXXX`, sorted by signal strength). Do not connect yet.
2. The user picks one; ask them to attach the charger. Keep scanning and watch that puck's advertisements.
3. As soon as its advertising carries the pairing marker, stop the scan, connect and trigger pairing straight away. On Android, call `createBond()`. On iOS, read an encrypted characteristic such as COUNT, which makes iOS pair.
4. Over the now encrypted link, read the DIS serial number and COUNT. Store the serial as this user's puck. From then on, connect only to that serial and use it as `device_id`.

Pucks on firmware 0.3.0 never show the marker: offer a manual "Pair now" button on the charger step that does step 3 at once. If pairing fails with "pairing not allowed", the window was closed: disconnect and ask the user to reattach the charger.

## Service `7a1e0001-5b0c-4f3a-9c6e-3a5d2b9e0a01`

| UUID (7a1e00XX-…) | Name | Props | Payload |
|---|---|---|---|
| `…0002…` | COUNT | read, notify | `uint32 total`, `uint32 last_seq` (8 B) |
| `…0003…` | EVENT | notify | 1–10 × `sp_event_t` (24 B each), below |
| `…0004…` | CONTROL | write | opcode + payload, below |
| `…0005…` | CONFIG | read, write | `sp_config_t` (19 B), below |
| `…0006…` | STATUS | read, notify | `sp_status_t` (7 B), below |
| `…0007…` | CAPTURE | notify | capture packets (header + data), below |
| `…0008…` | DIAG | read | `sp_diag_t` (8 B), below (firmware 0.3.1) |

All seven require a paired (encrypted) link.

### EVENT: every candidate, accepted or not

Each notification carries **one or more events back to back** (length = 24 × n, n ≤ 10). Always loop over the payload in 24-byte steps.

| Offset | Type | Field | Notes |
|---|---|---|---|
| 0 | u32 | seq | Monotonic across reboots and factory resets (not reset by RESET_COUNT); only a full-chip erase by cable starts it over |
| 4 | i64 | t_unix_ms | 0 if the phone had not sent SET_TIME since the puck booted |
| 12 | u32 | t_uptime_ms | Device uptime at the trigger |
| 16 | u16 | peak_hf_mg | Release shock (saturates about 32 g) |
| 18 | u16 | pre_still_ms | Stillness before the trigger |
| 20 | u16 | post_lf_mg | Bow motion after release |
| 22 | u8 | accepted | **1 = counted as a shot** |
| 23 | u8 | reason | 0 ok, 1 not still before, 2 no follow-through |

Rejected events are sent on purpose. Log them in the app during tuning: they are the data you need to set thresholds (see VALIDATION.md).

Events are sent live as they happen, and again on REPLAY. Replayed and live events can interleave and repeat, so order by `seq` and upsert.

### Storage on the puck

| Data | Where | Kept |
|---|---|---|
| Accepted shots | Flash log | about the last **900–1000 shots**; survives resets, battery cut-off and normal re-flashing (not a full-chip erase) |
| Rejected candidates | RAM | the last 128; lost on reset |
| `total`, `seq`, CONFIG, pairings | Flash (settings) | permanently |
| Captures | Flash, compressed (408 KB) | the newest that fit, see *CAPTURE*; survive resets, updates and normal re-flashing; a factory reset erases them |

### CONTROL opcodes

| Byte 0 | Payload | Effect |
|---|---|---|
| `0x01` SET_TIME | i64 unix_ms | Sets the wall clock. Send on every connect. |
| `0x02` RESET_COUNT | – | total = 0 (seq keeps counting) |
| `0x03` REPLAY | u32 from_seq | Re-sends stored events with seq ≥ from_seq: accepted shots from flash, then rejected candidates from RAM, batched on EVENT |
| `0x04` FACTORY_CFG | – | Restores default thresholds |
| `0x05` LED_BLINK | – | 5 blinks (find the puck) |
| `0x06` GET_CAPTURE | u32 seq | Re-sends the capture of shot `seq` from flash on CAPTURE (header with `n_samples = 0` if it is no longer stored) |
| `0x07` FORGET_BONDS | – | Removes all pairings (see *Pairing*) |

The puck queues up to 4 commands. A CONTROL write that arrives while 4 are still waiting fails with ATT error 0x11 (insufficient resources) and is not run: wait about 100 ms and write it again (firmware 0.3.1; 0.3.0 accepted the write and dropped the command).

A REPLAY of the whole flash log (about 1000 events) takes roughly 10–20 s at MTU 247. It runs in the background: live shots are still detected and sent first.

### CAPTURE: bow motion around each shot

For every **accepted** shot the puck records the raw accelerometer and gyroscope around the release trigger at 480 Hz. CONFIG `capture_mode` picks the window:

| Mode | Window | `n_samples` | `trigger_index` |
|---|---|---|---|
| 0 standard (default) | 1.0 s before to 0.5 s after the trigger | 720 | 480 |
| 1 extended | 6 s before to 2 s after the trigger | 3840 | 2880 |

If the puck woke up less than the pre-trigger time before the shot, the capture starts at the wake: `n_samples` and `trigger_index` are smaller by the missing part.

Each capture is compressed (lossless) into a 408 KB flash area as soon as its window is complete; the newest captures push out the oldest. How many fit depends on how well the motion compresses:

| Mode | Even with no compression | Expected (2–4× compression) |
|---|---|---|
| standard | 47 | about 90–150 |
| extended | 8 | about 17–35 |

The two modes share the area, so after a switch the new captures replace the old ones gradually. When the phone is subscribed to CAPTURE, the puck streams each capture automatically once it is stored (about 0.5 s after the EVENT in standard mode, 2 s in extended): one header packet, then data packets until all samples are sent. GET_CAPTURE fetches any stored capture again, also after a reset. Rejected candidates are not captured.

Byte 0 of every packet is the packet type.

**Header** (`sp_cap_header_t`, 15 B):

| Offset | Type | Field | Notes |
|---|---|---|---|
| 0 | u8 | type | `0x01` |
| 1 | u32 | seq | Same `seq` as the shot's EVENT |
| 5 | u16 | n_samples | 720 standard, 3840 extended, fewer after a recent wake; **0 if the capture is not available** |
| 7 | u16 | trigger_index | Sample index of the release trigger (480 standard, 2880 extended, less after a recent wake) |
| 9 | u16 | odr_hz | 480 |
| 11 | u16 | acc_ug_per_lsb | 976 (±32 g) |
| 13 | u16 | gyro_mdps_per_lsb | 70 (±2000 dps) |

**Data** (`sp_cap_data_t`, 7 B, then samples):

| Offset | Type | Field | Notes |
|---|---|---|---|
| 0 | u8 | type | `0x02` |
| 1 | u32 | seq | |
| 5 | u16 | first | Index of the first sample in this packet |
| 7 | 12 B × k | samples | `i16 ax, ay, az, gx, gy, gz`, raw LSB; k = (MTU − 3 − 7) / 12 |

Convert with `acc_mg = raw × acc_ug_per_lsb / 1000` and `gyro_dps = raw × gyro_mdps_per_lsb / 1000`. Sample `i` is at `(i − trigger_index) / odr_hz` seconds from the trigger, so the EVENT's `t_unix_ms` lines up with index `trigger_index`.

- **Axes** are the LSM6DSV32X's own axes. +Z points out of the cap (it reads about +1000 mg with the puck lying cap-up). The X/Y mapping to the bow is fixed by the mounting and must be confirmed once on hardware (VALIDATION.md §2).
- A transfer takes about 1–3 s (standard) or 6–12 s (extended) at MTU 247. Packets arrive in order. If a `first` is missing (disconnect), discard the capture and request it again with GET_CAPTURE.
- The IMU needs a few tens of ms after a wake before the gyroscope delivers data (datasheet turn-on time 35 ms), so a capture that starts at the wake can have zero gyro values in its first samples.

### CONFIG (`sp_config_t`, 20 B)

`u8 version(=3)`, then u16 each: `trig_hf_mg` (1000–30000, default 6000), `move_mg` (50–2000, 300), `pre_still_min_ms` (0–10000, 500), `post_start_ms` (100), `post_end_ms` (≤1500, 600; must be greater than start), `post_lf_min_mg` (0–5000, 250), `refractory_ms` (200–10000, 1500), `wake_mg` (16–2000, 150), `idle_timeout_s` (5–3600, 30), then `u8 capture_mode` (0 standard, 1 extended; default 0).

A 19-byte write with `version = 2` (no `capture_mode`) is still accepted and keeps the current mode. Reads always return the 20-byte v3 layout. FACTORY_CFG restores the thresholds but keeps the capture mode.

Out-of-range writes are rejected with ATT error 0x13 (value not allowed). Accepted values are persisted.

### STATUS (`sp_status_t`, 7 B)

`u8 battery_pct`, `u16 battery_mv`, `i8 temp_c`, `u8 flags`, `u8 fw_major`, `u8 fw_minor`.
Flags: bit0 VBUS (cable present), bit1 CHARGING, bit2 CHG_INHIBIT (too cold or hot to charge), bit3 ACTIVE, bit4 TIME_SET.

### DIAG (`sp_diag_t`, 8 B, firmware 0.3.1)

Read-only diagnostics, so a crash in the field leaves a trace the app can show or log.

| Offset | Type | Field | Meaning |
|---|---|---|---|
| 0 | u8 | reset_cause | Why the puck last started: 0 power-on, 1 reset pin, 2 software (OTA, factory reset, reboot), 3 watchdog (a hang), 4 crash (fatal error), 5 CPU lockup, 6 woke from System OFF (charger attached after a low-battery cut-off), 7 other (debugger) |
| 1 | u8 | crash_reason | Zephyr fatal error code when `reset_cause` = 4 (0 CPU exception, 1 spurious IRQ, 2 stack overflow, 3 kernel oops, 4 kernel panic), otherwise `0xFF` |
| 2 | u16 | crash_count | Crash, watchdog and lockup restarts since the storage was last wiped (kept across updates and pad resets) |
| 4 | u16 | main_stack_free | Fewest bytes the main thread's 3 KB stack has had free since boot |
| 6 | u16 | workq_stack_free | The same for the 1 KB system work queue |

On a fatal error the puck now reboots at once (0.3.0 halted until the 10 s watchdog fired). A crash still counts towards the boot-loop guard, and an unconfirmed update still rolls back. The values are refreshed at boot, on SET_TIME and every 10 s (60 s while idle); read DIAG when needed, there are no notifications. An app can log it once per connection and flag a growing `crash_count`.

## Recommended app sync (PowerSync-friendly)

1. Connect only to the user's own puck (its stored serial). The link encrypts automatically because the phone is paired.
2. Request MTU 247 or more, and subscribe to COUNT, EVENT, STATUS and CAPTURE.
3. Write SET_TIME.
4. Read COUNT, then write `REPLAY(last_seq_seen_by_app + 1)`. The app owns this position; the puck keeps no per-phone sync state, so any number of phones can sync.
5. Upsert events keyed by `(device_id, seq)`, with `device_id` = DIS serial number. seq is monotonic, so replays and duplicates are harmless.
6. Stay connected during the end. Each shot arrives as an EVENT with `accepted = 1` about 0.6 s after release (the follow-through window), followed by its CAPTURE.
7. Store captures keyed by `(device_id, seq)` as well. For accepted events without a complete capture, write `GET_CAPTURE(seq)`: the puck keeps the newest captures in flash (see *CAPTURE*), so this also works after a session offline.

Map shots to ends in the app: the puck has no notion of ends or arrows-per-end, and seq gaps are not errors.

## Firmware update (OTA)

The puck runs **MCUboot** and exposes the standard **MCUmgr SMP** service, so no custom update protocol is needed.

- SMP service `8D53DC1D-1DB7-4CD3-868B-8A527460AA84`, characteristic `DA2E7828-FBCE-4E01-AE9E-261174997C48`. It requires the same encrypted (paired) link as everything else.
- In the Flutter app, use Nordic's **`mcumgr_flutter`** plugin (wraps the iOS/Android nRF Connect Device Manager libraries) with the file `shotpuck_update.bin`. The nRF Connect Device Manager app works for manual updates.
- Recommended mode **Test and confirm**: upload (roughly 20–40 s at MTU 247), mark as test, reset. MCUboot then swaps the images (the puck is unavailable for about 10–20 s), the new version boots and advertises, and the phone reconnects and confirms it.
- The puck also confirms a new image by itself once a paired phone has connected over an encrypted link and the image has run 10 s. Reconnect and pair (encrypt) after the OTA reset; until then the image stays on trial. If the new image crashes or hangs before that (the watchdog resets a hang after 10 s), MCUboot **rolls back** to the previous version on the next boot.
- Only images signed with the ShotPuck private key are accepted: MCUboot checks the ECDSA-P256 signature before it boots anything.
- Check the result with the DIS Firmware Revision String or STATUS `fw_major`/`fw_minor`. SMP `image list` shows both slots with their versions and flags.
- Shots, captures, counts, config and pairings are kept across updates (separate flash partitions).
- **Firmware 0.3.0 changed the flash layout** (two 256 KB slots and a capture area, instead of two 460 KB slots). Never send it over the air to a puck running 0.2.x: flash 0.2.x pucks once by cable (`shotpuck_full.hex`, which includes the new bootloader). Updates between 0.3.x versions go over the air as before.
