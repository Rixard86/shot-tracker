# ShotPuck: automatic arrow counter for Avalon barebow riser weights

A 40 × 11 mm puck that screws onto the free face of an Avalon riser weight (5/16-24), detects each shot with a 6-axis IMU, records the bow's motion around it (accelerometer + gyroscope, 1.0 s before to 0.5 s after release), and reports both over BLE to the scoring app.

| | |
|---|---|
| Size / mass | Ø40 mm (= the Avalon barebow weight's diameter), 10.45 mm above the weight face (+8 mm stud; the cell sits in a notch in the PCB), 19.8 g with the optional brass trim pins (COM on axis) or 18.0 g without (COM 0.9 mm off axis); 1.5 mm free space above the cell as Varta requires |
| Radio / MCU | Raytac MDBT50Q-1MV2 (nRF52840, 1 MB flash / 256 KB RAM), BLE 5 |
| Sensor | ST LSM6DSO32 IMU: 416 Hz, ±32 g accelerometer + ±2000 dps gyroscope while active; accelerometer only, 12.5 Hz wake-on-motion, while idle |
| Battery | Varta CP1654 A3 Li-ion coin, 120 mAh, protected by TI BQ29700 + DMN2004DWK (Varta requires a PCM); magnetic charging via Samzo PR5L4015 (50 mA, temperature-gated) |
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
firmware/shotpuck_full.hex  MCUboot + signed app, for the first flash via SWD / Tag-Connect
firmware/shotpuck_update.bin  signed app image for OTA updates
hardware/design.py          the circuit (single source of truth)
hardware/gen_sch.py         -> kicad/shotpuck.kicad_sch
hardware/check_netlist.py   KiCad netlist vs design.py
hardware/gen_pcb.py         -> kicad/shotpuck.kicad_pcb (place, keep-outs, route, DRC)
hardware/islands.py         post-fill GND island bridging used by gen_pcb.py
hardware/gen_bom.py         -> kicad/fab/shotpuck-bom.csv
hardware/gen_panel.py       -> kicad/panel/shotpuck-panel.kicad_pcb (2x2 KiKit panel for JLCPCB)
hardware/gen_jlc.py         -> kicad/fab/jlc/ (JLCPCB assembly BOM + CPL with LCSC part numbers)
hardware/kicad/             KiCad project, ShotPuck.pretty, schematic PDF, fab/ (gerbers, drill, pos, BOM)
mechanical/puck.py          CadQuery model + balance solver -> out/ (STEP, STL, mass_report.txt, trim.json)
docs/PROTOCOL.md            BLE GATT protocol for the app
docs/VALIDATION.md          what is proven, and the on-device checklist
```

## Decisions (change any, then re-run the generators)

| # | Decision | Why | Alternative |
|---|---|---|---|
| D1 | **Cell beside the module**, optionally balanced by 2 brass pins (Ø8 mm, 1.8 g) | 10.45 mm tall; a coaxial stack with the cell under the PCB would be ~13.8 mm | Coaxial stack: naturally symmetric, +2.5 mm |
| D2 | **7075 base with integral stud + polycarbonate cap** | Metal only on the weight side, so the antenna radiates through the cap. No plastic creep in the clamp path. | All-PEEK body with a flanged stainless stud |
| D3 | **nRF52840 in high-voltage mode** (cell → VDDH, REG0 = 3.0 V) | No regulator, no battery divider (internal VDDH/5), fewer leaks | External LDO |
| D4 | **MCP73831 + firmware temperature gate** (nRF die temperature, <1 °C / >44 °C) | MCP73831 programs down to 15 mA; the TS-pin chargers found start at 100 mA. Default is *charging allowed* if the MCU is dead, so a flat cell always recovers. | Charger with an NTC input and a thermistor at the cell |
| D5 | **32 kHz from internal RC, REG0/REG1 in LDO mode** | Works whether or not your MDBT50Q variant has the LFXO / DC-DC inductor | Set `K32SRC_XTAL` and enable DC/DC if present (saves µA) |
| D6 | **Cell pads radial, both outside the cell footprint** (+ at +y by JP1, − at −y by the charger, ±10.2 mm), 3.0 × 2.0 mm pads with Ø1.0 mm plated holes | Varta publishes no standard tab drawing: the pads take the wire version or bent tabs (the bottom + tab bends up through the notch relief; tag pins into the holes only suit the top − tab). No copper within 0.35 mm of the cell notch | SMD pads matched to one tabbed variant |
| D7 | **Pairing required; new pairings only within 60 s of attaching the charger** (LE Secure Connections Just Works, up to 4 phones, FORGET_BONDS to reset) | Several pucks and shooters at one club: other apps must not read, reconfigure or reset your puck. The charger gesture proves physical possession. | Open access (v1), or a PIN shown by LED blinks |
| D8 | **Detector thresholds are defaults, tunable over BLE**; every candidate is reported | Real release-shock data is unknown until you shoot | – |
| D9 | IMU rotated 270° (I2C edge facing the module–cell channel), reference designators on F.Fab only | Routability; board too dense for silkscreen text | – |
| D10 | **LSM6DSO32 IMU** replaces the LIS2DE12 accelerometer. Pinout, pin states and register values checked against datasheet DocID032891 Rev 1; I3C disabled at init as the datasheet recommends | Gyroscope for bow rotation; ±32 g clips the release shock less; 16-bit data; same I2C bus and INT1 pin | LSM6DSV16X/32X (on-chip sensor fusion, ±4000 dps) |
| D11 | **Raw 6-axis capture per accepted shot** (−1.0 s to +0.5 s, 624 samples, 7.5 KB), analysed in the app | Which bow-movement metrics matter is unknown until real shots are recorded | On-device metrics (cant, steadiness, post-release rotation) in the EVENT |
| D12 | **Identity from the chip's factory ID**: name `ShotPuck-XXXX`, DIS serial number = `device_id` | Tell pucks apart at a club; one stable ID across phones for PowerSync | App-assigned IDs |
| D13 | **Accepted shots in a 32 KB flash ring** (~900 shots), rejected candidates in RAM; app-driven REPLAY, sent paced in the background | Shots survive dropouts and resets without per-phone sync state on the puck | Device push with ACK (rejected: shared state, first phone drains the backlog); captures in flash too |
| D14 | **MDBT50Q-1MV2 (nRF52840) instead of MDBT50Q-512K (nRF52833)**: same footprint, all 16 used pads identical | OTA needs two firmware slots: 2 × 460 KB instead of 2 × ~212 KB, so updates keep room to grow and debug builds fit | Stay on nRF52833 with ~13 KB headroom; external SPI flash |
| D16 | **Cell protection (PCM): TI BQ29700 + Diodes DMN2004DWK** in the cell's negative lead (OVP 4.275 V, UVP 2.80 V, ±0.1 A over-current, short-circuit) | Required by Varta for CoinPower cells; BQ29700 is on Varta's recommended list. Costs 4 µA standby | – |
| D17 | **Puck 1.3 mm taller** (cavity 7.15 mm above the cell's seat; D20 later took 0.8 mm back) | Varta: cell height max 7.0 mm including deflection, i.e. 1.5 mm free space for abuse conditions | Keep 9.95 mm and accept 0.25 mm |
| D19 | **Board Ø36 mm; screw bosses at r = 16.5 mm, 40°/148°/270°**; PCM in the pocket below the module | Puck Ø40 mm is fixed by the Avalon weight; 36 leaves 0.5 mm fit clearance and a 0.6 mm web outside the screw holes. 40° keeps the boss face on the board beside the cell notch (D20), 148° keeps it out of the module's antenna keep-out | Ø36.6 (0.2 mm clearance), thinner 1.2 mm cap wall (Ø37.2) |
| D20 | **Cell in a notch in the PCB, open to the rim** (Ø16.6 mm round end), on the Kapton over the base; a rounded relief (R3, 0.4 mm deep) on the + side; screw boss 35° → 40°, D2 moved to the bottom centre | Saves the 0.8 mm board: 10.45 mm instead of 11.25 mm. Only 2 charger traces ran under the cell. The relief lets the bottom (+ can) tab bend up to the + pad. Richard preferred the notch to a closed hole | Closed Ø16.6 mm hole (holds the cell all round, but leaves a 1.2 mm strip at the rim); also pocketing the base (~1 mm more, but over the stud root) |
| D18 | **Samzo PR5L4015-2P-C-F magnetic receptacle** (Electrokit, 18.50 SEK; cable side PR5L5015-2P-C-H, 28 SEK) on the PCB, reached through a sealed chimney in the cap | Cheapest in-stock pair in Sweden with a real drawing; the N/S magnets make it mate one way only | AliExpress generic pairs |
| D15 | **OTA = MCUboot swap-using-move + MCUmgr SMP over BLE**, ECDSA-P256 signed, encrypted link (paired phones) only. A new image runs on trial and confirms itself after 10 s of healthy running; a 10 s hardware watchdog turns a hang into a reset, and MCUboot then rolls back | Standard Nordic/Zephyr path; the Flutter app can use Nordic's `mcumgr_flutter` | Firmware-loader mode (single slot); no rollback |

## Needs your input before ordering

1. **Magnetic connector:** done: Samzo PR5L4015-2P-C-F from Electrokit (D18). For the charging cable, solder a 5 V lead to the male part (PR5L5015-2P-C-H) and check with a multimeter which pin meets the square pad (pin 1, +). A reversed lead cannot damage the puck (D1 blocks it).
2. **CP1654:** charging checked against the Varta datasheet (4.20 V ± 0.05 V, standard charge 60 mA; ours 4.20 V / 50 mA). Buy the **wire version (CP 1654 A3 W)** or any tabbed/tag version: the pads take all of them (D6). Solder by hand after SMT assembly, within 3 s at 320 °C, cell body below 60 °C, **never reflow**.
3. **Avalon weight thread depth ≥ 8 mm** (stud length `puck.stud_length`).
4. **Component masses:** weigh the real parts, put them in `layout.json`, and re-run `puck.py` before cutting the (optional) brass trim pins.
5. **Back up the OTA signing key** `%USERPROFILE%\.shotpuck\ota-signing-key.pem` (password manager or offline copy). Pucks accept only images signed with it: if it is lost, updates need a cable (SWD); if it leaks, anyone who can pair could install their firmware. It is never committed (`*.pem` is git-ignored).

## Simulation results (`firmware/sim/results.txt`, seed 2026)

| Set | Shots | Recall | Precision | Notes |
|---|---|---|---|---|
| Nominal sessions | 437 | 99.1 % | 99.8 % | walking knocks, set-downs, stand knocks, nock taps, drops, let-downs (1 FP: stand knock) |
| Hard cases | 479 | 99.6 % | 99.4 % | + taps while aiming, hard grabs off the stand (2 grabs, 1 stand knock) |
| Weak bow (info) | 253 | 80.2 % | 99.5 % | 4–12 g shock, 5–30° rotation: **thresholds must be tuned on real shots** |

The detector needs three things together: a high-frequency shock (6 g sample-to-sample), ≥ 0.5 s of stillness before it (aiming), and ≥ 250 mg of low-frequency bow motion 0.1–0.6 s after it (bow jump into the sling). See `firmware/src/shot_detect.h`. It uses the accelerometer only; the gyroscope feeds the capture.

## Battery life (estimate, verify via JP1)

The idle draw is roughly 17 µA (LSM6DSO32 accelerometer in low-power mode at 12.5 Hz, about 10 µA by interpolating the datasheet's 4.5 µA at 1.6 Hz and 26 µA at 52 Hz; BQ29700 protection 4 µA; nRF System ON + RC). While active it is roughly 0.7–0.8 mA: the gyroscope alone is about 0.55 mA, plus 416 Hz FIFO reads and the BLE link. At **2 h of training per day** that is ≈ 1.9 mAh/day, so roughly **2 months per charge** (v0.1 without gyroscope: 4–6 months). The low-battery cut-off (3.3 V) puts the puck into System OFF until the charger is attached.

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
- **First flash** (bootloader + app) by cable: `shotpuck_full.hex` with J-Link/nrfjprog via the Tag-Connect TC2030-CTX-NL.
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
"C:\Program Files\KiCad\10.0\bin\python.exe" gen_pcb.py            # place, route, bridge GND islands, DRC
"C:\Program Files\KiCad\10.0\bin\python.exe" gen_bom.py
kicad-cli pcb export gerbers / drill / pos                         # into kicad\fab\
```

Freerouting is not deterministic. `gen_pcb.py` re-routes up to 4 times until nothing is unconnected; if it still does not end with `0 DRC violations` and `0 unconnected pads`, run it again. ERC: 0 errors; the 2 warnings are the intentional straps of LSM6DSO32 SA0 and SDx to GND.

**Mechanical:** `C:\zp\.cq\Scripts\python.exe mechanical/puck.py` (CadQuery 2.8 in a Python 3.12 venv; CadQuery has no Python 3.14 wheels). The trim solver keeps the metal pins away from the antenna (2 mm margin) and allows pins above the module when they clear it by 0.5 mm.

**Panel + JLCPCB assembly files** (after `gen_pcb.py`, with KiCad 10's Python and KiKit: `python.exe -m pip install --user kikit`):

```
"C:\Program Files\KiCad\10.0\bin\python.exe" gen_panel.py      # 2x2, mouse bites, rails, fiducials, tooling holes
"C:\Program Files\KiCad\10.0\bin\python.exe" gen_jlc.py        # kicad/fab/jlc/shotpuck-panel-bom.csv + -cpl.csv
kicad-cli pcb export gerbers / drill for kicad/panel/shotpuck-panel.kicad_pcb
```

## Fabrication & assembly

- **PCB:** 2 layers, 0.8 mm FR-4, ENIG, Ø36 mm (0.5 mm clearance per side in the Ø37 mm cap cavity; puck Ø40 mm = the Avalon weight) with a notch for the cell (Ø16.6 mm round end, open to the rim; part of the outline), 0.127 mm track/space, vias 0.5 mm pad / 0.3 mm hole (standard at JLCPCB/PCBWay, no small-hole surcharge).
- **JLCPCB order (prototype):** upload `kicad/fab/panel/shotpuck-panel-gerbers.zip` as a 2×2 panel ("panel by customer", 4 boards per panel), order 5 panels = 20 boards, 0.8 mm, ENIG. For assembly (top side), upload `kicad/fab/jlc/shotpuck-panel-bom.csv` and `-cpl.csv` and check every part's rotation in the placement preview. Not assembled by JLCPCB: the cell BT1 and the magnetic connector J1 (hand-solder), JP1 (bridged solder jumper, copper only), J2 (Tag-Connect pads), R5/R6 (DNP). The module needs JLCPCB's X-ray inspection. After breaking the boards out, sand the mouse-bite nubs flush.
- **Base:** 7075-T6, turn + thread 5/16-24 UNF-2A, hard anodise (mask the thread or chase it after). Countersink 3× M1.6 at 90°.
- **Cap:** polycarbonate, CNC-machined (translucent so the LED shows). For prototypes, clear SLA resin. **No liquid threadlocker anywhere near PC** (it causes stress cracking): use nylon-patch screws.
- **Assembly order:**
  1. Heat-set the 3 M1.6 inserts into the cap bosses.
  2. Optional: cut the two pins from Ø8 mm brass rod (lengths in `mechanical/out/mass_report.txt`), then press and glue them into the cap pockets. Without them the puck works and is 0.9 mm off balance (≈ 0.01 mm on a 2 kg bow).
  3. Hand-solder the magnetic connector J1. Then lay the PCB on a flat surface, drop the cell into its notch (+ can down, flush with the board's underside), bend the bottom (+) tab or wire up through the relief onto the + pad and the top (−) one down onto the − pad, and solder them (never reflow the cell).
  4. Put a soft foam pad (≤ 1.5 mm, compressible) on the cell, a foam strip or a bead of RTV in the ~2 mm gap between the cell and the cap wall on the open side of the notch (so shot shocks don't load the cell tabs), and fit the O-ring (0.8 mm cross-section, in the base groove).
  5. Stack the Kapton disc (a full disc: it insulates the cell's + can from the base), the PCB with the cell, and the cap on the base, then screw it together from the weight side.
  6. Seal the gap between the cap chimney and the connector top with RTV.
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
