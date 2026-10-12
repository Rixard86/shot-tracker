# ShotPuck rev 0A: order package, 2026-09-30

USB-C board (`main` at 36e9f82: the fab fixes of 2026-09-30 and the 2026-10-01 changes listed at the end). Three JLC services, one folder each. Stock and prices are JLC's live figures from 2026-10-01.

**Ordered** (Richard) from `shotpuck-order-2026-09-30.zip`, which matches this folder at e059581 apart from line endings: the PCB with PCBA on 2 panels (4 assembled boards), and the cap (JLC3DP order 13698800A-W2026101020127812). The base plate is not ordered yet. JLC3DP flagged the cap's 0.40 mm seal band; Richard uploaded the new `3_Cap_JLC3DP/cap.step` to the order as its replacement on 2026-10-12 (changes at the end).

**Paid in full** on 2026-10-12, $217.63: PCB $35.37 (5 panels), PCBA $151.71 (2 panels), cap $3.76 (8 pcs, the 0.8 mm-band file), shipping $26.79 (DHL Express; Global Standard Direct Line was not offered for this order, so expect DHL to charge Swedish VAT and its clearance fee on delivery). JLC support confirmed that C5456944 is **BMD-340-A-R-10**. JLC's BOM matches `shotpuck-panel-bom.csv` line for line (21 lines, same LCSC parts and designators; parts $69.09).

| Folder | Service | Upload |
|---|---|---|
| `1_PCB_JLCPCB` | JLCPCB PCB + Standard PCBA | `shotpuck-panel-gerbers.zip`, then `shotpuck-panel-bom.csv` + `shotpuck-panel-cpl.csv` |
| `2_Plate_JLCCNC/A_sheet-metal_5052_prototype` | JLCCNC Sheet Metal (recommended for prototypes) | `plate_5052.step`, drawing `plate_drawing_5052.pdf` as the thread file |
| `2_Plate_JLCCNC/B_cnc_6061` | JLCCNC CNC Machining (production material) | `plate.step`, drawing `plate_drawing.pdf` |
| `3_Cap_JLC3DP` | JLC3DP SLA resin | `cap.step` (`cap.stl` if the STEP upload fails) |

Upload the panel files above, not `hardware/kicad/fab/shotpuck-gerbers.zip` / `shotpuck-pos.csv` / `shotpuck-bom.csv`. Those are regenerated for this board (2026-10-01) but are the single board, for reference only.

## Before you order

1. **Ask JLCPCB support** to confirm that C5456944 is type **BMD-340-A-R-10**; types -00/-01 do not work in high-voltage mode (README D14).
2. U2 is now the **LSM6DSV32XTR (C22461202)**, 5,864 in stock. It replaces the LSM6DSO32XTR (177 in stock) for lower noise at ±32 g. Same pinout and land pattern, and JLC's footprint lands within 0.013 mm of ours at 0°, so the gerbers and CPL are unchanged. It needs **firmware 0.2.2** or later (new IMU driver, 480 Hz).
3. Quantities for the plate and the cap are your call. Suggestion: 5 of each first, so you can fit the O-rings and tune the seal before printing more.

## 1. PCB + assembly (JLCPCB)

