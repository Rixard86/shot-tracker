# ShotPuck: automatic arrow counter for Avalon barebow riser weights

A 40 × 10 mm puck that screws onto the free face of an Avalon riser weight (5/16-24), detects each shot with an accelerometer, and reports it over BLE to the scoring app.

| | |
|---|---|
| Size / mass | Ø40 mm, 9.95 mm above the weight face (+8 mm stud), ~20.7 g, COM on axis (trimmed) |
| Radio / MCU | Raytac MDBT50Q-512K (nRF52833), BLE 5 |
| Sensor | ST LIS2DE12, 400 Hz ±16 g while active, 10 Hz wake-on-motion while idle |
| Battery | Varta CP1654 A3 Li-ion coin, 120 mAh, magnetic pogo charging (50 mA, temperature-gated) |
| Firmware | Zephyr 4.1, custom board `shotpuck/nrf52833`, 139 KB flash / 27 KB RAM |

## Repository

```
layout.json                 shared geometry: CAD and PCB both read it
firmware/                   Zephyr app (src/, boards/shotpuck/, prj.conf, debug.conf)
firmware/sim/               Monte Carlo validation of src/shot_detect.c (+ results.txt)
firmware/zephyr.hex         release build (programme via SWD / Tag-Connect)
hardware/design.py          the circuit (single source of truth)
hardware/gen_sch.py         -> kicad/shotpuck.kicad_sch
hardware/check_netlist.py   KiCad netlist vs design.py
hardware/gen_pcb.py         -> kicad/shotpuck.kicad_pcb (place, keep-outs, route, DRC)
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
| D7 | **No BLE pairing in v1** | Simplest for the app | See PROTOCOL.md *Security* |
| D8 | **Detector thresholds are defaults, tunable over BLE**; every candidate is reported | Real release-shock data is unknown until you shoot | – |
| D9 | Accelerometer rotated 180°, reference designators on F.Fab only | Routability; board too dense for silkscreen text | – |

## Needs your input before ordering

1. **Magnetic connector:** `J1` / `ShotPuck:MagPogo_2P` is a placeholder (2 pins, 2.5 mm pitch, 9 × 4.2 × 4.5 mm body). Buy the pair, then update `layout.json` → `connector` and the footprint in `gen_pcb.py`, and re-run.
2. **CP1654 variant:** confirm the tab geometry (pad positions `cell.neg_pad`), charge voltage 4.20 V and max charge current ≥ 50 mA in the Varta datasheet.
3. **Avalon weight thread depth ≥ 8 mm** (stud length `puck.stud_length`).
4. **Component masses:** weigh the real parts, put them in `layout.json`, and re-run `puck.py` before cutting the tungsten.

## Simulation results (`firmware/sim/results.txt`, seed 2026)

| Set | Shots | Recall | Precision | Notes |
|---|---|---|---|---|
| Nominal sessions | 475 | 99.2 % | 100 % | walking knocks, set-downs, stand knocks, nock taps, drops, let-downs |
| Hard cases | 440 | 98.4 % | 98.9 % | + taps while aiming, hard grabs off the stand (all 5 FPs are grabs) |
| Weak bow (info) | 244 | 75.8 % | 100 % | 4–12 g shock, 5–30° rotation: **thresholds must be tuned on real shots** |

The detector needs three things together: a high-frequency shock (6 g sample-to-sample), ≥ 0.5 s of stillness before it (aiming), and ≥ 250 mg of low-frequency bow motion 0.1–0.6 s after it (bow jump into the sling). See `firmware/src/shot_detect.h`.

## Battery life (estimate, verify via JP1)

The idle draw is about 6 µA (accelerometer 10 Hz + nRF System ON + RC). While active it is about 150–200 µA (400 Hz FIFO reads + BLE connection). At **2 h of training per day** that is ≈ 0.5 mAh/day, so roughly **4–6 months per charge**. The low-battery cut-off (3.3 V) puts the puck into System OFF until the charger is attached.

## Build & flash

**Firmware** (Zephyr 4.1 or nRF Connect SDK v3.x; on Windows use the nRF Connect for VS Code extension, or a west workspace):

```
west build -b shotpuck/nrf52833 firmware -d build
west flash -d build                      # J-Link via Tag-Connect TC2030-CTX-NL
west build ... -- -DEXTRA_CONF_FILE=debug.conf   # RTT logging
```

Upstream Zephyr needs CMake 3.x (not 4.x) and the modules `hal_nordic`, `cmsis` and `cmsis_6` (`segger` for debug.conf). The first boot programs UICR REGOUT0 = 3.0 V and resets once.

**Simulation:** `python firmware/sim/simulate.py --sessions 60 --stress` (needs gcc, numpy, scipy).

**Hardware** (KiCad 7 Python; the files open in KiCad 7/8/9):

```
cd hardware
python gen_sch.py && python check_netlist.py        # schematic + connectivity proof
python gen_pcb.py                                   # needs Java 17+ and freerouting.jar (FREEROUTING_JAR)
python gen_bom.py
kicad-cli pcb export gerbers ... / drill / pos      # already exported to kicad/fab/
```

Run **ERC** once in KiCad 8/9: KiCad 7's CLI has no ERC, so connectivity was instead proven with `check_netlist.py` (80/80 pins).

**Mechanical:** `python mechanical/puck.py` (CadQuery 2.x).

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
