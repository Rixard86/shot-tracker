import csv
import os
import sys

import pcbnew

import design

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_BOARD = os.path.join(HERE, "kicad", "panel", "shotpuck-panel.kicad_pcb")
OUT_DIR = os.path.join(HERE, "kicad", "fab", "jlc")
HAND_ASSEMBLED = {"BT1", "J2", "JP1", "JP2"}
PANEL_SUFFIX = "_"

LCSC = {
    "BMD-340-A-R": "C5456944",
    "LSM6DSO32TR": "C1864162",
    "MCP73831T-2ACI/OT": "C424093",
    "BQ29700DSER": "C183096",
    "DMN63D8LDW": "C211422",
    "DMN2004DWK": "C156343",
    "BAT54J": "C130415",
    "SMF5.0CA": "C908214",
    "LED yellow-green 0402": "C22470306",
    "4.7u 10V": "C19666",
    "10u 10V": "C19702",
    "4.7u 6.3V": "C23733",
    "100n": "C1525",
    "47k 1%": "C25792",
    "1M": "C26083",
    "100k": "C25741",
    "1k": "C11702",
    "330R": "C25104",
    "2.2k": "C25879",
    "5.1k 1%": "C25905",
    "TYPE-C 6PFS 2JCB1.6-H6.7 IPX8": "C3020041",
}


def assembled_parts():
    parts = {}
    for (ref, lib, fp, value, _pins, extra) in design.PARTS:
        if lib.startswith("Mechanical:") or extra.get("dnp") or ref in HAND_ASSEMBLED:
            continue
        parts[ref] = (value, fp.split(":")[1])
    return parts


def base_ref(ref):
    return ref.rsplit(PANEL_SUFFIX, 1)[0] if PANEL_SUFFIX in ref else ref


def placements(board, parts):
    rows = []
    for fp in board.GetFootprints():
        ref = fp.GetReference()
        if base_ref(ref) not in parts:
            continue
        pos = fp.GetPosition()
        rows.append((ref, pcbnew.ToMM(pos.x), -pcbnew.ToMM(pos.y), fp.GetOrientationDegrees()))
    return sorted(rows)


def write_cpl(rows, path):
    with open(path, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["Designator", "Mid X", "Mid Y", "Layer", "Rotation"])
        for ref, x, y, rot in rows:
            w.writerow([ref, f"{x:.4f}mm", f"{y:.4f}mm", "Top", f"{rot % 360:.1f}"])


def write_bom(rows, parts):
    groups = {}
    for ref, _x, _y, _r in rows:
        value, footprint = parts[base_ref(ref)]
        groups.setdefault((value, footprint), []).append(ref)
    path = os.path.join(OUT_DIR, "shotpuck-panel-bom.csv")
    with open(path, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["Comment", "Designator", "Footprint", "LCSC Part #"])
        for (value, footprint), refs in sorted(groups.items()):
            w.writerow([value, ",".join(sorted(refs)), footprint, LCSC[value]])
    return path, len(groups)


def main():
    board_path = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_BOARD
    os.makedirs(OUT_DIR, exist_ok=True)
    parts = assembled_parts()
    missing = sorted({v for v, _f in parts.values()} - set(LCSC))
    if missing:
        raise SystemExit(f"no LCSC part number for: {missing}")
    rows = placements(pcbnew.LoadBoard(board_path), parts)
    cpl = os.path.join(OUT_DIR, "shotpuck-panel-cpl.csv")
    write_cpl(rows, cpl)
    bom, n_lines = write_bom(rows, parts)
    print(f"wrote {bom} ({n_lines} lines) and {cpl} ({len(rows)} placements)")


if __name__ == "__main__":
    main()
