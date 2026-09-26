# ShotPuck BLE protocol (v1)

Source of truth: `firmware/src/protocol.h`. All multi-byte values are **little-endian**, structs are packed.

## Discovery

- Advertised name: `ShotPuck`. The advertising data includes the service UUID below, so scan with a service filter.
- The puck advertises only while it is **ACTIVE**: from motion wake until 30 s of stillness, plus a 120 s linger. A puck lying still in a bag is invisible, so pick up the bow to make it connectable.
- One connection at a time. No pairing or bonding (see *Security*).
- **MTU:** event notifications are 24 bytes, so the ATT MTU must be at least 27. The puck requests 65 on connect. flutter_blue_plus on Android requests 512 by default. On iOS the MTU is negotiated automatically (185+).

## Service `7a1e0001-5b0c-4f3a-9c6e-3a5d2b9e0a01`

| UUID (7a1e00XX-…) | Name | Props | Payload |
|---|---|---|---|
| `…0002…` | COUNT | read, notify | `uint32 total`, `uint32 last_seq` (8 B) |
| `…0003…` | EVENT | notify | `sp_event_t` (24 B), below |
| `…0004…` | CONTROL | write | opcode + payload, below |
| `…0005…` | CONFIG | read, write | `sp_config_t` (19 B), below |
| `…0006…` | STATUS | read, notify | `sp_status_t` (7 B), below |

The standard Battery Service (0x180F, level %) is also exposed.

### EVENT: every candidate, accepted or not

| Offset | Type | Field | Notes |
|---|---|---|---|
| 0 | u32 | seq | Monotonic across reboots (not reset by RESET_COUNT) |
| 4 | i64 | t_unix_ms | 0 until the phone has sent SET_TIME since boot |
| 12 | u32 | t_uptime_ms | Device uptime at the trigger |
| 16 | u16 | peak_hf_mg | Release shock (saturates about 32 g) |
| 18 | u16 | pre_still_ms | Stillness before the trigger |
| 20 | u16 | post_lf_mg | Bow motion after release |
| 22 | u8 | accepted | **1 = counted as a shot** |
| 23 | u8 | reason | 0 ok, 1 not still before, 2 no follow-through |

Rejected events are sent on purpose. Log them in the app during tuning: they are the data you need to set thresholds (see VALIDATION.md).

### CONTROL opcodes

| Byte 0 | Payload | Effect |
|---|---|---|
| `0x01` SET_TIME | i64 unix_ms | Sets the wall clock. Send on every connect. |
| `0x02` RESET_COUNT | – | total = 0 (seq keeps counting) |
| `0x03` REPLAY | u32 from_seq | Re-notifies logged events with seq ≥ from_seq |
| `0x04` FACTORY_CFG | – | Restores default thresholds |
| `0x05` LED_BLINK | – | 5 blinks (find the puck) |

The device keeps the last **128 events in RAM**, so they are lost on reset or battery cut-off; `total` is persisted in flash.

### CONFIG (`sp_config_t`, 19 B)

`u8 version(=1)`, then u16 each: `trig_hf_mg` (1000–30000, default 6000), `move_mg` (50–2000, 300), `pre_still_min_ms` (0–10000, 500), `post_start_ms` (100), `post_end_ms` (≤1500, 600; must be greater than start), `post_lf_min_mg` (0–5000, 250), `refractory_ms` (200–10000, 1500), `wake_mg` (16–2000, 150), `idle_timeout_s` (5–3600, 30).

Out-of-range writes are rejected with ATT error 0x13 (value not allowed). Accepted values are persisted.

### STATUS (`sp_status_t`, 7 B)

`u8 battery_pct`, `u16 battery_mv`, `i8 temp_c`, `u8 flags`, `u8 fw_major`, `u8 fw_minor`.
Flags: bit0 VBUS (cable present), bit1 CHARGING, bit2 CHG_INHIBIT (too cold or hot to charge), bit3 ACTIVE, bit4 TIME_SET.

## Recommended app sync (PowerSync-friendly)

1. Connect, request MTU 64 or more, and subscribe to COUNT, EVENT and STATUS.
2. Write SET_TIME.
3. Read COUNT, then write `REPLAY(last_seq_seen_by_app + 1)`.
4. Upsert events keyed by `(device_id, seq)`. seq is monotonic, so replays are idempotent.
5. Stay connected during the end. Each shot arrives as an EVENT with `accepted = 1` about 0.6 s after release (the follow-through window).

Map shots to ends in the app: the puck has no notion of ends or arrows-per-end, and seq gaps are not errors.

## Security

v1 has no pairing, so anyone in BLE range can read counts or write CONTROL and CONFIG. That is acceptable for a training counter. If you need protection, enable `CONFIG_BT_SMP` with LE Secure Connections (Just Works) and set `BT_GATT_PERM_WRITE_ENCRYPT` on CONTROL and CONFIG. That change is small, but it is a protocol change for the app.
