# BLE radio share of IDLE power: nRF52840 (BMD-340, LFRC, DC/DC), Zephyr 4.1 open-source controller

Conventions used in all estimates below (stated once so the report writer can carry them):
- Currents are at the nRF's 3.0 V VDD rail, like the Nordic figures. The cell-side current through REG0's DC/DC is a little lower; that is not modelled.
- I_RX = 6.26 mA and I_TX(0 dBm) = 6.40 mA (nRF52840 PS, 1 Mbps, HFXO, DC/DC; see Q2).
- Advertising event charge Q_adv ≈ 13.3 µC (Nordic Online Power Profiler, nRF52840, 0 dBm, 23-byte payload, 3 V, DC/DC; see Q2).
- Empty connection event charge Q_ev ≈ 4 µC (derived in Q3; ±50 %).
- Window widening floor I_WW = (SCA_local + SCA_central) × I_RX (derivation in Q3).
- "Idle base" = the ~17 µA board estimate (unmeasured), which has no radio activity in it.

## Q1. What does the firmware do today: TX power, advertising options, sleep clock accuracy, connection parameters, idle disconnects?

### Takeaway
The puck advertises connectably on all 3 channels at 0 dBm (the controller default; nothing sets TX power), every 500–525 ms, with a 21-byte payload (26 with the pairing marker) and its name in the scan response. The controller declares ±500 ppm because of the RC clock. When idle it asks for 105–210 ms, latency 4, 4 s timeout. It never drops a bonded, encrypted phone while idle, so a phone in range keeps the link open for as long as its app holds it.

### Cited Findings
- Advertising parameters: `BT_LE_ADV_PARAM_INIT(BT_LE_ADV_OPT_CONN, 0x0320, 0x0348, NULL)`, i.e. connectable, 500–525 ms, no channel-disable options (all of 37/38/39), with the comment "~10 uA average while advertising" — [ble.c L202–204](C:/Users/rixar/SynologyDrive/Documents/Cursor/shot-tracker/firmware/src/ble.c)
- Advertising data: Flags (3 B) + complete 128-bit service UUID (18 B) = 21 B. While the pairing window is open, manufacturer data `FFFF 01` is added (5 B), giving 26 of 31 B. The scan response carries the complete name (`ShotPuck-XXXX`, about 15 B with its header) — [ble.c L191–198, L272–289](C:/Users/rixar/SynologyDrive/Documents/Cursor/shot-tracker/firmware/src/ble.c); [HANDOVER.md L316](C:/Users/rixar/SynologyDrive/Documents/Cursor/shot-tracker/HANDOVER.md)
- TX power: prj.conf sets no `CONFIG_BT_CTLR_TX_PWR_*`, and the controller's choice defaults to `BT_CTLR_TX_PWR_0` (0 dBm). Per-role or per-connection power needs `CONFIG_BT_CTLR_TX_PWR_DYNAMIC_CONTROL` plus HCI vendor commands — [prj.conf](C:/Users/rixar/SynologyDrive/Documents/Cursor/shot-tracker/firmware/prj.conf); [Zephyr controller Kconfig L330–339, L544–550](C:/zp/zephyr/subsys/bluetooth/controller/Kconfig)
- Sleep clock: the board defconfig sets `CONFIG_CLOCK_CONTROL_NRF_K32SRC_RC=y`, with the comment that P0.00/P0.01 (the 32 kHz crystal pins) are free. With RC on nRF52 the accuracy choice defaults to `CLOCK_CONTROL_NRF_K32SRC_500PPM`, and calibration is on by default: a 4000 ms period, `MAX_SKIP=1`, `TEMP_DIFF=2` (0.5 °C) — [shotpuck_nrf52840_defconfig](C:/Users/rixar/SynologyDrive/Documents/Cursor/shot-tracker/firmware/boards/shotpuck/shotpuck/shotpuck_nrf52840_defconfig); [drivers/clock_control/Kconfig.nrf L57–172](C:/zp/zephyr/drivers/clock_control/Kconfig.nrf)
- Zephyr 4.1 has no `CONFIG_BT_CTLR_SCA`. The link layer reads the local SCA from the clock Kconfig through a lookup table `{500, 250, 150, 100, 75, 50, 30, 20}` ppm (`lll_clock_ppm_local_get`) — [lll_clock.c L19, L149–152](C:/zp/zephyr/subsys/bluetooth/controller/ll_sw/nordic/lll/lll_clock.c)
- Connection parameters: `CONFIG_BT_PERIPHERAL_PREF_MIN_INT=84`, `MAX_INT=168`, `LATENCY=4` and `TIMEOUT=400` give 105–210 ms, latency 4 and a 4 s supervision timeout, with `BT_GAP_AUTO_UPDATE_CONN_PARAMS=y`. Bulk mode asks for 15–30 ms, latency 0, the same 4 s timeout, and returns to the slow set 5 s after the last bulk activity (`BULK_LINGER_MS 5000`) — [prj.conf L44–48](C:/Users/rixar/SynologyDrive/Documents/Cursor/shot-tracker/firmware/prj.conf); [ble.c L25–28, L59–64, L311–326](C:/Users/rixar/SynologyDrive/Documents/Cursor/shot-tracker/firmware/src/ble.c)
- Advertising life cycle: `go_active()` calls `ble_adv_start()`. The main loop keeps advertising while ACTIVE, for `ADV_LINGER_MS = 120000` after IDLE begins, or while the pairing window is open, and stops after that. No advertising takes place while connected (`CONFIG_BT_MAX_CONN=1`) — [main.c L48, L200–208, L728–738](C:/Users/rixar/SynologyDrive/Documents/Cursor/shot-tracker/firmware/src/main.c)
- Disconnects: the only `bt_conn_disconnect` in the firmware is in claim.c, which drops links that are not encrypted 30 s after connecting. FORGET_BONDS also disconnects. Nothing disconnects an idle, encrypted phone — [claim.c L19–31](C:/Users/rixar/SynologyDrive/Documents/Cursor/shot-tracker/firmware/src/claim.c); grep of firmware/src
- Regulators: REG1 runs DC/DC in the app (`app.overlay`: `&reg1 { regulator-initial-mode = <NRF5X_REG_MODE_DCDC>; }`), and REG0's DC/DC has been on in every build — [app.overlay](C:/Users/rixar/SynologyDrive/Documents/Cursor/shot-tracker/firmware/app.overlay); [HANDOVER.md L287](C:/Users/rixar/SynologyDrive/Documents/Cursor/shot-tracker/HANDOVER.md)
- The firmware makes no direct HFCLK, clock-control or onoff request (grep for clock_control/onoff/HFCLK/HFXO in firmware/src: no hits) — grep of [firmware/src](C:/Users/rixar/SynologyDrive/Documents/Cursor/shot-tracker/firmware/src)
- The app is told to scan with the service-UUID filter (the UUID is in the advertising data) and to scan for at least 5 s, because the puck advertises about every 0.5 s — [archesk docs/shotpuck-integration.md L290](C:/Users/rixar/SynologyDrive/Documents/Cursor/archesk/docs/shotpuck-integration.md)

