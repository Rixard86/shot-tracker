#!/usr/bin/env python3
"""
puck.py - parametric ShotPuck enclosure (CadQuery 2.x).

Reads ../layout.json, builds:
  base.step / base.stl   7075-T6 aluminium, hard anodised, integral 5/16-24 stud
  cap.step  / cap.stl    polycarbonate (translucent, LED shows through)
  assembly.step          base + cap + PCB + cell + module + connector + trim
  mass_report.txt        mass, centre of mass, trim solution

Balance: every asymmetric mass (cell, module, connector, cap openings) is
summed, and trim pins (layout.json trim: material, density, diameter) are
sized and placed to cancel the first moment, so the centre of mass lands on
the axis. The pins are optional: the report also gives the balance without them.

    python puck.py            # writes into ./out
"""
import json
import math
import os

import cadquery as cq

HERE = os.path.dirname(os.path.abspath(__file__))
L = json.load(open(os.path.join(HERE, "..", "layout.json")))
P = L["puck"]
OUT = os.path.join(HERE, "out")
os.makedirs(OUT, exist_ok=True)

RHO = {"al7075": 2.81, "pc": 1.20, "fr4": 1.85}  # g/cm3

OD = P["od"]
R = OD / 2
T_BASE = P["base_thickness"]
Z_PCB = T_BASE + P["insulator"]                 # PCB bottom
Z_PCB_TOP = Z_PCB + P["pcb_thickness"]
CAV_H = L["cell"]["swell_h"] + L["cell"]["tab_h"]              # above PCB top: swollen cell fits
Z_CEIL = Z_PCB_TOP + CAV_H                      # cap ceiling (inside)
Z_TOP = Z_CEIL + P["cap_top"]                   # outer top face
WALL = P["cap_wall"]
BOSS_R = L["bosses"]["radius"]
BOSS_D = L["bosses"]["boss_dia"]
BOSS_ANG = L["bosses"]["angles_deg"]
WELL_CLEAR = 0.2
WELL_GAP = 0.1
MODULE_CLEAR = 0.5
ANTENNA_INSET = 3.9
ANTENNA_HALF_W = 6.3
ANTENNA_METAL_CLEAR = 2.0
PIN_BOSS_SKIRT = 0.6
PIN_PCB_CLEAR = 0.4


def boss_xy():
    return [(BOSS_R * math.cos(math.radians(a)), BOSS_R * math.sin(math.radians(a)))
            for a in BOSS_ANG]


# ----------------------------------------------------------------- base
def make_base():
    b = cq.Workplane("XY").circle(R).extrude(T_BASE)
    b = b.faces("<Z").edges().chamfer(0.3)
    # O-ring face-seal groove on top, under the cap wall
    g_r = R - WALL / 2
    cs = P["oring_cs"]
    groove = (cq.Workplane("XY").workplane(offset=T_BASE - cs * 0.75)
              .circle(g_r + cs * 0.65).circle(g_r - cs * 0.65).extrude(cs))
    b = b.cut(groove)
    # integral stud (thread cut by the machinist: 5/16-24 UNF-2A)
    stud_len = P["stud_length"]
    d = P["stud_major_dia"]
    stud = (cq.Workplane("XY").circle(d / 2).extrude(-stud_len)
            .faces("<Z").edges().chamfer(0.6))
    relief = (cq.Workplane("XY").workplane(offset=-1.0).circle(d / 2)
              .circle(d / 2 - 0.55).extrude(1.0))
    b = b.union(stud).cut(relief)
    # countersunk M1.6 clearance holes (heads flush with the seating face)
    for (x, y) in boss_xy():
        hole = cq.Workplane("XY").center(x, y).circle(0.9).extrude(T_BASE)
        csk = cq.Solid.makeCone(1.65, 0.0, 1.65, cq.Vector(x, y, 0), cq.Vector(0, 0, 1))
        b = b.cut(hole).cut(cq.Workplane().add(csk))
    return b


