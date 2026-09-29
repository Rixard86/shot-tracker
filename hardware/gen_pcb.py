#!/usr/bin/env python3
"""
gen_pcb.py - builds kicad/shotpuck.kicad_pcb from design.py + ../layout.json,
with the KiCad 10 pcbnew Python API.

  * round 2-layer board, 4 NPTH holes for the cap screws
  * custom footprints written to kicad/ShotPuck.pretty
  * placement from layout.json (big parts) and PLACE below (small parts)
  * keep-outs: antenna to board rim, cell notch + bolt-tube slot, screw bosses
  * GND pours both layers
  * routes with Freerouting if available (FREEROUTING_JAR), then DRC report

    python gen_pcb.py            # place + route + DRC
    python gen_pcb.py --no-route

Routing takes 1-5 min (Freerouting 2.1, Java 21: set JAVA and FREEROUTING_JAR).
"""
import json
import math
import os
import shutil
import re
import subprocess
import sys
import time

import pcbnew

import bmd340
import design
import board_shape as shape
import islands
import sexp
from gen_sch import U

HERE = os.path.dirname(os.path.abspath(__file__))
KI = os.path.join(HERE, "kicad")
LIB = os.path.join(KI, "ShotPuck.pretty")
STD = os.environ.get("KICAD10_FOOTPRINT_DIR", r"C:\Program Files\KiCad\10.0\share\kicad\footprints")
L = json.load(open(os.path.join(HERE, "..", "layout.json")))
JAR = os.environ.get("FREEROUTING_JAR", os.path.expanduser("~/tools/freerouting-2.1.0.jar"))
JAVA = os.environ.get("JAVA", os.path.expanduser("~/tools/jre21/bin/java.exe"))

MM = pcbnew.FromMM
CELL_PAD_W = 2.5
CELL_PAD_H = 2.0
CELL_COURTYARD = 0.3
CONN_COURTYARD_MARGIN = 0.5
PLUS_MARK_GAP = 0.3
PLUS_MARK_HALF = 0.6
CUTOUT_COPPER_KEEP = 0.35
CUTOUT_VIA_MARGIN = 0.65
ANTENNA_VIA_MARGIN = 0.6
ANTENNA_SEG_MARGIN = 0.3
ANTENNA_STITCH_MARGIN = 0.8
EDGE_WIDTH_MM = 0.1
BACK_TEXT_POS = (0.0, 15.3)
BOTTOM = "B"
TRACK_MM = 0.15
VIA_DIA_MM = 0.55
CLEARANCE_MM = 0.127
GND_NET = "GND"
SOLID_GND_REFS = {"U2", "J2", "Q2"}
ROUTER_JOB_MARGIN_S = 20
SECOND_PASS_SUFFIX = "-pass2.kicad_pcb"
PRE_ROUTED = [("Q2", "3", "6"), ("Q1", "4", "1")]
PRE_LINKS = [("U1", "21", "U2", "14", ((-6.3, 11.6),)),("U1", "23", "U2", "13", ((-7.3, 10.5), (-8.3, 11.5))),
             ("U1", "19", "U2", "4", ((-6.4, 12.7), (-7.1, 13.35), (-10.95, 13.35))),
             ("U1", "31", "R4", "1", ()),
             ("U4", "2", "Q2", "2", ()),
             ("Q1", "5", "R3", "1", ((-10.81, 2.2),)),
             ("U4", "3", "Q2", "5", ((-12.7, -7.6), (-12.1, -7.0), (-11.6, -6.3), (-9.0, -6.3), (-9.0, -8.2))),
             ("C1", "2", "U1", "45", ((5.75, -2.275), (5.75, 1.9), (4.9, 3.0), (4.4, 3.65), (3.96, 4.16),
                                      (3.88, 5.2)))]
PRE_ROUTE_WIDTH_MM = 0.2
PRE_VIAS = [("U1", "43", (-0.25, -0.75)), ("U1", "44", (0.05, -1.55)), ("U1", "33", (0.0, -0.75)),
            ("U1", "17", (0.55, 0.55)), ("U1", "18", (1.15, -0.25)), ("Q2", "1", (0.55, 0.71))]
PAD_BRIDGES = [("U1", "45", "46"), ("U1", "2", "1"), ("U1", "4", "3"), ("U2", "1", "2"), ("U2", "3", "2")]
VIN_ESCAPE = ("C1", "1", ((6.45, 2.9), (5.6, 3.75), (4.95, 3.95)),
              ((3.96, 4.16), (3.96, 15.2), (-6.8, 15.2), (-6.8, 3.25), (-7.75, 3.25)), ("R9", "1"))
TRACK_SAMPLE_MM = 0.3
FANOUT_TRACK_CLEAR_MM = 0.5
VIA_DRILL_MM = 0.3
SWD_PAD_DIA = 1.0
SWD_PITCH = 2.54
SWD_PADS = 4
SWD_COURTYARD = 0.5
BOARD_ATTEMPTS = 4
VIA_ECHO_TOL_MM = 0.01


def V(x, y):
    """board coords (mm, +y up) -> KiCad internal (y down)."""
    return pcbnew.VECTOR2I(MM(x), MM(-y))


