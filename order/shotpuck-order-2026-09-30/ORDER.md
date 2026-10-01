# ShotPuck rev A: order package, 2026-09-30

USB-C board (`main` at f313846 plus the fab fixes of 2026-09-30). Three JLC services, one folder each. Stock and prices are JLC's live figures from 2026-09-30.

| Folder | Service | Upload |
|---|---|---|
| `1_PCB_JLCPCB` | JLCPCB PCB + Standard PCBA | `shotpuck-panel-gerbers.zip`, then `shotpuck-panel-bom.csv` + `shotpuck-panel-cpl.csv` |
| `2_Plate_JLCCNC/A_sheet-metal_5052_prototype` | JLCCNC Sheet Metal (recommended for prototypes) | `plate_5052.step`, drawing `plate_drawing_5052.pdf` as the thread file |
| `2_Plate_JLCCNC/B_cnc_6061` | JLCCNC CNC Machining (production material) | `plate.step`, drawing `plate_drawing.pdf` |
| `3_Cap_JLC3DP` | JLC3DP SLA resin | `cap.step` (`cap.stl` if the STEP upload fails) |

Upload the panel files above, not `hardware/kicad/fab/shotpuck-gerbers.zip` / `shotpuck-pos.csv` / `shotpuck-bom.csv`. Those are regenerated for this board (2026-10-01) but are the single board, for reference only.

## Before you order

1. **Ask JLCPCB support** to confirm that C5456944 is type **BMD-340-A-R-10**; types -00/-01 do not work in high-voltage mode (README D14).
2. U2 is now the **LSM6DSO32XTR (C3663738)**, 177 in stock; the LSM6DSO32TR was down to 39 for the 20 needed. It is the LSM6DSO32 plus a machine-learning core we leave off: same package, pinout, JLC footprint (pad for pad) and registers, so the gerbers, CPL and firmware are unchanged.
3. Quantities for the plate and the cap are your call. Suggestion: 5 of each first, so you can fit the O-rings and tune the seal before printing more.

## 1. PCB + assembly (JLCPCB)

| Setting | Value |
|---|---|
| Layers / material | 2, FR-4 |
| Dimensions | 91.1 × 92.7 mm (read from the gerbers) |
| PCB qty | 5 (panels) = 20 boards |
| Delivery format | Panel by Customer, 1 design (2×2 inside, frame, tooling holes and fiducials already in) |
| Thickness / finish | **1.2 mm** / ENIG |
| Copper / via covering | 1 oz / Tented |
| Min via hole | 0.3 mm (no surcharge) |
| Remove order number | Specify a location (the `JLCJLCJLCJLC` text on the frame) |
| PCBA type | **Standard** (Economic X-rays BGAs only; U1 is an LGA) |
| Assembly side / PCBA qty | Top / 5 |
| Tooling holes | Added by customer |
| Confirm parts placement | Yes |

Note for the order: *"Please X-ray U1 (BMD-340, LGA) and U2 (LGA-14). J1 is a mid-mount USB-C in a board-edge notch; its front sticks out 1.3 mm past the board edge into the routed gap. BT1, J2, JP1 and JP2 are not assembled."*

**CPL corrections (new).** JLC places its own EasyEDA footprint of each LCSC part at the CPL position and rotation. Several of those footprints differ from ours, so `gen_jlc.py` now corrects them:

