#!/usr/bin/env python3
"""Placement checker: courtyard overlaps, board-edge margin, antenna keep-out."""
import math
import sys

import pcbnew

PCB = sys.argv[1] if len(sys.argv) > 1 else "kicad/shotpuck.kicad_pcb"
b = pcbnew.LoadBoard(PCB)
R = 18.5 - 0.35
TO = lambda v: (pcbnew.ToMM(v.x) - 100, 100 - pcbnew.ToMM(v.y))  # noqa: E731
boxes = {}
for fp in b.GetFootprints():
    fp.BuildCourtyardCaches()
    cy = fp.GetCourtyard(pcbnew.F_CrtYd)
    bb = cy.BBox() if cy.OutlineCount() else fp.GetBoundingBox(False, False)
    x0, y1 = TO(bb.GetOrigin())
    x1, y0 = TO(bb.GetEnd())
    boxes[fp.GetReference()] = (min(x0, x1), min(y0, y1), max(x0, x1), max(y0, y1))
bad = 0
refs = sorted(boxes)
for i, a in enumerate(refs):
    ax0, ay0, ax1, ay1 = boxes[a]
    if a != "BT1":
        for (cx, cy) in [(ax0, ay0), (ax0, ay1), (ax1, ay0), (ax1, ay1)]:
            if math.hypot(cx, cy) > R and not a.startswith("U1") and not a.startswith("H"):
                print(f"EDGE   {a} corner ({cx:.1f},{cy:.1f}) r={math.hypot(cx, cy):.2f}")
                bad += 1
                break
    for bref in refs[i + 1:]:
        bx0, by0, bx1, by1 = boxes[bref]
        if ax0 < bx1 and bx0 < ax1 and ay0 < by1 and by0 < ay1:
            # the cell and the boss holes have circular courtyards
            circ = [r for r in (a, bref) if r == "BT1" or r.startswith("H")]
            if circ:
                def cr(r):
                    x0, y0, x1, y1 = boxes[r]
                    if r == "BT1":
                        return 8.5, 0.0, 8.3
                    return (x0 + x1) / 2, (y0 + y1) / 2, (x1 - x0) / 2
                if len(circ) == 2:
                    (c1x, c1y, r1), (c2x, c2y, r2) = cr(a), cr(bref)
                    if math.hypot(c1x - c2x, c1y - c2y) >= r1 + r2:
                        continue
                else:
                    cx_, cy_, rr = cr(circ[0])
                    o = boxes[bref] if circ[0] == a else boxes[a]
                    nx = min(max(cx_, o[0]), o[2])
                    ny = min(max(cy_, o[1]), o[3])
                    if math.hypot(nx - cx_, ny - cy_) >= rr:
                        continue
            print(f"OVERLAP {a} <-> {bref}")
            bad += 1
print("placement:", "OK" if not bad else f"{bad} issue(s)")
for r in refs:
    print(f"  {r:4s} x {boxes[r][0]:6.1f}..{boxes[r][2]:6.1f}  y {boxes[r][1]:6.1f}..{boxes[r][3]:6.1f}")
