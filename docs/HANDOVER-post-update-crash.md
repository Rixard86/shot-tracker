# Request from archesk: F313 crashes about 10 s after booting a new image

Written 2026-11-20 on the archesk side, for whoever works on the ShotPuck firmware next. Found while Richard updated his two pucks over the air from the app. The updates themselves succeed; the puck then crashes once, a few seconds into the new image.

## What happens

Puck ShotPuck-F313 (`DE:FD:82:EE:17:7D`, Richard calls it RÖD), updated 0.3.15 → 0.3.16 on 2026-11-20 at 14:48, phone a Galaxy S21 (Android 15), read from the phone's logcat:

| Time | What the phone saw |
|---|---|
| 14:48:06–14:48:42 | Upload (489 chunks), test, reset command: all fine |
| 14:48:46.6 | Link lost: the puck reset for the swap (expected) |
| 14:49:03.4 | mcumgr reconnects to the new image, subscribes to SMP and sends **confirm** (test-and-confirm mode) |
| 14:49:07.3 | The app opens its own link to read Device Information |
| 14:49:11.75 | The connection switches to the fast interval (20 × 1.25 ms, latency 0), the puck's charger request |
| 14:49:15.75 | Link lost, status 8 (supervision timeout, 4.0 s): **the puck stopped answering exactly at the switch** |
| 14:49:20 | The app reconnects; the puck runs 0.3.16 |

DIAG read with nRF Connect right afterwards (`7a1e0008`): `04 03 08 00 98 08 20 06 01 00 00 00 00 00 00 00 A4 02 00 00`

- `reset_cause` **4 (crash)**, `crash_reason` **3 (kernel oops)**, `crash_count` 8
- stack free: main 2200 B, workqueue 1568 B, so not a stack overflow
- `uptime_s` 676 when read at about 15:01, so it booted about 14:49–14:50 (allowing for the 60 s refresh while idle): the restart after the crash

The same puck's update 0.3.13 → 0.3.15 at 13:03 the same day ended the same way: link lost exactly 4.0 s after a switch to the fast interval, about 8.5 s after mcumgr reconnected.

**Control:** ShotPuck-ED45 (`F8:7C:08:64:F4:78`), updated 0.3.15 → 0.3.16 at 14:43, did not crash: its switch to the fast interval came about 10.3 s after the reconnect and the link held. F313's ordinary switches to the fast interval (during sync, during the upload) also hold. Only the one about 10 s into a freshly swapped image fails.

## Why it looks like a race at the self-confirm moment

