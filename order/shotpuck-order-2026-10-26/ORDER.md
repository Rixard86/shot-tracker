# ShotPuck rev 0B: order package, 2026-10-26

Rev 0B board: `main` at 43c1643, with the panel, `hardware/kicad/fab/` and this package regenerated on 2026-10-26. It replaces the rev 0A PCB + PCBA order, which JLC cancelled on 2026-10-26 (`../shotpuck-order-2026-09-30/ORDER.md`, changes on 2026-10-26). The only design change from rev 0A: **R9 is fed from VIN_RAW** (README D27), so plugging in USB-C no longer takes the nRF VBUS pin over its 5.8 V absolute maximum. The silkscreen says "rev. 0B".

**Two boards per panel** (Richard, 2026-10-26): JLC assembles every board on a panel and at least 2 panels, so a 1×2 panel gives **4 assembled boards** (the 2×2 panel gave 8). The frame's top and bottom rails are 14 mm wide so the panel is 70 mm tall, JLC's minimum for assembly.

**Ordered** (Richard, 2026-10-26): JLCPCB PCBA order **SMT026102665400**, from the files in this folder (`../shotpuck-order-2026-10-26.zip` is the copy as uploaded). Quote $191.76 before shipping: PCB $33.98 (engineering $4.00, ENIG $17.30, board $3.10, X-out not accepted $8.53, confirm production file $1.05) and PCBA $157.78 (setup $25.75, stencil $8.27, parts $75.16 for 21 lines, feeder loading $32.55, SMT $1.77, confirm parts placement $0.45, X-ray $13.20, packaging $0.50, special components $0.12). The assembly remark was "quote after review". Build time 3 days (PCB) and 3–4 days (assembly). See the changes at the end.

| Folder | Service | Upload |
|---|---|---|
| `1_PCB_JLCPCB` | JLCPCB PCB + Standard PCBA | `shotpuck-panel-gerbers.zip`, then `shotpuck-panel-bom.csv` + `shotpuck-panel-cpl.csv` |

**Cap and plate are unchanged.** Their files are in `../shotpuck-order-2026-09-30/`: `3_Cap_JLC3DP/cap.step` (the 0.8 mm-band cap JLC3DP accepted) and `2_Plate_JLCCNC/`. On 2026-10-26 they matched `mechanical/out` (the cap STEP differs only in its timestamp). The cap was a separate JLC3DP order (13698800A-W2026101020127812): if it is still active, do not order the cap again.

## Before you order

1. In JLC's Order History, check that the refund for the cancelled order (Y2) has arrived, and whether the cap order is still active.
2. **Pay every requote.** If JLC's review changes the price (parts, replaced files), the order waits for payment in Order History, and no email may come. Unpaid, it is cancelled when the component reservation runs out, as the rev 0A order was.

## PCB + assembly (JLCPCB)