# small parts: ref -> (x, y, rot_deg[, "B" for the bottom side])   board coords, viewed from top
PLACE = {
    "C3": (-10.7, 8.0, 180), "C4": (-12.8, 7.3, 90), "C5": (-10.7, 9.1, 180),
    "U2": (-10.2, 11.5, 270), "C6": (-12.4, 11.2, 90), "C7": (-12.6, 9.2, 90),
    "R9": (-8.8, 2.4, 270), "C9": (-7.8, 1.8, 90),
    "R4": (-4.6, 4.44, 180), "D4": (-16.95, 2.6, 90),
    "U3": (-12.4, -1.5, 0), "Q1": (-13.2, 2.2, 0), "R1": (-15.2, -1.5, 90), "R2": (-15.6, 1.6, 90),
    "R3": (-10.3, 2.2, 90), "D2": (-9.0, -1.3, 90),
    "C2": (-13.4, -4.1, 0), "JP1": (-10.2, -5.0, 0, BOTTOM), "R7": (-15.4, -5.3, 90),
    "U4": (-13.4, -8.65, 180), "Q2": (-10.6, -8.5, 0), "C8": (-13.6, -6.9, 0), "R8": (-15.2, -7.4, 90),
    "C1": (6.45, -1.5, 270), "D1": (8.2, -6.8, 0), "D3": (9.3, -3.4, 270),
    "J2": (-0.7, 13.0, 180, BOTTOM),
}


# ---------------------------------------------------------------- footprints
def pad(fp, num, shape, x, y, w, h, smd=True, drill=None, npth=False, layers=None):
    p = pcbnew.PAD(fp)
    p.SetNumber(num)
    p.SetShape(shape)
    p.SetSize(pcbnew.VECTOR2I(MM(w), MM(h)))
    p.SetPosition(pcbnew.VECTOR2I(MM(x), MM(y)))
    if npth:
        p.SetAttribute(pcbnew.PAD_ATTRIB_NPTH)
        p.SetDrillSize(pcbnew.VECTOR2I(MM(drill), MM(drill)))
        p.SetLayerSet(p.UnplatedHoleMask())
    elif smd:
        p.SetAttribute(pcbnew.PAD_ATTRIB_SMD)
        p.SetLayerSet(p.SMDMask())
    else:
        p.SetAttribute(pcbnew.PAD_ATTRIB_PTH)
        p.SetDrillSize(pcbnew.VECTOR2I(MM(drill), MM(drill)))
        p.SetLayerSet(p.PTHMask())
    fp.Add(p)
    return p


def line(fp, layer, x1, y1, x2, y2, w=0.12):
    s = pcbnew.PCB_SHAPE(fp, pcbnew.SHAPE_T_SEGMENT)
    s.SetStart(pcbnew.VECTOR2I(MM(x1), MM(y1)))
    s.SetEnd(pcbnew.VECTOR2I(MM(x2), MM(y2)))
    s.SetLayer(layer)
    s.SetWidth(MM(w))
    fp.Add(s)


def rect(fp, layer, w, h, w_line=0.05, cx=0.0, cy=0.0):
    x1, y1, x2, y2 = cx - w / 2, cy - h / 2, cx + w / 2, cy + h / 2
    for a in [(x1, y1, x2, y1), (x2, y1, x2, y2), (x2, y2, x1, y2), (x1, y2, x1, y1)]:
        line(fp, layer, *a, w=w_line)


def circle(fp, layer, r, w=0.05, cx=0.0, cy=0.0):
    s = pcbnew.PCB_SHAPE(fp, pcbnew.SHAPE_T_CIRCLE)
    s.SetCenter(pcbnew.VECTOR2I(MM(cx), MM(cy)))
    s.SetEnd(pcbnew.VECTOR2I(MM(cx + r), MM(cy)))
    s.SetLayer(layer)
    s.SetWidth(MM(w))
    fp.Add(s)


def new_fp(name, descr):
    fp = pcbnew.FOOTPRINT(None)
    fp.SetFPID(pcbnew.LIB_ID("ShotPuck", name))
    fp.SetLibDescription(descr)
    fp.Reference().SetLayer(pcbnew.F_Fab)
    fp.Value().SetLayer(pcbnew.F_Fab)
    return fp


def make_lib():
    os.makedirs(LIB, exist_ok=True)
    bmd340.save_footprint(LIB)
    cell_footprint()
    swd_footprint()

    k = L["connector"]
    fp = new_fp("MagPogo_Samzo_2P", "Samzo PR5L4015-2P-C-F magnetic receptacle (drawing GZ0254-P001): "
                "2 contacts at %.2f mm, magnets %.1f mm apart; pin 1 = + (VIN_RAW)."
                % (k["pitch"], k["magnet_pitch"]))
    pd = k["pad_dia"]
    pad(fp, "1", pcbnew.PAD_SHAPE_RECT, -k["pitch"] / 2, 0, pd, pd, smd=False, drill=k["pin_drill"])
    pad(fp, "2", pcbnew.PAD_SHAPE_CIRCLE, k["pitch"] / 2, 0, pd, pd, smd=False, drill=k["pin_drill"])
    rect(fp, pcbnew.F_SilkS, k["body_w"], k["body_l"], 0.12)
    rect(fp, pcbnew.F_CrtYd, k["body_w"] + CONN_COURTYARD_MARGIN, k["body_l"] + CONN_COURTYARD_MARGIN)
    rect(fp, pcbnew.F_Fab, k["body_w"], k["body_l"], 0.1)
    pcbnew.FootprintSave(LIB, fp)

    b = L["bosses"]
    fp = new_fp("MountingHole_2.4mm_NPTH", "M2 clearance, cap boss Ø%.1f bears on board" % b["boss_dia"])
    pad(fp, "", pcbnew.PAD_SHAPE_CIRCLE, 0, 0, b["pcb_hole"], b["pcb_hole"], drill=b["pcb_hole"], npth=True)
    circle(fp, pcbnew.Cmts_User, b["boss_dia"] / 2)
    circle(fp, pcbnew.F_CrtYd, b["boss_dia"] / 2 + 0.25)
    pcbnew.FootprintSave(LIB, fp)

    open(os.path.join(KI, "fp-lib-table"), "w").write(
        '(fp_lib_table\n  (lib (name "ShotPuck")(type "KiCad")(uri "${KIPRJMOD}/ShotPuck.pretty")'
        '(options "")(descr "ShotPuck custom footprints"))\n)\n')


