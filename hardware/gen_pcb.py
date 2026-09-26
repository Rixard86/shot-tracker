#!/usr/bin/env python3
"""
gen_pcb.py - builds kicad/shotpuck.kicad_pcb from design.py + ../layout.json
(+ ../mechanical/out/trim.json), with the KiCad 10 pcbnew Python API.

  * round 2-layer board, 3 NPTH holes for the cap screws
  * custom footprints written to kicad/ShotPuck.pretty
  * placement from layout.json (big parts) and PLACE below (small parts)
  * keep-outs: antenna to board rim, under the cell (top copper), trim pin
  * GND pours both layers
  * routes with Freerouting if available (FREEROUTING_JAR), then DRC report

    python gen_pcb.py            # place + route + DRC
    python gen_pcb.py --no-route

Routing takes 1-5 min (Freerouting 2.1, Java 21: set JAVA and FREEROUTING_JAR).
"""
import json
import math
import os
import re
import subprocess
import sys

import pcbnew

import design
import islands
import sexp
from gen_sch import U

HERE = os.path.dirname(os.path.abspath(__file__))
KI = os.path.join(HERE, "kicad")
LIB = os.path.join(KI, "ShotPuck.pretty")
STD = os.environ.get("KICAD10_FOOTPRINT_DIR", r"C:\Program Files\KiCad\10.0\share\kicad\footprints")
L = json.load(open(os.path.join(HERE, "..", "layout.json")))
TRIM = json.load(open(os.path.join(HERE, "..", "mechanical", "out", "trim.json")))
JAR = os.environ.get("FREEROUTING_JAR", os.path.expanduser("~/tools/freerouting-2.1.0.jar"))
JAVA = os.environ.get("JAVA", os.path.expanduser("~/tools/jre21/bin/java.exe"))

MM = pcbnew.FromMM


def V(x, y):
    """board coords (mm, +y up) -> KiCad internal (y down)."""
    return pcbnew.VECTOR2I(MM(x), MM(-y))


