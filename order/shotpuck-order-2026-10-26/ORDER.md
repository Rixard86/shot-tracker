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
