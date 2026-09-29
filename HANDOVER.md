# HANDOVER: ShotPuck (for the next agent)

Read this first, then `README.md`. Everything below is the state as of 2026-09-27 (rev A: flat plate, centre bolt, LIR1254).

## 1. What this is

ShotPuck is a Ø40 × 9.90 mm BLE puck bolted onto the free face of an **Avalon barebow riser weight**: the weight's own 5/16-24 bolt, one size longer, goes through a sleeve in the puck's centre. It counts arrow shots and records the bow's motion around each shot, and reports both to the user's own **Flutter/PowerSync archery scoring app**.

- MCU: u-blox BMD-340-A-R-10 (nRF52840, 1 MB flash / 256 KB RAM, PCB antenna). Until 2026-09-29 the board used the Raytac MDBT50Q-1MV2, swapped because JLCPCB had none in stock (README D14).
- IMU: **ST LSM6DSO32** (accelerometer ±32 g + gyroscope ±2000 dps). v0.1 used a LIS2DE12 accelerometer.
- Cell: LIR1254 Li-ion coin (plain, no tabs, on spring contacts). v0.2 used a Varta CP1654.
- Charger: MCP73831, via a magnetic pogo connector.

Deliverable status: **rev A design package complete and verified off-device. No hardware has been built yet.**

v0.2 changes from v0.1: LSM6DSO32 IMU (U2, plus C7 for VDDIO), raw 6-axis capture per accepted shot streamed over a new CAPTURE characteristic, **pairing required** with new phones accepted only while the charger is attached, unique identity (`ShotPuck-XXXX` + DIS serial), accepted shots kept in a flash log with a paced background REPLAY (all protocol v2), **OTA updates** (MCUboot + MCUmgr SMP over BLE, signed images, rollback, watchdog) on the **nRF52840 module**, and the whole toolchain moved to **native Windows** (KiCad 10, Zephyr on Windows).

Prototype-order round (2026-09-27): Varta-required **cell protection** (BQ29700 + DMN2004DWK), **universal cell pads** (plated holes), puck **1.3 mm taller** for Varta's 1.5 mm swell space, real **magnetic connector** (Samzo PR5L4015 from Electrokit) with a cap chimney, **0.3 mm via holes**, a **2×2 KiKit panel** and **JLCPCB assembly files** with LCSC numbers. Then the board went to Ø36 mm, the trim pins became optional brass, and the **cell moved into a notch in the PCB, open to the rim** (Richard's idea and his choice over a closed hole; the puck is 0.8 mm lower, 10.45 mm). For that the 35° screw boss moved to 40° and D2 moved to the bottom centre.

Rev A redesign (2026-09-27): Richard found the stud version could not be mounted (it needs a bolt from above) and wanted a cheaper flat aluminium plate. Now:
- a 2.0 mm 6061 plate, and the Avalon bolt through a Ø10 steel sleeve that stands on the PCB;
- 3 M2 screws from above;
- **one plain LIR1254** (his choice, after a two-cell feasibility study that split the board and a pouch-cell alternative) on a + spring and a − strap, with R1 = 47k (21 mA);
- the chip antenna kept on board.

To route that on 2 layers:
- R5/R6 removed;
- the − contact and the PCM moved left, and JP1 moved to the bottom;
- SWD on bottom test pads instead of the Tag-Connect;
- SCL/SDA swapped (firmware rebuilt);
- vias 0.55/0.3;
- Freerouting now routes signals only.

The panel moved to annotation tabs in a frame. Then (Richard): the PCB went up on 3 mm standoffs so the magnetic connector is flush with the cap top, the cap came down 0.4 mm (9.90 mm, 1.2 mm free above the cell), the brass trim pin was dropped, the bolt became a button head, one steel tube now runs from the plate through the PCB (hole merged into the cell notch) so the PCB is out of the clamp path, and the M2 screws cheese heads (an M2×8 socket head would reach through the plate).

Shot-shock round (2026-09-29, Richard): the board has **four screws**. H4 was added on the left arm (226°), and H1 moved up beside the tube to (9.2, 1.2), where C1/D1/D3 were (they moved below it). A plate model of the board (VALIDATION.md) showed the left arm (PCM and both cell pads) flapping against the cell and the top half without a screw on the right. Q2's two identical FETs were swapped so the PCM still routes past H4 (§6, Cell protection). The BMD-340 and C2 get corner staking at assembly (README D29). Open for Richard: PCB thickness (0.8 mm now; 1.2 mm keeps the stock 3 mm standoffs if the puck grows 0.4 mm) and a tabbed cell (D6).

## 2. The user (Richard) – how to work with him

- Software developer in Sweden, on Windows + Cursor/VS Code. He owns the Flutter/PowerSync archery app that will consume the BLE protocol.
- Wants **concise, direct answers**.
- Wants **decisions clearly flagged** for his input.
- Wants **on-device validation requirements listed explicitly**.
- Wants native/embedded logic **validated by simulation before shipping**.
- Wants deliverables as **complete drop-in files/zips**, not patches or diffs. The repo is on GitHub (`Rixard86/shot-tracker`, branch `main`); commit/push only when he asks.
- His global coding rules (no comments, ≤ 2 parameters, ≤ 50-line functions, ≤ 250-line files, no magic numbers) apply to **new or changed code only**, not to refactoring existing files.

## 3. Hard rules of the codebase (do not break)

1. **`layout.json` is the only source of geometry.**
   - Readers: `mechanical/puck.py` (enclosure and balance) and `hardware/gen_pcb.py` (placement, holes, keep-outs).
   - Change geometry there, then re-run both.
2. **`hardware/design.py` is the only source of the circuit.**
   - `.kicad_sch` and `.kicad_pcb` are **generated**; never hand-edit them in the repo.
   - Flow: edit `design.py` → `gen_sch.py` → `check_netlist.py` → ERC → `gen_pcb.py` → `gen_bom.py` → re-export fab files (README "Build & flash").
3. **`firmware/src/shot_detect.c`, `capture.c` and `evlog.c` are portable and shared by the firmware and the host tests.**
   - Any change must be re-run through `firmware/sim/simulate.py --stress`, which must print `CAPTURE TESTS: PASS`, `EVLOG TESTS: PASS` and `RESULT: PASS` before shipping.
   - Keep new logic that can be portable in this form (no Zephyr includes, flash/BLE behind small interfaces) so it can be host-tested.
