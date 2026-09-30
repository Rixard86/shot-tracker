import math

import cadquery as cq

from geometry import (BOSS_D, BOSS_HEAD_WALL, COUNTERBORE_D, COUNTERBORE_DEPTH, L, P, R, SLEEVE_BORE_CLEAR,
                      SLEEVE_LIP_DEPTH, SLEEVE_LIP_WALL, T_PLATE, WALL, Z_BOSS, Z_CAP, Z_CEIL, Z_CELL, Z_KAPTON_TOP, Z_PCB, Z_PCB_TOP,
                      Z_SEAL_TOP, Z_SLEEVE_TOP, Z_TOP, boss_xy, cell_dir_deg, connector_pose, module_pose, notch_r)

EDGE_CHAMFER = 0.3
TOP_FILLET = 0.8
CUT_MARGIN = 1.0
BODY_CLEAR = 0.2
FACE_GAP = 0.05
OVERMOLD_CLEAR = 0.25
SEAL_LEAD = 0.2


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
    c = c.cut(column((0, 0, R - WALL, Z_CAP - CUT_MARGIN, Z_CEIL)))
    return c.cut(tube((0, 0, R + CUT_MARGIN, Z_CAP - CUT_MARGIN, Z_SEAL_TOP), L["seal"]["ledge_od"] / 2))


def add_bosses(cap):
    b = L["bosses"]
    head_r = COUNTERBORE_D / 2 + BOSS_HEAD_WALL
    z_head = Z_TOP - COUNTERBORE_DEPTH - BOSS_HEAD_WALL
    for x, y in boss_xy():
        cap = cap.union(column((x, y, BOSS_D / 2, Z_BOSS, Z_CEIL)))
        cap = cap.union(column((x, y, head_r, z_head, Z_CEIL)))
        cap = cap.cut(column((x, y, b["screw_hole"] / 2, Z_PCB_TOP - CUT_MARGIN, Z_TOP + CUT_MARGIN)))
        cap = cap.cut(column((x, y, COUNTERBORE_D / 2, Z_TOP - COUNTERBORE_DEPTH, Z_TOP + CUT_MARGIN)))
    return cap


def port_box(width, spans):
    (y0, y1), z_range = spans
    return placed(slab((width, y1 - y0), z_range).translate((0, -(y0 + y1) / 2, 0)), connector_pose())


def face_distance():
    k = L["connector"]
    return math.hypot(k["x"], k["y"]) + k["face"] + FACE_GAP


def port_numbers():
    skin = Z_TOP - Z_PCB_TOP - L["connector"]["body_h"] - BODY_CLEAR
    return skin, 2 * math.sqrt(R ** 2 - face_distance() ** 2)


def body_well():
    k = L["connector"]
    z0 = Z_PCB_TOP + k["mouth_z"] - k["ring_h"] / 2 - BODY_CLEAR
    spans = ((k["body_back"] - BODY_CLEAR, k["ring_back"]), (z0, Z_PCB_TOP + k["body_h"] + BODY_CLEAR))
    return port_box(k["body_w"] + 2 * BODY_CLEAR, spans)


def seal_hole():
    k = L["connector"]
    squeeze = 2 * k["seal_squeeze"]
    start = k["ring_back"] - SEAL_LEAD
    hole = cq.Workplane("XZ", origin=(0, -start, Z_PCB_TOP + k["mouth_z"]))
    hole = hole.slot2D(k["ring_w"] - squeeze, k["ring_h"] - squeeze).extrude(k["face"] + CUT_MARGIN - start)
    return placed(hole, connector_pose())


def plug_flat():
    k = L["connector"]
    face = face_distance()
    z0 = Z_PCB_TOP + k["mouth_z"] - k["overmold_h"] / 2 - OVERMOLD_CLEAR
    length = R + CUT_MARGIN - face
    cutter = slab((length, 2 * R), (z0, Z_TOP + CUT_MARGIN)).translate((face + length / 2, 0, 0))
    return placed(cutter, (0, 0, math.degrees(math.atan2(k["y"], k["x"]))))


def add_sleeve_lip(cap):
    r_bore = L["sleeve"]["od"] / 2 + SLEEVE_BORE_CLEAR
    z_lip = Z_CEIL - SLEEVE_LIP_DEPTH
    cap = cap.union(column((0, 0, r_bore + SLEEVE_LIP_WALL, z_lip, Z_TOP)))
    cap = cap.cut(column((0, 0, r_bore, z_lip - CUT_MARGIN, Z_CEIL)))
    return cap.cut(column((0, 0, L["bolt"]["clearance_dia"] / 2, Z_CEIL - CUT_MARGIN, Z_TOP + CUT_MARGIN)))


def make_cap():
    return add_sleeve_lip(add_bosses(cap_shell()).cut(body_well()).cut(seal_hole()).cut(plug_flat()))


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
    p = p.cut(placed(channel, (c["x"], c["y"], cell_dir_deg())))
    k = L["connector"]
    return p.cut(port_box(k["notch_w"], ((k["body_back"], R), (Z_PCB - CUT_MARGIN, Z_PCB + t + CUT_MARGIN))))


def make_cell():
    c = L["cell"]
    z0 = Z_CELL + c["tab_h"]
    return column((c["x"], c["y"], c["dia"] / 2, z0, z0 + c["h"]))


def make_module():
    m = L["module"]
    return placed(slab((m["l"], m["w"]), (Z_PCB_TOP, Z_PCB_TOP + m["h"])), module_pose())


def make_connector():
    k = L["connector"]
    z0 = Z_PCB_TOP + k["mouth_z"] - k["ring_h"] / 2
    body = port_box(k["body_w"], ((k["body_back"], k["ring_back"]), (z0, Z_PCB_TOP + k["body_h"])))
    return body.union(port_box(k["ring_w"], ((k["ring_back"], k["face"]), (z0, z0 + k["ring_h"]))))