def swd_footprint():
    fp = new_fp("SWD_Pads_1x4_P2.54", "SWD test pads for a 4-pin 2.54 mm pogo jig: 1 VDD, 2 SWDIO, 3 SWDCLK, 4 GND.")
    first = -(SWD_PADS - 1) * SWD_PITCH / 2
    for i in range(SWD_PADS):
        pad(fp, str(i + 1), pcbnew.PAD_SHAPE_CIRCLE, first + i * SWD_PITCH, 0, SWD_PAD_DIA, SWD_PAD_DIA)
    rect(fp, pcbnew.F_CrtYd, (SWD_PADS - 1) * SWD_PITCH + SWD_PAD_DIA + SWD_COURTYARD, SWD_PAD_DIA + SWD_COURTYARD)
    pcbnew.FootprintSave(LIB, fp)


def cell_footprint():
    c = L["cell"]
    fp = new_fp("LIR1254_Contacts", "LIR1254 in the board notch, no tabs: pad 1 = + spring contact on the can side, "
                "pad 2 = - strap across the top cap (Kapton where it passes the can). Hand-solder the contacts.")
    for num, key in (("1", "pos_pad"), ("2", "neg_pad")):
        dx, dy = c[key]["x"] - c["x"], c[key]["y"] - c["y"]
        p = pad(fp, num, pcbnew.PAD_SHAPE_RECT, dx, -dy, CELL_PAD_W, CELL_PAD_H)
        p.SetOrientationDegrees(math.degrees(math.atan2(dy, dx)) + 90)
    circle(fp, pcbnew.F_Fab, c["dia"] / 2, 0.1)
    circle(fp, pcbnew.F_CrtYd, c["dia"] / 2 + CELL_COURTYARD)
    dx, dy = c["pos_pad"]["x"] - c["x"], c["pos_pad"]["y"] - c["y"]
    k = (CELL_PAD_W / 2 + PLUS_MARK_GAP + PLUS_MARK_HALF) / math.hypot(dx, dy)
    mx, my = dx - dy * k, -(dy + dx * k)
    line(fp, pcbnew.F_SilkS, mx - PLUS_MARK_HALF, my, mx + PLUS_MARK_HALF, my)
    line(fp, pcbnew.F_SilkS, mx, my - PLUS_MARK_HALF, mx, my + PLUS_MARK_HALF)
    pcbnew.FootprintSave(LIB, fp)


def load_fp(fpid):
    lib, name = fpid.split(":")
    path = LIB if lib == "ShotPuck" else os.path.join(STD, lib + ".pretty")
    fp = pcbnew.FootprintLoad(path, name)
    if fp is None:
        raise SystemExit("footprint not found: " + fpid)
    fp.SetFPID(pcbnew.LIB_ID(lib, name))
    return fp


# --------------------------------------------------------------------- zones
def layer_set(layers):
    ls = pcbnew.LSET()
    for layer in layers:
        ls.AddLayer(layer)
    return ls


def poly_zone(board, layer_set, pts, net=None, rule=None, name=""):
    z = pcbnew.ZONE(board)
    z.SetLayerSet(layer_set)
    ol = z.Outline()
    ol.NewOutline()
    for (x, y) in pts:
        ol.Append(MM(x), MM(-y))
    if rule:
        z.SetIsRuleArea(True)
        z.SetDoNotAllowTracks("tracks" in rule)
        z.SetDoNotAllowVias("vias" in rule)
        z.SetDoNotAllowZoneFills("pour" in rule)
        z.SetDoNotAllowPads(False)
        z.SetDoNotAllowFootprints("footprints" in rule)
        z.SetZoneName(name)
    else:
        z.SetNet(net)
        z.SetMinThickness(MM(0.2))
        z.SetLocalClearance(MM(0.25))
        z.SetPadConnection(pcbnew.ZONE_CONNECTION_THERMAL)
        z.SetIslandRemovalMode(pcbnew.ISLAND_REMOVAL_MODE_ALWAYS)
        z.SetThermalReliefGap(MM(0.25))
        z.SetThermalReliefSpokeWidth(MM(0.3))
    board.Add(z)
    return z