# ------------------------------------------------------------------ cap
def make_cap(trim):
    h = Z_TOP - T_BASE
    c = (cq.Workplane("XY").workplane(offset=T_BASE).circle(R).extrude(h)
         .faces(">Z").edges().fillet(0.8))
    cavity = (cq.Workplane("XY").workplane(offset=T_BASE).circle(R - WALL)
              .extrude(Z_CEIL - T_BASE))
    c = c.cut(cavity)
    # screw bosses from ceiling down to PCB top (clamp PCB onto the base)
    for (x, y) in boss_xy():
        boss = (cq.Workplane("XY").workplane(offset=Z_PCB_TOP).center(x, y)
                .circle(BOSS_D / 2).extrude(Z_CEIL - Z_PCB_TOP))
        insert = (cq.Workplane("XY").workplane(offset=Z_PCB_TOP).center(x, y)
                  .circle(1.6 / 2 + 0.4).extrude(3.2))  # heat-set pilot 2.0
        c = c.union(boss).cut(insert)
    # PCB relief: cavity below PCB top must clear the board edge
    # charging connector: chimney from the ceiling down to the connector top, open to the outside
    k = L["connector"]
    z_conn = Z_PCB_TOP + k["body_h"] + WELL_GAP
    chimney = (cq.Workplane("XY").workplane(offset=z_conn).center(k["x"], k["y"])
               .rect(k["body_w"] + WELL_CLEAR + 2 * P["well_wall"], k["body_l"] + WELL_CLEAR + 2 * P["well_wall"])
               .extrude(Z_CEIL - z_conn))
    opening = (cq.Workplane("XY").workplane(offset=z_conn).center(k["x"], k["y"])
               .rect(k["body_w"] + WELL_CLEAR, k["body_l"] + WELL_CLEAR).extrude(Z_TOP - z_conn))
    c = c.union(chimney).cut(opening)
    # trim pin pocket: boss hanging from the ceiling
    for pin in (trim or []):
        tx, ty, tl = pin["x"], pin["y"], pin["len"]
        pb = (cq.Workplane("XY").workplane(offset=Z_CEIL - tl - 0.6).center(tx, ty)
              .circle(pin["dia"] / 2 + 0.8).extrude(tl + 0.61))
        pocket = (cq.Workplane("XY").workplane(offset=Z_CEIL - tl - 0.6).center(tx, ty)
                  .circle(pin["dia"] / 2 + 0.03).extrude(tl))
        c = c.union(pb).cut(pocket)
    return c


# ---------------------------------------------------------- components
def make_pcb():
    p = cq.Workplane("XY").workplane(offset=Z_PCB).circle(L["pcb_dia"] / 2).extrude(P["pcb_thickness"])
    for (x, y) in boss_xy():
        p = p.cut(cq.Workplane("XY").workplane(offset=Z_PCB).center(x, y)
                  .circle(L["bosses"]["pcb_hole"] / 2).extrude(1))
    return p


def make_cell():
    c = L["cell"]
    return (cq.Workplane("XY").workplane(offset=Z_PCB_TOP + c["tab_h"]).center(c["x"], c["y"])
            .circle(c["dia"] / 2).extrude(c["h"]))


def make_module():
    m = L["module"]
    w, l = (m["l"], m["w"]) if m["rot_deg"] in (90, 270) else (m["w"], m["l"])
    return (cq.Workplane("XY").workplane(offset=Z_PCB_TOP).center(m["x"], m["y"])
            .rect(w, l).extrude(m["h"]))


def make_connector():
    k = L["connector"]
    return (cq.Workplane("XY").workplane(offset=Z_PCB_TOP).center(k["x"], k["y"])
            .rect(k["body_w"], k["body_l"]).extrude(k["body_h"]))


def make_trim(pins):
    w = None
    for t in pins:
        s = (cq.Workplane("XY").workplane(offset=Z_CEIL - t["len"] - 0.6).center(t["x"], t["y"])
             .circle(t["dia"] / 2).extrude(t["len"]))
        w = s if w is None else w.union(s)
    return w


# ------------------------------------------------------------ balance
def props(wp, rho):
    s = wp.val()
    v = s.Volume() / 1000.0  # cm3
    c = s.Center()
    return v * rho, (c.x, c.y, c.z)


def moment(items):
    m = sum(i[0] for i in items)
    mx = sum(i[0] * i[1][0] for i in items)
    my = sum(i[0] * i[1][1] for i in items)
    mz = sum(i[0] * i[1][2] for i in items)
    return m, mx, my, mz


def collides(x, y, r):
    """True if a pocket boss of radius r at (x,y) hits a component/boss/wall."""
    if math.hypot(x, y) + r > R - WALL - 0.3:
        return True
    for (bx, by) in boss_xy():
        if math.hypot(x - bx, y - by) < r + BOSS_D / 2 + 0.3:
            return True
    c = L["cell"]
    if math.hypot(x - c["x"], y - c["y"]) < r + c["dia"] / 2 + 0.3:
        return True
    for (cx, cy, w, h) in [
        (L["connector"]["x"], L["connector"]["y"],
         L["connector"]["body_w"] + WELL_CLEAR + 2 * P["well_wall"] + 1,
         L["connector"]["body_l"] + WELL_CLEAR + 2 * P["well_wall"] + 1),
    ]:
        dx = max(abs(x - cx) - w / 2, 0)
        dy = max(abs(y - cy) - h / 2, 0)
        if math.hypot(dx, dy) < r:
            return True
    return False


