import math

import cadquery as cq

from geometry import (BOSS_D, BOSS_HEAD_WALL, COUNTERBORE_D, COUNTERBORE_DEPTH, L, P, R, SLEEVE_BORE_CLEAR,
                      LIP_GAP, LIP_WALL, T_PLATE, WALL, WELL_CLEAR, Z_CAP, Z_CEIL, Z_CELL, Z_KAPTON_TOP, Z_PCB, Z_PCB_TOP,
                      SLEEVE_LIP_DEPTH, Z_SLEEVE_TOP, Z_TOP, boss_xy, cell_dir_deg, connector_pose, module_pose, notch_r)

EDGE_CHAMFER = 0.3
TOP_FILLET = 0.8
CUT_MARGIN = 1.0


def column(spec):
    x, y, r, z0, z1 = spec
    return cq.Workplane("XY").workplane(offset=z0).center(x, y).circle(r).extrude(z1 - z0)


def placed(wp, pose):
    x, y, deg = pose
    return wp.rotate((0, 0, 0), (0, 0, 1), deg).translate((x, y, 0))


def slab(size, z_range):
    w, h = size
    z0, z1 = z_range
    return cq.Workplane("XY").workplane(offset=z0).rect(w, h).extrude(z1 - z0)


def make_plate():
    p = cq.Workplane("XY").circle(R).extrude(T_PLATE).faces("<Z").edges().chamfer(EDGE_CHAMFER)
    p = p.cut(column((0, 0, L["bolt"]["clearance_dia"] / 2, -CUT_MARGIN, T_PLATE + CUT_MARGIN)))
    for x, y in boss_xy():
        p = p.cut(column((x, y, L["bosses"]["tap_drill"] / 2, -CUT_MARGIN, T_PLATE + CUT_MARGIN)))
    return p


def stadium(size, z_range):
    length, width = size
    z0, z1 = z_range
    return cq.Workplane("XY").workplane(offset=z0).slot2D(length, width).extrude(z1 - z0)


def flange_top():
    k = L["connector"]
    return Z_PCB_TOP + k["body_h"] - k["boss_h"]


def tube(spec, bore_r):
    x, y, _r, z0, z1 = spec
    return column(spec).cut(column((x, y, bore_r, z0 - CUT_MARGIN, z1 + CUT_MARGIN)))


def make_sleeve():
    s = L["sleeve"]
    return tube((0, 0, s["od"] / 2, T_PLATE, Z_SLEEVE_TOP), s["id"] / 2)


def make_standoffs():
    so = L["standoffs"]
    solid = None
    for x, y in boss_xy():
        piece = tube((x, y, so["od"] / 2, Z_KAPTON_TOP, Z_PCB), so["id"] / 2)
        solid = piece if solid is None else solid.union(piece)
    return solid


def cap_shell():
    c = column((0, 0, R, Z_CAP, Z_TOP)).faces(">Z").edges().fillet(TOP_FILLET)
    return c.cut(column((0, 0, R - WALL, Z_CAP - CUT_MARGIN, Z_CEIL)))


def add_bosses(cap):
    b = L["bosses"]
    head_r = COUNTERBORE_D / 2 + BOSS_HEAD_WALL
    z_head = Z_TOP - COUNTERBORE_DEPTH - BOSS_HEAD_WALL
    for x, y in boss_xy():
        cap = cap.union(column((x, y, BOSS_D / 2, Z_PCB_TOP, Z_CEIL)))
        cap = cap.union(column((x, y, head_r, z_head, Z_CEIL)))
        cap = cap.cut(column((x, y, b["screw_hole"] / 2, Z_PCB_TOP - CUT_MARGIN, Z_TOP + CUT_MARGIN)))
        cap = cap.cut(column((x, y, COUNTERBORE_D / 2, Z_TOP - COUNTERBORE_DEPTH, Z_TOP + CUT_MARGIN)))
    return cap


def add_opening(cap):
    k = L["connector"]
    lip = stadium((k["body_w"] + 2 * LIP_WALL, k["body_l"] + 2 * LIP_WALL), (flange_top() + LIP_GAP, Z_TOP))
    opening = stadium((k["boss_w"] + WELL_CLEAR, k["body_l"] + WELL_CLEAR), (flange_top(), Z_TOP + CUT_MARGIN))
    return cap.union(placed(lip, connector_pose())).cut(placed(opening, connector_pose()))


def add_sleeve_lip(cap):
    r_bore = L["sleeve"]["od"] / 2 + SLEEVE_BORE_CLEAR
    z_lip = Z_CEIL - SLEEVE_LIP_DEPTH
    cap = cap.union(column((0, 0, r_bore + LIP_WALL, z_lip, Z_TOP)))
    cap = cap.cut(column((0, 0, r_bore, z_lip - CUT_MARGIN, Z_CEIL)))
    return cap.cut(column((0, 0, L["bolt"]["clearance_dia"] / 2, Z_CEIL - CUT_MARGIN, Z_TOP + CUT_MARGIN)))


def make_cap():
    return add_sleeve_lip(add_opening(add_bosses(cap_shell())))


def make_pcb():
    c = L["cell"]
    t = P["pcb_thickness"]
    p = column((0, 0, L["pcb_dia"] / 2, Z_PCB, Z_PCB + t))
    s = L["sleeve"]
    r_tube = s["od"] / 2 + s["pcb_clear"]
    reach = math.hypot(c["x"], c["y"])
    slot = slab((reach, 2 * r_tube), (Z_PCB - CUT_MARGIN, Z_PCB + t + CUT_MARGIN)).translate((reach / 2, 0, 0))
    p = p.cut(column((0, 0, r_tube, Z_PCB - CUT_MARGIN, Z_PCB + t + CUT_MARGIN)))
    p = p.cut(placed(slot, (0, 0, cell_dir_deg())))
    for x, y in boss_xy():
        p = p.cut(column((x, y, L["bosses"]["pcb_hole"] / 2, Z_PCB - CUT_MARGIN, Z_PCB + t + CUT_MARGIN)))
    p = p.cut(column((c["x"], c["y"], notch_r(), Z_PCB - CUT_MARGIN, Z_PCB + t + CUT_MARGIN)))
    channel = slab((2 * R, 2 * notch_r()), (Z_PCB - CUT_MARGIN, Z_PCB + t + CUT_MARGIN)).translate((R, 0, 0))
    return p.cut(placed(channel, (c["x"], c["y"], cell_dir_deg())))


def make_cell():
    c = L["cell"]
    z0 = Z_CELL + c["tab_h"]
    return column((c["x"], c["y"], c["dia"] / 2, z0, z0 + c["h"]))


def make_module():
    m = L["module"]
    return placed(slab((m["l"], m["w"]), (Z_PCB_TOP, Z_PCB_TOP + m["h"])), module_pose())


def make_connector():
    k = L["connector"]
    body = stadium((k["body_w"], k["body_l"]), (Z_PCB_TOP, flange_top()))
    body = body.union(stadium((k["boss_w"], k["body_l"]), (flange_top(), Z_PCB_TOP + k["body_h"])))
    return placed(body, connector_pose())

