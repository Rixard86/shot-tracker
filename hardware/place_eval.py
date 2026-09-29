import math
import os
import sys

import pcbnew

import gen_pcb

POUR_NETS = {"GND", ""}
LENGTH_WEIGHT = 0.05
LOCK_TOUCH_MM = 0.05
FIXED_REFS = {"U1", "J1", "J2", "BT1", "H1", "H2", "H3", "H4"}
BOARD = os.path.join(gen_pcb.KI, "shotpuck.kicad_pcb")


def layout_xy(v):
    return pcbnew.ToMM(v.x), -pcbnew.ToMM(v.y)


def rotate(pt, deg):
    a = math.radians(deg)
    return pt[0] * math.cos(a) - pt[1] * math.sin(a), pt[0] * math.sin(a) + pt[1] * math.cos(a)


def part_of(fp):
    pos = layout_xy(fp.GetPosition())
    rot = fp.GetOrientationDegrees()
    pads = []
    for p in fp.Pads():
        q = layout_xy(p.GetPosition())
        pads.append((rotate((q[0] - pos[0], q[1] - pos[1]), -rot), p.GetNetname(), p.GetNumber()))
    layer = pcbnew.B_CrtYd if fp.IsFlipped() else pcbnew.F_CrtYd
    court = fp.GetCourtyard(layer)
    ring = [layout_xy(court.Outline(0).CPoint(i)) for i in range(court.Outline(0).PointCount())] \
        if court.OutlineCount() else []
    return {"pos": pos, "rot": rot, "bottom": fp.IsFlipped(), "pads": pads,
            "court": [rotate((x - pos[0], y - pos[1]), -rot) for x, y in ring]}


def locked_nodes(board):
    nodes = []
    for t in board.GetTracks():
        if not t.IsLocked() or t.GetNetname() in POUR_NETS:
            continue
        ends = [t.GetPosition()] if t.Type() == pcbnew.PCB_VIA_T else [t.GetStart(), t.GetEnd()]
        nodes += [(layout_xy(e), t.GetNetname()) for e in ends]
    return nodes


def model_from_board(board):
    parts = {fp.GetReference(): part_of(fp) for fp in board.GetFootprints()}
    return {"parts": parts, "locked": locked_nodes(board)}


def pad_points(part):
    return [((part["pos"][0] + rotate(off, part["rot"])[0], part["pos"][1] + rotate(off, part["rot"])[1]), net)
            for off, net, _num in part["pads"]]


def net_nodes(model):
    nets = {}
    for part in model["parts"].values():
        for pt, net in pad_points(part):
            if net not in POUR_NETS:
                nets.setdefault(net, ([], []))[0].append(pt)
    for pt, net in model["locked"]:
        points, seeds = nets.setdefault(net, ([], []))
        points.append(pt)
        seeds.append(pt)
    return nets


def mst(spec):
    points, seeds = spec
    pts = list(dict.fromkeys(points))
    if len(pts) < 2:
        return []
    inside = {i for i, p in enumerate(pts) if any(math.dist(p, q) < LOCK_TOUCH_MM for q in seeds)} or {0}
    edges = []
    while len(inside) < len(pts):
        best = min(((math.dist(pts[i], pts[j]), i, j) for i in inside for j in range(len(pts)) if j not in inside))
        inside.add(best[2])
        edges.append((pts[best[1]], pts[best[2]]))
    return edges


def side(seg, r):
    p, q = seg
    return (q[0] - p[0]) * (r[1] - p[1]) - (q[1] - p[1]) * (r[0] - p[0])


def cross(ab, cd):
    return side(ab, cd[0]) * side(ab, cd[1]) < 0 and side(cd, ab[0]) * side(cd, ab[1]) < 0


def airwires(model):
    return [(net, e) for net, spec in net_nodes(model).items() for e in mst(spec)]


def score(model):
    wires = airwires(model)
    crossings = sum(1 for i, (n1, e1) in enumerate(wires) for n2, e2 in wires[i + 1:] if n1 != n2 and cross(e1, e2))
    length = sum(math.dist(*e) for _n, e in wires)
    return {"crossings": crossings, "length": round(length, 1), "score": round(crossings + LENGTH_WEIGHT * length, 2)}


def main():
    for path in sys.argv[1:] or [BOARD]:
        print(path, score(model_from_board(pcbnew.LoadBoard(path))))


if __name__ == "__main__":
    main()
