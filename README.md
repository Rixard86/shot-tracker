# ShotPuck: automatic arrow counter for Avalon barebow riser weights

A Ø40 × 9.9 mm puck that bolts onto the free face of an Avalon riser weight (the weight's own 5/16-24 bolt, one size longer, goes through its centre), detects each shot with a 6-axis IMU, records the bow's motion around it (accelerometer + gyroscope, 1.0 s before to 0.5 s after release), and reports both over BLE to the scoring app.

| | |
|---|---|
| Size / mass | Ø40 mm (= the Avalon barebow weight's diameter), 9.90 mm above the weight face (2.0 mm flat aluminium plate; the PCB on 3 mm standoffs, the cell in a notch in the PCB), 16.1 g, centre of mass 1.1 mm off axis (no trim weight); 1.2 mm free space above the cell |
| Radio / MCU | u-blox BMD-340 (nRF52840, 1 MB flash / 256 KB RAM, PCB antenna), BLE 5 |
| Sensor | ST LSM6DSO32 IMU: 416 Hz, ±32 g accelerometer + ±2000 dps gyroscope while active; accelerometer only, 12.5 Hz wake-on-motion, while idle |
| Battery | LIR1254 Li-ion coin, 45–65 mAh, plain (no tabs) on spring contacts, protected by TI BQ29700 + DMN2004DWK; magnetic charging via Samzo PR5L4015 (21 mA, temperature-gated) |
| Firmware | Zephyr 4.1, custom board `shotpuck/nrf52840`, MCUboot 28 KB + signed app 196 KB (452 KB slot) / 88 KB RAM (44 KB of it capture buffers) |
| Updates | Over the air: MCUboot with ECDSA-P256 signed images and automatic rollback, MCUmgr/SMP over BLE for paired phones, 10 s watchdog |
| Security / sync | Pairing required (LE Secure Connections); new phones only within 60 s of attaching the charger. Last ~900 shots kept in flash for replay. |

## Repository

```
layout.json                 shared geometry: CAD and PCB both read it
firmware/                   Zephyr app (src/, boards/shotpuck/, prj.conf, debug.conf)
firmware/sim/               Monte Carlo validation of src/shot_detect.c, unit tests of capture.c and evlog.c (+ results.txt)
firmware/VERSION            firmware version (signed into the OTA image, shown in DIS and STATUS)
firmware/sysbuild.*         MCUboot + signing setup (refuses MCUboot's public dev key)
firmware/shotpuck_full.hex  MCUboot + signed app, for the first flash via the SWD test pads
firmware/shotpuck_update.bin  signed app image for OTA updates
hardware/design.py          the circuit (single source of truth)
hardware/gen_sch.py         -> kicad/shotpuck.kicad_sch
hardware/check_netlist.py   KiCad netlist vs design.py
hardware/gen_pcb.py         -> kicad/shotpuck.kicad_pcb (place, keep-outs, route, DRC)
hardware/board_shape.py     board outline, cell notch, bolt hole and antenna zone (shapely, from layout.json)
hardware/islands.py         post-fill GND island bridging used by gen_pcb.py
hardware/gen_bom.py         -> kicad/fab/shotpuck-bom.csv
hardware/gen_panel.py       -> kicad/panel/shotpuck-panel.kicad_pcb (2x2 KiKit panel for JLCPCB)
hardware/gen_jlc.py         -> kicad/fab/jlc/ (JLCPCB assembly BOM + CPL with LCSC part numbers)
hardware/bmd340.py          u-blox BMD-340 footprint, 3D model placement and schematic symbol (from its data sheet)
hardware/kicad/             KiCad project, ShotPuck.pretty/.kicad_sym/.3dshapes, schematic PDF, fab/ (gerbers, drill, pos, BOM)
mechanical/puck.py          CadQuery model + mass/balance -> out/ (STEP, STL, mass_report.txt);
                            geometry.py, puck_parts.py, puck_report.py hold its constants, parts and output
mechanical/blender/         Blender scene + exploded-view assembly animation -> out/shotpuck_assembly.mp4/.blend
docs/PROTOCOL.md            BLE GATT protocol for the app
docs/VALIDATION.md          what is proven, and the on-device checklist
```

## Decisions (change any, then re-run the generators)

| # | Decision | Why | Alternative |
|---|---|---|---|
| D1 | **Cell beside the module, no trim weight** | Module at the top, bolt in the middle, cell in a notch at the bottom. The centre of mass is 1.1 mm off axis (≈ 0.01 mm on a 2 kg bow); Richard dropped the optional brass pin when the PCB went up on standoffs | Two LIR1254 side by side (studied: they cut the board in two); a brass trim pin |
| D2 | **Flat 6061 plate (2.0 mm) + polycarbonate cap, bolted on through a steel sleeve** | Richard: the v0.2 stud could not be fitted to the bow (it needs a bolt from above), and a flat plate is cheaper (laser or waterjet, no turning). The sleeve carries the bolt's clamp load, so the PC cap is never in the clamp path | Turned 7075 base with an integral stud (v0.2) |
| D3 | **nRF52840 in high-voltage mode** (cell → VDDH, REG0 = 3.0 V) | No regulator, no battery divider (internal VDDH/5), fewer leaks | External LDO |
| D4 | **MCP73831 + firmware temperature gate** (nRF die temperature, <1 °C / >44 °C) | MCP73831 programs down to 15 mA; the TS-pin chargers found start at 100 mA. Default is *charging allowed* if the MCU is dead, so a flat cell always recovers. | Charger with an NTC input and a thermistor at the cell |
| D5 | **32 kHz from internal RC, REG0/REG1 in LDO mode** | The BMD-340 has no 32.768 kHz crystal; LDO is the tested default | Enable DC/DC on REG0/REG1 (the BMD-340 has the inductors; about half the radio current) |
| D6 | **Plain LIR1254 (no tabs) on contacts:** a + spring against the can side and a − strap over the top cap, soldered to two SMD pads left of the notch; the cap's foam pad clamps the cell and the strap | Richard's choice: no tab variant to source, no soldering on the cell, swappable | Tabbed LIR1254 soldered to pads; 601015 pouch (closed-ring board, ~9.9 mm) |
| D7 | **Pairing required; new pairings only within 60 s of attaching the charger** (LE Secure Connections Just Works, up to 4 phones, FORGET_BONDS to reset) | Several pucks and shooters at one club: other apps must not read, reconfigure or reset your puck. The charger gesture proves physical possession. | Open access (v1), or a PIN shown by LED blinks |
| D8 | **Detector thresholds are defaults, tunable over BLE**; every candidate is reported | Real release-shock data is unknown until you shoot | – |
| D9 | IMU rotated 270°, reference designators on F.Fab only | Routability; board too dense for silkscreen text | – |
| D10 | **LSM6DSO32 IMU** replaces the LIS2DE12 accelerometer. Pinout, pin states and register values checked against datasheet DocID032891 Rev 1; I3C disabled at init as the datasheet recommends | Gyroscope for bow rotation; ±32 g clips the release shock less; 16-bit data; same I2C bus and INT1 pin | LSM6DSV16X/32X (on-chip sensor fusion, ±4000 dps) |
| D11 | **Raw 6-axis capture per accepted shot** (−1.0 s to +0.5 s, 624 samples, 7.5 KB), analysed in the app | Which bow-movement metrics matter is unknown until real shots are recorded | On-device metrics (cant, steadiness, post-release rotation) in the EVENT |
| D12 | **Identity from the chip's factory ID**: name `ShotPuck-XXXX`, DIS serial number = `device_id` | Tell pucks apart at a club; one stable ID across phones for PowerSync | App-assigned IDs |
| D13 | **Accepted shots in a 32 KB flash ring** (~900 shots), rejected candidates in RAM; app-driven REPLAY, sent paced in the background | Shots survive dropouts and resets without per-phone sync state on the puck | Device push with ACK (rejected: shared state, first phone drains the backlog); captures in flash too |
| D14 | **u-blox BMD-340-A-R (nRF52840) instead of the Raytac MDBT50Q-1MV2**: 15.0 × 10.2 × 1.9 mm LGA-68, PCB antenna, VCCH input for high-voltage mode, FCC/CE/Bluetooth certified. Footprint `ShotPuck:u-blox_BMD-340` from data sheet UBX-19033353 (Figure 12), checked pad for pad against two built open-source footprints | Richard: replace parts JLCPCB does not stock. On 2026-09-29 JLCPCB had 0 MDBT50Q-1MV2 and 227–393 BMD-340 (C5456944, $8.73 vs $18.93). nRF52840 kept for the two OTA slots (2 × 460 KB). **Only type BMD-340-A-R-10 is usable:** -00 (nRF52840 rev 1) has a non-functional high-voltage mode, so confirm the type with JLCPCB before ordering | MDBT50Q-1MV2 via global sourcing or consignment; u-blox NINA-B306 ($20, 59 in stock) |
| D16 | **Cell protection (PCM): TI BQ29700 + Diodes DMN2004DWK** in the cell's negative lead (OVP 4.275 V, UVP 2.80 V, ±0.1 A over-current, short-circuit) | LIR cells have no protection of their own; kept from the CP1654 design (BQ29700 was on Varta's list). Costs 4 µA standby | – |
| D17 | **1.2 mm free space above the cell** (6.6 mm from the Kapton to the ceiling) | No swell spec for LIR cells; set so the connector is flush with the cap top (D25). The foam pad fills it | Varta's 1.5 mm (puck 0.3 mm taller or the connector recessed) |
| D19 | **Board Ø36 mm; 4× M2×8 cheese head (Torx) screws from above: three at r = 16.5 mm (160°/226°/305°) and H1 at (9.2, 1.2)**, through the cap, PCB and standoffs into the tapped plate | Puck Ø40 mm is fixed by the Avalon weight; 36 leaves 0.5 mm fit clearance. The screws sit clear of the module, the cell notch and the connector, and none is in the antenna zone (H1's copper keep-out ends 0.9 mm below it, its standoff 1.1 mm). H1 sits right of the tube where C1/D1/D3 used to be, and H4 (226°) holds the left arm (D29). The 1.3 mm head lets an M2×8 end 0.1 mm inside the plate (a socket head would need the non-standard M2×7) | – |
| D20 | **Cell in a notch in the PCB, open to the rim** (Ø13.0 mm round end), on the Kapton over the plate | Saves the 0.8 mm board in height. Richard preferred the notch to a closed hole | Closed hole (holds the cell all round, leaves a thin strip at the rim) |
| D18 | **Samzo PR5L4015-2P-C-F magnetic receptacle** (Electrokit, 18.50 SEK; cable side PR5L5015-2P-C-H, 28 SEK) on the PCB, flush with the cap top (D25) | Cheapest in-stock pair in Sweden with a real drawing; the N/S magnets make it mate one way only | AliExpress generic pairs |
| D15 | **OTA = MCUboot swap-using-move + MCUmgr SMP over BLE**, ECDSA-P256 signed, encrypted link (paired phones) only. A new image runs on trial and confirms itself after 10 s of healthy running; a 10 s hardware watchdog turns a hang into a reset, and MCUboot then rolls back | Standard Nordic/Zephyr path; the Flutter app can use Nordic's `mcumgr_flutter` | Firmware-loader mode (single slot); no rollback |
| D21 | **One steel tube from the plate through the PCB to the cap top** carries the bolt load; the PCB hole (Ø10.4: 0.2 mm clearance covers JLCPCB's ±0.2 mm outline tolerance) is merged into the cell notch | Richard: the bolt must be fully supported, even over-tightened. With the PCB or a plastic tube in the clamp path, FR-4, Kapton or PC creep and the weight stack works loose. The merged hole leaves the right half (connector, TVS, D1, C1) joined across the top only; VIN is the only signal that crosses, through a ~0.5 mm gap. No stock insulating tube fits (glass-epoxy 10 × 8 mm grips a 5/16 bolt, plastics creep), so it is steel with Kapton tape towards the cell | Sleeve on the PCB with a spacer below (PCB in the clamp path); a tube moulded into the PC cap |
| D22 | **On-board PCB antenna** (BMD-340), antenna end at +x, copper-free to the rim (from 4.4 mm inside the module's end) | Richard's choice over moving the antenna out into the cap | Module with a u.FL connector and an antenna in the cap |
| D23 | **SWD on 4 bottom test pads** (VDD, SWDIO, SWDCLK, GND, 2.54 mm pitch, under the module) for a pogo-pin jig | Frees top-side space for 2-layer routing; the SWD escape vias are pre-placed | Tag-Connect TC2030-CTX-NL (v0.2) |
| D24 | **I2C on internal pull-ups only; SCL = P0.07, SDA = P0.05** (BMD-340 pads 23 and 21, facing the IMU) | The 4.7k DNP pads (R5/R6) were never fitted and cost routing space; the swap stops the IMU lines crossing on 2 layers | Keep R5/R6 as DNP pads for a stronger pull-up |
| D25 | **PCB on 3 mm standoffs, magnetic connector flush with the cap top; cap 0.4 mm lower (9.90 mm)** | Richard: the plug should meet the receptacle at the surface. The old 3.4 mm well needed the plug head to fit the chimney and would collect dirt. The cell sets the ceiling, so raising the board costs no height, and it moves the antenna 3 mm further from the plate | Board on the plate with a 3.4 mm chimney well down to the connector |
| D26 | **Button head bolt** (5/16-24, head Ø16.7 × 4.4 mm) with a bonded sealing washer | Richard: lower head than a socket head (7.9 mm). The sleeve stands 0.1 mm proud so the head clamps steel, not the PC cap | Socket head cap screw |
| D27 | **Charge-input protection: SMF5.0CA bidirectional TVS (D3) at the connector + R9 1k / C9 100n in front of the nRF VBUS pin** | The flush contacts are touched constantly (ESD), the magnetic plug makes and breaks as it snaps on (hot-plug spikes), and a user-wired cable can be reversed, so the TVS must be bidirectional before D1. The SMF5.0CA starts clamping at 6.4–7.0 V (200 W) against the MCP73831's 7 V absolute maximum. No 5 V TVS can hold the nRF52840 VBUS pin under its 5.8 V absolute maximum; it is used only to detect the charger (~24 µA), so the RC limits current into it and smooths spikes | PESD5V0S1BB (a signal-line ESD diode, clamps only from 5.5–9.5 V); unidirectional flat-clamp TVS after D1 (TI TVS0500); an overvoltage switch (needed only if 9–12 V adapters are a risk) |
| D28 | **Routing aids for 2 layers (BMD-340):** module pins chosen so its second pad column escapes through the outer column's gaps straight at the IMU; locked before Freerouting: the whole VIN path (under the module's top edge and down its left side), a 0.15 mm GND track through the passage for the right half, SDA/SCL/ACC_INT1 to the IMU, LED_IO to R4, the SWD, CHG_STAT and VDD vias under the module, and GND links between neighbouring module/IMU GND pads; since D29 also PCM_COUT (U4.2 → Q2.2 straight across), PCM_DOUT (over Q2's top to Q2.5), a via for Q2.1 (GND, inside that loop) and CHG_INH Q1.5 → R3.1 | With the bolt tube through the board, the right half joins the rest only through a ~0.5 mm passage beside the antenna zone, and the LGA module's inner pads need planned escapes. The board routes with 0 DRC / 0 unconnected (after D29: attempt 2 of the third full run; the IMU/charger column on the left is the tight spot, so re-run `gen_pcb.py` if it ends with 1–2 unconnected) | 4 layers |
| D29 | **Shot shock: 4th screw H4 on the left arm, H1 moved up beside the tube; BMD-340 and C2 corner-staked** (Richard) | The slot for the cell and the tube makes the board a horseshoe. Its left arm (PCM, both cell contact pads) had no screw below H2 and the top half none right of the module. A plate model of the board (Kirchhoff plate FE, 100 g along the puck axis, component masses, module 1–5× stiffer than the board) gives, at 0.8 mm: first mode 1.1–1.2 kHz → 1.7–2.0 kHz; worst strain 320–340 → 160–180 µε (bare board next to a screw; ≤ 70 µε at any part); the − cell pad moved 13–15 µm against the cell per 100 g, now ~0. At 1.2 mm with 4 screws: 2.7–3.2 kHz, ~100 µε | Foam pad under the board (damping, no layout change); thicker PCB alone (1.2 mm, 3 screws: 1.7–1.8 kHz) |

## Needs your input before ordering

1. **Magnetic connector:** done: Samzo PR5L4015-2P-C-F from Electrokit (D18). For the charging cable, solder a 5 V lead to the male part (PR5L5015-2P-C-H) and check with a multimeter which pin meets the square pad (pin 1, +). A reversed lead cannot damage the puck (D1 blocks it).
2. **LIR1254:** buy a plain cell (no tabs) from a maker with a datasheet. Check its charge voltage (ours 4.20 V) and that 21 mA (R1 = 47k) is within its standard charge current (usually 0.5 C).
3. **Cell contacts:** not a bought part yet. For prototypes, form the + spring and the − strap from 0.15 × 2 mm nickel strip (as in the animation) and put Kapton tape where the strap passes the can edge.
4. **Bolt:** a 5/16-24 button head socket cap screw, longer than the original Avalon bolt by the puck height plus the sealing washer (about 11.5 mm). Check that length is sold in 5/16-24 and the thread engagement in the riser; don't over-tighten (3/16 in hex).
5. **Component masses:** weigh the real parts, put them in `layout.json`, and re-run `puck.py` for the mass report.
6. **Back up the OTA signing key** `%USERPROFILE%\.shotpuck\ota-signing-key.pem` (password manager or offline copy). Pucks accept only images signed with it: if it is lost, updates need a cable (SWD); if it leaks, anyone who can pair could install their firmware. It is never committed (`*.pem` is git-ignored).
7. **Module type:** before ordering, ask JLCPCB support to confirm that their BMD-340-A-R stock (C5456944) is type **BMD-340-A-R-10**. Types -00 and -01 are obsolete, and -00's nRF52840 rev 1 silicon makes high-voltage mode unusable (D14).

## Simulation results (`firmware/sim/results.txt`, seed 2026)

| Set | Shots | Recall | Precision | Notes |
|---|---|---|---|---|
| Nominal sessions | 437 | 99.1 % | 99.8 % | walking knocks, set-downs, stand knocks, nock taps, drops, let-downs (1 FP: stand knock) |
| Hard cases | 479 | 99.6 % | 99.4 % | + taps while aiming, hard grabs off the stand (2 grabs, 1 stand knock) |
| Weak bow (info) | 253 | 80.2 % | 99.5 % | 4–12 g shock, 5–30° rotation: **thresholds must be tuned on real shots** |

The detector needs three things together: a high-frequency shock (6 g sample-to-sample), ≥ 0.5 s of stillness before it (aiming), and ≥ 250 mg of low-frequency bow motion 0.1–0.6 s after it (bow jump into the sling). See `firmware/src/shot_detect.h`. It uses the accelerometer only; the gyroscope feeds the capture.

## Battery life (estimate, verify via JP1)

The idle draw is roughly 17 µA (LSM6DSO32 accelerometer in low-power mode at 12.5 Hz, about 10 µA by interpolating the datasheet's 4.5 µA at 1.6 Hz and 26 µA at 52 Hz; BQ29700 protection 4 µA; nRF System ON + RC). While active it is roughly 0.7–0.8 mA: the gyroscope alone is about 0.55 mA, plus 416 Hz FIFO reads and the BLE link. At **2 h of training per day** that is ≈ 1.9 mAh/day, so roughly **3.5–5 weeks per charge** from a 45–65 mAh LIR1254 (a little less, as the 3.3 V cut-off leaves some capacity unused). A full charge at 21 mA takes about 2.5–3.5 h. The low-battery cut-off (3.3 V) puts the puck into System OFF until the charger is attached.

## Build & flash

Native Windows toolchain (what v0.2 was built and verified with):

| Tool | Where | Used for |
|---|---|---|
| KiCad 10.0 | `C:\Program Files\KiCad\10.0` (its `python.exe` has `pcbnew`) | schematic, PCB, ERC, DRC, fab exports |
| Freerouting 2.1.0 + Java 21 | `%USERPROFILE%\tools\freerouting-2.1.0.jar`, `%USERPROFILE%\tools\jre21` (or set `FREEROUTING_JAR`, `JAVA`) | PCB routing |
| Zephyr v4.1.0 workspace + SDK 0.17.0 | `C:\zp` (west, modules `cmsis`, `cmsis_6`, `hal_nordic`, `segger`, `hal_st`, `mbedtls` for pairing crypto, `mcuboot` + `zcbor` for OTA) | firmware |
| CMake 3.31 + Ninja | `%USERPROFILE%\tools\cmake3`, `%USERPROFILE%\tools\ninja` (Zephyr 4.1 fails with CMake 4.x) | firmware |
| w64devkit gcc | `%USERPROFILE%\tools\w64devkit` (set `CC` to its `gcc.exe`) | simulation, capture tests |

**Firmware** (Zephyr 4.1 sysbuild: MCUboot + signed app in one build):

```
set ZEPHYR_BASE=C:\zp\zephyr
set ZEPHYR_SDK_INSTALL_DIR=C:\zp\zephyr-sdk-0.17.0
set PATH=%USERPROFILE%\tools\cmake3\bin;%USERPROFILE%\tools\ninja;C:\zp\.venv\Scripts;%PATH%
cd C:\zp
west build --sysbuild -p always -b shotpuck/nrf52840 <repo>/firmware -d build\sb -- ^
  -DBOARD_ROOT=<repo>/firmware ^
  -DSB_CONFIG_BOOT_SIGNATURE_KEY_FILE=\"C:/Users/<you>/.shotpuck/ota-signing-key.pem\"
python zephyr\scripts\build\mergehex.py -o shotpuck_full.hex build\sb\mcuboot\zephyr\zephyr.hex build\sb\firmware\zephyr\zephyr.signed.hex
```

- The key path needs the escaped quotes (Kconfig string). Without the private key the build stops on purpose instead of signing with MCUboot's public dev key.
- **First flash** (bootloader + app) by cable: `shotpuck_full.hex` with J-Link/nrfjprog through the 4 SWD test pads on the bottom (VDD, SWDIO, SWDCLK, GND at 2.54 mm; pogo-pin jig or pogo probes), before the board goes onto the plate.
- **Updates over the air:** `build\sb\firmware\zephyr\zephyr.signed.bin` (copied to `firmware/shotpuck_update.bin`), sent from a paired phone (your app with `mcumgr_flutter`, or Nordic's nRF Connect Device Manager). Bump `firmware/VERSION` for every release.
- RTT logging build: add `-DEXTRA_CONF_FILE=debug.conf`. It fits the slot and can be installed over the air too.
- The first boot programs UICR REGOUT0 = 3.0 V and resets once.
- Generate a new signing key only for a new product line: `python C:\zp\bootloader\mcuboot\scripts\imgtool.py keygen -k ota-signing-key.pem -t ecdsa-p256`. Pucks flashed with one key never accept images signed with another.

**Simulation + unit tests:** `set CC=%USERPROFILE%\tools\w64devkit\bin\gcc.exe` then `python firmware/sim/simulate.py --sessions 36 --stress --seed 2026` (needs numpy, scipy). Must print `CAPTURE TESTS: PASS`, `EVLOG TESTS: PASS` and `RESULT: PASS`.

**Hardware** (run with KiCad 10's Python, from `hardware/`):

```
"C:\Program Files\KiCad\10.0\bin\python.exe" gen_sch.py
"C:\Program Files\KiCad\10.0\bin\python.exe" check_netlist.py      # must print PASS
kicad-cli sch erc --severity-all -o kicad\erc.rpt kicad\shotpuck.kicad_sch
"C:\Program Files\KiCad\10.0\bin\python.exe" gen_pcb.py --no-route  # placement only, then DRC
"C:\Program Files\KiCad\10.0\bin\python.exe" global_route.py       # routability estimate: overflow + hotspots
"C:\Program Files\KiCad\10.0\bin\python.exe" gen_pcb.py --quick     # one-attempt trial route (~5 min)
"C:\Program Files\KiCad\10.0\bin\python.exe" gen_pcb.py            # place, route, bridge GND islands, DRC
"C:\Program Files\KiCad\10.0\bin\python.exe" gen_bom.py
kicad-cli pcb export gerbers / drill / pos                         # into kicad\fab\
```

Freerouting routes the signals only (GND comes from the pours, fan-out and stitching vias) and is not deterministic. `gen_pcb.py` re-routes up to 4 times until nothing is unconnected; if it still does not end with `0 DRC violations` and `0 unconnected pads`, run it again. ERC: 0 errors; the 2 warnings are the intentional straps of LSM6DSO32 SA0 and SDx to GND.

**Mechanical:** `C:\zp\.cq\Scripts\python.exe mechanical/puck.py` (CadQuery 2.8 in a Python 3.12 venv; CadQuery has no Python 3.14 wheels). It writes the mass and balance report; there is no trim weight.

**Assembly animation (Blender 3.1+):** `"C:\Program Files\Blender Foundation\Blender 3.1\blender.exe" -b -P mechanical/blender/render_assembly.py` (after `puck.py`; it exports the populated board from KiCad as GLB itself). It renders an exploded view that assembles into the puck (Cycles on the GPU, 1080p, 13 s) to `mechanical/out/frames/` and encodes `mechanical/out/shotpuck_assembly.mp4`; the scene is saved as `shotpuck_assembly.blend`. Add `-- --preview` for 5 half-size stills, `-- --encode` to re-encode existing frames. Existing frames are skipped, so delete `mechanical/out/frames/` after a design change. The cell and its contacts, the connector, standoffs, M2 screws, button head bolt and sealing washer, gasket, Kapton and foam are modelled from `layout.json`; the board parts use KiCad's 3D models.

**Panel + JLCPCB assembly files** (after `gen_pcb.py`, with KiCad 10's Python and KiKit: `python.exe -m pip install --user kikit`):

```
"C:\Program Files\KiCad\10.0\bin\python.exe" gen_panel.py      # 2x2 in a frame, 4 tabs per board, mouse bites, fiducials, tooling holes
"C:\Program Files\KiCad\10.0\bin\python.exe" gen_jlc.py        # kicad/fab/jlc/shotpuck-panel-bom.csv + -cpl.csv
kicad-cli pcb export gerbers / drill for kicad/panel/shotpuck-panel.kicad_pcb
```

## Fabrication & assembly

- **PCB:** 2 layers, 0.8 mm FR-4, ENIG, Ø36 mm (0.5 mm clearance per side in the Ø37 mm cap cavity; puck Ø40 mm = the Avalon weight) with a notch for the cell (Ø13.0 mm round end, open to the rim) merged with a Ø10.4 mm hole for the bolt tube (all part of the outline), 0.15 mm track / 0.127 mm space, vias 0.55 mm pad / 0.3 mm hole (standard at JLCPCB/PCBWay, no small-hole surcharge).
- **JLCPCB order (prototype):** upload `kicad/fab/panel/shotpuck-panel-gerbers.zip` as a 2×2 panel ("panel by customer", 4 boards per panel, 91.1 × 88.7 mm; the top row is turned 180° so the tabs miss the cell notch and the antenna), order 5 panels = 20 boards, 0.8 mm, ENIG. For assembly (top side), upload `kicad/fab/jlc/shotpuck-panel-bom.csv` and `-cpl.csv` and check every part's rotation in the placement preview. Not assembled by JLCPCB: the cell BT1 and its contacts, the magnetic connector J1 (hand-solder), JP1 (bridged solder jumper, copper only), J2 (SWD test pads). The module needs JLCPCB's X-ray inspection. Cut the tabs with flush cutters instead of snapping them (U1 sits about 2 mm and the 0603 capacitor C2 about 2.7 mm from a tab, and bending there can crack joints or C2), then sand the nubs flush. The BQ29700 may start with its discharge FET off when a cell is first fitted (TI SLUSBU9I §8.4.1): attach the charger once to wake it.
- **Plate:** 6061-T6, 2.0 mm, laser or waterjet cut: Ø8.4 mm centre hole, 4× M2 tapped (tap drill Ø1.6) at (9.2, 1.2) and at r = 16.5 mm, 160°/226°/305° (`bosses.positions` in `layout.json`). Anodising is optional (mask the threads).
- **Cap:** polycarbonate, CNC-machined (translucent so the LED shows), with the M2 counterbores, the tube bore and the connector opening. The opening fits the connector's 12.5 mm top boss, and a lip under the cap top stops 0.1 mm above the connector's flange ends (RTV in the gap), so the cap, not the solder joints, takes the plug's pull (Samzo drawing GZ0254-P001). For prototypes, clear SLA resin. **No liquid threadlocker anywhere near PC** (it causes stress cracking): use nylon-patch screws.
- **Tube:** cold-drawn 304 stainless tube Ø10 × 0.8 mm (yield ≥ 500 MPa; annealed tube would yield when the bolt is over-tightened), 8.0 mm long (plate to 0.1 mm above the cap top), bonded into the cap. Kapton tape on the side facing the cell.
- **Standoffs:** 4 M2 round aluminium spacers, 3 mm long (stock length), OD 4 mm, ID 2.2–2.4 mm, at the screws.
- **Gasket:** die-cut 0.5 mm silicone ring under the cap wall (about 0.3 mm compressed). **Kapton:** 0.1 mm disc with holes for the tube (Ø10.4) and the 4 screws; it insulates the cell's + can from the plate.
- **Assembly order:**
  1. Bond the sleeve into the cap, 0.1 mm proud of the top.
  2. Program the board through the SWD pads on its underside.
  3. Hand-solder the magnetic connector J1 and the two cell contacts (+ spring, − strap). Corner-stake the BMD-340 (a small fillet of edge-bond epoxy at each of its 4 corners, board to module edge, clear of the antenna end's top) and the 0603 C2 (a dot at each end); cure as the adhesive's datasheet says. **No adhesive on or next to the IMU U2**: package stress shifts its offset.
  4. Lay the Kapton disc and the gasket ring on the plate. Set the 4 standoffs over the screw holes. Put the cell on the Kapton, + can down, then lower the PCB onto the spacers so the cell sits in the notch and the + spring presses on the can side. Fold the − strap over the top cap and put the foam pad on it.
  5. Put the cap on (its tube passes through the PCB onto the plate) and fit the 4 M2×8 cheese head screws from above (nylon patch).
  6. Before closing, put a small bead of RTV on the connector's flange ends (under the cap lip); after closing, seal the gap around the connector's top boss with RTV, flush with the top.
  7. On the bow: the button head bolt, with a bonded sealing washer under its head, goes through the sleeve and the weight into the riser.
- **Programming:** SWD only (no reset pin), which is fine for the nRF52.

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