| Setting | Value |
|---|---|
| Layers / material | 2, FR-4 |
| Dimensions | 91.1 × 92.7 mm (read from the gerbers) |
| PCB qty | 5 panels (JLC's minimum) = 20 boards, **4 of them assembled** |
| Delivery format | Panel by Customer, 1 design (2×2 inside, frame, tooling holes and fiducials already in) |
| Thickness / finish | **1.2 mm** / ENIG |
| Copper / via covering | 1 oz / Tented |
| Min via hole | 0.3 mm (no surcharge) |
| Order number / marking | Leave the default, **no mark** (or a 2D barcode on the rail for traceability). Do **not** pick "Specify a location": JLC stopped printing order numbers in May 2026, and the panel no longer carries `JLCJLCJLCJLC` |
| PCBA type | **Standard** (Economic X-rays BGAs only; U1 is an LGA) |
| Assembly side / PCBA qty | Top / **2** (panels; JLC's minimum). The BOM and CPL cover only the bottom-row boards of each panel (designators `_2`, `_3`), so 2 panels × 2 = 4 assembled boards. The top-row boards and the other 3 panels come bare |
| Tooling holes | Added by customer |
| Confirm parts placement | Yes |

Note for the order: *"Please X-ray U1 (BMD-340, LGA) and U2 (LGA-14). J1 is a mid-mount USB-C in a board-edge notch; its front sticks out 1.3 mm past the board edge into the routed gap. BT1, J2, JP1 and JP2 are not assembled. Only the two bottom-row boards of each panel (designators ending _2 and _3) are to be assembled; the two top-row boards (_0, _1) are intentionally left unpopulated."*

**CPL corrections (new).** JLC places its own EasyEDA footprint of each LCSC part at the CPL position and rotation. Several of those footprints differ from ours, so `gen_jlc.py` now corrects them:

| Part | Correction | Without it |
|---|---|---|
| U1 BMD-340 | centroid +2.0 mm (JLC's origin is the pad-array centre) | placed 2.05 mm off |
| U3 MCP73831 | +270° | pins 2.1 mm off (turned 90°) |
| Q1, Q2 SOT-363 | +270° | pins 1.7 mm off |
| D4 LED | +180° | fitted backwards (JLC's pad 1 is the cathode, on the other side) |
| D3 SMF5.0CA | +180° | pads swapped (JLC numbers them the other way). Harmless, since the TVS is bidirectional, but the preview showed its pin 1 at the opposite end from our silkscreen |
| J1 USB-C | centroid 0.15 mm | pins 0.15 mm off |

A placement simulation of all 112 placements of the full panel puts every JLC pad on our same-numbered pad; the 56 placements in this order are those rows unchanged. Most land within 0.1 mm. SOT-363 is within 0.27 mm and SOD-123F within 0.29 mm; those are land-pattern size differences, not placement errors. JLC's placement preview should now look right; confirm it anyway.

**Parts (4 boards):** all 21 lines are in stock, with 10 extended parts ($1.50–3 setup each). The parts cost about $68, or $17 per board, of which U1 is $35 and U2 $17. The thinnest stock (2026-10-01) is Q1 DMN63D8LDW (244), U4 BQ29700 (257) and U1 (322), far above the 4 needed. With the $25 setup, the stencil and 5 ENIG panels the whole PCBA order should come to roughly $140–155 before shipping and VAT; JLC's quote is the real figure. Ship with **Global Standard Direct Line**, which collects duty and Swedish VAT at checkout, so DHL does not charge a clearance fee on delivery.

| Check against JLC 1–2 layer capabilities | Design | JLC limit | |
|---|---|---|---|
| Track / clearance | 0.15 / 0.127 mm | 0.10 / 0.10 | OK |
| Via hole / pad (ring) | 0.30 / 0.55 mm (0.125) | 0.15 / 0.25 (0.05) | OK |
| Via hole to hole | 0.41 mm | 0.20 | OK |
| NPTH (mouse bites, J1 legs, tooling, screws) | 0.50 / 0.70 / 2.0 / 2.4 mm | ≥ 0.5 | OK |
| Copper to outline | ≥ 0.30 mm (rule) | 0.20 | OK |
| Mask web | none under 0.10 mm | 0.10 | OK |
| Vias in SMD pads | none | – | OK |
| Board spacing / mouse bites | ≥ 2.5 mm / Ø0.5 at 0.8 mm | ≥ 2 mm / Ø0.5–0.8 | OK |
| Mouse-bite tabs | sides, bottom 3 mm with 5–6 holes; top 2.3 mm with 5 holes; Ø0.5 at 0.8 mm (0.3 mm gap) | 3–4 mm for 25–50 mm boards; ≥ 5 holes per set, Ø0.6 with a 0.35–0.4 mm gap (min 0.3); ≥ 2 sets | OK (hole gap at the minimum) |
| Panel size (Standard PCBA) | 91.1 × 92.7 mm | ≥ 70 × 70, ≤ 250 × 250 | OK |
| Thickness 1.2 mm + ENIG (gerber job file says 1.2) | 1.2 | offered | OK |
| Silkscreen line / text height | board artwork 0.2 mm (fixed); KiCad footprint outlines 0.12 mm, D4 0.10 mm; "RST" 0.8 mm | ≥ 0.15 / ≥ 1.0 | Footprint outlines and "RST" below guideline: cosmetic, may print faint |
| Inner corners (router Ø1.0 mm) | panel milled with 0.5 mm radius, the same as the board's J1 notch fillets | ≥ R0.5 | OK |

Also verified: KiCad 10 DRC 0/0 on the board and the panel, 0 schematic-parity issues; the panel was regenerated from the board with your silkscreen fix (2026-09-30 21:01) and holds four exact copies of it; BOM/CPL regenerate from the panel unchanged. The gerbers now carry separate `-PTH.drl` / `-NPTH.drl` files, as JLC asks, and the silkscreen is clipped at the mask. Each comes with a `-drl_map.gbr` drill map (added 2026-10-01; the drill files themselves are unchanged).

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

SLA, **8001 Resin, Translucent**. The Transparent grade is sanded and oil-sprayed by default, which changes the O-ring ledge and port-seal fits; Translucent is only de-supported, and the LED still shows through. Note for the order: *"The thinnest wall is at your SLA minimum (0.80 mm top over the side-port pocket); please print as modelled. Please keep support marks off the stepped bottom rim and the side port hole."* That note was wrong: the seal band at the bottom rim was 0.40 mm, and JLC3DP flagged it (2026-10-12). It is 0.80 mm now; see the changes at the end.

| Check | Design | JLC3DP SLA guideline | |
|---|---|---|---|
| Model | 1 valid solid, 40 × 40 × 8.43 mm, 2.76 cm³ (was 2.71); STL matches | min 5 × 5 × 5 mm | OK |
| Walls: top 1.33, side 1.5 mm | 1.33 / 1.5 | 1.0 for a ~50 mm part | OK |
| Seal band at the bottom rim (O-ring seat) | 0.80 mm wide × 0.75 mm high, rising 1.2 mm more inside; 5 round gaps (was 0.40 mm all round) | 0.8 | OK (at the minimum) |
| Band under the port flat, over the seal O-ring | 0.90 mm (was 0.45) | 0.5 for a 5 × 5 feature | OK |
| Cap top over the USB-C pocket | 0.80 mm, ~9 × 7 mm (was 0.67) | 0.8 for a 10 × 10 feature | OK (at the minimum) |
| Port-flat ends, counterbore rings, tube lip | 0.73–0.8 mm, features < 6 mm | 0.5 (5 × 5); fasteners ≥ 1.5 recommended | OK |
| Screw holes / counterbores | Ø2.4 for M2 (0.2/side, was Ø2.2), Ø4.2 for Ø3.8 heads (0.2/side); boss collar 0.7 mm | assembled clearance ≥ 0.2; holes ±0.3; wall 0.5 (5 × 5) | OK |
| Port seal squeeze | 0.15 mm per side | tolerance ±0.2 mm | Fit depends on the print (tune `connector.seal_squeeze`) |
| Enclosed cavities | none | escape holes only for closed cavities | OK |
| Heat | 8001 HDT 53 °C | – | Prototype only; don't leave it in a hot car |

## 4. Not in this package (buy separately)

O-rings 36/37/38 × 1.0 and 2 × 1.0 (VMQ/NBR 70A); 4× M2×5 cheese head, nylon patch (M2×8 until the standoffs below); 4× brass male-female hex standoffs M2×3+3, 3 mm across flats, studs ground to 2.0 mm or less (**ordered** 2026-10-12, Amazon.se ORANXIN B0CZNQCVJ1, 20 pcs); 304 tube Ø10 × 0.8, cut to 7.1 mm; a foam pad for the 1.6 mm gap above the cell; Kapton; LIR1254 cells; 0.15 × 2 mm nickel strip; the 5/16-24 button head bolt and bonded washer (README "Needs your input").

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
- **Fiducials moved to 3.85 mm from the rail edge** (was 2.5 mm), as JLC asks. Same size (1 mm copper, 2 mm opening), still 5 mm from the side, and the opening stays on the 5 mm rail. Regenerated: the panel and `shotpuck-panel-gerbers.zip`. Panel DRC: 0/0 on two of three runs; the other run showed only the known frame-corner outline artefact. Only the six fiducial flashes changed (copper, mask, silkscreen clearance); drill, outline, paste, zone fills, BOM and CPL are unchanged.
- **Tooling holes enlarged to Ø2.0 mm** (was 1.152 mm), as JLC prefers. Still three, 2.5 mm in from both rail edges; each hole's wall is 1.5 mm from the panel edge and 0.84 mm from the nearest fiducial's mask opening. Regenerated: the panel and `shotpuck-panel-gerbers.zip`. Panel DRC: 0/0 on two of three runs; the other run showed only the known frame-corner outline artefact. Only the three tooling holes changed (NPTH drill, mask, silkscreen clearance); outline, copper, paste, BOM and CPL are unchanged.
- **U2 moved 0.9 mm inward, away from the top tab** (Richard, by hand), with C3, C6 and C7 and their tracks. Its courtyard is now 1.1 mm from the tab's mouse bites (was 0.34 mm; JLC suggests 1.5 mm). **The top tab is 2.3 mm wide** at x = +0.8 (was 1.5 mm at +1.2), so it has 5 holes like the others, as JLC asks; the GND via stays 0.36 mm from the bites and H1's courtyard is 0.29 mm away. Regenerated: the panel, `shotpuck-panel-gerbers.zip` and the CPL. Board DRC: 0/0, 0 parity issues. Panel DRC: 0/0 on 8 of 8 runs (with the outline fix below). In the CPL only U2, C3, C6 and C7 moved (rotations unchanged); the BOM is unchanged.
- **The panel outline's intermittent DRC error is fixed.** KiKit left 9 outline segments shorter than 10 µm: one at the frame's top-left corner, the rest where tabs meet the 0.5 mm milling fillets. KiCad joins outline segments within a small tolerance, and these most likely made it close the frame wrongly on some runs ("self-intersecting outline"). `gen_panel.py` now merges them into their neighbours (the outline moves ≤ 4 µm) and refills the pours. Regenerated: the panel and `shotpuck-panel-gerbers.zip`. Pads, drill and drawings are unchanged; the GND pours differ by about 0.01 mm² per board.
- **The `JLCJLCJLCJLC` text is removed from the top rail.** JLC's marking page says order numbers are no longer available since May 2026 and asks not to add that text; left in, it would print literally on the frame. `gen_panel.py` no longer adds it. Regenerated: the panel and `shotpuck-panel-gerbers.zip`. Panel DRC: 0/0 on three runs. Compared with the previous panel, only that 15.2 × 1.8 mm piece of front silkscreen is gone; copper, mask, paste, outline and drill are geometrically identical, and the BOM and CPL are unchanged.
- **Package re-checked against `main` at 36e9f82.** Gerbers, drill files and drill maps exported fresh from the panel match `shotpuck-panel-gerbers.zip` exactly, timestamps aside. `gen_jlc.py` regenerates the BOM and CPL byte for byte. All four panel copies carry the moved U2, C3 and C7, and no `JLCJLCJLCJLC` text. The plate and cap files are identical to `mechanical/out`, and no plate dimension in `layout.json` has changed since the plate drawings were made.
- **U2 → LSM6DSV32XTR (C22461202).** Only `shotpuck-panel-bom.csv` changed in this package (the U2 line). Gerbers, drill files and drill maps exported fresh from the panel match `shotpuck-panel-gerbers.zip`, timestamps aside, and `gen_jlc.py` regenerates the CPL byte for byte: the part name lives only in the hidden F.Fab value field. Board DRC 0/0 with 0 parity issues, panel DRC 0/0 on three runs, ERC unchanged (0 errors), netlist check PASS.
- **4 assembled boards instead of 20.** PCBA qty 2 panels, and `gen_jlc.py` (`ASSEMBLED_COPIES = {2, 3}`) writes the BOM and CPL for the bottom-row copies only: 21 lines, 56 placements, each identical to its row in the full-panel CPL. Gerbers and drill files are unchanged. The top-row copies get solder paste but no parts; they are spares for fit tests. To assemble whole panels again, set `ASSEMBLED_COPIES = {0, 1, 2, 3}` and rerun `gen_jlc.py`.

Changes on 2026-10-12 (JLC3DP order 13698800A-W2026101020127812):
- **JLC3DP flagged the cap's seal band at 0.40 mm.** That band is the inner step of the bottom rim, between the cavity and the O-ring ledge, 0.75 mm high. The 2026-09-30 wall scan had missed it. It is now **0.80 mm** (`seal.band_wall`), grown inward to Ø36.2. Inside the cap it rises 0.6 mm above the step and then chamfers back to the wall over another 0.6 mm (Richard), so it meets the wall 1.95 mm above the plate. The ledge, O-ring and squeeze are unchanged.
- **Five round gaps** go fully through the band and its rise (Richard): circles around each standoff (R 2.8; the standoffs reach the old cavity wall) and around the cell (R 7.55; it can slide out to the cavity wall in its open PCB notch). They reach 0.4 mm past the ledge, and the band's corners are rounded R 0.2. The gaps are 3.8 mm open at each standoff and 8.0 mm at the cell, wider at the inner edge, and each band end tapers to its tip over 0.6 mm (1.1 mm at the cell). A standoff shifted 0.2 mm outward, or the cell anywhere in its travel, touches nothing in the band; the old band was hit in both cases. In a gap the O-ring is held down by the wall above, so its squeeze is unchanged.
- **Kapton:** cut it Ø35.7, 0.25 mm inside the band, with a tab about 6.5 mm wide under the cell, out to the cavity wall inside the cell gap. It must not reach under the band, or the cap seats high there.
- **Files:** the new `cap.step`/`cap.stl` in `3_Cap_JLC3DP` are for the "replace file" upload. The files first uploaded are in `3_Cap_JLC3DP/original_0.40mm_band/`, and `shotpuck-order-2026-09-30.zip` still holds them too. Only the band changed: the difference between the two caps lies entirely between 0 and 1.95 mm above the plate. Puck 16.78 g, COM 0.45 mm off axis.
- **Fit:** the band clears the Ø36.0 PCB by only 0.1 mm per side (was 0.5), less than the PCB outline (±0.2 mm) and SLA (±0.2 mm) tolerances. The cap goes on tilted, port side first so the USB-C ring clears the wall below its hole, then the far side is lowered. If the band catches on the board edge, ease its inner face (Richard will post-process).
- **Still below 0.8 mm:** the port-flat ends, counterbore rings and tube lip (0.73–0.8 mm, features under 6 mm; table above), and the tapered band ends at the gaps. If JLC3DP flags them, they are intended.