| Part | Correction | Without it |
|---|---|---|
| U1 BMD-340 | centroid +2.0 mm (JLC's origin is the pad-array centre) | placed 2.05 mm off |
| U3 MCP73831 | +270° | pins 2.1 mm off (turned 90°) |
| Q1, Q2 SOT-363 | +270° | pins 1.7 mm off |
| D4 LED | +180° | fitted backwards (JLC's pad 1 is the cathode, on the other side) |
| D3 SMF5.0CA | +180° | pads swapped (JLC numbers them the other way). Harmless, since the TVS is bidirectional, but the preview showed its pin 1 at the opposite end from our silkscreen |
| J1 USB-C | centroid 0.15 mm | pins 0.15 mm off |

A placement simulation of all 112 placements puts every JLC pad on our same-numbered pad. Most land within 0.1 mm. SOT-363 is within 0.27 mm and SOD-123F within 0.29 mm; those are land-pattern size differences, not placement errors. JLC's placement preview should now look right; confirm it anyway.

**Parts (20 boards):** all 21 lines are in stock, with 10 extended parts (~$3 setup each). The parts cost about $323, or $16 per board, of which U1 is $171 and U2 $98. The thinnest stock (2026-10-01) is U2 (177), Q1 DMN63D8LDW (244), U4 BQ29700 (257) and U1 (322).

| Check against JLC 1–2 layer capabilities | Design | JLC limit | |
|---|---|---|---|
| Track / clearance | 0.15 / 0.127 mm | 0.10 / 0.10 | OK |
| Via hole / pad (ring) | 0.30 / 0.55 mm (0.125) | 0.15 / 0.25 (0.05) | OK |
| Via hole to hole | 0.41 mm | 0.20 | OK |
| NPTH (mouse bites, J1 legs, tooling, screws) | 0.50 / 0.70 / 1.152 / 2.4 mm | ≥ 0.5 | OK |
| Copper to outline | ≥ 0.30 mm (rule) | 0.20 | OK |
| Mask web | none under 0.10 mm | 0.10 | OK |
| Vias in SMD pads | none | – | OK |
| Board spacing / mouse bites | ≥ 2.5 mm / Ø0.5 at 0.8 mm | ≥ 2 mm / Ø0.5–0.8 | OK |
| Panel size (Standard PCBA) | 91.1 × 92.7 mm | ≥ 70 × 70, ≤ 250 × 250 | OK |
| Thickness 1.2 mm + ENIG (gerber job file says 1.2) | 1.2 | offered | OK |
| Silkscreen line / text height | board artwork 0.2 mm (fixed); KiCad footprint outlines 0.12 mm, D4 0.10 mm; "RST" 0.8 mm | ≥ 0.15 / ≥ 1.0 | Footprint outlines and "RST" below guideline: cosmetic, may print faint |
| Inner corners (router Ø1.0 mm) | panel milled with 0.5 mm radius, the same as the board's J1 notch fillets | ≥ R0.5 | OK |

Also verified: KiCad 10 DRC 0/0 on the board and the panel, 0 schematic-parity issues; the panel was regenerated from the board with your silkscreen fix (2026-09-30 21:01) and holds four exact copies of it; BOM/CPL regenerate from the panel unchanged. The gerbers now carry separate `-PTH.drl` / `-NPTH.drl` files, as JLC asks, and the silkscreen is clipped at the mask. Copper, mask, paste and outline are unchanged from the committed files.

## 2. Base plate (JLCCNC)

You allowed a cheaper prototype material. JLCCNC's sheet-metal service stocks only aluminium **5052**; 6061 is only available as CNC machining. For a flat laser-cut part, sheet metal is normally much cheaper, and both services quote instantly after upload.

**A, prototype (recommended):** Sheet Metal → Aluminum 5052, 2.0 mm, laser cut, no bending, finish none. Threads: **Yes**, upload `plate_drawing_5052.pdf` (4× M2×0.4 through at H1–H4). JLC does not publish an M2 tapping minimum for sheet metal; its riveting range starts at M2.5. If the form will not take M2, order the Ø1.6 holes plain and tap them by hand (M2×0.4 tap; 5052 taps easily). The 0.3 mm underside chamfer is left off, because a laser cannot cut it and the STEP must be a flat sheet. Deburr the edges by hand.

**B, production material:** CNC Machining → Aluminum 6061, as machined. Threads: **Yes**, upload `plate_drawing.pdf`. The tolerance is ISO 2768-m, which is JLC's default.

| Check | Design | JLC rule | |
|---|---|---|---|
| Size | Ø40 × 2.0 mm | sheet ≤ 800 mm; CNC warping warning only below 10 × 10 × 1 | OK |
| Holes | Ø1.6 (tap drill) ×4, Ø8.4 | sheet: ≥ max(t/2, 1 mm) = 1.0 | OK |
| Hole to rim / to hole | 2.70 mm / ≥ 11 mm | sheet ≥ 1 mm; CNC wall ≥ 0.8 | OK |
| Tapped holes | STEP has the Ø1.6 tap drill, drawing calls out M2×0.4 | CNC: model the tap drill, call out the thread in a 2D drawing | OK |
| Thread length | 2.0 mm through | ≤ 3 × D | OK |
| Geometry | matches `layout.json` to 0.000 mm | – | OK |

5052-H32 (yield ~190 MPa) takes the tube end at your 3 N·m (~85 MPa); at the bolt's maximum torque both alloys yield, as before. Tighten the M2 screws gently (≈ 0.1–0.15 N·m).

## 3. Cap (JLC3DP)

SLA, **8001 Resin, Translucent**. The Transparent grade is sanded and oil-sprayed by default, which changes the O-ring ledge and port-seal fits; Translucent is only de-supported, and the LED still shows through. Note for the order: *"The thinnest wall is at your SLA minimum (0.80 mm top over the side-port pocket); please print as modelled. Please keep support marks off the stepped bottom rim and the side port hole."*

| Check | Design | JLC3DP SLA guideline | |
|---|---|---|---|
| Model | 1 valid solid, 40 × 40 × 8.43 mm, 2.72 cm³; STL matches | min 5 × 5 × 5 mm | OK |
| Walls: top 1.33, side 1.5 mm | 1.33 / 1.5 | 1.0 for a ~50 mm part | OK |
| Band under the port flat, over the seal O-ring | 0.90 mm (was 0.45) | 0.5 for a 5 × 5 feature | OK |
| Cap top over the USB-C pocket | 0.80 mm, ~9 × 7 mm (was 0.67) | 0.8 for a 10 × 10 feature | OK (at the minimum) |
| Port-flat ends, counterbore rings, tube lip | 0.73–0.8 mm, features < 6 mm | 0.5 (5 × 5); fasteners ≥ 1.5 recommended | OK |
| Screw holes / counterbores | Ø2.4 for M2 (0.2/side, was Ø2.2), Ø4.2 for Ø3.8 heads (0.2/side); boss collar 0.7 mm | assembled clearance ≥ 0.2; holes ±0.3; wall 0.5 (5 × 5) | OK |
| Port seal squeeze | 0.15 mm per side | tolerance ±0.2 mm | Fit depends on the print (tune `connector.seal_squeeze`) |
| Enclosed cavities | none | escape holes only for closed cavities | OK |
| Heat | 8001 HDT 53 °C | – | Prototype only; don't leave it in a hot car |

## 4. Not in this package (buy separately)

O-rings 36/37/38 × 1.0 and 2 × 1.0 (VMQ/NBR 70A); 4× M2×8 cheese head, nylon patch; 4× M2 spacers 3 mm (OD 4); 304 tube Ø10 × 0.8, cut to 7.1 mm; a foam pad for the 1.6 mm gap above the cell; Kapton; LIR1254 cells; 0.15 × 2 mm nickel strip; the 5/16-24 button head bolt and bonded washer (README "Needs your input").

## 5. Risks to check on the first boards

- **J1 fit in the notch.** LCSC's 3D model reaches 0.03 mm past our notch's back wall. Its leg roots also touch the board top beside the Ø0.70 holes (0.3 mm deep); LCSC's own footprint puts those holes 0.095 mm closer to the pads. (The panel used to round the notch's back corners to 1.0 mm; it now uses JLC's 0.5 mm, the board's own fillet.) All of this is within the model's accuracy and JLC's ±0.2 mm outline tolerance, but if J1 does not sit flat, look here first.
- Silkscreen is below JLC's line/text minimums (see above). It is cosmetic.
- The cap's fits, as above. Its roof over the USB-C pocket sits exactly at JLC's minimum: check it on the first print.

Changes on 2026-09-30: to meet JLC's minimums, the cap top went 1.2 → 1.33 mm and the plug-overmold clearance 0.25 → 0.20 mm. Then the PCB went 0.8 → 1.2 mm on the same 3 mm standoffs, and the ceiling and cap rose 0.4 mm with it. The puck is now 10.43 mm and 16.72 g, the tube 7.1 mm and the bolt about 0.5 mm longer than planned, with 1.6 mm free above the cell. Nothing new overlaps the plate, board, module or USB-C model; the only overlap is still the 2.1 mm³ seal-ring squeeze, and the largest USB-C plug overmold clears by 0.20 mm.

Changes on 2026-10-01: U2 → LSM6DSO32XTR (see above); C5 removed from `design.py` (it was already off the board), so the netlist check passes; the stray off-board silkscreen text is deleted; the cap's screw holes are Ø2.4. The board, panel, gerbers, BOM/CPL and cap in this package were regenerated and re-checked: DRC 0/0 on board and panel, 0 parity issues, ERC 0 errors, panel = 4 exact copies, placement simulation passes, cap walls ≥ JLC's minimums.

Later on 2026-10-01:
- **J1's wing-tab pads were enlarged** from 0.99 to 2.0 mm² each, with 0.86 mm² under the tab (was 0.61) and room for solder fillets at the tab's outer edge and ends. They stay 0.31 mm from the notch and 0.25 mm from the leg holes.
- **D3 got a +180° CPL correction** (table above).
- **Regenerated:** the board, panel, gerbers and CPL in this package. Board DRC: 0/0, 0 parity issues. Panel DRC: 0/0 on three runs. The panel is still four exact copies of the board. Compared with the previous panel, only the front copper, mask and paste changed, all within 1.7 mm of J1's tabs. The BOM is unchanged, and the placement simulation still passes.