def near_antenna(x, y, r):
    m = L["module"]
    ant_x = m["x"] - m["l"] / 2 + ANTENNA_INSET
    dx = max(x - ant_x, 0)
    dy = max(abs(y - m["y"]) - ANTENNA_HALF_W, 0)
    return math.hypot(dx, dy) < r + ANTENNA_METAL_CLEAR


def over_module(x, y, r):
    m = L["module"]
    dx = max(abs(x - m["x"]) - m["l"] / 2, 0)
    dy = max(abs(y - m["y"]) - m["w"] / 2, 0)
    return math.hypot(dx, dy) < r


def max_pin_len(x, y, r):
    full = CAV_H - PIN_BOSS_SKIRT - PIN_PCB_CLEAR
    if not over_module(x, y, r):
        return full
    above_module = Z_CEIL - PIN_BOSS_SKIRT - (Z_PCB_TOP + L["module"]["h"] + MODULE_CLEAR)
    return min(full, above_module)


def solve_trim(mx, my):
    t = L["trim"]
    dia = t["pin_dia"]
    g_per_mm = t["density"] * math.pi * (dia / 2) ** 2 / 1000.0
    min_m = 1.5 * g_per_mm                        # no slivers: >= 1.5 mm long
    # free candidate spots on a polar grid, each with the longest pin that fits there
    cands = []
    for rr in [x / 2 for x in range(16, 31)]:     # 8.0 .. 15.0 mm
        for a in range(0, 360, 5):
            x, y = rr * math.cos(math.radians(a)), rr * math.sin(math.radians(a))
            if not collides(x, y, dia / 2 + 0.8) and not near_antenna(x, y, dia / 2):
                cands.append((x, y, max_pin_len(x, y, dia / 2 + 0.8) * g_per_mm))
    best = None
    # one pin exactly opposite, else best pair (2x2 solve, masses >= 0)
    for (x, y, max_m) in cands:
        k = -(mx * x + my * y) / (x * x + y * y)
        if min_m <= k <= max_m:
            res = math.hypot(mx + k * x, my + k * y)
            if res < 0.05 and (best is None or k < best[0]):
                best = (k, [(x, y, k)])
    if best is None:
        for i in range(len(cands)):
            x1, y1, max1 = cands[i]
            for j in range(i + 1, len(cands)):
                x2, y2, max2 = cands[j]
                if math.hypot(x1 - x2, y1 - y2) < dia + 2 * 0.8 + 0.5:
                    continue
                det = x1 * y2 - x2 * y1
                if abs(det) < 1e-6:
                    continue
                m1 = (-mx * y2 + my * x2) / det
                m2 = (-my * x1 + mx * y1) / det
                if min_m <= m1 <= max1 and min_m <= m2 <= max2:
                    tot = m1 + m2
                    if best is None or tot < best[0]:
                        best = (tot, [(x1, y1, m1), (x2, y2, m2)])
    if not best:
        raise SystemExit("no trim solution: enlarge pin_dia in layout.json")
    return [dict(x=x, y=y, dia=dia, len=round(mm / g_per_mm, 2)) for (x, y, mm) in best[1]]