4. **Wire protocol lives in `firmware/src/protocol.h`** and is documented in `docs/PROTOCOL.md`.
   - Keep the two in sync (packet layouts are also pinned by `test_capture.c`).
   - Bump `PROTO_VERSION` on any layout change, because the Flutter app parses these bytes. It is **2** now.
5. `puck.py` and `gen_pcb.py` share only `layout.json` now (the trim pin and its `trim.json` are gone).
6. **Placement before routing:** after any placement or circuit change, run `gen_pcb.py --no-route` (placement DRC), then `global_route.py` (overflow + hotspots; the split-tube board that routed cleanly scored 8.3, boards that failed scored 8.5–9.2, and parts sitting in a hotspot band showed up there), then `gen_pcb.py --quick` (one Freerouting attempt, ~5 min). Only a placement that passes the trial gets the full run.
7. `gen_pcb.py` must end with **0 DRC violations and 0 unconnected pads**. Freerouting is not deterministic; the script already retries up to 4 times, so re-run it if it still fails.

## 4. Repository map

```
HANDOVER.md, README.md       overview, decisions D1–D11, build/fab/assembly
layout.json                  shared geometry
firmware/
  src/main.c                 event loop, IDLE/ACTIVE modes, BLE glue, event ring (128, RAM only)
  src/shot_detect.[ch]       portable detector (trigger + pre-stillness + post-release LF motion), accel only
  src/accel.[ch]             raw LSM6DSO32 I2C driver (IDLE 12.5 Hz LP wake-up / ACTIVE 416 Hz accel+gyro FIFO)
  src/capture.[ch]           portable capture ring (2.8 s), 4 shot windows (−1.0 s..+0.5 s), BLE packet packing
  src/capture_tx.[ch]        queues captures and pumps CAPTURE notifications without blocking
  src/evlog.[ch]             portable flash ring log of accepted shots (32 B records, CRC, torn-write safe)
  src/event_tx.[ch]          shot log on the eventlog partition, RAM ring of rejects, live queue, paced batched EVENT/REPLAY
  src/claim.[ch]             pairing window (60 s after VBUS appears), pairing-accept callback, FORGET_BONDS
  src/identity.[ch]          name ShotPuck-XXXX and DIS serial from the nRF FICR device ID
  src/power.[ch]             VDDH/5 SAADC battery, die-temp charge inhibit, REGOUT0=3.0 V, System OFF
  src/store.[ch]             Zephyr settings/NVS: total, seq (+1024 margin at boot), cfg
  src/ble.[ch], protocol.h   GATT service 7a1e0001-5b0c-4f3a-9c6e-3a5d2b9e0a01 + BAS
  boards/shotpuck/shotpuck/  custom HWMv2 board, target `shotpuck/nrf52840` (flash partitions in the .dts)
  prj.conf / debug.conf      release (no log/UART) / RTT logging overlay
  VERSION                    app version, signed into the image
  sysbuild.conf / .cmake     MCUboot (swap-using-move, ECDSA-P256) + guard against the public dev key
  shotpuck_full.hex          MCUboot + signed app, first flash by SWD (merged with zephyr/scripts/build/mergehex.py)
  shotpuck_update.bin        signed app image for OTA
  src/ota.[ch]               watchdog (10 s) and self-confirm of a trial image after 10 s healthy
  sim/simulate.py            Monte Carlo generator + scorer (ctypes → libsd.dll/.so), runs test_capture
  sim/sim_runner.c           IDLE/ACTIVE pipeline + LSM6DSO32 accelerometer model
  sim/test_capture.c         host unit tests for capture.c
  sim/test_evlog.c           host unit tests for evlog.c (fake NOR flash)
  sim/results.txt            last run (seed 2026, 36 sessions)
hardware/
  design.py                  parts/pins/nets table (the schematic)
  sexp.py                    tiny S-expression reader/writer
  gen_sch.py                 → kicad/shotpuck.kicad_sch (lib symbols copied, labels on pins)
  check_netlist.py           kicad-cli netlist export vs design.py (must PASS)
  gen_pcb.py                 footprints, placement (PLACE dict), keep-outs, GND fanout,
                             Freerouting, SES import, stitching, pours, island bridging, DRC (kicad-cli)
  board_shape.py             board outline, cell notch, bolt hole, sleeve land, antenna zone (shapely, from layout.json)
  islands.py                 bridges GND pour fragments cut off from the main plane (post-fill)
  place_eval.py              airwire model of a placed board (MST per net, locked copper pre-connected), crossings
  global_route.py            routability estimate before Freerouting: 0.5 mm grid global router (A*, congestion
                             negotiation) on the board's own outline, keep-outs and pads -> overflow + hotspots
  gen_bom.py                 → kicad/fab/shotpuck-bom.csv
  gen_panel.py               → kicad/panel/shotpuck-panel.kicad_pcb (KiKit 2x2 in a frame, 4 annotation tabs per board, top row turned 180°, mouse bites, fiducials, tooling)
  gen_jlc.py                 → kicad/fab/jlc/ BOM + CPL for JLCPCB assembly (LCSC numbers in the script)
  check_place.py             stray helper of unknown provenance, not part of the flow (safe to ignore/delete)
  kicad/                     project, ShotPuck.pretty, schematic PDF, PCB render PNG, drc.rpt, erc.rpt,
                             fab/ (gerbers+drill zip, pos, BOM)
mechanical/puck.py           CadQuery model + mass/balance → out/ (STEP/STL/mass_report)
mechanical/geometry.py, puck_parts.py, puck_report.py   its heights and poses, part builders, report and export
mechanical/blender/          render_assembly.py (entry) + parts/cellconn/shapes/stack/looks/motion: exploded-view animation → out/shotpuck_assembly.mp4
docs/PROTOCOL.md             BLE protocol v2 + recommended app sync
docs/VALIDATION.md           proven vs on-device checklist
```

## 5. Verified state (numbers to quote)

