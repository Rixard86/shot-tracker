import math

import pcbnew

GND = "GND"
GRID_MM = 0.1
VIA_KEEP_MM = 0.26
VIA_SPACING_MM = 0.6
PAD_MARGIN_MM = 0.3
RING_POINTS = 8
VIA_DIA_MM = 0.5
VIA_DRILL_MM = 0.3
COPPER = (pcbnew.F_Cu, pcbnew.B_Cu)


def fragments(board):
    out = []
    for z in board.Zones():
        if z.GetIsRuleArea() or z.GetNetname() != GND:
            continue
        for layer in COPPER:
            if z.IsOnLayer(layer):
                polys = z.GetFilledPolysList(layer)
                out += [(layer, polys, i) for i in range(polys.OutlineCount())]
    return out


def joints(board):
    pts = [v.GetPosition() for v in board.GetTracks()
           if v.Type() == pcbnew.PCB_VIA_T and v.GetNetname() == GND]
    pts += [p.GetPosition() for fp in board.GetFootprints() for p in fp.Pads()
            if p.GetNetname() == GND and p.GetAttribute() == pcbnew.PAD_ATTRIB_PTH]
    return pts


def groups(frags, pts):
    parent = list(range(len(frags)))

    def root(k):
        while parent[k] != k:
            k = parent[k]
        return k

    for pt in pts:
        hits = [root(k) for k, (_l, polys, i) in enumerate(frags) if polys.Contains(pt, i)]
        for k in hits[1:]:
            parent[k] = hits[0]
    return [root(k) for k in range(len(frags))]


def fits(frag, pt):
    _layer, polys, i = frag
    r = pcbnew.FromMM(VIA_KEEP_MM)
    ring = [pcbnew.VECTOR2I(pt.x + int(r * math.cos(2 * math.pi * k / RING_POINTS)),
                            pt.y + int(r * math.sin(2 * math.pi * k / RING_POINTS)))
            for k in range(RING_POINTS)]
    return all(polys.Contains(p, i) for p in ring + [pt])


def clear_of_parts(board, pt):
    near = pcbnew.FromMM(VIA_SPACING_MM)
    for v in board.GetTracks():
        vp = v.GetPosition()
        if v.Type() == pcbnew.PCB_VIA_T and math.hypot(vp.x - pt.x, vp.y - pt.y) < near:
            return False
    margin = pcbnew.FromMM(PAD_MARGIN_MM)
    for fp in board.GetFootprints():
        for p in fp.Pads():
            bb = p.GetBoundingBox()
            bb.Inflate(margin)
            if bb.Contains(pt):
                return False
    return True


def spot(board, pair):
    island, target = pair
    bb = island[1].Outline(island[2]).BBox()
    step = pcbnew.FromMM(GRID_MM)
    for x in range(bb.GetLeft(), bb.GetRight(), step):
        for y in range(bb.GetTop(), bb.GetBottom(), step):
            pt = pcbnew.VECTOR2I(x, y)
            if fits(island, pt) and fits(target, pt) and clear_of_parts(board, pt):
                return pt
    return None


def add_via(board, pt):
    via = pcbnew.PCB_VIA(board)
    via.SetPosition(pt)
    via.SetWidth(pcbnew.FromMM(VIA_DIA_MM))
    via.SetDrill(pcbnew.FromMM(VIA_DRILL_MM))
    via.SetLayerPair(pcbnew.F_Cu, pcbnew.B_Cu)
    via.SetNet(board.FindNet(GND))
    board.Add(via)


def bridge_once(board, frags):
    g = groups(frags, joints(board))
    areas = [polys.Outline(i).Area() for (_l, polys, i) in frags]
    main = g[areas.index(max(areas))]
    for k, frag in enumerate(frags):
        if g[k] == main:
            continue
        for t, target in enumerate(frags):
            if g[t] == main and target[0] != frag[0]:
                pt = spot(board, (frag, target))
                if pt is not None:
                    add_via(board, pt)
                    return True
        print(f"islands: no bridge for GND fragment {k} on {pcbnew.LayerName(frag[0])}")
    return False


def bridge_islands(board):
    n = 0
    while bridge_once(board, fragments(board)):
        n += 1
    print(f"islands: {n} bridging GND vias")
    return n