def circ(cx, cy, r, n=48):
    return [(cx + r * math.cos(2 * math.pi * i / n), cy + r * math.sin(2 * math.pi * i / n))
            for i in range(n)]


def add_outline(board):
    for loop in shape.outline_loops():
        for a, b in zip(loop, loop[1:]):
            edge = pcbnew.PCB_SHAPE(board)
            edge.SetShape(pcbnew.SHAPE_T_SEGMENT)
            edge.SetStart(V(*a))
            edge.SetEnd(V(*b))
            edge.SetLayer(pcbnew.Edge_Cuts)
            edge.SetWidth(MM(EDGE_WIDTH_MM))
            board.Add(edge)


# ---------------------------------------------------------------------- main
def setup_rules(board):
    ds = board.GetDesignSettings()
    ds.SetCopperLayerCount(2)
    ds.SetBoardThickness(MM(L["puck"]["pcb_thickness"]))
    ds.m_TrackMinWidth = MM(0.127)
    ds.m_MinClearance = MM(0.127)
    ds.m_ViasMinSize = MM(0.45)
    ds.m_MinThroughDrill = MM(0.2)
    ds.m_CopperEdgeClearance = MM(0.3)
    nc = ds.m_NetSettings.GetDefaultNetclass()
    nc.SetTrackWidth(MM(TRACK_MM))
    nc.SetClearance(MM(CLEARANCE_MM))
    nc.SetViaDiameter(MM(VIA_DIA_MM))
    nc.SetViaDrill(MM(0.3))


def add_nets(board):
    nets = {}
    for (_r, _l, _f, _v, pm, _e) in design.PARTS:
        for n in pm.values():
            if n not in nets:
                ni = pcbnew.NETINFO_ITEM(board, n)
                board.Add(ni)
                nets[n] = ni
    return nets


def fixed_places():
    bosses = shape.boss_xy()
    return {
        "U1": (L["module"]["x"], L["module"]["y"], L["module"]["rot_deg"]),
        "BT1": (L["cell"]["x"], L["cell"]["y"], 0),
        "J1": (L["connector"]["x"], L["connector"]["y"], L["connector"]["rot_deg"]),
        **{f"H{i + 1}": (x, y, 0) for i, (x, y) in enumerate(bosses)},
    }


def place_footprints(board, nets):
    fixed = fixed_places()
    for (ref, lib_id, fpid, value, pm, extra) in design.PARTS:
        fp = load_fp(fpid)
        fp.SetReference(ref)
        fp.SetValue(value)
        x, y, rot, *side = fixed.get(ref) or PLACE[ref]
        fp.SetPosition(V(x, y))
        fp.SetOrientationDegrees(rot)
        fp.SetPath(pcbnew.KIID_PATH("/" + U(f"sym-{ref}-1")))
        fp.Value().SetVisible(False)
        fp.Reference().SetLayer(pcbnew.F_Fab)
        fp.Reference().SetTextSize(pcbnew.VECTOR2I(MM(0.5), MM(0.5)))
        fp.Reference().SetTextThickness(MM(0.08))
        if extra.get("dnp"):
            fp.SetAttributes(fp.GetAttributes() | pcbnew.FP_EXCLUDE_FROM_BOM)
        for p in fp.Pads():
            n = pm.get(p.GetNumber())
            if n:
                p.SetNet(nets[n])
            if ref in SOLID_GND_REFS and n == GND_NET:
                p.SetLocalZoneConnection(pcbnew.ZONE_CONNECTION_FULL)
        board.Add(fp)
        if side == [BOTTOM]:
            fp.Flip(fp.GetPosition(), pcbnew.FLIP_DIRECTION_LEFT_RIGHT)


def add_keepouts(board):
    allcu = layer_set([pcbnew.F_Cu, pcbnew.B_Cu])
    poly_zone(board, allcu, shape.polygon_points(shape.antenna_zone()), rule="tracks vias pour",
              name="antenna_to_rim")
    keep = shape.cutout().buffer(CUTOUT_COPPER_KEEP)
    for i, part in enumerate(getattr(keep, "geoms", None) or [keep]):
        poly_zone(board, allcu, shape.polygon_points(part), rule="tracks vias pour", name=f"cutout_{i + 1}")


def add_rim_and_bosses(board):
    R = L["pcb_dia"] / 2
    allcu = layer_set([pcbnew.F_Cu, pcbnew.B_Cu])
    z = poly_zone(board, allcu, circ(0, 0, R + 1.0, 72), rule="tracks vias", name="rim")
    z.Outline().NewHole()
    for (x, y) in circ(0, 0, R - 0.35, 72):
        z.Outline().Append(MM(x), MM(-y), -1, 0)
    for i, (bx, by) in enumerate(shape.boss_xy()):
        poly_zone(board, allcu, circ(bx, by, L["bosses"]["boss_dia"] / 2 + 0.3, 24),
                  rule="tracks vias pour", name=f"boss_{i + 1}")


def add_label(board):
    t = pcbnew.PCB_TEXT(board)
    t.SetText("ShotPuck rev A")
    t.SetPosition(V(*BACK_TEXT_POS))
    t.SetLayer(pcbnew.B_SilkS)
    t.SetMirrored(True)
    t.SetTextSize(pcbnew.VECTOR2I(MM(1.0), MM(1.0)))
    board.Add(t)


