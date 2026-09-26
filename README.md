# ShotPuck: automatic arrow counter for Avalon barebow riser weights

A 40 × 10 mm puck that screws onto the free face of an Avalon riser weight (5/16-24), detects each shot with a 6-axis IMU, records the bow's motion around it (accelerometer + gyroscope, 1.0 s before to 0.5 s after release), and reports both over BLE to the scoring app.

| | |
|---|---|
| Size / mass | Ø40 mm, 9.95 mm above the weight face (+8 mm stud), ~20.7 g, COM on axis (trimmed) |
| Radio / MCU | Raytac MDBT50Q-512K (nRF52833), BLE 5 |
| Sensor | ST LSM6DSO32 IMU: 416 Hz, ±32 g accelerometer + ±2000 dps gyroscope while active; accelerometer only, 12.5 Hz wake-on-motion, while idle |
| Battery | Varta CP1654 A3 Li-ion coin, 120 mAh, magnetic pogo charging (50 mA, temperature-gated) |
| Firmware | Zephyr 4.1, custom board `shotpuck/nrf52833`, 183 KB flash / 83 KB RAM (44 KB of it capture buffers) |
| Security / sync | Pairing required (LE Secure Connections); new phones only within 60 s of attaching the charger. Last ~900 shots kept in flash for replay. |

## Repository

```
layout.json                 shared geometry: CAD and PCB both read it
firmware/                   Zephyr app (src/, boards/shotpuck/, prj.conf, debug.conf)
firmware/sim/               Monte Carlo validation of src/shot_detect.c, unit tests of capture.c and evlog.c (+ results.txt)
firmware/zephyr.hex         release build (programme via SWD / Tag-Connect)
hardware/design.py          the circuit (single source of truth)
hardware/gen_sch.py         -> kicad/shotpuck.kicad_sch
hardware/check_netlist.py   KiCad netlist vs design.py
hardware/gen_pcb.py         -> kicad/shotpuck.kicad_pcb (place, keep-outs, route, DRC)
hardware/islands.py         post-fill GND island bridging used by gen_pcb.py
hardware/gen_bom.py         -> kicad/fab/shotpuck-bom.csv
hardware/kicad/             KiCad project, ShotPuck.pretty, schematic PDF, fab/ (gerbers, drill, pos, BOM)
mechanical/puck.py          CadQuery model + balance solver -> out/ (STEP, STL, mass_report.txt, trim.json)
docs/PROTOCOL.md            BLE GATT protocol for the app
docs/VALIDATION.md          what is proven, and the on-device checklist
```

## Decisions (change any, then re-run the generators)

| # | Decision | Why | Alternative |
|---|---|---|---|
| D1 | **Cell beside the module**, balanced by 2 tungsten pins (2.0 g) | 9.95 mm tall instead of ~12.5 mm with the cell under the PCB | Coaxial stack: naturally symmetric, +2.5 mm |
| D2 | **7075 base with integral stud + polycarbonate cap** | Metal only on the weight side, so the antenna radiates through the cap. No plastic creep in the clamp path. | All-PEEK body with a flanged stainless stud |
| D3 | **nRF52833 in high-voltage mode** (cell → VDDH, REG0 = 3.0 V) | No regulator, no battery divider (internal VDDH/5), fewer leaks | External LDO |
| D4 | **MCP73831 + firmware temperature gate** (nRF die temperature, <1 °C / >44 °C) | MCP73831 programs down to 15 mA; the TS-pin chargers found start at 100 mA. Default is *charging allowed* if the MCU is dead, so a flat cell always recovers. | Charger with an NTC input and a thermistor at the cell |
| D5 | **32 kHz from internal RC, REG0/REG1 in LDO mode** | Works whether or not your MDBT50Q variant has the LFXO / DC-DC inductor | Set `K32SRC_XTAL` and enable DC/DC if present (saves µA) |
| D6 | **Cell tabs radial, both outside the cell footprint** (+ at −y, − at +y); no top copper under the cell | Nothing can short against the can | Match to the exact tabbed variant you buy |
| D7 | **Pairing required; new pairings only within 60 s of attaching the charger** (LE Secure Connections Just Works, up to 4 phones, FORGET_BONDS to reset) | Several pucks and shooters at one club: other apps must not read, reconfigure or reset your puck. The charger gesture proves physical possession. | Open access (v1), or a PIN shown by LED blinks |
| D8 | **Detector thresholds are defaults, tunable over BLE**; every candidate is reported | Real release-shock data is unknown until you shoot | – |
| D9 | IMU rotated 270° (I2C edge facing the module–cell channel), reference designators on F.Fab only | Routability; board too dense for silkscreen text | – |
| D10 | **LSM6DSO32 IMU** replaces the LIS2DE12 accelerometer. Pinout, pin states and register values checked against datasheet DocID032891 Rev 1; I3C disabled at init as the datasheet recommends | Gyroscope for bow rotation; ±32 g clips the release shock less; 16-bit data; same I2C bus and INT1 pin | LSM6DSV16X/32X (on-chip sensor fusion, ±4000 dps) |
| D11 | **Raw 6-axis capture per accepted shot** (−1.0 s to +0.5 s, 624 samples, 7.5 KB), analysed in the app | Which bow-movement metrics matter is unknown until real shots are recorded | On-device metrics (cant, steadiness, post-release rotation) in the EVENT |
| D12 | **Identity from the chip's factory ID**: name `ShotPuck-XXXX`, DIS serial number = `device_id` | Tell pucks apart at a club; one stable ID across phones for PowerSync | App-assigned IDs |
| D13 | **Accepted shots in a 32 KB flash ring** (~900 shots), rejected candidates in RAM; app-driven REPLAY, sent paced in the background | Shots survive dropouts and resets without per-phone sync state on the puck | Device push with ACK (rejected: shared state, first phone drains the backlog); captures in flash too |