About 10 s of uptime is exactly when `ota_confirm_when_healthy()` runs (`CONFIRM_AFTER_MS` plus a secured link, which mcumgr's SMP access provides). In the same main-loop tick the puck:

1. writes the confirm flag to flash (`boot_write_img_confirmed()`), and
2. powers RAM8 sections 3–5 off (`note_image_state(true)` → `supply_unused_ram_off()`), which a normal boot does at start-up instead.

On F313 both crashes coincide with the connection-update instant; on ED45 the instant came a second or two later. A kernel oops is how Zephyr's Bluetooth asserts end (`BT_ASSERT`/`LL_ASSERT` → `k_oops`) with the Zephyr controller this firmware uses. So the leading suspect is a controller assert from the flash write, or the RAM power-off, landing on the connection-update instant. That is a guess from timing, not proven.

Suggested ways to pin it down:

- Reproduce on the bench: build with `debug.conf`, update over the air, and read the assert's file and line from the log. Or skip the update: force a parameter update so its instant lands on the confirm (or call `boot_write_img_confirmed()` while an update is pending).
- Check whether moving the confirm (and the RAM power-off) out of a pending connection update, or doing the RAM power-off only at the next boot, makes it go away.

## Why it matters

- It does not lose the update today, but only because the phone's mcumgr confirm arrives before the crash. With test-only mode (archesk audit item 6), this crash would come before the puck's own confirm and MCUboot would roll F313 back on every update.
- Every update of F313 currently ends with a crash, a dropped link, and a crash count that grows (8 so far).
- The app's sheet reported these updates as failed. That wording is fixed on the app side: a failure after the restart now says the version could not be checked. Per Richard's rule, the app does not work around the crash itself.

## Response from shot-tracker (2026-11-20)

**The phone's log** (Richard's Galaxy S21 on USB, `adb logcat`, F313's update to 0.3.16) puts the confirm later than above. mcumgr and the app share one link (handle 0x40):

| Phone time | Event |
|---|---|
| 14:49:03.25 | Connected |
| 14:49:04.21 | Encrypted: the puck's self-confirm is now allowed once 10 s of uptime have passed |
| 14:49:04.92 | Interval 200 ms, latency 4 (the phone applying the puck's preferred slow parameters) |
| 14:49:06.94 / 07.32 | mcumgr's confirm (57-byte SMP write) and the puck's answer: **the image was confirmed here** |
| 14:49:08.32 | The puck's request for the fast interval arrives (5 s after connecting, as in 0.3.15) |
| 14:49:11.75 | Update instant: 25 ms, latency 0 |
| 14:49:15.75 | Supervision timeout, so the puck's last packet came at the instant itself |

**What the code rules out at the self-confirm:**

- **The flash write:** `boot_write_img_confirmed()` writes nothing once mcumgr has set `image_ok`.
- **The RAM power-off:** it switches off 0x20028000 and up, and the image's RAM ends at 0x2001D3B0, so nothing lives there.
- **The capture-store re-scan in the same pass** (slot 1 joins the store, about 0.4–0.7 s of the main thread): it only reads flash. Every Bluetooth thread (host RX, HCI TX, controller RX) is cooperative and pre-empts the main thread.
- **The 10 s housekeeping:** it runs all the time without trouble.
- **The GATT database hash timer:** 10 ms, not 10 s.

So the coincidence with the self-confirm is still unexplained, and the instant itself is where the puck dies.

**Next step: firmware 0.3.17.** DIAG gains `crash_pc` and `crash_lr`, the fault handler's program counter and link register on the boot after a crash. The crash happens in the new image, so updating F313 to 0.3.17 should reproduce it and record where. Read DIAG before the next restart; the shot-tracker session decodes it against the 0.3.17 build.

## Result with 0.3.17 (2026-11-20, 15:22)

The crash reproduced on F313's update 0.3.16 → 0.3.17, on the same timeline. Phone times:

| Phone time | Event |
|---|---|
| 15:22:25.95 | Drop for the swap |
| 15:22:42.06 | Reconnect |
| 15:22:43.06 | Encrypted |
| 15:22:45.79 | mcumgr's confirm |
| 15:22:50.60 | Switch to 25 ms (8.54 s after the reconnect, as before) |
| 15:22:54.60 | Supervision timeout |

DIAG afterwards: `04 03 09 00 98 08 20 06 00 00 00 00 00 00 00 00 1E 00 00 00 38 D6 00 00 2B D6 00 00`, so crash 9, kernel oops, `crash_pc` 0x0000D638, `crash_lr` 0x0000D62B.

**Decoded** against the 0.3.17 ELF: `prepare_cb`, `zephyr/subsys/bluetooth/controller/ll_sw/nordic/lll/lll_peripheral.c:342`, which is `LL_ASSERT_OVERHEAD(overhead)`. The controller started preparing the peripheral's connection event later than its reserved margin. With `CONFIG_BT_CTLR_ASSERT_OVERHEAD_START=y` (Zephyr's default) that late start is an assert; with it off, the controller skips that one radio event and carries on (Kconfig help: "Disabling this option permits the Controller to gracefully skip radio events that are delayed due to CPU usage latencies").

So the puck dies over one late connection event at the update instant. What made that event late is still open: some interrupt-level work or an interrupt lock at that moment. It isn't the main thread's re-scan, whose flash reads lock nothing. The fast switch on the following boot (15:23:08, no self-confirm pass) held.

**Fix proposed for 0.3.18:** `CONFIG_BT_CTLR_ASSERT_OVERHEAD_START=n`. Updating F313 to 0.3.18 tests it on the spot: no drop about 4 s after the switch.

## Fixed in 0.3.18 (2026-11-20, 15:32)

F313's update 0.3.17 → 0.3.18 went through without the restart. Reconnect at 15:32:55.02, switch to 25 ms at 15:33:05.22, and the link held. DIAG afterwards read `02 FF 09 00 98 08 20 06 … 28 00 00 00 00 00 00 00 00 00 00 00`: reset_cause 2 (the update's reset), crash_count still 9, crash_pc 0. This switch came 10.2 s after the reconnect rather than 8.5 s, so a few more updates will confirm it.

**ED45, right before its update (still 0.3.16, confirmed image, on the charger):** it dropped 4.0 s after 2 of 3 switches to the fast interval (15:35:31.63 → 35.62 and 15:35:46.62 → 50.62; the switch at 15:36:15.23 held), and it was advertising again within 0.5 s each time. So the late connection event is not tied to a new image or to the self-confirm. It can come at any switch from 200 ms / latency 4 to 25 ms / latency 0, which 0.3.15 made routine whenever a phone connects on the charger. 0.3.18 handles it either way by skipping the event. ED45's update 0.3.16 → 0.3.18 (reconnect 15:37:57.83, switch 15:38:07.63) held.