| Item | Result |
|---|---|
| Firmware (Zephyr v4.1.0, SDK 0.17.0, Windows, sysbuild) | 0 compiler warnings; app 200,016 B of a 452 KB slot, RAM 90,048 B of 256 KB; log+assert variant 267,092 B / 95,308 B; MCUboot 27,980 B of 48 KB (one harmless upstream Kconfig warning about MBEDTLS_CFG_FILE, MCUboot uses TinyCrypt) |
| OTA signing | Image validates with the private key (`imgtool verify`), fails with MCUboot's dev key; MCUboot embeds the matching public key (91 bytes compared); build refuses to run without the private key |
| Capture unit tests | PASS (also mutation-checked: an off-by-one window start is caught) |
| Shot log unit tests | PASS: 952 of 3000 kept after wrap; torn write/erase recovery; never writes unerased flash (mutation-checked) |
| Sim nominal (36 sessions, 437 shots) | recall 99.08 %, precision 99.77 % (1 FP, stand knock) |
| Sim hard cases (479 shots) | recall 99.58 %, precision 99.38 % (3 FP: 2 grab, 1 stand knock) |
| Sim weak bow (info only, 253 shots) | recall 80.2 %, precision 99.5 %, dominated by missed triggers / no follow-through |
| Firmware rev A (2026-09-29, BMD-340) | Rebuilt for the BMD-340 pin map: same sizes (200,016 B / 27,980 B), only the known MCUboot Kconfig warning, `imgtool verify` valid with our key and rejected with the dev key; compiled DTS has SCL = P0.07, SDA = P0.05, ACC_INT1 = P0.03, CHG_STAT = P0.15 (active low), CHG_INH = P0.11, LED = P0.13 |
| Schematic | `check_netlist.py` PASS, 99/99 pin assignments, 26 nets; KiCad 10 ERC 0 errors, 2 warnings (intentional SA0/SDx straps to GND) |
| PCB | Ø36 mm 2-layer, 0.8 mm, track 0.15 / clearance 0.127, vias 0.55/0.3 mm, fully routed, **0 DRC violations, 0 unconnected** (KiCad 10 DRC). After the 4-screw change (D29) it took three full runs (attempt 2 of the third; misses were 1–7 connections in the IMU/charger column, then CHG_INH was locked). DRC with `--schematic-parity` only reports the "/GND" vs "GND" net-name style, the named no-connect pins and the BOM-exclude flag on H1–H4/JP1 (checked by script: all 107 pad nets match) |
| Panel | 91.1 × 88.7 mm, 2×2 in a frame, 16 tabs, **0 DRC violations, 0 unconnected**; JLCPCB BOM 19 lines / CPL 104 placements |
| Enclosure | 9.90 mm above the weight face (plate 2.0 + Kapton 0.1 + 3.0 mm standoffs + PCB 0.8; cell on the Kapton in the PCB notch, 6.60 mm from there to the ceiling = 1.2 mm free), connector top flush with the cap top, 16.09 g, COM 1.14 mm off axis (no trim weight), 4 standoffs |
| Board under shot shock (plate FE, 100 g along the axis) | 4 screws, 0.8 mm: first mode 1.7–2.0 kHz (was 1.1–1.2), worst strain 160–180 µε (was 320–340), cell pads ~0 µm against the cell (was 13–15). The model script (scikit-fem) is not in the repo |

Before the IMU change, the unchanged v0.1 sources were rebuilt on Windows as a toolchain check: firmware byte-identical in size (142,620 B / 27,856 B), netlist PASS.

Detector defaults (tunable over BLE CONFIG) are unchanged except the sample rate (416 Hz):

| Parameter | Default |
|---|---|
| Trigger (sample-to-sample jump) | 6000 mg |
| Motion threshold | 300 mg |
| Required pre-trigger stillness | ≥ 500 ms |
| Post-release LF motion (mean over 100–600 ms) | ≥ 250 mg |
| Refractory period | 1500 ms |
| Wake threshold | 150 mg |
| Idle timeout | 30 s |
| Gravity filter time constant | 0.25 s |

## 6. Key design facts (easy to get wrong)

- **Power:** cell → U1 **VCCH** (BMD-340 pad 65 = nRF52840 VDDH, high-voltage mode). REG0 drives **VCC = VDD = 3.0 V** (pad 17); firmware writes UICR REGOUT0 on first boot and resets once. VDD powers the LSM6DSO32 (VDD and VDDIO), LED and SWD VCC, well under u-blox's 5 mA limit for external loads in high-voltage mode.
- **Battery voltage:** read via SAADC `NRF_SAADC_VDDHDIV5`. The define needs `#include <zephyr/dt-bindings/adc/nrf-saadc-v3.h>`.
- **Charge inhibit logic:**
  - PROG → R1 47k (21 mA for the LIR1254) → Q1A. Q1A's gate is pulled to VIN by R2 (1M).
  - Q1B (gate = CHG_INH on P0.11, R3 100k pull-down) pulls Q1A's gate low, which floats PROG and disables charging.
  - **Default = charging allowed** (deliberate, so a flat cell can recover).
- **Charge status:** STAT → D2 BAT54J → P0.15. The GPIO pull-up is enabled **only while VBUS is detected**, which avoids standby leakage.
- **Pins:**

  | Signal | nRF pin | BMD-340 pad |
  |---|---|---|
  | I2C SCL | P0.07 | 23 |
  | I2C SDA | P0.05 | 21 |
  | ACCEL_INT1 (IMU INT1) | P0.03 | 19 |
  | CHG_STAT | P0.15 | 33 |
  | CHG_INH | P0.11 | 27 |
  | LED | P0.13 | 31 |
  | SWD | SWDIO / SWCLK | 44 / 43 |
  | VBUS (charger present, System-OFF wake), via R9 1k + C9 100n (net VBUS_SNS) | VBUS | 66 |
  | Supply in / out | VCCH / VCC | 65 / 17 |
  | GND | – | 1–5, 16, 18, 29, 30, 45–47, 55 |

  - LSM6DSO32 is at I2C address **0x6A** (SA0 = GND), CS = VDD (I2C mode), SDx/SCx = GND, INT2/OCS_Aux/SDO_Aux unconnected. KiCad symbol: `Sensor_Motion:LSM6DSL` (same pinout), footprint `LGA-14_3x2.5mm_P0.5mm_LayoutBorder3x4y`.
  - BMD-340 pin choice: the module's second pad column (17–29) escapes through the gaps of the outer column (61–68), so VDD, ACC_INT1, SDA, SCL and CHG_INH leave its left end straight towards the IMU and the charger. ACC_INT1 is the only signal on a low-frequency-only pin (P0.03, fine for an interrupt). P0.09/P0.10 (NFC) and the USB pads 67/68 are not used. LED (pad 31) and CHG_STAT (pad 33) sit on the bottom row: the LED drops straight to R4, CHG_STAT goes through a locked via to B.Cu.
  - `design.py`, the board DTS and the pinctrl `.dtsi` must change together. The I2C pull-ups are the nRF's internal ones (~13 kΩ, 400 kHz): there are no external pull-up pads.
  - Footprint and symbol are generated by `hardware/bmd340.py` from data sheet UBX-19033353 R13 (Figure 12 land pads, Table 2 pinout). Its pad table matched two independent open-source footprints of boards that were built (OSHWA 2020 badge, DEF CON 27 badge) to 0.000 mm, and its 68 pin names were compared programmatically with the data sheet table. The 3D model is u-blox's own STEP (`kicad/ShotPuck.3dshapes/u-blox_BMD-340.step`, corner origin, long axis +Y, antenna at +Y), rotated −90° (KiCad turns a positive Z rotation clockwise; +90° put it 15 mm off and backwards) and offset (7.5, −5.1) to the body centre. Checked in a STEP export of the board: body at x −6.5…8.5, y 5.4…15.6, shield can ending at x 3.88 so the bare antenna sits over the keep-out, and all 68 model pads on the copper pads (0.000 mm).