def main():
    base = make_base()
    cap0 = make_cap(None)
    pcb, cell, mod, conn = make_pcb(), make_cell(), make_module(), make_connector()

    items = [props(base, RHO["al7075"]), props(cap0, RHO["pc"]), props(pcb, RHO["fr4"])]
    for part, key in [(cell, "cell"), (mod, "module"), (conn, "connector")]:
        c = part.val().Center()
        items.append((L[key]["mass_g"], (c.x, c.y, c.z)))
    items.append((L["other_components_g"], (0.0, 0.0, Z_PCB_TOP + 0.5)))  # spread, ~centred

    m0, mx0, my0, _ = moment(items)
    need = math.hypot(mx0, my0)                   # g*mm to cancel (before trim)
    mx, my = mx0, my0
    pins = None
    for _it in range(4):
        pins = solve_trim(mx, my)
        capi = make_cap(pins)
        it = [items[0], props(capi, RHO["pc"])] + items[2:]
        _, mx, my, _ = moment(it)
    cap = make_cap(pins)
    trim = make_trim(pins)
    items2 = [props(base, RHO["al7075"]), props(cap, RHO["pc"]), props(pcb, RHO["fr4"])]
    for part, key in [(cell, "cell"), (mod, "module"), (conn, "connector")]:
        c = part.val().Center()
        items2.append((L[key]["mass_g"], (c.x, c.y, c.z)))
    items2.append((L["other_components_g"], (0.0, 0.0, Z_PCB_TOP + 0.5)))
    tv = props(trim, L["trim"]["density"])
    m_bare, mx_bare, my_bare, _ = moment(items2)
    items2.append(tv)
    m, mx2, my2, mz2 = moment(items2)
    com = (mx2 / m, my2 / m, mz2 / m)

    # exports
    cq.exporters.export(base, os.path.join(OUT, "base.step"))
    cq.exporters.export(base, os.path.join(OUT, "base.stl"), tolerance=0.02, angularTolerance=0.1)
    cq.exporters.export(cap, os.path.join(OUT, "cap.step"))
    cq.exporters.export(cap, os.path.join(OUT, "cap.stl"), tolerance=0.02, angularTolerance=0.1)
    assy = (cq.Assembly(name="shotpuck")
            .add(base, name="base", color=cq.Color(0.55, 0.55, 0.6))
            .add(cap, name="cap", color=cq.Color(0.85, 0.9, 1.0, 0.35))
            .add(pcb, name="pcb", color=cq.Color(0.1, 0.4, 0.15))
            .add(cell, name="cell_cp1654", color=cq.Color(0.8, 0.8, 0.8))
            .add(mod, name="mdbt50q", color=cq.Color(0.2, 0.2, 0.2))
            .add(conn, name="mag_connector", color=cq.Color(0.9, 0.7, 0.2))
            .add(trim, name=f"{L['trim']['material']}_trim", color=cq.Color(0.8, 0.65, 0.2)))
    assy.save(os.path.join(OUT, "assembly.step"))

    rep = []
    rep.append("ShotPuck mass & balance report (generated by puck.py)\n")
    rep.append(f"Overall: OD {OD:.1f} mm, height above weight face {Z_TOP:.2f} mm "
               f"(+ {P['stud_length']:.1f} mm stud into the weight)")
    rep.append(f"Cavity above PCB: {CAV_H:.2f} mm (swollen cell {L['cell']['swell_h']:.1f} mm fits)")
    rep.append(f"Connector well depth (cap top to connector): "
               f"{Z_TOP - Z_PCB_TOP - L['connector']['body_h']:.2f} mm\n")
    rep.append(f"{'part':22s}{'mass g':>9s}{'x':>8s}{'y':>8s}{'z':>8s}")
    names = ["base (7075)", "cap (PC)", "pcb (FR4)", "cell CP1654", "module MDBT50Q",
             "mag connector", "other SMD parts", f"{L['trim']['material']} trim"]
    for n, (mm, c) in zip(names, items2):
        rep.append(f"{n:22s}{mm:9.2f}{c[0]:8.2f}{c[1]:8.2f}{c[2]:8.2f}")
    rep.append(f"\nTotal mass: {m:.2f} g")
    rep.append(f"Unbalance before trim: {need:.2f} g*mm "
               f"(COM {need / m0:.3f} mm off axis)")
    for i, pin in enumerate(pins, 1):
        rep.append(f"Trim pin {i}: {L['trim']['material']} D{pin['dia']:.1f} x {pin['len']:.2f} mm at "
                   f"x={pin['x']:.2f}, y={pin['y']:.2f} (cut from D{pin['dia']:.0f} rod)")
    rep.append(f"Trim total: {tv[0]:.2f} g")
    for pin in pins:
        pin["clear_above_pcb"] = round(Z_CEIL - pin["len"] - 0.6 - Z_PCB_TOP, 2)
        pin["keepout_r"] = round(pin["dia"] / 2 + 0.8 + 0.3, 2)
    json.dump(pins, open(os.path.join(OUT, "trim.json"), "w"), indent=1)
    off = math.hypot(com[0], com[1])
    rep.append(f"Centre of mass after trim: x={com[0]:.3f} y={com[1]:.3f} z={com[2]:.2f} mm "
               f"-> {off:.3f} mm off axis ({off * m:.3f} g*mm)")
    off_bare = math.hypot(mx_bare, my_bare) / m_bare
    rep.append(f"Without the (optional) trim pins: {m_bare:.2f} g, COM {off_bare:.3f} mm off axis "
               f"({math.hypot(mx_bare, my_bare):.2f} g*mm; the cap pockets stay empty)")
    rep.append("\nComponent masses are datasheet/estimated values from layout.json."
               " Weigh the real parts and re-run; the trim updates automatically.")
    txt = "\n".join(rep)
    open(os.path.join(OUT, "mass_report.txt"), "w").write(txt + "\n")
    print(txt)


if __name__ == "__main__":
    main()