## Needs your input before ordering

1. **Magnetic connector:** `J1` / `ShotPuck:MagPogo_2P` is a placeholder (2 pins, 2.5 mm pitch, 9 × 4.2 × 4.5 mm body). Buy the pair, then update `layout.json` → `connector` and the footprint in `gen_pcb.py`, and re-run.
2. **CP1654 variant:** confirm the tab geometry (pad positions `cell.neg_pad`), charge voltage 4.20 V and max charge current ≥ 50 mA in the Varta datasheet.
3. **Avalon weight thread depth ≥ 8 mm** (stud length `puck.stud_length`).
4. **Component masses:** weigh the real parts, put them in `layout.json`, and re-run `puck.py` before cutting the tungsten.

## Simulation results (`firmware/sim/results.txt`, seed 2026)

| Set | Shots | Recall | Precision | Notes |
|---|---|---|---|---|
| Nominal sessions | 437 | 99.1 % | 99.8 % | walking knocks, set-downs, stand knocks, nock taps, drops, let-downs (1 FP: stand knock) |
| Hard cases | 479 | 99.6 % | 99.4 % | + taps while aiming, hard grabs off the stand (2 grabs, 1 stand knock) |
| Weak bow (info) | 253 | 80.2 % | 99.5 % | 4–12 g shock, 5–30° rotation: **thresholds must be tuned on real shots** |

The detector needs three things together: a high-frequency shock (6 g sample-to-sample), ≥ 0.5 s of stillness before it (aiming), and ≥ 250 mg of low-frequency bow motion 0.1–0.6 s after it (bow jump into the sling). See `firmware/src/shot_detect.h`. It uses the accelerometer only; the gyroscope feeds the capture.

## Battery life (estimate, verify via JP1)

