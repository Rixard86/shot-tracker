# ShotPuck BLE protocol (v4)

Firmware 0.3.22 (v4: CONFIG grows to 22 bytes):
- **COUNT `last_seq` is the newest seq of any event.** Up to 0.3.21 the puck refreshed COUNT only for accepted shots, RESET_COUNT, DELETE_SHOT and at boot, so `last_seq` stayed at the last accepted shot although rejects and dev recordings (`reason` 3) take a seq too. Now COUNT is refreshed and notified after every event: accepted shot, reject or recording. An app that replays from its highest stored seq + 1, capped at `last_seq` (because a full-chip erase starts seq over), no longer asks again for the rejects after the last accepted shot on every reconnect. A pairing floor of `last_seq + 1` now also skips the rejects from before the pairing. Expect one more 12-byte COUNT notification per reject; `total` and `lifetime` still change only with accepted shots.
- **Reject captures are a dev setting** (CONTROL `0x0D` REJECT_CAPTURE + u8 threshold in g, see *CONTROL opcodes*), no longer a separate firmware build. With a threshold set, a rejected candidate (`reason` 1, 2 or 4) whose `peak_hf_mg` reaches it is captured as 0.3.21-Debug did at 20 g: in the current `capture_mode`, under the reject's `seq`, sent after its EVENT on CAPREC or CAPTURE and kept for GET_CAPTURE. Nothing is counted and the LED does not flash. It starts off and is not saved: a reboot turns it off. STATUS flag bit 6 shows it is on, so an app that wants it should write it again when it sees the bit clear.
- The puck reports 0.3.22 (image and DIS `0.3.22+0`), file `shotpuck-0.3.22.bin`.
- **Follow-through default 200 mg** (CONFIG `post_lf_min_mg`, was 250). On 2026-11-23 F313 missed one of 120 arrows (seq 28180) with `post_lf_mg` 249; that day's arrows otherwise reached 297 and more, while knocks and string plucks on a still bow gave 42–191. A saved config that holds the old default 250 moves to 200 at boot; any other saved value is kept. An app that shows or writes CONFIG reads the new value back as usual.
- **Ring check: drops and knock-overs are no longer shots.** After a release the bow keeps vibrating for a few hundred milliseconds; a puck or bow that hits a floor or table goes quiet within about 30 ms. The puck now averages the sample-to-sample jump from 30 to 150 ms after the trigger and rejects the candidate with the new `reason` **4** (no ring) when that average is below `ring_min_permille` thousandths of its `peak_hf_mg`. The check comes after stillness and follow-through, so reasons 1 and 2 keep their meaning. Default 40 (4 % of the peak); 0 turns the check off. On 2026-11-23 a dropped puck (ED45, seq 22694) was counted as a shot at 11 ‰, while 167 arrows from three bows rang at 113 ‰ or more, and a set-down rang at 5 ‰. String plucks ring like arrows (207 ‰), so they are still stopped by the stillness and follow-through checks. While REJECT_CAPTURE is set, a reason 4 reject above its threshold is captured, so the rule can be checked on new drops.
- **CONFIG v4 (22 B)** appends `u16 ring_min_permille` at offset 20 (see *CONFIG*). Reads return 22 bytes with `version` 4. 20-byte v3 writes (as archesk's capture-mode change sends) and 19-byte v2 writes are still accepted and keep the current ring value. A saved config from an older firmware gets the default 40 at boot.
- One new command (REJECT_CAPTURE), one new STATUS flag, one new CONFIG field and one new reject reason (4).

Firmware 0.3.21-Debug (still v3), a diagnostic build for finding missed shots:
- **Big rejects get a capture too.** A rejected candidate (`reason` 1 or 2) with `peak_hf_mg` of at least 20,000 (20 g) is captured like an accepted shot, in the current `capture_mode`, under the reject's `seq`. While the phone is subscribed, the capture follows the reject's EVENT on CAPREC or CAPTURE as a shot's does, and GET_CAPTURE returns it later. Nothing is counted: COUNT, `total`, lifetime and the shot log are untouched, and the LED does not flash. Real arrows peak at 40–65 g and handling bumps mostly below 20 g, so a missed arrow now comes with its raw motion and can be run through the detector again. The setting is `CONFIG_SHOTPUCK_REJECT_CAPTURE_MIN_MG` (`prj.conf`; 0 turns it off, the default).
- **The puck reports 0.3.21.** The image header says 0.3.21+0 and DIS Firmware Revision `0.3.21+0`; "-Debug" is only in the file name (`shotpuck-0.3.21-Debug.bin`) and the build (`EXTRAVERSION = debug`), because apps parse plain `x.y.z` versions. An app updates over it like any newer version; the next normal release is 0.3.22.
- Everything else as 0.3.20. No layout or command changes.

Firmware 0.3.20 (still v3):
- **The release may move the bow up to 60 ms before the trigger.** The stillness check (500 ms without movement over 300 mg) ignores the samples just before the trigger, because the release itself moves the bow. That window was 10 samples (21 ms); now it is 29 (60 ms). On 2026-11-22, 5 of F313's 21 arrows were rejected with `reason` 1 and `pre_still_ms` exactly 20: their release started moving the bow earlier than 21 ms before the trigger. The 19 shots F313 captured that day already moved 146–291 mg at the 21 ms mark, while none of 47 accepted captures moved more than 174 mg between 60 and 560 ms before the trigger. Accepted shots are unchanged. A rejected candidate that was moving right up to the window now reports `pre_still_ms` 60 instead of 20.
- **Transmit power +8 dBm** (was 0 dBm), for advertising and connections. On 2026-11-22 the link dropped 7 times in 15 minutes with the phone in a trouser pocket, each time while the bow was raised, drawn or held: the archer's body was between the puck and the phone.
- **Supervision timeout 8 s** (was 4 s) in both parameter sets (15–30 ms with latency 0, 105–210 ms with latency 4), so a link blocked for a few seconds at full draw survives. A phone that really leaves notices it 4 s later than before.
- **Rejects shot while the link is down can still be missed by an app.** COUNT `last_seq` moves only on accepted shots, so an app that compares it with its highest received seq sees nothing to replay after a reconnect, although REPLAY would also send the rejects kept in RAM. To get them, write REPLAY from the next seq after every reconnect even when COUNT shows nothing new, as step 4 of *Recommended app sync* says; the end marker tells when it is done. The puck behaves as before here. From 0.3.22 `last_seq` follows every event (see 0.3.22 above).
- No layout or command changes.

Firmware 0.3.19 (still v3):
- **No preferred connection parameters in the GAP service.** Up to 0.3.18 the GAP service had the Peripheral Preferred Connection Parameters characteristic (105–210 ms, latency 4). Android reads it once, in its own service discovery after pairing, keeps the values for that puck until the phone's Bluetooth restarts, and applies them about 1 s into every connection. A connection on the charger therefore went slow at 1 s and only became fast when the puck's own request (sent 5 s after connecting) took effect, about 8–10 s in. An app update that started in that window ran on the slow link and could hit Nordic's 2.5 s chunk timeout (ED45, 2026-11-20 15:35:40). Now the phone keeps its own default interval until the puck asks, 5 s after connecting: 15–30 ms with latency 0 on the charger, 105–210 ms with latency 4 off it.
- **Handles move down by 2.** The GAP service is 2 attributes shorter, so every later attribute (Battery, Device Information, MCUmgr SMP and the whole ShotPuck service) has a handle 2 lower than in 0.3.18. Find characteristics by UUID after service discovery, as before. The puck has Service Changed and Database Hash: the first boot of 0.3.19 (or of the image MCUboot reverts to) indicates Service Changed for the whole database to every bonded phone, and Android reads the hash on each connection to a bonded puck and rediscovers before it uses its cache.
- **A phone keeps the old values until its Bluetooth restarts.** A phone that paired with a puck since its Bluetooth last started still applies 105–210 ms / latency 4 to that puck after the update. Turning the phone's Bluetooth off and on clears them, and a pairing with 0.3.19 finds none to store.
- No layout or command changes.

Firmware 0.3.18 (still v3):
- **No crash at the switch to the fast interval.** Since 0.3.15 a puck on the charger asks for the 15–30 ms interval about 5 s after a phone connects. At that switch the Bluetooth controller sometimes prepared one connection event late and asserted (`lll_peripheral.c`, `LL_ASSERT_OVERHEAD`, found with 0.3.17's DIAG `crash_pc`). The puck then restarted and the phone saw a supervision timeout 4 s after the switch: on F313 after every update, and on ED45 at 2 of 3 ordinary connections on the charger. The controller now skips such a late event and the link carries on (`CONFIG_BT_CTLR_ASSERT_OVERHEAD_START=n`). The update to 0.3.18 already runs 0.3.18's code after the swap, so it is the first update without that restart.
- No layout or command changes.

Firmware 0.3.17 (still v3):
- **DIAG grows from 20 to 28 bytes** with where the last crash happened: `u32 crash_pc` and `u32 crash_lr`, the program counter and link register the fault handler saw. They are non-zero only when `reset_cause` is 4 (crash), for the boot right after it, like `crash_reason`. Looking them up in that firmware build's ELF gives the source line of the crash (for example the assert behind a kernel oops). An app that reads 20 bytes keeps working.
- No other layout or command changes.

Firmware 0.3.16 (still v3):
- **Shot times after a reboot:** an event recorded before any phone had sent SET_TIME since the puck started no longer stays at `t_unix_ms` 0. When the puck sends an event from the current boot after a SET_TIME, live or on REPLAY, it fills in the time from that boot's clock (uptime at the trigger plus the offset the last SET_TIME gave). The shot log keeps each record as it was written, so events from an earlier boot that never got the time still arrive with 0: nobody knows that boot's clock. A round shot after a reboot and synced in the same boot now arrives timed, even when nothing is logged after the sync.
- **Gyroscope offset in every capture header:** while awake, the puck measures the gyroscope's zero-rate offset itself in every second it is truly still (the bow on its stand) and stores the latest value with each capture. The CAPTURE header grows from 15 to 23 bytes and the CAPREC header from 24 to 32: `i16 gyro_offset_mdps[3]` and `u16 gyro_offset_age_s`, see *Gyro offset* below. CAPREC `payload_len`, `check` and the data packets are unchanged, so an app that reads only the old header bytes keeps working. CAPREC now needs an ATT MTU of at least 35 (27 before).
- **End of a replay:** after the last event of a REPLAY the puck sends one 4-byte EVENT notification, `u32 from_seq` of that REPLAY, see *EVENT*. An app that loops over EVENT in 24-byte steps ignores it.
- No other layout or command changes.

Firmware 0.3.15 (still v3):
- **Fast connection interval the whole time the charger is attached:** while a phone is connected and the charger is in, the puck asks for the 15–30 ms interval with latency 0 and keeps it, not only while events, captures or upload chunks are flowing. An update needs the charger, so an upload now starts on the fast interval. Before, it started on 105–210 ms with latency 4 and the puck asked for the fast interval only at the first chunk. One phone took 3.6–6.7 s to switch, and Nordic's update library gives up when a chunk gets no answer within 2.5 s, so app updates from 0.3.13 to 0.3.14 failed after the first chunk. On a connection made with the charger in, the puck's first interval request (sent 5 s after connecting) is already the fast one. After unplugging, the puck goes back to 105–210 ms 5 s later unless data is still flowing.
- The update to 0.3.15 itself still runs on the old firmware: a puck on 0.3.14 or older has the old behaviour (see *Firmware update*).
- No layout or command changes.

Firmware 0.3.14 (still v3):
- **Visible the whole time the charger is attached:** the puck keeps advertising while VBUS is present, also once it has gone IDLE, so the app can find it for an update or a sync without anyone moving it. After unplugging, the usual rule applies again: advertising stops at once if the puck has been IDLE for more than 120 s. The motion sensor still sleeps as before while charging.
- No layout or command changes.

Firmware 0.3.13 (still v3):
- **DIAG grows from 8 to 20 bytes** with a wake counter: `u32 motion_wakes` (how often a lift or other movement woke the sleeping puck since it started), `u32 wakes_without_shot` (of those, how many went back to sleep without an accepted shot: false wakes), `u32 uptime_s` (seconds since it started, to turn the counts into a rate). The counts start at 0 at every boot. An app that reads only the first 8 bytes keeps working.
- Two safeguards with no visible change: the charger-status input is switched off while no cable is in (a floating input can draw current), and the floating-point status flags are cleared before the puck sleeps (nRF52840 erratum 87).
- No other layout or command changes.

Firmware 0.3.12 (still v3):
- **Ultra capture mode** (CONFIG `capture_mode` 2): from 30 s before to 2 s after the trigger. The window start is rounded down to a group of 8 samples, so `n_samples` is 15,360–15,367 and `trigger_index` 14,400–14,407. The part until 6 s before the trigger is stored as means of 8 samples (60 Hz), 6 s to 1 s as means of 4 (120 Hz, as in extended), and the last second plus everything after it bit-exact. About 38 ultra captures fit in 660 KB (23 in 408 KB), estimated from real captures. Firmware 0.3.11 and older refuse mode 2.
- **CAPREC characteristic** (`…000A…`, notify): a capture as the puck stores it, compressed, for the app to decode: 2 (standard) to 10 (ultra) times fewer bytes than CAPTURE. An app opts in by subscribing to CAPREC instead of CAPTURE; an app that only subscribes to CAPTURE gets the same packets as before (ultra captures rebuilt at 480 Hz there too). See *CAPREC* below.
- **Fewer movement checks during long rests:** 20 minutes after the puck goes to sleep it checks for movement 1.875 times a second instead of 15 (about 3 µA less while asleep). Slow lifts now wake it more reliably; the first lift after a long rest can wake it up to about 0.5 s later. Every wake returns to the 15 Hz checks, so shooting and the breaks between ends are unchanged.
- No other layout or command changes.

Firmware 0.3.11 (still v3):
- **Extended captures hold about 40 % more shots:** in extended mode the puck stores the aiming part of each shot's capture (from the start of the window until 1 s before the trigger) as means of 4 samples (120 Hz), rounded to nearest. The last second before the trigger and everything after it stay bit-exact. When it sends the capture, the puck repeats each mean 4 times, so the CAPTURE header and packets are exactly as before: 3840 samples at `odr_hz` 480, `trigger_index` 2880. The aiming part therefore holds no detail above 60 Hz, and each group of 4 samples there has the same value. On 37 real captures, about 68 now fit in 660 KB (48 before) and 42 in 408 KB (29 before).
- Standard mode and RECORD captures are unchanged. Captures stored by older firmware still transfer exactly as they were stored.
- No change to any layout or command.

Firmware 0.3.10 (still v3):
- **A sleeping puck wakes when the bow is lifted or drawn.** Before, the sensor woke on a sudden change: more than 312 mg on one axis between two 15 Hz samples. A smooth lift or draw never made one. None of 37 real shots (2026-11-15) did in the 6 s before release, and neither did a 100° lift. Now it wakes on high-pass filtered acceleration (cutoff 1/45 of the sample rate, 0.33 Hz), so tilting and steady motion count.
- The CONFIG `wake_mg` default is now **90**; the sensor rounds it up to 93.75 mg. A saved config that still has the old default of 150 changes to 90 at boot; any other saved value is kept.
- No change to any layout or command.

Firmware 0.3.9 (still v3):
- **Faster inside; stored formats unchanged.** Stored shots, captures and settings keep their format, so an update from 0.3.8 keeps everything a 0.3.8-to-0.3.8 re-flash would keep (captures held in the second firmware slot are lost by any update, as before).
- **Two developer commands** for the app's hidden dev page (nothing changes for an app that does not use them):
  - **REG0_MODE** (CONTROL `0x0A` + u8: 0 = DC/DC, 1 = LDO) switches the regulator that makes the puck's 3.0 V from the cell. It is not saved: every reboot returns to DC/DC. STATUS `flags` bit 5 is set while REG0 runs as LDO, and STATUS is notified right after the switch.
  - **STREAM** (CONTROL `0x0C` + u8 divider, new STREAM characteristic `…0009…`): a live IMU stream for a plot, see *STREAM* below. The characteristic is appended to the ShotPuck service, the last service on the puck, so no existing handle moves; an app finds it after a new service discovery.
  - **RECORD** (CONTROL `0x0B` + u8 seconds, 1–10) records the IMU from now on for that many seconds as a capture that is not a shot. The puck sends an EVENT with `accepted` 0 and `reason` 3 (recording) and the same `seq` as the capture; the capture follows on CAPTURE like a shot's (header `trigger_index` 0, `n_samples` = seconds × 480). Nothing is counted (COUNT, lifetime and the shot log are untouched). An idle puck wakes for it and stays awake until it is stored. Any other value is refused with ATT 0x13.

Firmware 0.3.8 (still v3):
- **Lifetime shot counter:** COUNT grows from 8 to 12 bytes; the new `uint32 lifetime` at offset 8 counts accepted shots over the puck's whole life. It goes up with every accepted shot and down by one when DELETE_SHOT removes a shot taken since the counter started. RESET_COUNT, factory resets (pads or app), the boot-loop recovery and firmware updates (over the air or by cable without a chip erase) never change it; only a full-chip erase by cable clears it. It starts at 0 on the first boot of 0.3.8 or later. Apps that read only the first 8 bytes keep working.
- **More room for captures:** the second firmware slot holds captures while no update is in progress, about 660 KB instead of 408 KB (see *CAPTURE*). An update loses the captures stored there, so fetch captures before updating.

Firmware 0.3.7 (still v3):
- **FACTORY_RESET** (CONTROL `0x09` + the 4 ASCII bytes `WIPE`, i.e. `09 57 49 50 45`) does the same erase as the reset pads: settings, pairings, shot count, shot log and captures; `seq` carries on. It runs only while the charger is attached (the same rule as pairing and updates, so the puck is in someone's hand), and the puck refuses the write itself: ATT 0x13 (value not allowed) for a missing or wrong code, ATT 0xFC (write request rejected) without the charger; nothing is erased then. When accepted, the LED stays on while it erases (about 10 s), the link drops (the pairing is gone), it blinks 5 times fast and reboots. The phone must forget the puck and pair again through the charger window. Firmware 0.3.6 and older ignore the opcode.

Firmware 0.3.6 (still v3):
- **DELETE_SHOT** (CONTROL `0x08` + u32 seq) removes an accepted shot from the shot log, for example a false shot: REPLAY no longer sends it, and if it is newer than the last RESET_COUNT, `total` goes down by one and COUNT is notified. Its capture stays (GET_CAPTURE still returns it until it ages out). There is no undo. An unknown, already deleted or aged-out seq is ignored. Firmware 0.3.5 and older ignore the opcode.
- RESET_COUNT now also stores the seq at the reset, so deleting a shot from before the reset leaves the count alone. A puck updated from 0.3.5 or older has no stored reset point, so until its next RESET_COUNT every deletion lowers the count.

Firmware 0.3.5 (still v3):
- **Battery percent while charging** (STATUS `battery_pct` and the Battery Service): the charge current's lift (40 mV) is taken off the reading before it becomes a percent, and the percent stops at 99 while the charger is charging. It shows 100 only once the charge has ended. `battery_mv` stays the raw reading. The voltage table's 5/10/20 % points follow the cell's datasheet (3500/3580/3710 mV).
- **The firmware ends the charge**: when the charger terminates by itself (cell ≥ 4.10 V), or 60 min after the reading first reaches 4.15 V, whichever comes first. It then keeps the charger off (LED steady, 100 %, CHARGING clear) until the cell falls below 4.05 V, then tops up. A charge that has not ended 5 h after it started is stopped until the cable is plugged in again: LED dark, STATUS shows VBUS without CHARGING or CHG_INHIBIT.

Firmware 0.3.4 (still v3):
- A phone the puck already has a pairing for can pair again through the charger window, for example after "forget" on the phone or a reinstall. 0.3.3 and older refused that with "authentication requirements" (SMP 0x03) and needed a factory reset.

Firmware 0.3.3 (still v3):
- Faster firmware updates: the puck answers the MCUmgr parameters command (4 buffers of 384 bytes), so an app can send several upload chunks at once in larger frames, and it uses the 15–30 ms connection interval while an upload runs (see *Firmware update*).

Firmware 0.3.2 (still v3):
- The pairing window closes as soon as the charger is removed (still at most 60 s), see *Pairing*.
- Firmware uploads over Bluetooth work only while the charger is attached; otherwise the puck refuses each upload chunk with SMP rc 6 (see *Firmware update*).

Firmware 0.3.1 (still v3; the existing layouts are unchanged):
- While the pairing window is open, the advertising data carries a **pairing marker** (manufacturer data), so an app can pair as soon as the charger goes in (see *Discovery and identity*).
- A link that is **not encrypted 30 s after it connects is dropped**. Connect and pair in one go; never hold an unpaired link open while the user does something.
- A CONTROL write that finds the command queue full fails with ATT error 0x11 (see *CONTROL*).
- `seq` never goes backwards, also across a factory reset.
- Fixes 0.3.0's link loss about 2 s after a bonded phone changes its subscriptions (a stack overflow in the firmware's settings store). 0.3.0 is obsolete; no puck runs it.
- New read-only **DIAG** characteristic (`…0008…`): why the puck last restarted, a crash count and stack headroom (see *DIAG*).
- While events or captures are streaming, the puck asks for a 15–30 ms connection interval, and goes back to 105–210 ms 5 s after the transfer ends.

v4 (compared with v3, firmware 0.3.22):
- CONFIG gains `u16 ring_min_permille` and is 22 bytes. 20-byte v3 and 19-byte v2 writes are still accepted.
- EVENT `reason` 4: no ring (the ring check).

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
- The puck advertises while it is **ACTIVE** (from motion wake until 30 s of stillness, plus a 120 s linger), while its **pairing window** is open and, from firmware 0.3.14, whenever the **charger** is attached. A puck lying still in a bag is invisible, so pick up the bow to make it connectable.
- **Pairing marker** (firmware 0.3.1): while the pairing window is open, the advertising data also carries manufacturer-specific data, company ID `0xFFFF` (`SP_ADV_COMPANY_ID`), payload one flags byte with bit 0 set (`SP_ADV_PAIRING_OPEN`). Outside the window the field is absent. The app matches it only on devices that also advertise the ShotPuck service UUID. `0xFFFF` is the Bluetooth SIG's ID for tests and prototypes; a product release needs an assigned company ID, and then both firmware and app change this constant together.
- One connection at a time.
- **Unencrypted links are dropped after 30 s** (firmware 0.3.1): a connection that has not reached an encrypted link 30 s after it was made is disconnected by the puck. Paired phones encrypt within a second or two (at their first subscribe, or by re-encrypting on connect). Tools such as nRF Connect can still read the open services; they are disconnected 30 s later unless they pair.
- **MTU:** a single event is 24 bytes, so the ATT MTU must be at least 27. The puck requests 247 on connect (and supports LE data length extension). A larger MTU makes transfers faster: up to 10 events or 19 capture samples per packet at MTU 247. flutter_blue_plus on Android requests 512 by default. On iOS the MTU is negotiated automatically (185+).

## Pairing

Everything in the ShotPuck service needs an **encrypted link**, which a phone only gets by pairing (bonding) with the puck. Reading COUNT, CONFIG or STATUS, subscribing to any notification, and writing CONTROL or CONFIG all fail without it (ATT "insufficient encryption/authentication"). The Device Information Service and the standard Battery Service stay open.

- **Pairing window:** the puck accepts a *new* pairing only for **60 s after the charger is attached, and only while it stays attached** (0.3.2; also after a boot with the charger attached). Removing the charger closes the window at once. The LED blinks once a second while the window is open, and blinks twice when a phone has paired. Outside the window, pairing requests are rejected. Phones that are already paired reconnect and encrypt at any time.
- **Charger LED** (after the pairing window, while the charger stays attached; `firmware/src/chg_led.c`): one short blink every 2 s while charging; **steady on** once the charge has ended (the charger reports done with the cell ≥ 4.10 V, or since 0.3.5 the firmware ends it 60 min after the reading first reaches 4.15 V); three quick blinks every 2 s when charging is paused by the temperature gate; dark if the charger is attached but neither charging nor full (no input power, a fault, or since 0.3.5 the 5 h charge time limit). The steady LED starts only after the charge has ended, so its ~1 mA never holds the charge current above the termination threshold. It slowly drains the cell; since 0.3.5 the firmware keeps the charger off until the cell falls below 4.05 V, then the LED blinks again while it tops up. STATUS carries the same state (`SP_ST_VBUS`, `SP_ST_CHARGING`, `SP_ST_CHG_INHIBIT`).
- **Factory reset from the app** (0.3.7, charger attached): CONTROL FACTORY_RESET, see the 0.3.7 notes at the top. It erases the same things as the reset pads below.
- **Factory reset (reset pads JP2, cap off):** bridge the two round pads beside the module with tweezers. The LED lights while they are bridged (within about 2 s); held for 5 s, the puck erases its settings, pairings, shot count, shot log and captures (the LED stays on for about 10 s while it erases), blinks 5 times fast and reboots. `seq` carries on from where it was, so later events never reuse an earlier seq. Only a bridge made while the puck runs counts: pads already shorted at boot do nothing until released. After 3 crash or watchdog resets in a row, each within a minute of boot, the firmware erases its settings and pairings by itself; the shot log and captures stay. Phones must forget the puck and pair again (through the charger window).
- Method: LE Secure Connections, "Just Works" (no PIN). iOS and Android show their standard pairing prompt.
- Up to **4 phones** can be paired. Pairing a 5th replaces the one paired longest ago.
- **A phone that forgot the puck** (removed it in its Bluetooth settings, reinstalled the app) can pair again through the charger window from firmware 0.3.4: the new pairing replaces the puck's old one for that phone. Firmware 0.3.3 and older refuse it with "authentication requirements" (SMP 0x03), and only a factory reset on the board helps.
- **FORGET_BONDS** (CONTROL `0x07`, from a paired phone) removes all pairings, including the caller's, and disconnects. Use it when giving the puck away.

App flow for adding a puck (firmware 0.3.1; no unencrypted link is held while the user waits):
1. Scan for the service and list pucks (`ShotPuck-XXXX`, sorted by signal strength). Do not connect yet.
2. The user picks one; ask them to attach the charger. Keep scanning and watch that puck's advertisements.
3. As soon as its advertising carries the pairing marker, stop the scan, connect and trigger pairing straight away. On Android, call `createBond()`. On iOS, read an encrypted characteristic such as COUNT, which makes iOS pair.
4. Over the now encrypted link, read the DIS serial number and COUNT. Store the serial as this user's puck. From then on, connect only to that serial and use it as `device_id`.

Ask the user to keep the charger in until the phone has paired: unplugging closes the window (0.3.2). If pairing fails with "pairing not allowed", the window was closed: disconnect and ask the user to plug the charger in again.

## Service `7a1e0001-5b0c-4f3a-9c6e-3a5d2b9e0a01`

| UUID (7a1e00XX-…) | Name | Props | Payload |
|---|---|---|---|
| `…0002…` | COUNT | read, notify | `uint32 total`, `uint32 last_seq`, from 0.3.8 `uint32 lifetime` (12 B; 8 B before 0.3.8). `last_seq` is the newest event's seq (any event from 0.3.22, the last accepted shot before) |
| `…0003…` | EVENT | notify | 1–10 × `sp_event_t` (24 B each), below |
| `…0004…` | CONTROL | write | opcode + payload, below |
| `…0005…` | CONFIG | read, write | `sp_config_t` (22 B from 0.3.22, 20 B before), below |
| `…0006…` | STATUS | read, notify | `sp_status_t` (7 B), below |
| `…0007…` | CAPTURE | notify | capture packets (header + data), below |
| `…0008…` | DIAG | read | `sp_diag_t` (28 B since 0.3.17, 20 B in 0.3.13–0.3.16, 8 B in 0.3.1–0.3.12), below |
| `…0009…` | STREAM | notify | live IMU samples for the dev page (firmware 0.3.9), see *STREAM* below |
| `…000A…` | CAPREC | notify | captures as stored, compressed (firmware 0.3.12), see *CAPREC* below |

All of them require a paired (encrypted) link.

### EVENT: every candidate, accepted or not

Each notification carries **one or more events back to back** (length = 24 × n, n ≤ 10). Always loop over the payload in 24-byte steps.

| Offset | Type | Field | Notes |
|---|---|---|---|
| 0 | u32 | seq | Monotonic across reboots and factory resets (not reset by RESET_COUNT); only a full-chip erase by cable starts it over |
| 4 | i64 | t_unix_ms | 0 if the phone had not sent SET_TIME since the puck booted. From 0.3.16 the puck fills it in when it sends an event of the current boot after a SET_TIME, so 0 only stays for events of an earlier boot that never got the time |
| 12 | u32 | t_uptime_ms | Device uptime at the trigger |
| 16 | u16 | peak_hf_mg | Release shock (saturates about 32 g) |
| 18 | u16 | pre_still_ms | Stillness before the trigger |
| 20 | u16 | post_lf_mg | Bow motion after release |
| 22 | u8 | accepted | **1 = counted as a shot** |
| 23 | u8 | reason | 0 ok, 1 not still before, 2 no follow-through, 3 dev recording (0.3.9, CONTROL RECORD; never a shot), 4 no ring (0.3.22: a drop, set-down or knock-over); 254 and 255 reserved for the app (below) |

**Reserved reasons:** the firmware never sends `reason` 254 or 255; they belong to the app. archesk marks its stored shots with them: 254 = removed by the archer, the puck not told yet; 255 = removed and DELETE_SHOT sent. Value 3 is the dev recording (0.3.9); 4–253 are free for future firmware reasons. A new firmware reason must never use 254 or 255.

Rejected events are sent on purpose. Log them in the app during tuning: they are the data you need to set thresholds (see VALIDATION.md).

Events are sent live as they happen, and again on REPLAY. Replayed and live events can interleave and repeat, so order by `seq` and upsert.

**End of a replay** (firmware 0.3.16): when a REPLAY has sent its last event (shots from flash, then rejects from RAM), the puck sends one EVENT notification of exactly 4 bytes: `u32 from_seq`, the value that REPLAY was written with. It comes after all of that replay's events; live events may follow it. A replay with nothing to send sends only this marker. There is no marker when the replay is cut off (disconnect, EVENT unsubscribed, a failed send) or when a newer REPLAY replaces it before it ends: then only the newer one ends with a marker. Wait for the marker with your REPLAY's `from_seq` instead of a quiet period, and keep a timeout for firmware 0.3.15 and older and for cut-off replays. An EVENT notification is either 4 bytes (this marker) or a multiple of 24 bytes (events).

### Storage on the puck

| Data | Where | Kept |
|---|---|---|
| Accepted shots | Flash log | about the last **900–1000 shots**; survives resets, battery cut-off and normal re-flashing (not a full-chip erase) |
| Rejected candidates | RAM | the last 128; lost on reset |
| `total`, `seq`, CONFIG, pairings | Flash (settings) | permanently |
| `lifetime` (0.3.8) | Flash, the free end of the bootloader area | through factory resets, RESET_COUNT and every firmware update; only a full-chip erase clears it |
| Captures | Flash, compressed (408 KB; about 660 KB with the second firmware slot from 0.3.8) | the newest that fit, see *CAPTURE*; survive resets and normal re-flashing; an update keeps those in the 408 KB area and loses those in the second slot; a factory reset erases them |

### CONTROL opcodes

| Byte 0 | Payload | Effect |
|---|---|---|
| `0x01` SET_TIME | i64 unix_ms | Sets the wall clock. Send on every connect. |
| `0x02` RESET_COUNT | – | total = 0 (seq keeps counting); from 0.3.6 also remembers the seq at the reset for DELETE_SHOT |
| `0x03` REPLAY | u32 from_seq | Re-sends stored events with seq ≥ from_seq: accepted shots from flash (not deleted ones), then rejected candidates from RAM, batched on EVENT; from 0.3.16 followed by a 4-byte end marker on EVENT (see *EVENT*) |
| `0x04` FACTORY_CFG | – | Restores default thresholds |
| `0x05` LED_BLINK | – | 5 blinks (find the puck) |
| `0x06` GET_CAPTURE | u32 seq | Re-sends the capture of shot `seq` from flash, on CAPREC when it is subscribed (0.3.12), else on CAPTURE (header with `n_samples = 0` if it is no longer stored) |
| `0x07` FORGET_BONDS | – | Removes all pairings (see *Pairing*) |
| `0x08` DELETE_SHOT | u32 seq | Removes accepted shot `seq` from the shot log (0.3.6): REPLAY skips it, `total` drops by one if the shot is newer than the last RESET_COUNT (COUNT notified), the capture stays; no undo, unknown seq ignored |
| `0x09` FACTORY_RESET | 4 bytes `WIPE` (`57 49 50 45`) | Erases settings, pairings, count, shot log and captures, then reboots (0.3.7). Only with the charger attached: refused with ATT 0x13 for a wrong code and ATT 0xFC without the charger |
| `0x0A` REG0_MODE | u8 mode (0 DC/DC, 1 LDO) | Dev page (0.3.9): switches REG0, not saved (a reboot returns to DC/DC); STATUS bit 5 shows LDO. Other values: ATT 0x13 |
| `0x0B` RECORD | u8 seconds (1–10) | Dev page (0.3.9): records the IMU for that long as a capture that is not a shot (EVENT reason 3, then the capture on CAPTURE or CAPREC). Other values: ATT 0x13 |
| `0x0C` STREAM | u8 divider (0 stop, 1–8) | Dev page (0.3.9): live IMU stream on STREAM at 480 Hz / divider, see *STREAM*. Not subscribed: ATT 0xFC; divider above 8: ATT 0x13 |
| `0x0D` REJECT_CAPTURE | u8 threshold in g (0 off, 1–65) | Dev page (0.3.22): also capture every rejected candidate whose `peak_hf_mg` is at least threshold × 1000, as for a shot (see *CAPTURE*); 20 matches 0.3.21-Debug. Not saved (a reboot turns it off); STATUS bit 6 shows it is on. Above 65: ATT 0x13 |

The puck queues up to 4 commands. A CONTROL write that arrives while 4 are still waiting fails with ATT error 0x11 (insufficient resources) and is not run: wait about 100 ms and write it again (firmware 0.3.1; 0.3.0 accepted the write and dropped the command).

A REPLAY of the whole flash log (about 1000 events) takes roughly 10–20 s at MTU 247. It runs in the background: live shots are still detected and sent first.

### CAPTURE: bow motion around each shot

For every **accepted** shot (and every reject at or above the REJECT_CAPTURE threshold from 0.3.22, of 20 g or more in 0.3.21-Debug) the puck records the raw accelerometer and gyroscope around the release trigger at 480 Hz. CONFIG `capture_mode` picks the window:

| Mode | Window | `n_samples` | `trigger_index` |
|---|---|---|---|
| 0 standard (default) | 1.0 s before to 0.5 s after the trigger | 720 | 480 |
| 1 extended | 6 s before to 2 s after the trigger | 3840 | 2880 |
| 2 ultra (0.3.12) | 30 s before (rounded down to a group of 8 samples) to 2 s after the trigger | 15,360–15,367 | 14,400–14,407 |

If the puck woke up less than the pre-trigger time before the shot, the capture starts at the wake: `n_samples` and `trigger_index` are smaller by the missing part.

Each capture is compressed into a 408 KB flash area as soon as its window is complete; the newest captures push out the oldest. From 0.3.8 the area also takes in the second firmware slot while no update is in progress, about 660 KB in all. Standard captures are lossless. From 0.3.11 an extended capture keeps only the means of 4 samples (120 Hz, rounded to nearest) from the start of the window until 1 s before the trigger, and sends each mean 4 times: that part holds no detail above 60 Hz. The last second before the trigger and everything after it stay lossless. After a recent wake, the averaged part is shorter; if the trigger came 1 s or less after the wake, nothing is averaged. An ultra capture (0.3.12) adds means of 8 samples (60 Hz, no detail above 30 Hz) from the start of its window until 6 s before the trigger; the 60 Hz groups start at the window start, which is always a multiple of 8 samples after the wake. If the trigger came 6 s or less after the wake, an ultra capture is stored like an extended one. How many captures fit depends on how well the motion compresses:

| Mode | No compression, 408 KB | Compressed, 408 KB | No compression, 660 KB (0.3.8) | Compressed, 660 KB (0.3.8) |
|---|---|---|---|---|
| standard | 47 | about 90–150 (expected, 2–4×) | about 75 | about 145–240 (expected, 2–4×) |
| extended, 0.3.10 and older | 8 | 29 (measured) | about 12 | 48 (measured) |
| extended, 0.3.11 | 8 | 42 (measured) | about 12 | 68 (measured) |
| ultra, 0.3.12 | 2 | about 23 (estimated) | 3 | about 38 (estimated) |

The measured counts come from 37 real extended captures (2026-11-12 to 2026-11-15) run through the firmware's own encoder and store. The ultra estimate joins the same 37 captures into one stream, so each 30 s window holds three or four other shots: real ultra captures with calmer history may be a little smaller.

The 660 KB area shrinks back to 408 KB when a firmware update starts (the captures stored in the second slot are lost then) and while a just-updated firmware is on trial; it grows again once the new firmware has confirmed itself. Fetch the captures you want to keep before starting an update.

The two modes share the area, so after a switch the new captures replace the old ones gradually. When the phone is subscribed to CAPTURE, the puck streams each capture automatically once it is stored (about 0.5 s after the EVENT in standard mode, 2 s in extended): one header packet, then data packets until all samples are sent. GET_CAPTURE fetches any stored capture again, also after a reset. Rejected candidates are not captured, except those at or above the REJECT_CAPTURE threshold (0.3.22, dev setting) and in 0.3.21-Debug those with a peak of 20 g or more.

Byte 0 of every packet is the packet type.

**Header** (`sp_cap_header_t`, 23 B; 15 B in firmware 0.3.15 and older):

| Offset | Type | Field | Notes |
|---|---|---|---|
| 0 | u8 | type | `0x01` |
| 1 | u32 | seq | Same `seq` as the shot's EVENT |
| 5 | u16 | n_samples | 720 standard, 3840 extended, fewer after a recent wake; **0 if the capture is not available** |
| 7 | u16 | trigger_index | Sample index of the release trigger (480 standard, 2880 extended, less after a recent wake) |
| 9 | u16 | odr_hz | 480 |
| 11 | u16 | acc_ug_per_lsb | 976 (±32 g) |
| 13 | u16 | gyro_mdps_per_lsb | 70 (±2000 dps) |
| 15 | i16 × 3 | gyro_offset_mdps | Gyroscope zero-rate offset x, y, z in mdps (0.3.16), see *Gyro offset* |
| 21 | u16 | gyro_offset_age_s | Seconds from the still second it was measured in to the trigger; 65534 means that long or longer; **65535 = unknown** (offsets 0) |

**Data** (`sp_cap_data_t`, 7 B, then samples):

| Offset | Type | Field | Notes |
|---|---|---|---|
| 0 | u8 | type | `0x02` |
| 1 | u32 | seq | |
| 5 | u16 | first | Index of the first sample in this packet |
| 7 | 12 B × k | samples | `i16 ax, ay, az, gx, gy, gz`, raw LSB; k = (MTU − 3 − 7) / 12 |

Convert with `acc_mg = raw × acc_ug_per_lsb / 1000` and `gyro_dps = raw × gyro_mdps_per_lsb / 1000`. Sample `i` is at `(i − trigger_index) / odr_hz` seconds from the trigger, so the EVENT's `t_unix_ms` lines up with index `trigger_index`.

- **Axes** are the LSM6DSV32X's own axes. +Z points out of the cap (it reads about +1000 mg with the puck lying cap-up). The X/Y mapping to the bow is fixed by the mounting and must be confirmed once on hardware (VALIDATION.md §2).
- A transfer takes about 1–3 s (standard), 6–12 s (extended) or 25–50 s (ultra) at MTU 247. CAPREC sends the same captures in about 1, 2 and 3–6 s. Packets arrive in order. If a `first` is missing (disconnect), discard the capture and request it again with GET_CAPTURE.
- The IMU needs a few tens of ms after a wake before the gyroscope delivers data (datasheet turn-on time 35 ms), so a capture that starts at the wake can have zero gyro values in its first samples.

**Gyro offset** (firmware 0.3.16): a gyroscope reads a small rate when it is not turning (LSM6DSV32X: ±1 dps typical, changing by about 0.006 dps per °C). Integrated over a second of aiming, that is an angle error of the same size in degrees. While awake, the puck checks every second of samples (480 samples; after a wake it first skips 48, which can hold the gyroscope's turn-on zeros). A second counts as still when, on every axis, the gyroscope's standard deviation is at most 0.25 dps, the accelerometer's at most 5 mg, and the mean rate at most 5 dps. Its mean rate becomes the offset. The puck keeps the newest one until it restarts, also while it sleeps, and puts it into each capture's header with its age at the trigger. In practice the still seconds come from the bow resting on its stand until the puck goes to sleep (`idle_timeout_s`); aiming is never that still. On real data, none of the 259 seconds in 37 shot captures (2026-11-12 to 15) counted as still, while every second of a puck lying still (2026-11-16) did, giving (−18, −181, −15) mdps within ±5 mdps. The median of each shot's mean rate in the last second of aiming gave (38, −19, 46) mdps on the same shots. Subtract `gyro_offset_mdps / 1000` from every `gyro_dps` sample before integrating. A large age means the temperature may have changed since. **Age 65535 means unknown:** no still second since the puck started, or a capture stored by 0.3.15 or older (CAPTURE and CAPREC send those as before, with this value). Then fall back to the app's own estimate.

### CAPREC: captures as stored (firmware 0.3.12)

Subscribe to CAPREC (`…000A…`) instead of CAPTURE and the puck sends each capture as the compressed record it keeps in flash: about 4 KB for a standard capture, 10 KB extended and 18 KB ultra, against 8.6, 46 and 184 KB on CAPTURE. The app decodes it. Everything else works as on CAPTURE: the capture is sent automatically after the shot while subscribed, GET_CAPTURE sends it again, and it waits while STREAM runs. With both subscribed, the puck uses CAPREC. Its header needs an ATT MTU of at least 35 from 0.3.16 (27 before). From 0.3.16 the puck stores the gyro offset in the first 8 bytes of each record and leaves them out of what it sends: `payload_len`, the data packets and `check` cover only the compressed samples, exactly as before.

**Header** (`sp_caprec_header_t`, 32 B; 24 B in firmware 0.3.12–0.3.15):

| Offset | Type | Field | Notes |
|---|---|---|---|
| 0 | u8 | type | `0x03` |
| 1 | u32 | seq | Same `seq` as the shot's EVENT |
| 5 | u16 | n_samples | As in the CAPTURE header; **0 if the capture is not available** (then `payload_len` is 0 and no data follows) |
| 7 | u16 | trigger_index | As in the CAPTURE header |
| 9 | u8 | format | 0 full rate (standard and RECORD), 1 extended (0.3.11), 2 ultra (0.3.12). Captures stored by 0.3.10 and older are format 0 |
| 10 | u32 | payload_len | Bytes of compressed data that follow |
| 14 | u32 | check | CRC-32 of the record, see below |
| 18 | u16 | odr_hz | 480 |
| 20 | u16 | acc_ug_per_lsb | 976 |
| 22 | u16 | gyro_mdps_per_lsb | 70 |
| 24 | i16 × 3 | gyro_offset_mdps | As in the CAPTURE header (0.3.16) |
| 30 | u16 | gyro_offset_age_s | As in the CAPTURE header; 65535 = unknown |

**Data** (`sp_caprec_data_t`, 9 B, then bytes): `u8 type` = `0x04`, `u32 seq`, `u32 offset` (byte offset of the first byte in this packet), then up to MTU − 12 payload bytes. Packets arrive in order. If `offset` does not continue where the last packet ended, discard the capture and GET_CAPTURE it again.

**Check:** the standard CRC-32 (as zlib's `crc32`: reflected polynomial 0xEDB88320, start 0xFFFFFFFF, final XOR 0xFFFFFFFF) over the payload followed by 12 bytes, little-endian: `u32 seq`, `u16 n_samples`, `u16 trigger_index`, `u32 payload_len`. It must equal `check`.

**Decoding** (n = `n_samples`, t = `trigger_index`):

1. Parts of the window. A (samples before the full-rate part) = t − 480 if `format` is 1 or 2 and t > 480, else 0. L (samples in the 60 Hz part) = ⌊(t − 2880) / 8⌋ × 8 if `format` is 2 and t > 2880, else 0. The record holds S = L / 8 + ⌈(A − L) / 4⌉ + (n − A) stored samples.
2. The payload is a bit stream, most significant bit of each byte first, holding the S stored samples in blocks of 32 (the last block is shorter when S is not a multiple of 32). Each block holds the six axes one after another, in the order ax, ay, az, gx, gy, gz. For each axis:
   - 2 bits mode: 0 delta, 1 slope, 2 raw (3 never occurs).
   - Raw: one 16-bit two's complement value per sample of the block.
   - Delta or slope: 5 bits k (0–17), then one Rice code per sample. Count 1 bits until a 0 bit: q ones give the value (q << k) | the next k bits. After 24 ones in a row no 0 bit follows, and the value is the next 18 bits. The residual is value / 2 for an even value and −(value + 1) / 2 for an odd one. The prediction is `last` for delta and 2 × `last` − `before_last` for slope; the sample is prediction + residual.
   - Each axis keeps its own `last` and `before_last`, both 0 at the start of the record and carried across blocks; every sample, raw ones too, moves them on (`before_last` = `last`, `last` = sample). Every sample lies within the int16 range.
3. Rebuild 480 Hz (n samples) from the stored samples in order. The first L / 8 each stand for 8 samples (indices 0 to L − 1). The next ones cover L to A − 1 in groups counted back from A: the first group has (A − L) mod 4 samples if that is not 0, then groups of 4, each stored sample standing for its group. The rest map one to one to A to n − 1. The puck's CAPTURE path repeats each stored sample over its group; an app may also interpolate for plots, but each group's sum must stay equal to its size × the stored sample (repeating does that exactly).

**Test vectors and a reference decoder:** shot-tracker `firmware/sim/vectors/caprec-vectors.json` holds 9 records written by the firmware's own capture code (standard, a recording, extended, ultra, captures shortened by a wake, and two stress signals that use raw blocks, slope blocks and the 24-ones escape). Each has the header fields, the payload as hex, every stored sample and the CRC-32 of the rebuilt 480 Hz samples (each sample 6 × int16 little-endian, ax to gz). `firmware/sim/caprec_decode.py` decodes and checks them all: `python firmware/sim/caprec_decode.py firmware/sim/vectors/caprec-vectors.json`.

### CONFIG (`sp_config_t`, 22 B)

`u8 version(=4)`, then u16 each: `trig_hf_mg` (1000–30000, default 6000), `move_mg` (50–2000, 300), `pre_still_min_ms` (0–10000, 500), `post_start_ms` (100), `post_end_ms` (≤1500, 600; must be greater than start), `post_lf_min_mg` (0–5000, 200 from 0.3.22, 250 before), `refractory_ms` (200–10000, 1500), `wake_mg` (16–2000, 90; high-pass filtered from 0.3.10), `idle_timeout_s` (5–3600, 30), then `u8 capture_mode` (0 standard, 1 extended, 2 ultra from 0.3.12; default 0), then `u16 ring_min_permille` (0–1000, default 40; from 0.3.22): the ring check rejects a candidate (`reason` 4) whose mean sample-to-sample jump 30–150 ms after the trigger is below this many thousandths of its `peak_hf_mg`; 0 turns it off. With `post_end_ms` below 150 the ring window ends there, and below 30 the check is skipped.

A 20-byte write with `version = 3` (no `ring_min_permille`) and a 19-byte write with `version = 2` (no `capture_mode` either) are still accepted and keep the fields they lack. Reads always return the 22-byte v4 layout. FACTORY_CFG restores the thresholds, the ring minimum included, but keeps the capture mode.

Out-of-range writes are rejected with ATT error 0x13 (value not allowed). Accepted values are persisted.

### STATUS (`sp_status_t`, 7 B)

`u8 battery_pct`, `u16 battery_mv`, `i8 temp_c`, `u8 flags`, `u8 fw_major`, `u8 fw_minor`.
Flags: bit0 VBUS (cable present), bit1 CHARGING, bit2 CHG_INHIBIT (too cold or hot to charge), bit3 ACTIVE, bit4 TIME_SET, bit5 REG0_LDO (0.3.9, set while REG0 runs as LDO after REG0_MODE 1), bit6 REJECT_CAPTURE (0.3.22, set while a REJECT_CAPTURE threshold is on).
`battery_pct` comes from the cell voltage through a table; from 0.3.5 it is at most 99 while CHARGING and 100 while the charge has ended with the cable still attached (see the 0.3.5 notes at the top). `battery_mv` is the raw reading.

### DIAG (`sp_diag_t`, 28 B; 20 B in firmware 0.3.13–0.3.16, 8 B in 0.3.1–0.3.12)

Read-only diagnostics, so a crash in the field leaves a trace the app can show or log.

| Offset | Type | Field | Meaning |
|---|---|---|---|
| 0 | u8 | reset_cause | Why the puck last started: 0 power-on, 1 reset pin, 2 software (OTA, factory reset, reboot), 3 watchdog (a hang), 4 crash (fatal error), 5 CPU lockup, 6 woke from System OFF (charger attached after a low-battery cut-off), 7 other (debugger) |
| 1 | u8 | crash_reason | Zephyr fatal error code when `reset_cause` = 4 (0 CPU exception, 1 spurious IRQ, 2 stack overflow, 3 kernel oops, 4 kernel panic), otherwise `0xFF` |
| 2 | u16 | crash_count | Crash, watchdog and lockup restarts since the storage was last wiped (kept across updates and pad resets) |
| 4 | u16 | main_stack_free | Fewest bytes the main thread's 3 KB stack has had free since boot |
| 6 | u16 | workq_stack_free | The same for the 2 KB system work queue |
| 8 | u32 | motion_wakes | Since 0.3.13: how often movement woke the sleeping puck since it started (wakes for a recording, the live stream or a sensor restart are not counted) |
| 12 | u32 | wakes_without_shot | Since 0.3.13: of those wakes, how many went back to sleep without an accepted shot (false wakes: a knock on the stand, the bag moving) |
| 16 | u32 | uptime_s | Since 0.3.13: seconds since the puck started, so `wakes_without_shot / uptime_s` is a false-wake rate. The three counts start at 0 at every boot |
| 20 | u32 | crash_pc | Since 0.3.17: program counter at the fatal error when `reset_cause` = 4, otherwise 0. For a kernel oops (an assert) it points just after the assert's call. Look it up with `arm-zephyr-eabi-addr2line -f -i -e zephyr.elf` of the same build |
| 24 | u32 | crash_lr | Since 0.3.17: link register at the fatal error when `reset_cause` = 4, otherwise 0 (odd: Thumb bit set). Usually the caller of the function that failed |

On a fatal error the puck now reboots at once (0.3.0 halted until the 10 s watchdog fired). A crash still counts towards the boot-loop guard, and an unconfirmed update still rolls back. The values are refreshed at boot, on SET_TIME and every 10 s (60 s while idle); read DIAG when needed, there are no notifications. An app can log it once per connection and flag a growing `crash_count`.

### STREAM (firmware 0.3.9, dev page)

Subscribe to STREAM, then write CONTROL `0C` + divider: the puck streams every divider-th IMU sample, 480 Hz / divider (1 = 480 Hz, 2 = 240 Hz, 4 = 120 Hz, 8 = 60 Hz); `0C 00` stops. Starting without a STREAM subscription fails with ATT 0xFC, a divider above 8 with ATT 0x13. Each notification is `u32 first` (index of its first sample, counted at the stream rate from the start), `u8 divider`, then up to 19 samples in the CAPTURE sample format (`i16 acc[3]`, `i16 gyro[3]`, 976 µg and 70 mdps per LSB); with a smaller MTU fewer samples fit. A sample reaches the app at most 19 samples' time after it was measured, plus the link: about 0.1 s at 480 Hz, 0.35 s at 60 Hz. Packets the link cannot take are dropped, and so are those while a shot's EVENT is being sent: a jump in `first` is a gap. **Captures wait while the stream runs** (Richard): a shot still counts and its EVENT is sent at once, but its capture is only stored, and is sent when the stream stops (up to 8 wait; beyond that the oldest waiting ones are not sent by themselves but stay stored for GET_CAPTURE). GET_CAPTURE requests wait too. A capture that is already being sent when the stream starts finishes first, so the stream may start with a gap of a few seconds. The puck stays awake and on the fast connection interval while streaming, and stops by itself on disconnect, when STREAM is unsubscribed, and 10 minutes after the start (write `0C` + divider again to continue; that restarts `first` at 0). Shot detection and counting carry on as usual. Full rate is 5.8 KB/s, about twice what capture transfers were designed for: if `first` jumps a lot at 480 Hz, use 2 or 4. While the stream runs, the LED blinks fast and continuously (50 ms on, 50 ms off, 10 Hz, about 0.5 mA); other signals such as a shot flash or the charger pulses play between two blinks, and the blinking stops with the stream. Streaming costs battery: an estimated 1–3 mA on top of the ~0.85 mA of an active puck plus the LED's ~0.5 mA, unmeasured.

## Recommended app sync (PowerSync-friendly)

1. Connect only to the user's own puck (its stored serial). The link encrypts automatically because the phone is paired.
2. Request MTU 247 or more, and subscribe to COUNT, EVENT, STATUS and, from firmware 0.3.12, CAPREC (CAPTURE on older firmware).
3. Write SET_TIME.
4. Read COUNT, then write `REPLAY(last_seq_seen_by_app + 1)`, also when COUNT's `last_seq` is not above it: up to 0.3.21 `last_seq` moved only on accepted shots, so the rejects the app missed while offline came only with the replay. From 0.3.22 `last_seq` is the newest seq of any event (shot, reject or recording). The app owns this position; the puck keeps no per-phone sync state, so any number of phones can sync. From firmware 0.3.16 the replay ends with a 4-byte marker on EVENT holding that `from_seq`; older firmware sends none, so keep a timeout.
5. Upsert events keyed by `(device_id, seq)`, with `device_id` = DIS serial number. seq is monotonic, so replays and duplicates are harmless.
6. Stay connected during the end. Each shot arrives as an EVENT with `accepted = 1` about 0.6 s after release (the follow-through window), followed by its capture.
7. Store captures keyed by `(device_id, seq)` as well. For accepted events without a complete capture, or a CAPREC capture that fails its check, write `GET_CAPTURE(seq)`: the puck keeps the newest captures in flash (see *CAPTURE*), so this also works after a session offline.

Map shots to ends in the app: the puck has no notion of ends or arrows-per-end, and seq gaps are not errors.

## Firmware update (OTA)

The puck runs **MCUboot** and exposes the standard **MCUmgr SMP** service, so no custom update protocol is needed.

- SMP service `8D53DC1D-1DB7-4CD3-868B-8A527460AA84`, characteristic `DA2E7828-FBCE-4E01-AE9E-261174997C48`. It requires the same encrypted (paired) link as everything else.
- In the Flutter app, use Nordic's **`mcumgr_flutter`** plugin (wraps the iOS/Android nRF Connect Device Manager libraries) with the file `shotpuck_update.bin`. The nRF Connect Device Manager app works for manual updates.
- **Charger required (0.3.2):** the puck accepts upload data only while the charger is attached. Each upload request is checked; without the charger the puck answers SMP `rc` 6 (`MGMT_ERR_EBADSTATE`) and writes nothing, and unplugging during an upload stops it at the next chunk (the incomplete image never boots). Check STATUS `SP_ST_VBUS` before starting and tell the user to plug the charger in. Image state changes (test, confirm) and reset are not gated.
- **Upload speed (0.3.3):** the puck reports its MCUmgr buffers through the OS group's parameters command (`mcumgr params`: 4 buffers of 384 bytes, with SMP reassembly over BLE), and switches to the 15–30 ms connection interval while upload chunks arrive (back to 105–210 ms 5 s after the last). nRF Connect Device Manager reads the parameters by itself. With `mcumgr_flutter` set `pipelineDepth: 3` and `reassemblyBufferSize: 384` in `FirmwareUpgradeConfiguration` (the defaults, 1 and 0, send one MTU-sized chunk at a time).
- **Fast interval on the charger (0.3.15):** from 0.3.15 the puck keeps the 15–30 ms interval for as long as a phone is connected and the charger is in, so an upload starts on it. Pucks on 0.3.14 or older still start on 105–210 ms with latency 4, and on some phones `mcumgr_flutter` then gives up after the first chunk (it allows 2.5 s per chunk and does not retry). nRF Connect Device Manager resends late chunks; use it for such a puck if the app update fails, with *Erase application settings* off.
- **Slow first seconds on Android (0.3.19):** up to 0.3.18, Android applies the puck's preferred parameters (105–210 ms, latency 4) about 1 s into each connection, and the fast request takes effect only 8–10 s in. An upload that starts earlier can time out. For an update from 0.3.18 or older, either start the upload about 15 s after connecting, or turn the phone's Bluetooth off and on first: Android reads the preferred parameters only when pairing, so after a Bluetooth restart it has none to apply until the puck is paired again.
- Recommended mode **Test and confirm**: upload (roughly 20–40 s at MTU 247), mark as test, reset. MCUboot then swaps the images (the puck is unavailable for about 10–20 s), the new version boots and advertises, and the phone reconnects and confirms it.
- The puck also confirms a new image by itself once a paired phone has connected over an encrypted link and the image has run 10 s. Reconnect and pair (encrypt) after the OTA reset; until then the image stays on trial. If the new image crashes or hangs before that (the watchdog resets a hang after 10 s), MCUboot **rolls back** to the previous version on the next boot.
- Only images signed with the ShotPuck private key are accepted: MCUboot checks the ECDSA-P256 signature before it boots anything.
- Check the result with the DIS Firmware Revision String or STATUS `fw_major`/`fw_minor`. SMP `image list` shows both slots with their versions and flags.
- Shots, captures, counts, config and pairings are kept across updates (separate flash partitions).
- **Firmware 0.3.0 changed the flash layout** (two 256 KB slots and a capture area, instead of two 460 KB slots). Never send it over the air to a puck running 0.2.x: flash 0.2.x pucks once by cable (`shotpuck_full.hex`, which includes the new bootloader). Updates between 0.3.x versions go over the air as before.