### Inferences
- The radio costs nothing in three idle states: no phone connected and past the 120 s linger, no adv, no connection. Then the only BLE-related idle cost is the RC clock's calibration and its higher run current (Q6). The radio's share of IDLE power is entirely (a) the 120 s linger with no phone connected, and (b) any time a bonded phone stays connected while the puck is idle.
- The "~10 µA while advertising" comment is probably 2–3× too low (Q2: about 26 µA by the Power Profiler model at about 0.51 s).

### Gaps
- No measured current for the puck in any radio state (HANDOVER: unmeasured). Which interval Android or iOS actually picks inside 105–210 ms is unknown: the firmware has no `le_param_updated` callback, so nothing records it.

## Q2. What does connectable advertising cost on the nRF52840 at 0 dBm with DC/DC, at 0.5, 1 and 2 s, with fewer channels, lower TX power or a smaller payload?

### Takeaway
At 0 dBm with DC/DC and a ~23-byte payload, Nordic's Online Power Profiler gives about 13.3 µC per advertising event on the nRF52840: about 26 µA at the puck's ~0.51 s, 13 µA at 1 s and 6.6 µA at 2 s. A DK measurement of the same kind of event with DC/DC off came to 20.4 µC. Other measures are smaller: a lower TX power saves at most about 18 % of the event, a shorter payload about 0.15 µC per byte, and one channel instead of three roughly halves it (that last figure is my own estimate). All of it applies only while advertising, which in IDLE means only the 120 s linger.