def build():
    make_lib()
    board = pcbnew.BOARD()
    setup_rules(board)
    nets = add_nets(board)
    add_outline(board)
    place_footprints(board, nets)
    add_keepouts(board)
    pre_route(board)
    pre_vias(board)
    pre_links(board)
    pad_bridges(board)
    pre_path(board)
    add_pours(board)
    gnd_fanout(board)
    add_rim_and_bosses(board)
    add_label(board)
    return board


def pre_route(board):
    for ref, a, b in PRE_ROUTED:
        fp = board.FindFootprintByReference(ref)
        pa, pb = fp.FindPadByNumber(a), fp.FindPadByNumber(b)
        cx = fp.GetPosition().x
        pts = [pa.GetPosition(), pcbnew.VECTOR2I(cx, pa.GetPosition().y),
               pcbnew.VECTOR2I(cx, pb.GetPosition().y), pb.GetPosition()]
        for start, end in zip(pts, pts[1:]):
            tr = pcbnew.PCB_TRACK(board)
            tr.SetStart(start)
            tr.SetEnd(end)
            tr.SetWidth(MM(PRE_ROUTE_WIDTH_MM))
            tr.SetLayer(pcbnew.F_Cu)
            tr.SetNet(pa.GetNet())
            board.Add(tr)


def locked_track(board, spec):
    start, end, layer, net, width = spec
    tr = pcbnew.PCB_TRACK(board)
    tr.SetStart(start)
    tr.SetEnd(end)
    tr.SetWidth(MM(width))
    tr.SetLayer(layer)
    tr.SetNet(net)
    tr.SetLocked(True)
    board.Add(tr)


def locked_via(board, spec):
    pos, net = spec
    via = pcbnew.PCB_VIA(board)
    via.SetPosition(pos)
    via.SetWidth(MM(VIA_DIA_MM))
    via.SetDrill(MM(VIA_DRILL_MM))
    via.SetLayerPair(pcbnew.F_Cu, pcbnew.B_Cu)
    via.SetNet(net)
    via.SetLocked(True)
    board.Add(via)


def pre_vias(board):
    for ref, num, (dx, dy) in PRE_VIAS:
        pad = board.FindFootprintByReference(ref).FindPadByNumber(num)
        start = pad.GetPosition()
        end = pcbnew.VECTOR2I(start.x + MM(dx), start.y - MM(dy))
        locked_track(board, (start, end, pcbnew.F_Cu, pad.GetNet(), TRACK_MM))
        locked_via(board, (end, pad.GetNet()))


def pre_links(board):
    for ref_a, num_a, ref_b, num_b, via_points in PRE_LINKS:
        pa = board.FindFootprintByReference(ref_a).FindPadByNumber(num_a)
        pb = board.FindFootprintByReference(ref_b).FindPadByNumber(num_b)
        width = TRACK_MM if pa.GetNetname() == GND_NET else design.POWER_NETS.get(pa.GetNetname(), TRACK_MM)
        points = [pa.GetPosition()] + [V(x, y) for x, y in via_points] + [pb.GetPosition()]
        for a, b in zip(points, points[1:]):
            locked_track(board, (a, b, pcbnew.F_Cu, pa.GetNet(), width))


def pad_bridges(board):
    for ref, num_a, num_b in PAD_BRIDGES:
        fp = board.FindFootprintByReference(ref)
        pa, pb = fp.FindPadByNumber(num_a), fp.FindPadByNumber(num_b)
        locked_track(board, (pa.GetPosition(), pb.GetPosition(), pcbnew.F_Cu, pa.GetNet(), TRACK_MM))


def pre_path(board):
    ref, num, top, bottom, (end_ref, end_num) = VIN_ESCAPE
    pad = board.FindFootprintByReference(ref).FindPadByNumber(num)
    end = board.FindFootprintByReference(end_ref).FindPadByNumber(end_num).GetPosition()
    points = [pad.GetPosition()] + [V(x, y) for x, y in top]
    for a, b in zip(points, points[1:]):
        locked_track(board, (a, b, pcbnew.F_Cu, pad.GetNet(), TRACK_MM))
    locked_via(board, (points[-1], pad.GetNet()))
    points = points[-1:] + [V(x, y) for x, y in bottom]
    for a, b in zip(points, points[1:]):
        locked_track(board, (a, b, pcbnew.B_Cu, pad.GetNet(), TRACK_MM))
    locked_via(board, (points[-1], pad.GetNet()))
    locked_track(board, (points[-1], end, pcbnew.F_Cu, pad.GetNet(), TRACK_MM))


def front_signal(track):
    return track.GetLayer() == pcbnew.F_Cu and track.GetNetname() != GND_NET