| Setting | Value |
|---|---|
| Layers / material | 2, FR-4 |
| Dimensions | 91.1 × 70.1 mm (read from the gerbers) |
| PCB qty | 5 panels (JLC's minimum) = 10 boards, **4 of them assembled** |
| Delivery format | Panel by Customer, 1 design (1×2 inside, frame, tooling holes and fiducials already in) |
| Thickness / finish | **1.2 mm** / ENIG |
| Solder mask / silkscreen | **Black** / white |
| Copper / via covering | 1 oz / Tented |
| Min via hole | 0.3 mm (no surcharge) |
| Order number / marking | Leave the default, **no mark**. Do **not** pick "Specify a location" |
| Confirm production file | **Yes** (as on rev 0A; JLC recommends it with assembly, and it showed their CAM edits, NPTH +0.05 mm) |
| PCBA type | **Standard** (U1 is an LGA) |
| Assembly side / PCBA qty | Top / **2** panels = 4 boards (JLC assembles only whole panels) |
| Tooling holes | Added by customer |
| Confirm parts placement | Yes |
| Parts selection | By Customer (Self-Service) |
| Depanel boards & edge rail before delivery | No (cut the tabs yourself) |
| Assembly remark | **Yes**: the note below |
| X-ray | U1, U2. Not an option on the form: on the rev 0A order it appeared as a charge in the quote ($13.28); check it is there after the BOM step |
| Shipping | Global Standard Direct Line if offered (duty and Swedish VAT paid at checkout); otherwise DHL Express, which charges VAT and a clearance fee on delivery |

Note for the order. It also answers the questions JLC asked on the rev 0A order: *"Please X-ray U1 (BMD-340, LGA) and U2 (LGA-14). Both boards on each panel are to be assembled. BT1, J2, JP1 and JP2 are bare pads: do not buy or place parts for them. J1 (C3020041) is a mid-mount USB-C in a board-edge notch; its front sticks out 1.3 mm past the board edge into the routed gap, and its shell pads are larger than your library's on purpose. Please solder J1 as placed (option D, as on our previous order SMT026101963486)."* Put it in the **Assembly remark** (500 characters; the note is 437), not the PCB Remark (200 characters; leave it empty).

**Files.** The gerbers are the rev 0B panel, exported on 2026-10-26 (= `hardware/kicad/fab/panel/shotpuck-panel-gerbers.zip`). `gen_jlc.py` wrote the BOM and CPL (`ASSEMBLED_COPIES = {0, 1}`): 21 lines, 56 placements. The two boards are the bottom row of the 2×2 panel moved up one row: every CPL row equals that row's `_2`/`_3` row, renamed `_0`/`_1` and moved 32.1694 mm in Y, rotation unchanged. Those rows were in the rev 0A order as placed, passed the placement simulation (d86eec3) and looked right in JLC's placement preview (2026-10-15). The BOM has the same 21 parts as the rev 0A BOM, with half the designators. Its U4 line names BQ29700DSET (C2876282), as JLC saw it; `gen_jlc.py` writes the design value BQ29700DSER there.

**JLC questions to expect** (asked on the rev 0A order): parts for BT1, J2, JP1 and JP2: no. J1 "the package cannot match the pads": answer D, solder as is. J1's pads and position are unchanged since that check (rev 0A `ORDER.md`, changes on 2026-10-22).

## Checks (2026-10-26)

| Check | Result |
|---|---|
| Board | KiCad 10.0.6 DRC: 0 violations, 0 unconnected, 0 schematic-parity issues. `check_netlist.py` PASS (117 pin assignments, 29 nets). ERC: 0 errors, the 2 known warnings |
| Hot-plug (`hotplug_sim.py`) | VBUS_SNS > 5.8 V in 0 of 324 cases (max 5.47 V), VIN > 7.0 V in 0. Rev 0A: 9 cases over 5.8 V, up to 6.36 V |
| Panel | `gen_panel.py`: 1×2, 3 mm between the boards and to the frame, frame 5 mm at the sides and 14 mm top and bottom (`mintotalheight: 70mm`), no backbone or rotation. DRC 0/0 on 5 of 5 runs. Both copies match the board: every track, via and pad within 4 µm |
| Panel against the 2×2 panel's bottom row | Same tabs and mouse bites (5–6 holes, Ø0.5, top-tab bites within 1 µm), U2 courtyard 1.12 mm and H1 courtyard 0.29 mm from the top-tab bites as before. Paste identical; mask, silkscreen and drill differ only on the frame (the corner cuts run across the wider rails, 18 holes instead of 6); GND pours inside the boards within 0.005 mm² |
| Rev 0B against rev 0A, per board | Changed: R9 pad 1 joins the VIN_RAW track by D3 (front copper) and the old VIN branch (6 tracks, 2 vias) is gone; "rev. 0A" → "rev. 0B" (0.7 × 1.0 mm of front silkscreen); the GND pours fill where the branch was (+5.2 mm² back, +3.8/−0.2 mm² front, all within 6 mm of R9). Parts, outline, mask and paste unchanged |
| JLC stock for 4 boards | All 21 lines in stock. Thinnest: D1/D2 BAT54J C130415, 58 (8 needed); U1 BMD-340 C5456944, 416; J1 C3020041, 736; U4 BQ29700DSET C2876282, 767 |

Price guide: the rev 0A order with 4 assembled boards was quoted PCB $35.37 + PCBA $151.71 (parts $69.09), plus $26.79 shipping with the cap. Expect about the same: both panel sizes are under 100 × 100 mm. JLC's quote is the real figure.

## Risks to check on the first boards

The same as for rev 0A (`../shotpuck-order-2026-09-30/ORDER.md` §5): J1's fit in the notch, the faint silkscreen, the cap's fits.

Changes on 2026-10-27 (JLCPCB PCBA order SMT026102665400):
- **JLC (Swee) asked about the assembly remark**: is special handling needed for some components, or should they ignore the remark and continue? Answer drafted: no special handling; X-ray is already in the order, BT1/J2/JP1/JP2 are not in the BOM/CPL, solder J1 at the CPL position (option D).
- **JLC's DFM is being regenerated** by their engineers; the order page's "Confirm" button activates when the final one is ready (Confirm Parts Placement and Confirm Production file are both on). Check it against the panel before confirming.

Changes on 2026-10-28 (JLCPCB PCBA order SMT026102665400):
- **Production file `shotpuck-panel-gerbers_Y15.zip` (13698800A_Y15) checked**, OK to confirm. The 14 gerbers in its input zip are byte-identical to `1_PCB_JLCPCB/shotpuck-panel-gerbers.zip`. JLC's CAM output, moved back from its origin (the panel corner, our (103.0, −90.0)): outline within 0.1 µm; all 273 drills at our positions, vias 0.30 mm unchanged, **all NPTH +0.05 mm** as on rev 0A (mouse bites Ø0.55, J1 pegs Ø0.75, 2.0/2.4 mm holes Ø2.05/2.45); copper edges +8 µm, islands one-to-one (199 front, 35 back); mask only opened (along the routed edges, rings round the NPTH, the routed areas), no copper newly exposed, no opening closed; 120 silkscreen tips trimmed at pads (0.45 mm² in all), back silkscreen unchanged; SMT QR code on both sides of the bottom rail at x 117.8–130.3. Job: FR-4, 2 layers, 1.2 mm, 1 oz, ENIG, black mask, white silkscreen, vias tented, no order number, 5 panels of 2×1.
- **Parts placement preview checked** (Top/Bottom images), OK to confirm: 56 assembled, 0 unassembled, nothing on the bottom. With our pad centres overlaid from the panel, every part on both boards sits on its pads; the pin-1 marks of U1, U2, U3, U4, Q1 and Q2 are on our pad 1; D1/D2 have the cathode band and "−" on pad 1 and "+" on pad 2; D4's pin-1 (cathode) mark is on pad 1. Zoomed in JLC's live viewer: D4 has JLC's pin-1 dot on the pad-1 terminal, its "−" on our silkscreen dot at that end and "+" at the pad-2 end; U4's model pin-1 dot is at the pad-1 corner, and JLC's two large marks sit on the outer ends of pads 1 and 3 (meaning unknown; both on the pin-1 side, so not a 180° turn). "Show Revised Items" lists only J1_0 and J1_1: on both, JLC's revised body lies on our F.Fab outline and its leg marks inside our Ø0.7 peg holes, front out into the routed gap, so the revision is below what the image shows (about 0.1 mm, 1°).

Changes on 2026-10-31 (JLCPCB PCBA order SMT026102665400):
- **X-ray report checked, no fault found** (JLC email 2026-10-31, `SMT026102665400-13698800A+X-ray+Inspection+Report.zip`: `13698800A-Y15/1.png`–`4.png`, one per assembled board; 1 and 3 have the neighbouring board on the image's left, 2 and 4 on its right, so each panel position is there twice). The images are the top view turned 180°, 26.1–26.2 px/mm, not mirrored (fitted to our F.Cu pads). U2: a joint on each of the 14 pads on all four boards, on the pad, every gap much lighter than the joints. U1: a joint on each of the 68 LGA pads, the same on all four boards; the only gap darker on one board than on the others (pads 42–43, image 2) is a part inside the module, seen on all four. J1: the 6 signal joints separate. The grey dots outside the boards are the rail fiducials. Not visible at about 38 µm per pixel: voiding, head-in-pillow, opens; first power-up and the SWD connection test those.

Changes on 2026-11-02 (JLCPCB PCBA order SMT026102665400):
- **JLC (Violet) asked about J1's waterproof ring** (email "Production issue about 13698800A SMT026102665400_Y15", 08:12): C3020041 comes in two pieces, the connector and its silicone ring, and the ring cannot be fitted by machine. Option 1: fit the ring onto the connector by hand after OQC visual inspection, before shipment. Option 2: throw the ring away. The manual work is a supplementary order that falls under JLC's small-value waiver, so there is nothing to pay. **Richard answered option 1** (08:15). The ring is the port seal (`layout.json` connector: 10.44 × 4.66 × 0.95 mm, squeezed 0.15 mm per side in the cap wall's hole), so it is needed. On the first boards, check that each J1 has its ring and that it sits fully back on the body.