### Cited Findings
- The Online Power Profiler estimates 13.28 µC per advertising event for the nRF52840 at 0 dBm, 23-byte TX payload, 3 V, DC/DC on. The user's DK measurement (ble_app_hrs, DC/DC disabled in sdk_config) gave 20.44 µC per event over 5.449 ms. Nordic (run_ar) said the difference came from the setup (DC/DC disabled) and that the profiler "is using the average value for an actual measured event" — [DevZone 80728](https://devzone.nordicsemi.com/f/nordic-q-a/80728/apparent-discrepancy-between-online-power-profiler-measurements)
- Profiler at 1000 ms, 0 dBm, DC/DC, LFXO, 0-byte payload: a total average of 11 µA including 2 µA idle, a 7 mA peak and a 3.5 ms pulse. The measured figure on an nRF52840 kit was about 20 µA average with 7 µA idle, a 14 mA peak and a ~6 ms pulse. Nordic put the longer pulse down to a scan request and scan response from a nearby central, and the higher idle to leakage — [DevZone 104405](https://devzone.nordicsemi.com/f/nordic-q-a/104405/optimizing-advertising-power-nrf52840/449039)
- A DevZone reply warns that the profiler was built for the nRF52832 and may deviate for the nRF52840 — [DevZone 63521 (search summary)](https://devzone.nordicsemi.com/f/nordic-q-a/63521/nrf52840-battery-life-estimation/259188)
- Profiler output in another thread: connectable advertising with a 20-byte payload, LDO, 40 ms interval gave 20.24 µC per event, a 4.09 ms event and "LF clock calibration current 1.0 µA" — [DevZone 63521](https://devzone.nordicsemi.com/f/nordic-q-a/63521/nrf52840-battery-life-estimation)
- nRF52840 radio currents (PS, 1 Mbps BLE, HFXO): TX +8 dBm DC/DC 16.40 mA; TX 0 dBm DC/DC 6.40 mA; TX −40 dBm DC/DC 3.83 mA; TX 0 dBm LDO 10.80 mA; TX −40 dBm LDO 4.82 mA; RX DC/DC 6.26 mA; RX LDO 10.10 mA — [nRF52840 PS, radio current parameters](https://docs.nordicsemi.com/r/bundle/ps_nrf52840/page/_tmp/nrf52840/autodita/current/parameters.i_radio.html)
- Older PS v1.6 / key-feature figures of 4.8 mA TX (0 dBm) and 4.6 mA RX cover the RADIO block only. The newer 6.40/6.26 mA figures appear to include the HFXO — [DevZone 82026](https://devzone.nordicsemi.com/f/nordic-q-a/82026/nrf52840-discrepancy-between-product-specification-pdf-and-infocentre-which-is-right/340480)
- On an nRF54L15 (not a 52840), the profiler predicted 8.1 µA at 0 dBm and 1 s; 9.7 µA was measured — [DevZone 122857](https://devzone.nordicsemi.com/f/nordic-q-a/122857/ble-advertising-on-nrf54l15dk-consuming-more-than-online-power-profiler-suggests/542150)
- Zephyr's link layer turns the HFXO on at the start of each advertising event's preparation (`lll_adv_prepare` → `lll_hfclock_on()`). The ULL prepare is scheduled `EVENT_OVERHEAD_XTAL_US = 1500` µs ahead of the event (`ticks_prepare_to_start`), a compile-time #define, not a Kconfig option — [lll_adv.c L679–688](C:/zp/zephyr/subsys/bluetooth/controller/ll_sw/nordic/lll/lll_adv.c); [ull_adv.c L1278–1282](C:/zp/zephyr/subsys/bluetooth/controller/ll_sw/ull_adv.c); [lll_vendor.h L7](C:/zp/zephyr/subsys/bluetooth/controller/ll_sw/nordic/lll/lll_vendor.h)

### Inferences
- Interval scaling with Q_adv = 13.3 µC (add the spec's random 0–10 ms advDelay, about 5 ms on average):
  - 500–525 ms (today, about 0.51 s mean): about **26 µA**. If the event really costs what the DK measured (20.4 µC), about 40 µA.
  - 1.0 s: about **13 µA**, saving about 13 µA against today.
  - 1.285 s: about 10 µA. 2.0 s: about **6.6 µA**, saving about 19 µA.
- Payload: each advertising byte goes out on 3 channels at 8 µs/byte (1 Mbps) and 6.40 mA, which is about 0.15 µC per byte per event, or **about 0.30 µA per byte at 0.51 s**.
  - Dropping the 128-bit UUID (18 B) would save about 2.8 µC per event (about 21 %), or about 5 µA while advertising. It would break the app's service-UUID scan filter and iOS background service scans, so it is not practical. A 16-bit SIG UUID (paid membership) would save 14 B, about 4 µA.
  - The 5-byte pairing marker is only present while the charger is attached, so the battery does not pay for it.
  - The scan response (~31 B on air, about 248 µs TX) is sent only when an active scanner asks for it: about 2 µC per response, and only while a phone scans nearby.
- TX power: of the ~13.3 µC, TX is about 3 × 312 µs ≈ 0.94 ms (23 B payload) ≈ 6.0 µC at 0 dBm. Even at −40 dBm (3.83 mA) the saving is ≤ 2.4 µC per event (≤ 18 %), which is **≤ 4.7 µA at 0.51 s**. −8 dBm or −20 dBm would save a fraction of that, since their PS currents lie between 6.40 and 3.83 mA (exact values not retrieved). The cost is about 8–20 dB less link budget on a puck on a bow, with the phone often in a pocket or bag.
- Fewer channels (Zephyr `BT_LE_ADV_OPT_DISABLE_CHAN_37/38/39`): radio time falls to about 1/3, but the HFXO start, CPU and ramp overhead per event stay. My estimate (assuming about 3 µC of the 13.3 µC is fixed overhead) is about 6–7 µC per event on one channel: about −50 %, or about 13 µA saved at 0.51 s. The phone's scanner hops channels, so discovery gets slower and less robust to interference on that channel. This conflicts with "connect quickly".
- Non-connectable advertising would drop the RX listen after each packet (maybe 25–30 % of the event). It is not usable here, because the phone must connect. Directed advertising also fits badly: up to 4 bonded phones, random resolvable addresses, controller privacy off.
- Linger energy at today's settings: 120 s × ~26 µA ≈ 3.1 mC ≈ **0.86 µAh per idle entry with no phone connected**. At a 2 s interval it would be about 0.22 µAh.

### Gaps
- No Nordic or measured figure for the Zephyr open-source controller's own advertising event charge. The profiler models Nordic's SoftDevice/SDC, so the Zephyr link layer may differ (see Q6).
- The nRF52840 PS's TX current at −4, −8, −12, −16 and −20 dBm was not retrieved (only +8, 0 and −40 dBm). A search snippet with 2.2 mA (−8 dBm) and 1.7 mA (−20 dBm) "TX only run current" had no clear source or chip revision, so it is excluded.
- Apple's recommended advertising intervals (a list such as 152.5 … 1285 ms after 30 s at 20 ms) could not be verified against a current Apple document.

## Q3. What does an idle connection cost per event and on average, at 105–210 ms against 1–6 s with peripheral latency, and how much does the ±500 ppm LFRC add through window widening?

### Takeaway
An idle connected phone costs a fixed amount per anchor actually listened to (Q_ev ≈ 4 µC) plus a window-widening floor. That floor does not depend on interval or latency: (SCA_local + SCA_central) × I_RX. With the LFRC at ±500 ppm and a 50 ppm phone it is ≈ 3.4 µA; with a 50 ppm crystal it would be ≈ 0.6 µA. Today's 105–210 ms, latency 4 therefore costs about **7–11 µA** while connected and idle, close to the whole 17 µA idle base again. The longest Apple-legal setting (about 5–6 s between anchors) cuts this to about 4 µA with the LFRC, or about 1.4 µA with a crystal. If iOS rejects the request and leaves a short default interval, a connected idle link could cost on the order of 100+ µA.

### Cited Findings
- Window widening formula (Nordic Academy): WindowWidening = timeSinceLastAnchorPoint × (SCA_central + SCA_peripheral) / 1,000,000 + 2 × 16 µs. LFRC gives a longer widening than LFXO. Sleep clock accuracy must be ≤ 500 ppm. LFRC with calibration every 4–8 s is at the 250 ppm level. The internal oscillator draws "at least 0.6 µA" more than a crystal. LFSYNT (LF clock synthesized from the HFCLK) has "huge current consumption" — [Nordic Academy: Designing low-power BLE products](https://academy.nordicsemi.com/?p=46652)
- Zephyr 4.1 implementation: `window_widening_periodic_us = ceil((local_ppm + central_ppm(SCA from CONNECT_IND)) × conn_interval_us / 1e6)`, capped at `window_widening_max_us = conn_interval/2 − T_IFS`. It accumulates `× (lazy + 1)` for skipped (latency) events. The RX timeout is `2 × (EVENT_JITTER_US 16 + ticker margin + window_widening_event_us)` plus ramp and address times — [ull_peripheral.c L196–203](C:/zp/zephyr/subsys/bluetooth/controller/ll_sw/ull_peripheral.c); [lll_peripheral.c prepare_cb](C:/zp/zephyr/subsys/bluetooth/controller/ll_sw/nordic/lll/lll_peripheral.c); [lll_conn.c L248–257](C:/zp/zephyr/subsys/bluetooth/controller/ll_sw/nordic/lll/lll_conn.c)
- Zephyr's peripheral uses latency only when nothing is queued: `latency_event = 0` if the TX queue has data, otherwise `= lll->latency` when `latency_enabled`. `ull_periph_latency_cancel()` breaks latency when the host has something to send, so notifications from the puck are not held back by latency — [ull_conn.c L1090–1100](C:/zp/zephyr/subsys/bluetooth/controller/ll_sw/ull_conn.c); [ull_peripheral.c L502–514](C:/zp/zephyr/subsys/bluetooth/controller/ll_sw/ull_peripheral.c)
- LFRC against LFXO in a connection, worked example (verified answer by Stian Røed Hafskjold):
  - Run current: RC 0.6 µA against XO 0.25 µA, so +0.35 µA.
  - Calibration: 17 ms at 460 µA = 7.82 µC every 8 s, so +0.98 µA.
  - Window widening: (250 + 20) ppm × 6.6 mA = 1.78 µA against 0.26 µA with XO, so +1.52 µA, and "the result doesn't depend on the interval".
  - Totals: +2.85 µA in a connection and +1.33 µA while advertising (advertising has no widening).
  - — [DevZone 14214](https://devzone.nordicsemi.com/f/nordic-q-a/14214/how-much-current-consumption-when-we-use-internal-32k-rc)
- Nordic's position: the real LFRC accuracy is ±500 ppm even where a PS states ±250 ppm (nRF52832 erratum 146). The only recommended RC setting is 500 ppm, and configuring 250 ppm risks a window too small and missed packets. "The wider the window, the higher current consumption … not a very big effect" — [DevZone 21192 / 62300 (search summary)](https://devzone.nordicsemi.com/f/nordic-q-a/62300/rc-oscillator-frequency-tolerance-for-lfrc-after-calibration-accuracy-increment/254016)
- Profiler output, peripheral role: 40 ms interval, 1 packet with 20 B TX, 20 ppm both sides, LDO gave 8.43 µC per event, a 2.53 ms event and 1.0 µA LF calibration current — [DevZone 63521](https://devzone.nordicsemi.com/f/nordic-q-a/63521/nrf52840-battery-life-estimation)
- On a 2 s-interval nRF52840 test, the Zephyr controller woke three times per connection interval and the SoftDevice Controller once. The Zephyr controller drew more current, and Nordic said "Softdevice controller is little better power efficiency wise" (the numbers are only in images) — [DevZone 114717](https://devzone.nordicsemi.com/f/nordic-q-a/114717/nordic-softdevice-controller-versus-zephyr-ble-controller-config_bt_ll_sw_split-y)
- Silicon Labs, a different chip: running from an RC clock extends the peripheral's RX window (about 250 µs measured on one device), and the sleep current differs by about 280 nA — [Silicon Labs: LFRCO as LF clock source](https://docs.silabs.com/bluetooth/7.0.1/bluetooth-system-performance/lfrco-clock-source)

### Inferences
- **Per-event charge, derivation of Q_ev ≈ 4 µC.**
  - Start from the profiler's 8.43 µC (LDO, 20 B payload).
  - Scale to DC/DC by about 6.26/10.10 ≈ 0.62, giving ≈ 5.2 µC.
  - Remove the 20 B payload's TX (160 µs × 6.4 mA ≈ 1.0 µC), giving **≈ 4 µC** for an empty-packet anchor (central empty PDU, peripheral empty PDU, 80 µs each at 1 Mbps).
  - The Zephyr link layer's three wake-ups per event and its 1.5 ms HFXO lead could push this higher. Treat the figure as ±50 %.
- **Window-widening floor.** The peripheral opens its receiver WW early, and the central's packet arrives near the expected anchor (the clocks are better than declared). The extra RX time per anchor is therefore ≈ WW. WW is proportional to the time since the last anchor, so the average is I_WW = (SCA_local + SCA_central) × I_RX:
  - LFRC 500 ppm + phone 50 ppm: 550 ppm × 6.26 mA ≈ **3.4 µA**
  - LFRC 500 ppm + phone 20 ppm: ≈ 3.3 µA
  - LFRC 500 ppm + phone 250 ppm: ≈ 4.7 µA
  - LFRC 500 ppm + phone 500 ppm: ≈ 6.3 µA
  - Crystal 50 ppm + phone 50 ppm: ≈ **0.63 µA**
  - Crystal 20 ppm + phone 20 ppm: ≈ 0.25 µA
  - The floor stays the same however long the interval or latency, as long as WW < interval/2.
- **Average connected-idle current** (I = Q_ev / anchor spacing + I_WW; LFRC 500 ppm, phone 50 ppm). With the phone the puck is listening to anchors only every (latency + 1) × interval.

  | Parameters | Anchor spacing | Q_ev term | I_WW | Total | Per hour |
  |---|---|---|---|---|---|
  | Phone keeps ~30 ms, latency 0 (iOS rejects the request; illustrative) | 30 ms | ~133 µA | 3.4 µA | **~137 µA** | 137 µAh |
  | 105 ms, latency 4 (if the phone picks the minimum) | 0.525 s | 7.6 µA | 3.4 µA | **~11 µA** | 11 µAh |
  | 210 ms, latency 4 (if the phone picks the maximum) | 1.05 s | 3.8 µA | 3.4 µA | **~7.2 µA** | 7.2 µAh |
  | 495–510 ms, latency 10, timeout 18 s (Apple-legal) | ≈ 5.6 s | 0.7 µA | 3.4 µA | **~4.1 µA** | 4.1 µAh |
  | 990–1005 ms, latency 4, timeout 16 s (Apple-legal) | ≈ 5.0 s | 0.8 µA | 3.4 µA | **~4.2 µA** | 4.2 µAh |
  | 990–1005 ms, latency 4, with a 50 ppm crystal | ≈ 5.0 s | 0.8 µA | 0.63 µA | **~1.4 µA** | 1.4 µAh |

- **What this means for the battery (65 mAh cell; idle base 17 µA × 162 h ≈ 2.75 mAh/week):**
  - A phone that stays connected all the time while the puck is idle (bow case and phone in the same room) costs at today's 210 ms/latency 4 about 7.2 µA × 162 h ≈ 1.2 mAh/week, **+42 % of idle drain**. At 105 ms it is about 1.8 mAh/week (+65 %).
  - An Apple-legal low-duty set (~5 s anchors) cuts this to ~0.68 mAh/week. Disconnecting idle phones cuts it to ~0.
  - The 30 ms fallback case would be ~3.3 mAh/day, about 5 % of the cell per day.
- **LFRC cost while connected.** The ±500 ppm declaration costs ≈ 2.8 µA more than a 50 ppm crystal (3.4 − 0.63) for every hour a phone is connected, whatever the parameters. Declaring 250 ppm with the RC (300 ppm total) would save ≈ 1.6 µA connected, but Nordic advises against it.
- **Side effects of long anchors** (consequences of the code above):
  - Notifications from the puck still go out at the next connection event, at most one interval (≤ 0.51 s or ≤ 1 s) after queuing, because Zephyr cancels latency when data is queued.
  - Writes from the phone (REPLAY, GET_CAPTURE, CONFIG) can wait up to interval × (latency + 1), about 5–6 s.
  - A switch to the 15–30 ms bulk parameters takes effect at an "instant" several events ahead (the central chooses it; usually ≥ 6 events), so up to about 6 s at a 1 s interval before the first fast transfer. This is my inference: I did not fetch the core-spec "≥ 6 events" wording.

### Gaps
- No measured Q_ev for the Zephyr open-source controller on an nRF52840 with DC/DC. The 4 µC is derived from profiler (SoftDevice) numbers.
- The phones' declared SCA in CONNECT_IND (Android, iPhone) was not found. It sets I_WW between ≈ 3.3 µA and ≈ 6.3 µA with the LFRC.
- Which interval in 105–210 ms each phone picks is unknown. iOS's actual default interval after connecting was not confirmed from Apple (Punch Through only says most stacks default below 100 ms with latency 0), so the 30 ms row is illustrative.

## Q4. Could advertising stop as soon as the puck goes idle (and restart on wake), or slow down during the 120 s linger? What does the app need for reconnect latency?

### Takeaway
Advertising already starts only on wake and stops 120 s after idle. Stopping it at once, or slowing it to 1–2 s during the linger, saves only about 0.4–0.9 µAh per idle entry, and only when no phone is connected, which is small against the 17 µA idle base. Disconnecting an idle phone (Q3) is worth 10–100× more. Reconnect speed while the archer shoots is set by the active-state interval (0.5 s) and the phone's scan, not by the linger.

### Cited Findings
- Advertising restarts in `go_active()` on every motion wake (high-pass lift/draw wake since 0.3.10) and stops at the end of the linger; the pairing window also keeps it on — [main.c L200–208, L728–738](C:/Users/rixar/SynologyDrive/Documents/Cursor/shot-tracker/firmware/src/main.c); [PROTOCOL.md L82](C:/Users/rixar/SynologyDrive/Documents/Cursor/shot-tracker/docs/PROTOCOL.md)
- PROTOCOL: "A puck lying still in a bag is invisible, so pick up the bow to make it connectable." The app should scan with the service filter for at least 5 s — [PROTOCOL.md L82](C:/Users/rixar/SynologyDrive/Documents/Cursor/shot-tracker/docs/PROTOCOL.md); [shotpuck-integration.md L290](C:/Users/rixar/SynologyDrive/Documents/Cursor/archesk/docs/shotpuck-integration.md)
- Shots are counted and logged in flash on the puck (about 900 kept), and the app syncs with REPLAY from its last seq, so a missed live link loses no shot — [README.md L13](C:/Users/rixar/SynologyDrive/Documents/Cursor/shot-tracker/README.md); [shotpuck-integration.md L405](C:/Users/rixar/SynologyDrive/Documents/Cursor/archesk/docs/shotpuck-integration.md)
- Advertising event cost ≈ 13.3 µC (profiler, see Q2) — [DevZone 80728](https://devzone.nordicsemi.com/f/nordic-q-a/80728/apparent-discrepancy-between-online-power-profiler-measurements)

### Inferences
- Energy per idle entry without a connected phone:
  - Today (120 s at ~0.51 s): ≈ 3.1 mC ≈ 0.86 µAh.
  - Linger at 2 s: ≈ 0.22 µAh, saving 0.64 µAh.
  - Linger 30 s at 0.51 s: ≈ 0.22 µAh.
  - No linger: 0.
  - Example: 3 sessions a week × ~20 idle entries (rests between ends) with no phone connected is ≈ 52 µAh/week, about 0.3 µA averaged over the week (≈ 2 % of idle drain). If the phone stays connected through the session, the linger never runs and costs nothing.
- Spurious motion wakes (puck in a bag or car) each cost about 30 s ACTIVE (≈ 7 µAh at 0.85 mA) plus 150 s of advertising (≈ 1.1 µAh). Advertising adds about 15 % to each such wake. The IMU wake threshold matters more than the radio here.
- A middle course keeps 0.5 s while ACTIVE (fast reconnect while shooting) and switches to about 1–2 s, or stops, for the linger. With a phone scanning continuously in the foreground, discovery at 2 s takes on average about one interval, ~1–2 s. That fits the app's 5 s scan, but a phone that scans only briefly may miss a 2 s puck. The linger exists for "open the app after the session to sync". Shortening it means the user must pick up the bow to sync, which PROTOCOL already tells them.
- If an idle-disconnect policy is added (Q3/Q7), the linger is what lets the phone come back after a disconnect without the bow being moved. A slow linger interval (≈ 2 s) keeps that path cheap.

### Gaps
- No fetched source on how fast Android (background or low-power scan modes, autoConnect) and iOS (background scanning, pending connect) reconnect to a peripheral advertising at 0.5, 1 or 2 s. This decides how slow the linger interval may go, and it should be tested with the app.
- How the app behaves while the puck is idle (does it hold the link indefinitely?) was not checked beyond the integration doc.

## Q5. What limits do iOS and Android put on connection parameters, and is a low-duty idle link acceptable to them?

### Takeaway
Apple's current Accessory Design Guidelines (as quoted by an Apple DTS engineer) require a supervision timeout of 6–18 s, interval max × (latency + 1) ≤ 6 s, latency ≤ 30, interval min ≥ 15 ms in multiples of 15 ms, max ≥ min + 15 ms, and timeout > 3 × max × (latency + 1). **Both of the puck's requests (slow and fast) use a 4 s timeout, which breaks the current 6 s minimum.** They pass only the older rule set (timeout 2–6 s, product ≤ 2 s) that Punch Through and older Silicon Labs pages still cite. iOS "may reject" non-compliant requests. Android has no published limits and generally decides on its own.

### Cited Findings
- Apple DTS engineer quoting Accessory Design Guidelines §55.6:
  - The rules: "Peripheral Latency ≤ 30 connection intervals. Supervision Timeout from 6 seconds to 18 seconds. Interval Min ≥ 15 ms. Interval Min ≤ 2 seconds. Interval Min is a multiple of 15 ms. Interval Max at least 15 ms greater than Interval Min (or both 15 ms). Interval Max × (Peripheral Latency + 1) of 6 seconds or less. Supervision Timeout greater than Interval Max × (Peripheral Latency + 1) × 3."
  - On iOS's behaviour: "Even if your peripheral is requesting these parameters, iOS may still reject them and instead suggest another set … your peripheral should be ready for any set of parameters."
  - — [Apple Developer Forums 775811](https://developer.apple.com/forums/thread/775811)
- The same rule set (6–18 s timeout, ≤ 6 s product) is quoted in a second Apple thread with an Apple engineer, which adds that the phone "will not accept parameters that is not suitable for itself at the time" — [Apple Developer Forums 822187](https://developer.apple.com/forums/thread/822187)
- The same Apple numbers are quoted on DevZone. On iOS 26 one user saw iOS cycle the interval between 7.5 and 15 ms whatever was requested (possibly linked to HID); Nordic did not analyse it — [DevZone 124468](https://devzone.nordicsemi.com/f/nordic-q-a/124468/repeated-connection-interval-updates-on-ios-26/549156)
- Older rule set: product ≤ 2 s and 2 s ≤ timeout ≤ 6 s (Punch Through); Silicon Labs references release R11 (2019-11-12), §25.6, with 15 ms multiples — [Punch Through: BLE connection interval and battery life](https://punchthrough.com/?p=15988); [Silicon Labs: connection parameters for Apple devices](https://docs.silabs.com/bluetooth/3.3/bluetooth-miscellaneous-mobile/selecting-suitable-connection-parameters-for-apple-devices)
- Android:
  - Android supports a 7.5 ms minimum interval. Apps choose only presets through `requestConnectionPriority()` (high ≈ 11.25–15 ms, balanced default, low power ≈ 100 ms), and the central has the final say — [Punch Through](https://punchthrough.com/?p=15988)
  - On Android the supervision timeout is not adjustable from the app API and the preset ranges vary by device — [Qt QLowEnergyConnectionParameters](https://doc.qt.io/qt/qlowenergyconnectionparameters.html)
  - Samsung units of one model sometimes reject a 15 ms / 4 s request that other units accept — [Samsung developer forum](https://forum.developer.samsung.com/t/bluetooth-low-energy-allowed-connection-parameters-sm-g991b/39920)
- The puck's requests: slow 105–210 ms, latency 4, 4 s; fast 15–30 ms, latency 0, 4 s. HANDOVER says the 15 ms multiple was chosen for Apple's guidelines — [prj.conf L45–48](C:/Users/rixar/SynologyDrive/Documents/Cursor/shot-tracker/firmware/prj.conf); [HANDOVER.md L318](C:/Users/rixar/SynologyDrive/Documents/Cursor/shot-tracker/HANDOVER.md)

### Inferences
- **The smallest compliant change is the timeout: 400 → 600–1800 (6–18 s) for both sets.**
  - Slow 105–210 ms, latency 4, 6 s passes every current rule: 210 × 5 = 1.05 s ≤ 6 s; 6 s > 3.15 s.
  - Fast 15–30 ms, latency 0, 6 s passes too.
  - The longer timeout only affects how long a lost link takes to be noticed.
- **Lowest-duty Apple-legal idle sets:**
  - 495–510 ms, latency 10, timeout 18 s: product 5.61 s, 3× = 16.8 s < 18 s; puck→phone ≤ 0.51 s.
  - 990–1005 ms, latency 4, timeout 16 s: product 5.03 s, 3× = 15.1 s.
  - 1500–1515 ms, latency 2, timeout 14–18 s: product 4.55 s.
  - All give ≈ 4.1–4.2 µA connected idle with the LFRC (Q3), against ~7–11 µA today. A short interval with high latency (the 495–510/10 set) keeps puck→phone latency low and is the pattern Punch Through recommends.
- Because iOS may reject a non-compliant request and keep its own (short) interval, the 4 s timeout is a **risk for idle power on iPhones**: the Q3 "~137 µA" row. It needs checking with an iPhone, for example by logging the negotiated parameters (a `le_param_updated` callback into DIAG) or with a sniffer.
- With two stages (a fast set during transfers, a low-duty set while idle), the slow set used for idle should not also serve the 5 s after a transfer. A third tier, applied only once the puck enters IDLE, would avoid slowing replay and sync while ACTIVE. This is a design suggestion and has not been tested.

### Gaps
- The Apple PDF itself could not be fetched; the rules come from Apple engineers' quotes (no release number given) and from third parties.
- No authoritative Android (AOSP or vendor) statement of the limits a peripheral's L2CAP or LL parameter request must meet, nor which value inside a range Android picks.

## Q6. Does anything keep the HFXO or the radio running while idle? Plus the LF clock source, DC/DC for the radio, and the choice of controller

### Takeaway
The firmware keeps nothing on. The Zephyr controller requests the HFXO only around radio events, about 1.5 ms ahead of each one. With no advertising and no connection it never runs it. One HFXO user remains while idle: LFRC calibration (temperature check every 4 s, calibration at least every 8 s), costing about 1 µA by Nordic's figures. REG1 and REG0 DC/DC are already on, which takes the radio from ~10.1–10.8 mA (LDO) to ~6.3–6.4 mA, so nothing more is available there. A 32.768 kHz crystal on the free P0.00/P0.01 pins (a hardware change) would save ≈ 1.3 µA always plus ≈ 2.8 µA while connected. The SoftDevice Controller (nRF Connect SDK) is reported as somewhat more efficient per event, but no numbers were found, and while disconnected and idle it saves nothing.

### Cited Findings
- Controller HFXO handling:
  - `lll_hfclock_on()` → `z_nrf_clock_bt_ctlr_hf_request()` (reference-counted) in each role's prepare, `lll_hfclock_off()` at done or abort — [lll_clock.c L100–110](C:/zp/zephyr/subsys/bluetooth/controller/ll_sw/nordic/lll/lll_clock.c); [lll.c](C:/zp/zephyr/subsys/bluetooth/controller/ll_sw/nordic/lll/lll.c)
  - The prepare runs `EVENT_OVERHEAD_XTAL_US = 1500` µs ahead of the event — [lll_vendor.h L7](C:/zp/zephyr/subsys/bluetooth/controller/ll_sw/nordic/lll/lll_vendor.h)
  - `BT_CTLR_XTAL_ADVANCED` (default y) keeps the crystal running between events closer than `BT_CTLR_XTAL_THRESHOLD` (default 1500 µs) — [Kconfig.ll_sw_split L701–730](C:/zp/zephyr/subsys/bluetooth/controller/Kconfig.ll_sw_split)
- LFRC calibration needs the HFXO:
  - Zephyr's `nrf_clock_calibration.c` requests the HFCLK (`hf_request()`) at each calibration opportunity, reads the die temperature, and calibrates if the temperature moved ≥ 0.5 °C or the skip count (1) has run out, then releases it — [nrf_clock_calibration.c L88–112, L137–226](C:/zp/zephyr/drivers/clock_control/nrf_clock_calibration.c)
  - The TEMP driver also requests the HF clock through onoff — [temp_nrf5.c L33–65](C:/zp/zephyr/drivers/sensor/nordic/temp/temp_nrf5.c)
  - The defaults are a 4000 ms period, `MAX_SKIP=1` and `TEMP_DIFF=2` — [Kconfig.nrf L83–110](C:/zp/zephyr/drivers/clock_control/Kconfig.nrf)
  - Nordic: the HFXO is the reference during LFRC calibration — [nRF52805 PS clock chapter (search summary)](https://docs.nordicsemi.com/r/bundle/ps_nrf52805/page/clock.html)
- Calibration energy: 17 ms at 460 µA (7.82 µC), at least every 8 s, ≈ 0.98 µA — [DevZone 14214](https://devzone.nordicsemi.com/f/nordic-q-a/14214/how-much-current-consumption-when-we-use-internal-32k-rc). The profiler models "LF clock calibration current 1.0 µA" — [DevZone 63521](https://devzone.nordicsemi.com/f/nordic-q-a/63521/nrf52840-battery-life-estimation). A user saw spikes every 4 and 8 s raise System ON sleep from about 2 to 3.5 µA, and they went away with calibration off — [DevZone 76049 / search summary](https://devzone.nordicsemi.com/f/nordic-q-a/76049/high-current-consumption-offset-when-using-internal-rc-oscillator-as-compared-to-crystal-oscillator-for-low-frequency-clock)
- LF clock run currents: RC 0.6 µA against XO 0.25 µA — [DevZone 14214](https://devzone.nordicsemi.com/f/nordic-q-a/14214/how-much-current-consumption-when-we-use-internal-32k-rc). The Nordic Academy says "at least 0.6 µA" more for RC, LFXO start-up 0.43 s against 1 ms for LFRC, and that LFSYNT has "huge current consumption" — [Nordic Academy](https://academy.nordicsemi.com/?p=46652)
- DC/DC against LDO radio current: TX 0 dBm 6.40 against 10.80 mA; RX 6.26 against 10.10 mA — [nRF52840 PS](https://docs.nordicsemi.com/r/bundle/ps_nrf52840/page/_tmp/nrf52840/autodita/current/parameters.i_radio.html). Both are enabled in the app — [app.overlay](C:/Users/rixar/SynologyDrive/Documents/Cursor/shot-tracker/firmware/app.overlay); [HANDOVER.md L287](C:/Users/rixar/SynologyDrive/Documents/Cursor/shot-tracker/HANDOVER.md)
- Controller choice: the Zephyr LL wakes 3 times per connection interval against once for the SDC, and the SDC is "little better power efficiency wise". Nordic advises comparing the `.config` files because defaults change between the two — [DevZone 114717](https://devzone.nordicsemi.com/f/nordic-q-a/114717/nordic-softdevice-controller-versus-zephyr-ble-controller-config_bt_ll_sw_split-y)
- prj.conf already trims controller features (privacy, accept list, LE Ping, peripheral feature exchange, min-used-channels). With LTO the controller is built with size optimisation instead of `-O2` (radio ISR timing changes, unverified) — [HANDOVER.md L452](C:/Users/rixar/SynologyDrive/Documents/Cursor/shot-tracker/HANDOVER.md)

### Inferences
- **Disconnected idle after the linger.** No BLE event is scheduled, so the controller holds no HFXO request. The remaining BLE-related costs are the LFRC's calibration (≈ 1 µA) and its higher run current (≈ 0.35 µA). Both are a clock cost that the 17 µA idle base may or may not include.
- **The LFRC is still needed while idle** for the RTC (k_timer, uptime and timestamps). Stopping calibration while the radio is off would save up to ≈ 1 µA. It conflicts with Nordic's "at least every 8 s" guidance as soon as BLE resumes, and Zephyr 4.1 offers it only as compile-time Kconfig (period and skips).
  - Raising `CONFIG_CLOCK_CONTROL_NRF_CALIBRATION_MAX_SKIP` would cut the calibrations (not the 4 s HFXO-and-temperature checks) at stable temperature, at the cost of the ±500 ppm assurance.
  - I would not recommend it without measurement and link-stability tests.
- **A 32.768 kHz crystal** (hardware: crystal plus load capacitors on the free P0.00/P0.01, `K32SRC_XTAL`, 50 ppm or better):
  - Always: ≈ −0.35 µA run current and ≈ −1 µA calibration, so ≈ −1.3 µA (≈ 8 % of the idle base).
  - While connected: a further ≈ −2.8 µA of window widening.
  - About 0.4 s slower LF start at boot.
  - The puck is 40 mm across and the BMD-340 module is fixed, so board space and the rev 0B layout are the cost.
- **Synthesized LF clock (`K32SRC_SYNTH`)** keeps the HFXO running permanently. It is far worse for idle (hundreds of µA by the HFXO's nature; Nordic: "huge"), so it is not an option.
- **DC/DC for the radio:** done. Turning REG1 DC/DC off would raise radio current by about 60–70 %, so there is no saving left there.
- **Controller choice:** moving to the SDC means moving to the nRF Connect SDK (the SDC lives in Nordic's nrfxlib, not in upstream Zephyr), which is a large change. It would help only connected idle and advertising. With Q_ev ≈ 4 µC and an anchor every 1.05 s, even a 30–40 % lower per-event charge is ≈ 1–1.5 µA. It does nothing for the LFRC window-widening floor, and nothing while disconnected.
- **Zephyr LL internals** (the 1.5 ms HFXO lead in `lll_vendor.h`) are compile-time constants in the Zephyr tree. Changing them means patching Zephyr, which is not recommended.

### Gaps
- The nRF52840 HFXO run current and start-up time (PS clock chapter) were not retrieved, so the energy of the 1.5 ms HFXO lead per Zephyr event is unquantified.
- The cost of the 4 s HFXO-and-temperature check alone (without calibration) was not found.
- Whether the BMD-340 routes P0.00/P0.01 well enough for an external crystal (u-blox data sheet) was not checked here; the board defconfig comment only says they are free.
- No numeric SDC-vs-Zephyr-LL comparison for the nRF52840 (the DevZone figures are images).

## Q7. Ranked list of radio-related idle measures with estimated savings

### Takeaway
The radio's real idle cost is a **phone that stays connected while the puck is idle** (≈ 7–11 µA today if the phone accepts the request; possibly ≈ 100+ µA on an iPhone that rejects the non-compliant 4 s timeout). Advertising during the 120 s linger is small (≈ 0.9 µAh per idle entry). The best measures, in order:
1. Make the idle parameter request Apple-compliant (timeout ≥ 6 s).
2. Add an idle-disconnect policy, or a low-duty idle parameter tier.
3. Optionally slow or shorten the linger.
A crystal is the only way past the ≈ 3.4 µA LFRC window-widening floor of any idle connection.

### Cited Findings
- All figures are carried from Q2–Q6, which hold the sources: profiler advertising charge 13.28 µC per event — [DevZone 80728](https://devzone.nordicsemi.com/f/nordic-q-a/80728/apparent-discrepancy-between-online-power-profiler-measurements); window-widening and calibration terms — [DevZone 14214](https://devzone.nordicsemi.com/f/nordic-q-a/14214/how-much-current-consumption-when-we-use-internal-32k-rc); Apple rules — [Apple Developer Forums 775811](https://developer.apple.com/forums/thread/775811); radio currents — [nRF52840 PS](https://docs.nordicsemi.com/r/bundle/ps_nrf52840/page/_tmp/nrf52840/autodita/current/parameters.i_radio.html); the firmware's current behaviour — [ble.c](C:/Users/rixar/SynologyDrive/Documents/Cursor/shot-tracker/firmware/src/ble.c), [main.c](C:/Users/rixar/SynologyDrive/Documents/Cursor/shot-tracker/firmware/src/main.c), [prj.conf](C:/Users/rixar/SynologyDrive/Documents/Cursor/shot-tracker/firmware/prj.conf)

### Inferences
Estimates are my own calculations from the cited inputs; none are measured.

| # | Measure | When it saves | Estimated saving | Cost / risk |
|---|---|---|---|---|
| 1 | Supervision timeout 4 s → 6–18 s in both parameter sets (`CONFIG_BT_PERIPHERAL_PREF_TIMEOUT` and `fast_param`) | Connected idle on iPhones | Avoids a possible ~137 µA fallback (if iOS rejects and stays near 30 ms); 0 if accepted today | None found; a lost link is detected later. Verify on an iPhone |
| 2 | Disconnect a bonded phone after N minutes of IDLE with nothing pending (e.g. 5–10 min), then stop advertising | Connected idle | All of the connected-idle cost: ≈ 7–11 µA (≈ 7–11 µAh/h; up to +42–65 % of the weekly idle drain if a phone stays in range all the time) | The phone reconnects on the next wake (advertising restarts in `go_active`); shots are kept and replayed, so none are lost. The first live update after a wake waits for the reconnect. The app must not treat the drop as an error |
| 3 | Low-duty idle tier, used only once the puck is IDLE: e.g. 495–510 ms, latency 10, timeout 18 s (or 990–1005 ms, latency 4, timeout 16 s) | Connected idle | ≈ 7.2 → 4.1 µA (−3 µA) from 210 ms; ≈ 11 → 4.1 µA (−7 µA) from 105 ms | Phone→puck writes wait up to ~5.6 s; switching to fast parameters takes several (long) events; iOS/Android may override |
| 4 | 32.768 kHz crystal on P0.00/P0.01 + `K32SRC_XTAL` at 50 ppm (hardware rev) | Always; more when connected | ≈ 1.3 µA always (run current + calibration); ≈ 2.8 µA more while connected (window widening 3.4 → 0.6 µA) | PCB change on a tight board; 0.43 s LF start |
| 5 | Linger interval 0.5 s → 2 s (or linger 120 s → 30 s) | Linger with no phone | ≈ 0.64 µAh per idle entry (≈ 26 → 6.6 µA during the linger); ≈ 0.2–0.3 µA averaged over a week | Slower discovery after a session for "sync later" |
| 6 | No linger (stop advertising at IDLE) | Linger with no phone | ≈ 0.86 µAh per idle entry (≈ 26 µA × 120 s) | Phone cannot connect to an idle puck until the bow moves |
| 7 | Advertising interval while ACTIVE 0.5 → 1 s | ACTIVE without a phone (not idle) | ≈ 13 µA, ≈ 1.5 % of the 0.85 mA ACTIVE draw | Slower reconnect while shooting |
| 8 | TX power 0 → −8/−20 dBm (`CONFIG_BT_CTLR_TX_PWR_MINUS_8/_20`) | Advertising; very little in connections | ≤ 4.7 µA while advertising at 0.5 s (−40 dBm bound, ≤ 18 % of the event); connected idle ≤ 0.2 µA (empty 80 µs TX) | 8–20 dB less link margin, body shadowing on a bow |
| 9 | One or two advertising channels | Advertising | ≈ −30 to −50 % of the advertising event (≈ 8–13 µA at 0.5 s) | Slower, less reliable discovery; conflicts with "connect quickly" |
| 10 | Smaller advertising payload | Advertising | ≈ 0.3 µA per byte at 0.5 s; dropping the 128-bit UUID ≈ 5 µA | Breaks the service-UUID scan filter and iOS background scanning; not practical without a 16-bit SIG UUID |
| 11 | Declare 250 ppm instead of 500 ppm with the RC | Connected | ≈ 1.6 µA while connected | Nordic advises against it: missed anchors and link loss |
| 12 | Fewer LFRC calibrations (`CALIBRATION_MAX_SKIP` up) | Always | ≤ ≈ 0.5–1 µA | Breaks Nordic's "every 8 s" for ±500 ppm; risk to links |
| 13 | Switch to the SoftDevice Controller (nRF Connect SDK) | Connected and advertising | Unquantified; perhaps ≈ 1–1.5 µA connected idle (if events are 30–40 % cheaper) | SDK migration; the MCUboot/OTA chain is retested |
| 14 | DC/DC for the radio | — | Already on (REG1 and REG0); no further saving | — |
| 15 | Non-connectable or directed advertising | — | Not applicable (the phone must connect; up to 4 bonds; RPAs) | — |

- Combining #1 + #2 (+ #5) keeps every functional requirement:
  - Connect quickly when shooting: 0.5 s advertising while ACTIVE and the existing wake on lift or draw.
  - Sync, pairing through the charger window and OTA: all keep using the fast parameters while the puck is awake and charging.
  - Never miss a shot: detection is local and logged.
  - The combination removes nearly all of the radio's idle share, leaving the ≈ 1.3 µA LFRC clock overhead.
- Before any change, measure three states with a power profiler (HANDOVER: JP1): idle with no phone, idle connected at the phone's actual parameters, and advertising at 0.5 s. That settles whether the 4 µC/13 µC event figures and the "~10 µA" advertising comment hold for the Zephyr LL on this board.

### Gaps
- No hardware measurement exists, and every saving above depends on Q_ev (±50 %) and Q_adv (profiler, SoftDevice-based).
- No data was found on how often, in real use, a phone stays connected to an idle puck. That usage figure sets the size of measures #2 and #3.