The idle draw is roughly 13 µA (LSM6DSO32 accelerometer in low-power mode at 12.5 Hz, about 10 µA by interpolating the datasheet's 4.5 µA at 1.6 Hz and 26 µA at 52 Hz, + nRF System ON + RC). While active it is roughly 0.7–0.8 mA: the gyroscope alone is about 0.55 mA, plus 416 Hz FIFO reads and the BLE link. At **2 h of training per day** that is ≈ 1.8 mAh/day, so roughly **2 months per charge** (v0.1 without gyroscope: 4–6 months). The low-battery cut-off (3.3 V) puts the puck into System OFF until the charger is attached.

## Build & flash

Native Windows toolchain (what v0.2 was built and verified with):

| Tool | Where | Used for |
|---|---|---|
| KiCad 10.0 | `C:\Program Files\KiCad\10.0` (its `python.exe` has `pcbnew`) | schematic, PCB, ERC, DRC, fab exports |
| Freerouting 2.1.0 + Java 21 | `%USERPROFILE%\tools\freerouting-2.1.0.jar`, `%USERPROFILE%\tools\jre21` (or set `FREEROUTING_JAR`, `JAVA`) | PCB routing |
| Zephyr v4.1.0 workspace + SDK 0.17.0 | `C:\zp` (west, modules `cmsis`, `cmsis_6`, `hal_nordic`, `segger`, `hal_st`, `mbedtls` for pairing crypto) | firmware |
| CMake 3.31 + Ninja | `%USERPROFILE%\tools\cmake3`, `%USERPROFILE%\tools\ninja` (Zephyr 4.1 fails with CMake 4.x) | firmware |
| w64devkit gcc | `%USERPROFILE%\tools\w64devkit` (set `CC` to its `gcc.exe`) | simulation, capture tests |

**Firmware** (Zephyr 4.1 or nRF Connect SDK v3.x):

```
set ZEPHYR_BASE=C:\zp\zephyr
set ZEPHYR_SDK_INSTALL_DIR=C:\zp\zephyr-sdk-0.17.0
set PATH=%USERPROFILE%\tools\cmake3\bin;%USERPROFILE%\tools\ninja;C:\zp\.venv\Scripts;%PATH%
cd C:\zp
west build -p always -b shotpuck/nrf52833 <repo>\firmware -d build\fw
west flash -d build\fw                                   # J-Link via Tag-Connect TC2030-CTX-NL
west build ... -- -DEXTRA_CONF_FILE=debug.conf           # RTT logging
```

The first boot programs UICR REGOUT0 = 3.0 V and resets once.

**Simulation + unit tests:** `set CC=%USERPROFILE%\tools\w64devkit\bin\gcc.exe` then `python firmware/sim/simulate.py --sessions 36 --stress --seed 2026` (needs numpy, scipy). Must print `CAPTURE TESTS: PASS`, `EVLOG TESTS: PASS` and `RESULT: PASS`.

**Hardware** (run with KiCad 10's Python, from `hardware/`):

```
"C:\Program Files\KiCad\10.0\bin\python.exe" gen_sch.py
"C:\Program Files\KiCad\10.0\bin\python.exe" check_netlist.py      # must print PASS
kicad-cli sch erc --severity-all -o kicad\erc.rpt kicad\shotpuck.kicad_sch
"C:\Program Files\KiCad\10.0\bin\python.exe" gen_pcb.py            # place, route, bridge GND islands, DRC
"C:\Program Files\KiCad\10.0\bin\python.exe" gen_bom.py
kicad-cli pcb export gerbers / drill / pos                         # into kicad\fab\
```

Freerouting is not deterministic: if `gen_pcb.py` does not end with `0 DRC violations` and `0 unconnected pads`, run it again. ERC: 0 errors; the 2 warnings are the intentional straps of LSM6DSO32 SA0 and SDx to GND.

**Mechanical:** `python mechanical/puck.py` (CadQuery 2.x). Not re-run for v0.2: no geometry changed and the IMU's mass is inside the "other SMD parts" estimate.

## Fabrication & assembly

- **PCB:** 2 layers, 0.8 mm FR-4, ENIG, Ø37 mm. Rules are 0.127 mm track/space and 0.45/0.25 mm vias (standard at JLCPCB/PCBWay). Upload the files in `kicad/fab/gerbers/` + BOM + pos.
- **Base:** 7075-T6, turn + thread 5/16-24 UNF-2A, hard anodise (mask the thread or chase it after). Countersink 3× M1.6 at 90°.
- **Cap:** polycarbonate, CNC-machined (translucent so the LED shows). For prototypes, clear SLA resin. **No liquid threadlocker anywhere near PC** (it causes stress cracking): use nylon-patch screws.
- **Assembly order:**
  1. Heat-set the 3 M1.6 inserts into the cap bosses.
  2. Press and glue the tungsten pins into the cap pockets.
  3. Solder the cell tabs.
  4. Put foam on the cell and fit the O-ring (0.8 mm cross-section, in the base groove).
  5. Stack Kapton disc, PCB and cap on the base, then screw it together from the weight side.
  6. Seal the connector with RTV.
- **Programming:** before closing, over TC2030-NL. There is no reset pin on the header (SWD only), which is fine for the nRF52.

## Validation

See **docs/VALIDATION.md**. The blockers:

- Standby current.
- Charging current and termination voltage.
- Cold charge inhibit.
- VBUS wake from System OFF.
- BLE range through the cap.
- **Threshold tuning from logged real shots.**
- IMU axis orientation and a complete capture transfer to the app.
- Pairing on iOS and Android through the charger window, and REPLAY from the flash log after a reset.