- **IMU configuration** (register values checked against ST's `lsm6dso32_reg.h` in `C:\zp\modules\hal\st`):
  - Init: software reset, BDU + IF_INC, **I3C disabled** (CTRL9_XL = 0xE2, datasheet recommendation for I2C-only use). INT1 has an internal pull-down and must be low at power-on for I2C mode: the nRF pin is high-Z then, so never add a pull-up to ACC_INT1.
  - IDLE: gyro off, accel low-power 12.5 Hz ±8 g, slope wake-up (31.25 mg/LSB, latched, clear on read) → INT1.
  - ACTIVE: accel 416 Hz ±32 g (0.976 mg/LSB), gyro 416 Hz ±2000 dps (70 mdps/LSB), FIFO continuous, both batched at 417 Hz, watermark 52 words → INT1. The FIFO holds 3 KB uncompressed (~0.5 s at this rate), so the main thread must not block for long. The driver reads it word by word (7 bytes each) and pairs each accel word with the latest gyro word.
- **Capture:** ring of 1152 samples; for each **accepted** shot a window from trigger − 416 to trigger + 208 samples is copied into one of 4 slots; the window completes when the ring has its last sample. Detector time ↔ sample index uses `cap_index_to_ms` / `cap_ms_to_index` (exact inverses, tested).
- **Clocks and regulators:** 32 kHz from RC (the BMD-340 has no 32.768 kHz crystal); REG0/REG1 in LDO mode. The module does carry the DC/DC inductors, so DC/DC could be enabled later to save active current.
- **Board orientation** (layout coordinates, +y up, board centre = bolt axis):
  - Module U1 (BMD-340, 15.0 × 10.2 mm) at (0.0, 11.0), rotated 180°, long axis tangential above the bolt hole; the antenna end (the footprint's −x end) points to +x and is copper-free out to the rim (`board_shape.antenna_zone`). Centred on x = 0 it sits as high as it goes: both top corners 0.24 mm inside the rim (the outline tolerance is ±0.2 mm), the body 0.9 mm above the tube; its pads are ≥ 0.66 mm from any edge. Moving it left brought the antenna zone's corner toward the tube, so that corner is chamfered 45° by 1.1 mm (`ANTENNA_CHAMFER`: only the keep-out's margin below the module body, not the area under the antenna) to keep the passage open (next bullet but one).
  - Cell at (0, −11.75) in a notch open to the rim: Ø13.0 mm round end (`cell.pcb_hole_clear` 0.25 mm) with straight edges out to the rim. The Edge.Cuts outline (rim, notch, Ø8.4 bolt hole) comes from `board_shape.py`; `puck.py` cuts the same shape.
  - Cell contact pads (2.5 × 2.0 mm SMD, `ShotPuck:LIR1254_Contacts`) both left of the notch: + at (−7.27, −7.55) for a spring on the can side, − at (−8.27, −13.2) for a strap over the top cap.
  - Magnetic connector J1 at (13.52, −3.62), rotated 75°, pin 1 (square) = VIN_RAW. Below H1: C1 at (6.45, −1.5) turned 270° beside the tube (VIN pad up), D3 at (9.3, −3.4) turned 270° next to J1's pins, D1 at (8.2, −6.8).
  - IMU U2 at (−10.2, 11.5), rotated 270°, C6/C7 on its left; C3 (VBAT) and C5 (VDD) at x = −10.7 below it, C4 (VDD bulk) at (−12.8, 7.3). R4 stays under the module's left end (P0.13 drops straight onto it); the status LED D4 sits at the left rim, (−16.95, 2.6), rotated 90°, so it shows through the cap wall (Richard). LED_A runs on B.Cu from R4 to it.
  - Charger (U3, Q1, R1–R3, D2) on the left at y ≈ −1.5…2; PCM (U4, Q2, R7, R8, C8) at the lower left, x −15.5…−9, y −4…−10, just above H4; R8 at (−15.2, −7.4), turned 90°, left of U4.
  - Bottom side: JP1 at (−10.2, −5.0) and the SWD test pads J2 at (−1.7, 13.5) under the module, rotated 180° so VDD sits at the −x end: VDD x −5.51, SWDIO −2.97, SWDCLK −0.43, GND 2.11 at y 13.5 (2.54 mm pitch, board coordinates seen from the cap; the pogo jig must match). J2's GND pad gets a solid zone connection and no fan-out via; SWDIO/SWDCLK leave U1 through locked vias just below the module (`PRE_VIAS`), and VDD through a locked via off U1 pad 17 at (−5.4, 14.85).
  - Board Ø36 mm: the puck is Ø40 mm = the Avalon barebow weight (the true constraint); cap wall 1.5 mm leaves a Ø37 mm cavity, 0.5 mm clearance per side.
  - Screw holes Ø2.4 mm at (9.2, 1.2) (H1) and at r = 16.5 mm, 160° (H2), 305° (H3) and 226° (H4) (`bosses.positions` in `layout.json`), clear of the module, the notch and the connector. H1 sat at 40° (inside the antenna zone), then at (8.1, −5.0) beside the tube slot; for shot shock (D29) it moved up to where C1/D1/D3 were, right of the tube: its copper keep-out ring ends 0.9 mm below the antenna zone, its standoff 1.1 mm. H4 holds the left arm (PCM, cell pads), which had no screw below H2; R8 moved to (−15.2, −7.4), U4 and Q2 up by 0.35/0.5 mm to clear it.
  - The bolt tube (Ø10) passes through a Ø10.4 hole (0.2 mm clearance for JLCPCB's ±0.2 mm outline tolerance) that a straight slot merges with the cell notch (`board_shape.sleeve_hole`), so no FR-4 web is left between them. The right half (J1, D1, D3, C1) reaches the rest only across the top, through a 0.83 mm passage between the hole's 0.35 mm copper keep-out and the antenna zone's chamfered corner ((3.1, 5.9)–(4.2, 4.8)): VIN on B.Cu and a locked 0.15 mm GND track on F.Cu (C1.2 → U1.45, bridged to U1.46 and its via; on the board only its tail from U1.45 into the passage pour is kept, since the SWDIO via under pad 45 leaves the pour no room for a thermal spoke). Below the passage both climb the ~1.3 mm gap between the tube keep-out and H1's ring: GND at x 5.75, VIN at x 6.45. Without that track the right half's GND was cut off: the VIN via's clearance left the pour less than its 0.2 mm minimum width. The antenna zone matches the BMD-340 keep-out (the module's last 4.4 mm, ±6.2 mm across; `ANTENNA_INSET`/`ANTENNA_HALF_W` in `board_shape.py`), then runs on to the rim. VIN is pre-routed and locked end to end (`VIN_ESCAPE`): C1 → straight up x 6.45 between the tube and H1 → via at (4.95, 3.95) → B.Cu through the passage (3.95, 4.55) and up beside the antenna zone (x 2.96) → under the module's top edge (y 15.7) → down under its left end (x −6.8) → via at (−7.75, 3.25) → R9's VIN pad. J2 and the interior GND vias sit inside that loop; CHG_STAT (via under U1 pad 33) and LED_A leave it on B.Cu below R9. Also locked (`PRE_LINKS`, `PAD_BRIDGES`): SDA, SCL and ACC_INT1 to the IMU (INT1 over its top at y 13.35), LED_IO to R4, the GND pad pairs U1 2–1, 4–3, 45–46 and U2 1–2, 3–2 (the INT1 path cuts the pour above the IMU), and a via for U1 pad 18. `gnd_fanout` keeps its stubs clear of F.Cu signal tracks (it used to check only the via spot).
- **Keep-outs in `gen_pcb.py`:** antenna region to the rim; 0.35 mm copper-free band along the cell notch (both layers); 4 boss faces, Ø3.8 + 0.3 mm; 0.35 mm rim ring. The boss rings keep both layers copper-free under the cap bosses and the standoffs.
- **Cell protection:** BAT_N (cell −) → Q2B (pins 4/5, DOUT) → PCM_D → Q2A (pins 1/2, COUT) → GND. The DMN2004DWK's two FETs are identical; they were swapped (2026-09-29) so U4's COUT pin faces Q2 pin 2 straight across (locked link) and DOUT, locked too, loops over Q2's top to pin 5: H4 took the room under Q2 where the old COUT loop ran. Q2 pin 1 (GND) sits inside that loop, so it has a locked via above Q2 (`PRE_VIAS`) and a solid zone connection. BQ29700: OVP 4.275 V, UVP 2.80 V, OCD/OCC ±100 mV over ~1 Ω of FETs (≈ 0.1 A), SCD 0.5 V, 4 µA. R7 feeds BAT from VBAT (= cell + through JP1). The firmware's 3.3 V cut-off stays the primary over-discharge guard.
- **Mechanical (`puck.py`):** plate (2.0) → Kapton (0.1) → standoffs (`puck.pcb_standoff`, 3.0) → PCB (0.8); the cell sits on the Kapton in the PCB notch (`Z_CELL`); ceiling = `Z_CELL` + `cell.swell_h` (6.6 mm); cap top 1.2 mm, total 9.90 mm, which puts the connector top exactly at the cap top (the report prints the offset; 0 = flush). The tube runs from the plate through the PCB to 0.1 mm above the cap top (8.0 mm) and carries the whole clamp load, steel on aluminium. The cap sits on a 0.3 mm gasket on the plate and is held by 3 M2×8 cheese head screws from above; the counterbore depth is set so the screw tip ends `tip_inset` (0.1 mm) inside the plate. The connector is stepped (Samzo GZ0254-P001: a 12.5 mm top boss on a 14.5 mm flange, 2 mm each; `connector.boss_w/boss_h`). The cap opening fits the boss (0.1 mm RTV gap per side) and a 0.8 mm lip under the cap top stops 0.1 mm above the flange ends, so the cap takes the plug's magnetic pull and side loads. The connector's pins stick out 0.7 mm under the PCB, into the standoff gap. `mechanical/blender/stack.py` imports these heights from `geometry.py`.
- **Panel (`gen_panel.py`):** fixed KiKit tabs are symmetric, and no symmetric set clears both the cell notch (|x| ≤ 6.6 at the bottom) and the antenna/J1 side. So `gen_panel.py` adds `kikit:Tab` annotation footprints to a temporary copy of the board: top at x = 0, bottom at x = −8.5 (left of the notch; at −10 and −9 its mouse bites fell inside H4's courtyard), left and right at y = −4, 3 mm wide. The top row is turned 180° (`rotation: 180deg; alternation: rows`), so the two rows meet top to top and their centre tabs pair up; the notch sides and the outer sides tab to a full frame.
- **Ground strategy:** every GND SMD pad gets a stub + via before routing (`gnd_fanout`, which keeps clear of every via already on the board, including the SWD escape vias). The DSN handed to Freerouting has the GND planes and GND pins stripped (`signals_only`), so it routes signals only; with the planes in, it stalls. `stitch()` adds grid GND vias only after all routing (stitch vias placed before the second pass filled the PCM pocket and blocked it), dangling track ends are deleted (`remove_dangling`), pours fill with island removal (connectivity is rebuilt before every fill; a stale one kept dead islands and got a useless bridge via), then `islands.bridge_islands()` adds a via for any GND fragment group not tied to the main plane, never inside a via keep-out. U2's GND pads use a solid zone connection (a thermal on pad 6 was starved). Pre-routed links (`PRE_ROUTED`): Q2's common drain PCM_D and Q1.4–Q1.1.
- **Identity:** `identity_apply()` (after `bt_enable` and `settings_load_subtree("bt")`) reads the 8-byte FICR device ID, sets the name `ShotPuck-` + last 4 hex digits, and sets the DIS serial via `settings_runtime_set("bt/dis/serial")` (needs `CONFIG_SETTINGS_RUNTIME`).
- **Pairing (claim):** LE Secure Connections Just Works, bonds stored by `CONFIG_BT_SETTINGS`, max 4 (oldest overwritten). `pairing_accept` rejects new pairings unless the window is open; the window opens for 60 s on a VBUS rising edge (polled via `power_vbus_present()` in the main loop, which wakes at least every 2 s; boot with the charger attached counts). LED blinks 1/s while open, twice on a new bond. Every ShotPuck characteristic value and CCC needs encryption; DIS and BAS are open. Zephyr restores a bonded phone's subscriptions only after the link is encrypted.
- **Flash layout (1 MB):** `boot_partition` 0x00000 48 KB (MCUboot); `slot0` 0x0C000 460 KB (running app); `slot1` 0x7F000 460 KB (OTA download); `eventlog` 0xF2000 32 KB; `storage` 0xFA000 24 KB (NVS: counts, config, bonds). Shots, settings and bonds survive OTA.
- **OTA:** sysbuild builds MCUboot + app and signs the app with ECDSA-P256 (key `C:\Users\rixar\.shotpuck\ota-signing-key.pem`, never in the repo; pass it quoted as `-DSB_CONFIG_BOOT_SIGNATURE_KEY_FILE=\"...\"`, otherwise Kconfig silently falls back to the public dev key and `sysbuild.cmake` stops the build). MCUmgr SMP over BLE (img + os groups) with `PERM_RW_ENCRYPT`, progressive erase. MCUboot swap-using-move; a new image runs as a trial, `ota.c` confirms it after 10 s of main loop, the phone may confirm earlier. The watchdog (10 s, fed every main-loop pass; paused only under a debugger) turns a hang into a reset, and MCUboot then reverts an unconfirmed image. MCUboot feeds the watchdog during the swap. In the no-IMU SOS loop the watchdog is fed only if the image is confirmed, so a broken update rolls back while a confirmed image with a dead sensor stays up for diagnostics.
- **Event transmission:** `event_tx` sends live events first, then a REPLAY (flash shots, then RAM rejects), up to 10 events per EVENT notification. EVENT and CAPTURE share a limit of 2 notifications in flight; the completion callback wakes the main loop. Nothing in the main loop waits on BLE buffers any more (the v0.1 replay loop slept up to 1 s per event).
- **BLE:**
  - Advertises while ACTIVE, plus a 120 s linger, and while the pairing window is open.
  - Requests MTU 247 (data length extension on). EVENT is 24 bytes, so MTU must be ≥ 27; capture works at any MTU ≥ 23.
  - Every detector candidate (accepted or rejected) is notified as EVENT; only accepted shots get a CAPTURE.
  - CAPTURE notifications are paced (max 2 in flight, completion callback wakes the main loop) so the main thread never blocks on BLE buffers while the IMU FIFO fills.

## 7. Environment (native Windows, already installed on Richard's PC)

| Tool | Location |
|---|---|
| KiCad 10.0.6 (Python 3.11 with `pcbnew`, `kicad-cli`) | `C:\Program Files\KiCad\10.0\bin` |
| Java 21 (Temurin JRE), Freerouting 2.1.0 | `C:\Users\rixar\tools\jre21`, `C:\Users\rixar\tools\freerouting-2.1.0.jar` |
| w64devkit gcc 16.2 | `C:\Users\rixar\tools\w64devkit\bin` |
| CMake 3.31.8, Ninja 1.12.1 | `C:\Users\rixar\tools\cmake3\bin`, `C:\Users\rixar\tools\ninja` (system CMake is 4.3: do not use it for Zephyr) |
| Zephyr v4.1.0 west workspace | `C:\zp` (project filter: cmsis, cmsis_6, hal_nordic, segger, hal_st, mbedtls, mcuboot, zcbor), venv `C:\zp\.venv` (west, numpy, scipy, pypdf, imgtool deps, Zephyr requirements) |
| OTA signing key | `C:\Users\rixar\.shotpuck\ota-signing-key.pem` (ECDSA-P256; Richard must back it up) |
| CadQuery 2.8 (Python 3.12, via uv) | `C:\zp\.cq` (run `C:\zp\.cq\Scripts\python.exe mechanical\puck.py`) |
| Blender 3.1 (Cycles on the RTX 3080 via OptiX) | `C:\Program Files\Blender Foundation\Blender 3.1` (run `blender.exe -b -P mechanical\blender\render_assembly.py`) |
| KiKit 1.8.1 | installed with `--user` into KiCad 10's Python (`python.exe -m kikit.ui ...`) |
| Zephyr SDK 0.17.0 (arm only) | `C:\zp\zephyr-sdk-0.17.0` |

Build commands are in README "Build & flash". Keep toolchains and build directories out of the repo: it lives in a Synology-synced folder.

### Gotchas already paid for

- **CMake 4.x breaks Zephyr 4.1.** Use the 3.31 in `tools\cmake3`.
- **`CONFIG_BT_SMP` needs the `mbedtls` module** (PSA crypto); without it Kconfig aborts with MBEDTLS/PSA_WANT dependency warnings.
- **Upstream sysbuild makes no `merged.hex`** (that is nRF Connect SDK). Merge `build/sb/mcuboot/zephyr/zephyr.hex` + `build/sb/firmware/zephyr/zephyr.signed.hex` with `zephyr/scripts/build/mergehex.py`.
- **Custom board with sysbuild:** pass `-DBOARD_ROOT=<repo>/firmware` on the command line so MCUboot finds the board too.
- **The harness blocks `git rm` inside long PowerShell command lines** (it reads it as Remove-Item on a system path). Run it alone (`git -C <repo> rm ...`).
- **KiCad 10 Python API (vs the v0.1 KiCad 7 scripts):**
  - `pcbnew.WriteDRCReport` pops a wxWidgets assert dialog on the user's screen ("assert process failed in Pgm()") and hangs. Never call it; DRC runs via `kicad-cli pcb drc`. ERC via `kicad-cli sch erc`.
  - `FP_SHAPE` is gone (use `PCB_SHAPE(fp, …)` with `SetStart/SetEnd/SetCenter`); no `SetPos0`; `SetDescription` → `SetLibDescription`; `SetDoNotAllowCopperPour` → `SetDoNotAllowZoneFills`; netclass via `GetDefaultNetclass()`; `LSET(layer)` does not construct: use `AddLayer`.
  - `Q_Dual_NMOS_S1G1D2S2G2D1` moved from the `Device` to the `Transistor_FET` symbol library.
  - `FootprintLoad`/`FootprintSave`, zone fill, `ExportSpecctraDSN`, `SaveBoard`/`LoadBoard` all work standalone.
- **Board thickness:** `gen_pcb.py` sets it from `layout.json` `puck.pcb_thickness` (0.8 mm). Before 2026-09-27 it was never set, so the board, panel and Gerber job files said KiCad's default 1.6 mm (found when the Blender render showed a board twice as thick).
- **Blender render:** KiCad's GLB materials import as metallic/rough 1.0 and the solder mask is 83 % transparent; `looks.tune_imported()` fixes both (dark green opaque mask, dark parts non-metallic). Parts are built in place (origin at the world origin) and animated as z offsets from their rest position (`motion.EXPLODE_MM`, `motion.STEPS`).
- **Freerouting quirks:**
  - 2.1 ignores the old `-mp`/`-mt` flags (its settings file says `max_passes: 9999`) and can loop forever on a blocked connection, so `route()` has a subprocess timeout; a timed-out pass can leave an empty `.ses` (handled).
  - With its multi-threaded optimizer on, it "restores an earlier board" and then writes a `.ses` that is missing whole nets while reporting 0 incomplete. `route()` therefore passes `--router.optimizer.enabled=false --router.max_threads=1`.
  - It skipped Q2's diagonal PCM_D link even with the optimizer off, so that link is pre-routed (`PRE_ROUTED`).
  - Results still vary run to run: `gen_pcb.py` rebuilds and re-routes up to `BOARD_ATTEMPTS` times until nothing is unconnected. `--reuse-ses` re-imports an existing `kicad/shotpuck.ses` (git-ignored). Always trust KiCad DRC, not the router log.
  - It stalls when the DSN still holds the GND planes, so `signals_only()` strips the planes and GND pins before routing (`-inc` and `--router.ignore_net_classes` did not help).
  - The second pass writes its own `-pass2` files; routing back into the same `.ses` emptied it.
  - The router echoes the fan-out vias back in the `.ses`. `import_ses` used to drop echoes by rounding to 0.01 mm, so a via at y 3.474999 and its echo at 3.4750 ended up stacked (DRC `holes_co_located`). It now matches within `VIA_ECHO_TOL_MM` (0.01 mm).
  - `remove_dangling` sometimes misses a stub on the in-memory board that the reloaded board shows as dangling. `main()` therefore reloads the chosen board and runs `tidy()` (remove dangling tracks, fill, bridge islands) before DRC.
- **KiCad 10 Python, more:** `fp.Flip` before `board.Add` segfaults (add first, then flip). Delete tracks with `board.Delete(t)`; `board.Remove` through SWIG left `GetTracks()` broken.
- **KiKit annotation tabs:** a `kikit:Tab` footprint outside the board's bounding box + 1 mm is silently ignored (the notch raises the bbox bottom to −16.8 mm), and a tab that reaches no partition line is silently dropped. The origin must be outside the rim across the whole tab width. KiKit reads the `.kicad_pro` next to its input, so the temporary tabbed board is written beside the real one with a copy of its project and deleted afterwards.
- **PowerShell 5.1 `Set-Content -Encoding utf8` writes a BOM.** Edit sources with the Edit tool or Python instead. Wildcard `Remove-Item` is blocked by the harness.
- Long jobs (`gen_pcb.py` 1–5 min per attempt, up to 4 attempts; full sim ~5 min) run fine in the background; `gen_pcb.py` overwrites the board, so back it up if you need the old one.
- st.com and Mouser block downloads from this machine. Richard's copy of the datasheet (DocID032891 Rev 1) is at `C:\Users\rixar\SynologyDrive\Downloads\DS_lsm6dso32.pdf`; no PDF renderer is installed, so extract text with `pypdf` (installed in `C:\zp\.venv`).

## 8. Open items (blocking manufacture)

1. **Buy the cells:** plain LIR1254 (no tabs) from a maker with a datasheet; check the 4.20 V charge voltage and that 21 mA is within its standard charge current.
2. **Cell contacts:** choose or make the + spring and − strap (prototype: 0.15 × 2 mm nickel strip, as modelled in `mechanical/blender/cellconn.py`), and check them with the real cell.
3. **Bolt:** the Avalon bolt, longer by the puck height plus the sealing washer (about 12 mm); check the thread engagement in the riser.
4. **Real part masses** into `layout.json` (cell 1.9 g, connector 0.6 g, module 0.6 g are estimates; u-blox gives no BMD-340 mass), then re-run `puck.py` for the mass report.
5. **Back up the OTA signing key** (see §7). Losing it means updates only by cable; leaking it lets anyone who can pair install firmware.
6. **Richard's call:** R5/R6 (external I2C pull-up pads) were removed for routing space; put them back as DNP pads if he wants that fallback.
(Done in v0.2: ERC runs in the flow; the LSM6DSO32 pinout, internal pin states and register values were checked against datasheet DocID032891 Rev 1; magnetic connector chosen (Samzo, Electrokit) with its drawing; cell pads made variant-independent; Varta's PCM and swell-space requirements met.)
(Done in rev A: flat plate + centre bolt, LIR1254 contacts and 21 mA charge, 2-layer re-route with 0 DRC, firmware rebuilt for the I2C swap, panel tabs moved off the notch.)

## 9. Known limitations / risks

- **Thresholds are unvalidated on real bows.** Tuning from logged EVENT data is the #1 on-device task (`docs/VALIDATION.md` §3). Captures now give the raw signals for it.
- **Battery life ≈ 3.5–5 weeks at 2 h/day is an estimate** (45–65 mAh; gyro + accel 0.55 mA whenever ACTIVE; idle ~17 µA estimated). Measure with JP1 cut. Richard chose the single LIR1254 knowing this; do not add gyro duty-cycling unless he asks. A further idle saving would be the accelerometer's ultra-low-power mode (datasheet §6.2.1; check wake-up support first).
- **IMU X/Y axes vs the bow are unconfirmed**; +Z is out of the cap. First capture on hardware fixes the mapping (VALIDATION §2).
- **Hard grab off the stand and stand knocks** are the false-positive sources in simulation.
- **Load-bearing but unverified assumptions:** nRF52840 VBUS wake from System OFF; MDBT50Q LFXO (Raytac: not fitted by default; we use the RC oscillator); BLE range through the PC cap next to the aluminium plate; I2C at 400 kHz on the nRF's internal pull-ups (~13 kΩ, estimated 130–200 ns rise on this short bus; there are no pads for external ones).
- **REG1 DC/DC:** Raytac's spec (MDBT50Q-1MV2 ver. K, §8) says the REG1 DC/DC inductor is inside the module, so REG1's DC/DC mode could be enabled in firmware to save active current. Not done yet (D5 kept LDO mode while this was unconfirmed).
- **VBUS detect and charger headroom on a weak supply:** the nRF52840 detects VBUS rising at 3.4–4.3 V and drops it only below 3.0–3.9 V. Detection happens at plug-in, before the MCP73831 starts (5 ms start delay), when D1 (BAT54J, max 320 mV at 1 mA) carries only a few hundred µA: from a 4.75 V supply VBUS reaches ~4.43 V, 0.13 V above the worst-case threshold. While charging (21 mA, D1 max ~0.46 V) VIN falls to ~4.29 V, far above the removal threshold. The tighter limit is the charger: MCP73831 accuracy is specified from VDD ≥ 5.2 V, so on a 4.75 V supply the charge may end slightly below 4.20 V. Use a 5.0–5.25 V charger; a lower-drop D1 would add margin.
- **Sustained overvoltage:** D3 and R9/C9 handle ESD and hot-plug spikes, not a 9–12 V adapter; that would need an overvoltage switch on VIN.
- **Stock (JLCPCB assembly, 2026-09-29):** every BOM line is in stock for 20 boards after two swaps: U1 MDBT50Q-1MV2 (0 in stock) → u-blox BMD-340-A-R C5456944 (393), and D4 → C22470306, a second listing of the same XL-1005SYGC (C965794 had 0). Thinnest: LSM6DSO32TR (44; the LSM6DSO32XTR C3663738 is the fallback) and DMN63D8LDW (244). LCSC's own shop stock differs and is not what PCBA uses. JLCPCB's search API (`selectSmtComponentList`) returns live `stockCount`.
- **BMD-340 type:** confirm with JLCPCB that C5456944 is BMD-340-A-R-10; -00 (nRF52840 rev 1) has an unusable high-voltage mode (README D14).
- **Captures and rejected candidates are RAM-only** (4 captures, 128 rejects). Accepted shots persist (~900 in flash).
- **Pairing window:** anyone within range during the 60 s after the charger is attached can pair. Acceptable because attaching the charger needs the puck in hand.
- **LED_BLINK (find-my-puck) blocks the main loop for ~1 s**, longer than the IMU FIFO (~0.5 s). Using it while shooting can drop a shot (FIFO overrun restarts the detector). Not fixed; it is meant for a puck lying around.
- **OTA is verified off-device only** (build, signatures, key guard). Upgrade, rollback-on-reset and rollback-on-hang must be tested on hardware (VALIDATION §2b). During the MCUboot swap (about 10–20 s) the puck is unavailable.
- **Firmware reads the FIFO word by word** (52 I2C transactions per watermark). A burst read would cut ACTIVE current; verify the LSM6DSO32 FIFO address roll-over first.
- **Material constraint:** PC cap + liquid threadlocker = stress cracking. Use nylon-patch screws only.
- **Mouse-bite tabs:** cut them with flush cutters, never snap (U1 is about 2 mm from the top tab, the 0603 C2 about 2.7 mm from the left one), then sand the nubs flush; the board has 0.5 mm radial clearance in the cap cavity.
- **JLCPCB part rotations** come from KiCad orientations; JLCPCB's conventions differ for some packages. Check every part in their placement preview (especially U1, U2, U4, Q1, Q2, D1–D4).
- **Cell in the PCB notch:** the Kapton under the PCB must cover the plate under the cell (it insulates the + can from the plate). The cell is held only by the + spring, the − strap and the foam pad: shot shocks (8–40 g) must not lift a contact, or the puck browns out. Check for resets over a shooting session (VALIDATION §2). The contacts are not a bought part yet.
- **Bolt head near the antenna:** the steel button head (Ø16.7) and its Ø14 sealing washer sit on the sleeve, about 4 mm above the PCB and 2.2 mm above the module, and overlap the inner corner of the antenna zone (x ≥ 4.1, y ≥ 4.3) out to about 7 mm from the axis in plan view. Check BLE range on the bow (VALIDATION §2); a smaller washer or a plastic one would help. No cap screw is in or near the antenna zone any more (H1 moved to (8.1, −5.0)); it now sits about 5 mm from J1's lower magnet, so prefer A2 stainless screws (barely magnetic).
- **Bolt tube:** it carries the whole clamp load (a 5/16 button head at its maximum torque gives ~9 kN, ~400 MPa on the tube end), so it must be cold-drawn tube: annealed 304 yields at ~205 MPa. It stands 0.5 mm from the cell's + can with no FR-4 between them: keep the Kapton tape on it. A touch is harmless on its own (the tube, plate and bow are not connected to the circuit).

## 10. Likely next requests and where to start

| Request | Start at |
|---|---|
| Order prototypes | README "Fabrication & assembly": `kicad/fab/panel/shotpuck-panel-gerbers.zip` + `kicad/fab/jlc/*.csv`; cell contacts and J1 hand-soldered, cell dropped in |
| Flutter integration | `docs/PROTOCOL.md`: *Pairing* (add-a-puck flow), "Recommended app sync", CAPTURE; `device_id` = DIS serial; upsert events and captures keyed by `(device_id, seq)`; EVENT payloads hold several events |
| Bow-movement metrics (cant, steadiness, rotation) | Compute in the app from captures first; move to firmware only once the useful metrics are known |
| Tune thresholds from real logs | Add the captured feature vectors (or raw captures) to `simulate.py` as a replay set; adjust `sd_default_config()`; re-run `--stress`; rebuild the hex |
| New firmware release | Bump `firmware/VERSION`, sysbuild with the signing key, re-merge `shotpuck_full.hex`, copy `zephyr.signed.bin` to `shotpuck_update.bin`, verify with `imgtool verify` |
| Cheaper/smaller variant | Mention the 601015 pouch cell (D6 alternative: closed-ring board, ~9.9 mm) and the chip-down nRF option; both need full mechanical + PCB regeneration |

Always finish by rebuilding everything affected, re-running the relevant checks (sim + unit tests PASS, netlist PASS, ERC 0 errors, DRC 0/0, sysbuild 0 compiler warnings, signed image verifies), and delivering the result (the repo on GitHub; a zip only if Richard asks).