def track_points(board, keep=None):
    pts = []
    for t in board.GetTracks():
        if t.Type() != pcbnew.PCB_TRACE_T or (keep and not keep(t)):
            continue
        s, e = t.GetStart(), t.GetEnd()
        n = max(1, int(pcbnew.ToMM((e - s).EuclideanNorm()) / TRACK_SAMPLE_MM))
        pts += [(pcbnew.ToMM(s.x + (e.x - s.x) * k // n), -pcbnew.ToMM(s.y + (e.y - s.y) * k // n))
                for k in range(n + 1)]
    return pts


def gnd_fanout(board):
    """Give every GND SMD pad a short stub + via to the bottom GND plane."""
    R = L["pcb_dia"] / 2
    gnd = board.FindNet("GND")
    vias = [(pcbnew.ToMM(v.GetPosition().x), -pcbnew.ToMM(v.GetPosition().y))
            for v in board.GetTracks() if v.Type() == pcbnew.PCB_VIA_T] + track_points(board)
    bosses = shape.boss_xy()
    pads = []
    for fp in board.GetFootprints():
        for p in fp.Pads():
            bb = p.GetBoundingBox()
            pads.append((p, pcbnew.ToMM(bb.GetLeft()), pcbnew.ToMM(-bb.GetBottom()),
                         pcbnew.ToMM(bb.GetRight()), pcbnew.ToMM(-bb.GetTop())))

    def free(x, y, own):
        if math.hypot(x, y) > R - 0.9:
            return False
        if shape.in_cutout((x, y), CUTOUT_VIA_MARGIN) or shape.in_antenna((x, y), ANTENNA_VIA_MARGIN):
            return False
        if any(math.hypot(x - bx, y - by) < L["bosses"]["boss_dia"] / 2 + 0.9 for bx, by in bosses):
            return False
        if any(math.hypot(x - vx, y - vy) < 0.8 for vx, vy in vias):
            return False
        for (p, x1, y1, x2, y2) in pads:
            if p is own:
                continue
            cl = 0.45 if p.GetNetname() == "GND" else 0.5
            if x1 - cl < x < x2 + cl and y1 - cl < y < y2 + cl:
                return False
        return True

    foreign = track_points(board, front_signal)

    def seg_ok(ax, ay, bx, by, own):
        for k in range(1, 9):
            x, y = ax + (bx - ax) * k / 8, ay + (by - ay) * k / 8
            if shape.in_antenna((x, y), ANTENNA_SEG_MARGIN):
                return False
            if any(math.hypot(x - tx, y - ty) < FANOUT_TRACK_CLEAR_MM for tx, ty in foreign):
                return False
            for (p, x1, y1, x2, y2) in pads:
                if p is own or p.GetNetname() == "GND":
                    continue
                if x1 - 0.28 < x < x2 + 0.28 and y1 - 0.28 < y < y2 + 0.28:
                    return False
        return True

    n = 0
    for fp in board.GetFootprints():
        fx, fy = pcbnew.ToMM(fp.GetPosition().x), -pcbnew.ToMM(fp.GetPosition().y)
        for p in fp.Pads():
            if p.GetNetname() != "GND" or p.GetAttribute() != pcbnew.PAD_ATTRIB_SMD or not p.IsOnLayer(pcbnew.F_Cu):
                continue
            px, py = pcbnew.ToMM(p.GetPosition().x), -pcbnew.ToMM(p.GetPosition().y)
            base = math.atan2(py - fy, px - fx) if (px, py) != (fx, fy) else 0.0
            done = False
            for dist in (0.9, 1.2, 1.6, 2.0):
                for da in sorted(range(-180, 180, 15), key=abs):
                    a = base + math.radians(da)
                    vx, vy = px + dist * math.cos(a), py + dist * math.sin(a)
                    if free(vx, vy, p) and seg_ok(px, py, vx, vy, p):
                        via = pcbnew.PCB_VIA(board)
                        via.SetPosition(V(vx, vy))
                        via.SetWidth(MM(VIA_DIA_MM))
                        via.SetDrill(MM(0.3))
                        via.SetLayerPair(pcbnew.F_Cu, pcbnew.B_Cu)
                        via.SetNet(gnd)
                        board.Add(via)
                        tr = pcbnew.PCB_TRACK(board)
                        tr.SetStart(p.GetPosition())
                        tr.SetEnd(V(vx, vy))
                        tr.SetWidth(MM(0.2))
                        tr.SetLayer(pcbnew.F_Cu)
                        tr.SetNet(gnd)
                        board.Add(tr)
                        vias.append((vx, vy))
                        n += 1
                        done = True
                        break
                if done:
                    break
            if not done:
                print(f"fanout: no via spot for {fp.GetReference()}.{p.GetNumber()}")
    print(f"fanout: {n} GND vias")


def stitch(board, pitch=1.6):
    """GND stitching vias on a grid wherever there is room, so every piece
    of the top pour is tied to the bottom plane."""
    R = L["pcb_dia"] / 2
    gnd = board.FindNet("GND")
    bosses = shape.boss_xy()
    segs, vias, pads = [], [], []
    for tr in board.GetTracks():
        if tr.Type() == pcbnew.PCB_VIA_T:
            vias.append((pcbnew.ToMM(tr.GetPosition().x), -pcbnew.ToMM(tr.GetPosition().y)))
        else:
            a, b = tr.GetStart(), tr.GetEnd()
            segs.append((pcbnew.ToMM(a.x), -pcbnew.ToMM(a.y), pcbnew.ToMM(b.x), -pcbnew.ToMM(b.y),
                         pcbnew.ToMM(tr.GetWidth()) / 2))
    for fp in board.GetFootprints():
        for p in fp.Pads():
            bb = p.GetBoundingBox()
            pads.append((pcbnew.ToMM(bb.GetLeft()), -pcbnew.ToMM(bb.GetBottom()),
                         pcbnew.ToMM(bb.GetRight()), -pcbnew.ToMM(bb.GetTop())))
        # stay out of module/LGA bodies (thermal/assembly) and all courtyards
        bb = fp.GetBoundingBox(False, False)
        pads.append((pcbnew.ToMM(bb.GetLeft()), -pcbnew.ToMM(bb.GetBottom()),
                     pcbnew.ToMM(bb.GetRight()), -pcbnew.ToMM(bb.GetTop())))

    def dseg(x, y, s):
        x1, y1, x2, y2, _ = s
        dx, dy = x2 - x1, y2 - y1
        l2 = dx * dx + dy * dy
        u = 0 if l2 == 0 else max(0, min(1, ((x - x1) * dx + (y - y1) * dy) / l2))
        return math.hypot(x - x1 - u * dx, y - y1 - u * dy)

    n = 0
    steps = int(R / pitch) + 1
    for i in range(-steps, steps + 1):
        for j in range(-steps, steps + 1):
            x, y = i * pitch, j * pitch
            if math.hypot(x, y) > R - 1.0:
                continue
            if shape.in_cutout((x, y), CUTOUT_VIA_MARGIN) or shape.in_antenna((x, y), ANTENNA_STITCH_MARGIN):
                continue
            if any(math.hypot(x - bx, y - by) < L["bosses"]["boss_dia"] / 2 + 0.9 for bx, by in bosses):
                continue
            if any(x1 - 0.45 < x < x2 + 0.45 and y1 - 0.45 < y < y2 + 0.45 for (x1, y1, x2, y2) in pads):
                continue
            if any(math.hypot(x - vx, y - vy) < 1.0 for vx, vy in vias):
                continue
            if any(dseg(x, y, s) < s[4] + 0.25 + 0.2 for s in segs):
                continue
            via = pcbnew.PCB_VIA(board)
            via.SetPosition(V(x, y))
            via.SetWidth(MM(VIA_DIA_MM))
            via.SetDrill(MM(0.3))
            via.SetLayerPair(pcbnew.F_Cu, pcbnew.B_Cu)
            via.SetNet(gnd)
            board.Add(via)
            vias.append((x, y))
            n += 1
    print(f"stitching: {n} GND vias")


def add_pours(board):
    """GND pours on both layers, added after routing so that GND is routed
    as real traces too (no pads stranded on pour islands)."""
    R = L["pcb_dia"] / 2
    gnd = board.FindNet("GND")
    for layer in (pcbnew.F_Cu, pcbnew.B_Cu):
        poly_zone(board, layer_set([layer]), circ(0, 0, R + 0.5, 72), net=gnd)


def fill(board):
    board.BuildConnectivity()
    pcbnew.ZONE_FILLER(board).Fill(board.Zones())


def drop_block(text, marker):
    out, i = [], 0
    while (j := text.find(marker, i)) >= 0:
        out.append(text[i:j])
        depth, k = 0, j
        while True:
            depth += {"(": 1, ")": -1}.get(text[k], 0)
            if depth == 0:
                break
            k += 1
        i = k + 1
    return "".join(out) + text[i:]


def signals_only(dsn):
    text = drop_block(open(dsn).read(), "(plane " + GND_NET)
    net = text.find("(net " + GND_NET + "\n")
    if net >= 0:
        pins = text.index("(pins", net)
        text = text[:pins] + "(pins)" + text[text.index("\n    )", net):]
    open(dsn, "w").write(text)


def route(board, path, timeout=900):
    dsn = path.replace(".kicad_pcb", ".dsn")
    ses = path.replace(".kicad_pcb", ".ses")
    if not os.path.exists(JAR):
        print("Freerouting not found; board left unrouted")
        return False
    if "--reuse-ses" in sys.argv and os.path.exists(ses):
        return import_ses(board, ses)
    if not pcbnew.ExportSpecctraDSN(board, dsn):
        print("DSN export failed")
        return False
    signals_only(dsn)
    if os.path.exists(ses):
        os.remove(ses)
    job = time.strftime("%H:%M:%S", time.gmtime(timeout - ROUTER_JOB_MARGIN_S))
    cmd = [JAVA, "-jar", JAR, "--gui.enabled=false", "-de", dsn, "-do", ses, "--router.max_passes=100",
           "--router.optimizer.enabled=false", "--router.max_threads=1", f"--router.job_timeout={job}"]
    print(" ".join(cmd))
    try:
        subprocess.run(cmd, stdout=open(os.path.join(KI, "freerouting.log"), "w"),
                       stderr=subprocess.STDOUT, timeout=timeout)
    except subprocess.TimeoutExpired:
        print("router timed out; keeping previous copper")
        return False
    if not os.path.exists(ses):
        print("routing produced no session file (see kicad/freerouting.log)")
        return False
    return import_ses(board, ses)


def import_ses(board, ses):
    """Add Freerouting's wires/vias to the board (pcbnew's ImportSpecctraSES
    only works inside the GUI). SES units: 0.1 um, y up."""
    parsed = sexp.parse(open(ses).read())
    if not parsed:
        print("empty session file; keeping previous copper")
        return False
    tree = parsed[0]

    def find(node, key):
        return [x for x in node if isinstance(x, list) and x and x[0] == key]

    routes = find(tree, "routes")[0]
    net_out = find(routes, "network_out")[0]
    k = 100  # 0.1 um -> nm
    layers = {"F.Cu": pcbnew.F_Cu, "B.Cu": pcbnew.B_Cu}
    n_tr = n_via = 0
    have = [v.GetPosition() for v in board.GetTracks() if v.Type() == pcbnew.PCB_VIA_T]
    echo_tol = pcbnew.FromMM(VIA_ECHO_TOL_MM)
    seen = set()
    for t0 in board.GetTracks():
        if t0.Type() == pcbnew.PCB_TRACE_T:
            a, b = t0.GetStart(), t0.GetEnd()
            seen.add((round(a.x / k), round(-a.y / k), round(b.x / k), round(-b.y / k)))
    for net in find(net_out, "net"):
        ni = board.FindNet(str(net[1]))
        for w in find(net, "wire"):
            path = find(w, "path")[0]
            layer, width = layers[str(path[1])], int(float(path[2])) * k
            pts = [float(v) for v in path[3:] if not isinstance(v, list)]
            for i in range(0, len(pts) - 2, 2):
                key = tuple(round(v, 0) for v in pts[i:i + 4])
                if key in seen or (key[2], key[3], key[0], key[1]) in seen:
                    continue
                seen.add(key)
                tr = pcbnew.PCB_TRACK(board)
                tr.SetStart(pcbnew.VECTOR2I(int(pts[i] * k), int(-pts[i + 1] * k)))
                tr.SetEnd(pcbnew.VECTOR2I(int(pts[i + 2] * k), int(-pts[i + 3] * k)))
                tr.SetWidth(width)
                tr.SetLayer(layer)
                tr.SetNet(ni)
                board.Add(tr)
                n_tr += 1
        for v in find(net, "via"):
            m = re.search(r"_(\d+):(\d+)_um", str(v[1]))
            dia, drill = (int(m.group(1)), int(m.group(2))) if m else (500, 300)
            pos = pcbnew.VECTOR2I(int(float(v[2]) * k), int(-float(v[3]) * k))
            if any(abs(h.x - pos.x) <= echo_tol and abs(h.y - pos.y) <= echo_tol for h in have):
                continue                      # fanout via echoed back by the router
            via = pcbnew.PCB_VIA(board)
            via.SetPosition(pos)
            via.SetWidth(pcbnew.FromMM(dia / 1000))
            via.SetDrill(pcbnew.FromMM(drill / 1000))
            via.SetLayerPair(pcbnew.F_Cu, pcbnew.B_Cu)
            via.SetNet(ni)
            board.Add(via)
            n_via += 1
    print(f"imported {n_tr} track segments, {n_via} vias")
    return n_tr > 0


def drc(path):
    rpt = os.path.join(KI, "drc.rpt")
    subprocess.run(["kicad-cli", "pcb", "drc", "--severity-all", "--all-track-errors", "-o", rpt, path],
                   stdout=subprocess.DEVNULL, check=True)
    lines = [l.strip() for l in open(rpt) if l.startswith("** Found")]
    print("\n".join(lines))
    return lines


def unconnected(board):
    board.BuildConnectivity()
    return board.GetConnectivity().GetUnconnectedCount(False)


def remove_dangling(board):
    removed = 0
    while True:
        board.BuildConnectivity()
        conn = board.GetConnectivity()
        loose = [t for t in board.GetTracks() if t.Type() == pcbnew.PCB_TRACE_T and not t.IsLocked()
                 and conn.TestTrackEndpointDangling(t, False)]
        if not loose:
            return removed
        for t in loose:
            board.Delete(t)
        removed += len(loose)


def attempt(path):
    board = build()
    pcbnew.SaveBoard(path, board)
    board = pcbnew.LoadBoard(path)
    if "--no-route" not in sys.argv:
        routed = route(board, path, timeout=240)
        print("routed:", routed)
        fill(board)
        left = unconnected(board)
        if routed and left:
            second = path.replace(".kicad_pcb", SECOND_PASS_SUFFIX)
            print(f"second pass for {left} connection(s):", route(board, second, timeout=200))
        print("dangling tracks removed:", remove_dangling(board))
        stitch(board)
    fill(board)
    if islands.bridge_islands(board):
        fill(board)
    pcbnew.SaveBoard(path, board)
    return unconnected(board)


def tidy(path):
    board = pcbnew.LoadBoard(path)
    if not remove_dangling(board):
        return
    fill(board)
    if islands.bridge_islands(board):
        fill(board)
    pcbnew.SaveBoard(path, board)


def main():
    path = os.path.join(KI, "shotpuck.kicad_pcb")
    single = any(flag in sys.argv for flag in ("--no-route", "--reuse-ses", "--quick"))
    best = path.replace(".kicad_pcb", "-best.kicad_pcb")
    fewest = None
    for n in range(1, BOARD_ATTEMPTS + 1):
        left = attempt(path)
        print(f"attempt {n}: {left} unconnected")
        if fewest is None or left < fewest:
            fewest = left
            shutil.copyfile(path, best)
        if left == 0 or single:
            break
    shutil.copyfile(best, path)
    os.remove(best)
    if "--no-route" not in sys.argv:
        tidy(path)
    drc(path)


if __name__ == "__main__":
    main()