# small parts: ref -> (x, y, rot_deg, side)   board coords, viewed from top
PLACE = {
    # supply decoupling at the module's power pads (VDD y=1.6, VDDH y=2.4)
    "C4": (-1.3, 6.9, 0), "C5": (-1.3, 8.1, 0), "C3": (1.1, 6.9, 0),
    # status LED next to P0.13
    "R4": (-6.9, 7.4, 0), "D4": (-4.9, 7.4, 180),
    # SWD pads next to SWDIO/SWDCLK
    "J2": (-9.0, 11.0, 0),
    # charging input around the magnetic connector
    "D3": (-2.5, 15.5, 0), "D1": (6.9, 12.9, 180), "C1": (10.2, 13.2, 0),
    # charger + inhibit, below the cell next to the + tab
    "JP1": (5.3, -10.0, 0), "C2": (2.4, -10.2, 90), "U3": (3.9, -13.4, 0),
    "R1": (6.9, -13.6, 90), "Q1": (9.2, -12.6, 0), "R2": (8.8, -14.6, 0),
    "R3": (11.5, -12.6, 90), "D2": (13.0, -10.0, 90),
    # IMU by the I2C pads: SCL/SDA/CS edge faces the module-cell channel, C6 at VDDIO, C7 at VDD
    "U2": (-3.6, -8.2, 270), "C6": (-6.1, -7.2, 180), "C7": (-4.35, -10.5, 0), "R5": (-1.0, -9.3, 90),
    "R6": (0.1, -10.8, 90),
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
    c = L["cell"]
    # CP1654 with radial solder tabs. Footprint origin = cell centre.
    # + tab (can, bottom) exits at -y, - tab (cap, top, bent down) at +y.
    fp = new_fp("CP1654_Tabbed", "Varta CP1654 A3 with solder tabs; + at -y, - at +y. "
                "VERIFY tab positions against the tabbed variant drawing.")
    dy = c["neg_pad"]["y"] - c["y"]
    pad(fp, "1", pcbnew.PAD_SHAPE_RECT, 0, dy, 3.0, 2.2)      # + (y flipped: -y board)
    pad(fp, "2", pcbnew.PAD_SHAPE_RECT, 0, -dy, 3.0, 2.2)     # -
    circle(fp, pcbnew.F_SilkS, c["dia"] / 2 + 0.1, 0.12)
    circle(fp, pcbnew.F_Fab, c["dia"] / 2, 0.1)
    circle(fp, pcbnew.F_CrtYd, c["dia"] / 2 + 0.3)   # tabs: see pads
    line(fp, pcbnew.F_SilkS, -0.6, dy - 2.0, 0.6, dy - 2.0)             # +
    line(fp, pcbnew.F_SilkS, 0, dy - 2.6, 0, dy - 1.4)
    pcbnew.FootprintSave(LIB, fp)

    k = L["connector"]
    fp = new_fp("MagPogo_2P", "2-pin magnetic pogo receptacle, pitch %.2f mm. PLACEHOLDER: "
                "replace pads/outline with the purchased part's drawing." % k["pitch"])
    pad(fp, "1", pcbnew.PAD_SHAPE_RECT, -k["pitch"] / 2, 0, 1.5, 1.5, smd=False, drill=k["pin_drill"])
    pad(fp, "2", pcbnew.PAD_SHAPE_CIRCLE, k["pitch"] / 2, 0, 1.5, 1.5, smd=False, drill=k["pin_drill"])
    rect(fp, pcbnew.F_SilkS, k["body_w"], k["body_l"], 0.12)
    rect(fp, pcbnew.F_CrtYd, k["body_w"] + 0.5, k["body_l"] + 0.5)
    rect(fp, pcbnew.F_Fab, k["body_w"], k["body_l"], 0.1)
    pcbnew.FootprintSave(LIB, fp)

    b = L["bosses"]
    fp = new_fp("MountingHole_1.8mm_NPTH", "M1.6 clearance, cap boss Ø%.1f bears on board" % b["boss_dia"])
    pad(fp, "", pcbnew.PAD_SHAPE_CIRCLE, 0, 0, b["pcb_hole"], b["pcb_hole"], drill=b["pcb_hole"], npth=True)
    circle(fp, pcbnew.Cmts_User, b["boss_dia"] / 2)
    circle(fp, pcbnew.F_CrtYd, b["boss_dia"] / 2 + 0.25)
    pcbnew.FootprintSave(LIB, fp)

    open(os.path.join(KI, "fp-lib-table"), "w").write(
        '(fp_lib_table\n  (lib (name "ShotPuck")(type "KiCad")(uri "${KIPRJMOD}/ShotPuck.pretty")'
        '(options "")(descr "ShotPuck custom footprints"))\n)\n')


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


# ---------------------------------------------------------------------- main
def build():
    make_lib()
    board = pcbnew.BOARD()
    ds = board.GetDesignSettings()
    ds.SetCopperLayerCount(2)
    ds.m_TrackMinWidth = MM(0.127)
    ds.m_MinClearance = MM(0.127)
    ds.m_ViasMinSize = MM(0.45)
    ds.m_MinThroughDrill = MM(0.2)
    ds.m_CopperEdgeClearance = MM(0.3)
    nc = ds.m_NetSettings.GetDefaultNetclass()
    nc.SetTrackWidth(MM(0.2))
    nc.SetClearance(MM(0.15))
    nc.SetViaDiameter(MM(0.5))
    nc.SetViaDrill(MM(0.25))

    # nets
    nets = {}
    for (_r, _l, _f, _v, pm, _e) in design.PARTS:
        for n in pm.values():
            if n not in nets:
                ni = pcbnew.NETINFO_ITEM(board, n)
                board.Add(ni)
                nets[n] = ni

    # outline
    R = L["pcb_dia"] / 2
    s = pcbnew.PCB_SHAPE(board)
    s.SetShape(pcbnew.SHAPE_T_CIRCLE)
    s.SetCenter(V(0, 0))
    s.SetEnd(V(R, 0))
    s.SetLayer(pcbnew.Edge_Cuts)
    s.SetWidth(MM(0.1))
    board.Add(s)

    # footprints
    bosses = [(L["bosses"]["radius"] * math.cos(math.radians(a)),
               L["bosses"]["radius"] * math.sin(math.radians(a))) for a in L["bosses"]["angles_deg"]]
    fixed = {
        "U1": (L["module"]["x"], L["module"]["y"], L["module"]["rot_deg"]),
        "BT1": (L["cell"]["x"], L["cell"]["y"], 0),
        "J1": (L["connector"]["x"], L["connector"]["y"], 0),
        "H1": (*bosses[0], 0), "H2": (*bosses[1], 0), "H3": (*bosses[2], 0),
    }
    for (ref, lib_id, fpid, value, pm, extra) in design.PARTS:
        fp = load_fp(fpid)
        fp.SetReference(ref)
        fp.SetValue(value)
        x, y, rot = fixed.get(ref) or PLACE[ref]
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
        board.Add(fp)

    # keep-outs
    allcu = pcbnew.LSET()
    allcu.AddLayer(pcbnew.F_Cu)
    allcu.AddLayer(pcbnew.B_Cu)
    m = L["module"]
    ant_x = m["x"] - m["l"] / 2 + 3.9          # inner edge of antenna region
    poly_zone(board, allcu, [(-R - 1, -6.3), (ant_x, -6.3), (ant_x, 6.3), (-R - 1, 6.3)],
              rule="tracks vias pour", name="antenna_to_rim")
    c = L["cell"]
    poly_zone(board, layer_set([pcbnew.F_Cu]), circ(c["x"], c["y"], c["dia"] / 2 + 0.3),
              rule="tracks vias pour", name="under_cell_top")
    for i, t in enumerate(TRIM):
        if t["clear_above_pcb"] < 2.0:           # pin reaches low: no parts below it
            poly_zone(board, layer_set([pcbnew.F_Cu]), circ(t["x"], t["y"], t["keepout_r"]),
                      rule="footprints", name=f"trim_pin_{i + 1}")
    add_pours(board)
    gnd_fanout(board)
    # 0.35 mm copper-free ring at the rim (router keeps only 0.15 otherwise)
    z = poly_zone(board, allcu, circ(0, 0, R + 1.0, 72), rule="tracks vias", name="rim")
    z.Outline().NewHole()
    for (x, y) in circ(0, 0, R - 0.35, 72):
        z.Outline().Append(MM(x), MM(-y), -1, 0)
    # screw bosses clamp here: no copper under the boss face
    for i, (bx, by) in enumerate(bosses):
        poly_zone(board, allcu, circ(bx, by, L["bosses"]["boss_dia"] / 2 + 0.3, 24),
                  rule="tracks vias pour", name=f"boss_{i + 1}")

    # silkscreen label
    t = pcbnew.PCB_TEXT(board)
    t.SetText("ShotPuck rev A")
    t.SetPosition(V(0, -4.0))
    t.SetLayer(pcbnew.B_SilkS)
    t.SetMirrored(True)
    t.SetTextSize(pcbnew.VECTOR2I(MM(1.0), MM(1.0)))
    board.Add(t)
    return board


def gnd_fanout(board):
    """Give every GND SMD pad a short stub + via to the bottom GND plane."""
    R = L["pcb_dia"] / 2
    gnd = board.FindNet("GND")
    vias = []
    c = L["cell"]
    m = L["module"]
    ant_x = m["x"] - m["l"] / 2 + 3.9
    bosses = [(L["bosses"]["radius"] * math.cos(math.radians(a)),
               L["bosses"]["radius"] * math.sin(math.radians(a))) for a in L["bosses"]["angles_deg"]]
    pads = []
    for fp in board.GetFootprints():
        for p in fp.Pads():
            bb = p.GetBoundingBox()
            pads.append((p, pcbnew.ToMM(bb.GetLeft()), pcbnew.ToMM(-bb.GetBottom()),
                         pcbnew.ToMM(bb.GetRight()), pcbnew.ToMM(-bb.GetTop())))

    def free(x, y, own):
        if math.hypot(x, y) > R - 0.9:
            return False
        if math.hypot(x - c["x"], y - c["y"]) < c["dia"] / 2 + 0.9:
            return False
        if x < ant_x + 0.6 and abs(y) < 6.9:
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

    def seg_ok(ax, ay, bx, by, own):
        for k in range(1, 9):
            x, y = ax + (bx - ax) * k / 8, ay + (by - ay) * k / 8
            if x < ant_x + 0.3 and abs(y) < 6.9:
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
            if p.GetNetname() != "GND" or p.GetAttribute() != pcbnew.PAD_ATTRIB_SMD:
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
                        via.SetWidth(MM(0.5))
                        via.SetDrill(MM(0.25))
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
    c = L["cell"]
    m = L["module"]
    ant_x = m["x"] - m["l"] / 2 + 3.9
    bosses = [(L["bosses"]["radius"] * math.cos(math.radians(a)),
               L["bosses"]["radius"] * math.sin(math.radians(a))) for a in L["bosses"]["angles_deg"]]
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
            if math.hypot(x - c["x"], y - c["y"]) < c["dia"] / 2 + 0.9:
                continue
            if x < ant_x + 0.8 and abs(y) < 7.0:
                continue
            if any(math.hypot(x - bx, y - by) < L["bosses"]["boss_dia"] / 2 + 0.9 for bx, by in bosses):
                continue
            if any(x1 - 0.45 < x < x2 + 0.45 and y1 - 0.45 < y < y2 + 0.45 for (x1, y1, x2, y2) in pads):
                continue
            if any(math.hypot(x - vx, y - vy) < 1.0 for vx, vy in vias):
                continue
            if any(dseg(x, y, s) < s[4] + 0.25 + 0.2 for s in segs):
                continue
            if any(math.hypot(x - t["x"], y - t["y"]) < t["keepout_r"] for t in TRIM):
                continue
            via = pcbnew.PCB_VIA(board)
            via.SetPosition(V(x, y))
            via.SetWidth(MM(0.5))
            via.SetDrill(MM(0.25))
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
    pcbnew.ZONE_FILLER(board).Fill(board.Zones())


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
    if os.path.exists(ses):
        os.remove(ses)
    cmd = [JAVA, "-jar", JAR, "--gui.enabled=false", "-de", dsn, "-do", ses, "-mp", "40",
           "-mt", "1"]
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
    have = {(round(pcbnew.ToMM(v.GetPosition().x), 2), round(pcbnew.ToMM(v.GetPosition().y), 2))
            for v in board.GetTracks() if v.Type() == pcbnew.PCB_VIA_T}
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
            dia, drill = (int(m.group(1)), int(m.group(2))) if m else (500, 250)
            pos = pcbnew.VECTOR2I(int(float(v[2]) * k), int(-float(v[3]) * k))
            if (round(pcbnew.ToMM(pos.x), 2), round(pcbnew.ToMM(pos.y), 2)) in have:
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


def main():
    path = os.path.join(KI, "shotpuck.kicad_pcb")
    board = build()
    pcbnew.SaveBoard(path, board)
    board = pcbnew.LoadBoard(path)
    if "--no-route" not in sys.argv:
        print("routed:", route(board, path, timeout=240))
        stitch(board)
        fill(board)
        board.BuildConnectivity()
        left = board.GetConnectivity().GetUnconnectedCount(False)
        if left:
            print(f"second pass for {left} connection(s):", route(board, path, timeout=200))
            stitch(board)
    fill(board)
    if islands.bridge_islands(board):
        fill(board)
    pcbnew.SaveBoard(path, board)
    drc(path)


if __name__ == "__main__":
    main()
