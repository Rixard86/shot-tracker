import math
import os

import cadquery as cq

from fdm_shapes import arc_deg, backed_ring, backing_run, flared_column, keep_clear, sector, sloped_post
from geometry import L, OUT, R, T_PLATE, WALL, Z_KAPTON_TOP, Z_PCB, Z_SEAL_TOP, boss_xy
from puck_parts import CUT_MARGIN, EDGE_CHAMFER, band_inner_r, column, tube

PLATE_EXTRA_THICKNESS = 0.5
Z_UNDERSIDE = -PLATE_EXTRA_THICKNESS
FDM_HOLE_GROWTH = 0.2
M2_SELF_THREAD_HOLE = 1.7
STUD_LEN = Z_KAPTON_TOP - L["bosses"]["tip_inset"]
STUD_TIP_CLEAR = 0.2
LIP_FIT_CLEAR = 0.1
LIP_TOP_CLEAR = 0.05
ORING_CS = 0.6
ORING_SQUEEZE = 0.25
ORING_STEP_WIDTH = 0.6
CELL_FIT_CLEAR = 0.15
CELL_POCKET_DEPTH = 1.0
TUBE_FIT_CLEAR = 0.2
TUBE_LIP_WALL = 0.8
TUBE_LIP_PCB_CLEAR = 0.2
TUBE_LIP_CELL_GAP = 0.5
STANDOFF_PAD_MARGIN = 0.2
STRIP_T = 0.15
POST_STRIP_GAP = 0.05
POST_DEPTH = 1.0
POST_WIDTH = 4.0
POST_PCB_CLEAR = 0.2
CELL_LIP_WALL = 0.8
CELL_LIP_HEIGHT = 1.0
CELL_LIP_CHAMFER = 0.3
LIP_CAP_CLEAR = 0.2
LIP_TUBE_GAP = 0.4
HALF_TURN_DEG = 180.0
FULL_TURN_DEG = 360.0
SPRING_TURN_DEG = 15.0
SPRING_ARC_DEG = 55.0
SPRING_POCKET_GAP = 0.1
SPRING_T = 0.6
SPRING_ROOT_T = 1.6
SPRING_ROOT_LEN = 2.0
SPRING_INNER_GAP = 0.2
SPRING_SLOT_GAP = 0.5
SPRING_PCB_CLEAR = 0.6
SOCKET_FIT_CLEAR = 0.05
BOTTOM_SOLID = 1.0
ARM_FLOOR_CLEAR = 0.3
BUMP_W = 1.5
POST_CHAMFER = 0.4
SOCKET_WALL = 0.8
STL_NAME = "plate_fdm.stl"
STL_FINE = {"tolerance": 0.005, "angularTolerance": 0.05}
TUBE_BORE_R = (L["sleeve"]["od"] + FDM_HOLE_GROWTH) / 2 + TUBE_FIT_CLEAR
KEEP_CLEAR_RADII = (TUBE_BORE_R, band_inner_r() - LIP_CAP_CLEAR, L["standoffs"]["od"] / 2 + STANDOFF_PAD_MARGIN)


def thick_plate():
    disc = cq.Workplane("XY").workplane(offset=Z_UNDERSIDE).circle(R).extrude(T_PLATE - Z_UNDERSIDE)
    return disc.faces("<Z").edges().chamfer(EDGE_CHAMFER)


def printed_hole(xy, dia):
    x, y = xy
    return column((x, y, (dia + FDM_HOLE_GROWTH) / 2, Z_UNDERSIDE - CUT_MARGIN, T_PLATE + CUT_MARGIN))


def blind_stud_hole(xy):
    x, y = xy
    z_bottom = Z_KAPTON_TOP - STUD_LEN - STUD_TIP_CLEAR
    return column((x, y, (M2_SELF_THREAD_HOLE + FDM_HOLE_GROWTH) / 2, z_bottom, T_PLATE + CUT_MARGIN))


def locating_lip():
    r_in = (L["seal"]["ledge_od"] + FDM_HOLE_GROWTH) / 2 + LIP_FIT_CLEAR
    lip = tube((0, 0, R, T_PLATE, Z_SEAL_TOP - LIP_TOP_CLEAR), r_in)
    return lip.cut(column((0, 0, r_in + ORING_STEP_WIDTH, Z_SEAL_TOP - ORING_CS * (1 - ORING_SQUEEZE), Z_SEAL_TOP)))


def cell_pocket_r():
    return (L["cell"]["dia"] + FDM_HOLE_GROWTH) / 2 + CELL_FIT_CLEAR


def cell_pocket():
    c = L["cell"]
    return column((c["x"], c["y"], cell_pocket_r(), T_PLATE - CELL_POCKET_DEPTH, T_PLATE + CUT_MARGIN))


def tube_lip():
    c = L["cell"]
    z_top = Z_PCB - TUBE_LIP_PCB_CLEAR
    ring = backed_ring((0, 0), ((TUBE_BORE_R, TUBE_BORE_R + TUBE_LIP_WALL), (T_PLATE, z_top)))
    r_cut = cell_pocket_r() + TUBE_LIP_CELL_GAP - backing_run((T_PLATE - CUT_MARGIN, T_PLATE))
    return ring.cut(flared_column((c["x"], c["y"], r_cut, T_PLATE - CUT_MARGIN, z_top + CUT_MARGIN)))


def cell_lip_top():
    return T_PLATE + CELL_LIP_HEIGHT


def lip_gap_deg(r_clear):
    c = L["cell"]
    d, rho = math.hypot(c["x"], c["y"]), cell_pocket_r() + CELL_LIP_WALL
    return math.degrees(math.acos((r_clear ** 2 - d ** 2 - rho ** 2) / (2 * d * rho)))


def cell_lip_backing():
    c, r_out = L["cell"], cell_pocket_r() + CELL_LIP_WALL
    back = backed_ring((c["x"], c["y"]), ((r_out - CELL_LIP_WALL / 2, r_out), (T_PLATE, cell_lip_top())))
    return keep_clear(back, KEEP_CLEAR_RADII)


def cell_lip():
    c, r0, top = L["cell"], cell_pocket_r(), cell_lip_top()
    rim, near = math.degrees(math.atan2(c["y"], c["x"])), lip_gap_deg(R - WALL - LIP_CAP_CLEAR)
    far = lip_gap_deg((L["sleeve"]["od"] + FDM_HOLE_GROWTH) / 2 + TUBE_FIT_CLEAR + TUBE_LIP_WALL + LIP_TUBE_GAP)
    ring = tube((c["x"], c["y"], r0 + CELL_LIP_WALL, T_PLATE, top), r0)
    ring = ring.faces(">Z").edges(cq.selectors.RadiusNthSelector(0)).chamfer(CELL_LIP_CHAMFER).union(cell_lip_backing())
    r_cut = r0 + CELL_LIP_WALL + backing_run((T_PLATE, top)) + CUT_MARGIN
    wedge, z_span = (r0 - CUT_MARGIN, r_cut), (T_PLATE - CUT_MARGIN, top + CUT_MARGIN)
    ring = ring.cut(sector(wedge, (rim - near, rim + near) + z_span))
    return ring.cut(sector(wedge, (spring_deg(), rim + FULL_TURN_DEG - far) + z_span))


def standoff_pads():
    r = L["standoffs"]["od"] / 2 + STANDOFF_PAD_MARGIN
    pads = None
    for x, y in boss_xy():
        pad = column((x, y, r, T_PLATE, Z_KAPTON_TOP))
        pads = pad if pads is None else pads.union(pad)
    return pads


def pad_dir_deg():
    c = L["cell"]
    pad = c["pos_pad"]
    return math.degrees(math.atan2(pad["y"] - c["y"], pad["x"] - c["x"]))


def spring_deg():
    return pad_dir_deg() - HALF_TURN_DEG + SPRING_TURN_DEG


def strip_post():
    r_face = L["cell"]["dia"] / 2 + STRIP_T + POST_STRIP_GAP
    a, half = pad_dir_deg(), arc_deg(POST_WIDTH / 2, r_face)
    top = Z_PCB - POST_PCB_CLEAR
    profile = [(r_face, T_PLATE), (r_face, top - POST_CHAMFER), (r_face + POST_CHAMFER, top), (r_face + POST_DEPTH, top)]
    return keep_clear(sloped_post(profile, (a - half, a + half)), KEEP_CLEAR_RADII)


def spring_inner_r():
    return cell_pocket_r() + SPRING_POCKET_GAP


def spring_outer_r():
    return spring_inner_r() + SPRING_T


def slot_floor_z():
    return Z_UNDERSIDE + BOTTOM_SOLID


def part_bottom_z():
    return slot_floor_z() + ARM_FLOOR_CLEAR


def spring_top_z():
    return Z_PCB - SPRING_PCB_CLEAR


def root_deg():
    return spring_deg() + SPRING_ARC_DEG


def tab_deg():
    return arc_deg(SPRING_ROOT_T, spring_inner_r() + SPRING_ROOT_LEN / 2)


def socket_span(clear):
    r0 = spring_inner_r()
    return root_deg() - arc_deg(clear, r0), root_deg() + tab_deg() + arc_deg(clear, r0)


def socket_collar():
    clear = FDM_HOLE_GROWTH / 2 + SOCKET_FIT_CLEAR + SOCKET_WALL
    r_out, top = spring_inner_r() + SPRING_ROOT_LEN + clear, spring_top_z()
    profile = [(cell_pocket_r(), T_PLATE), (cell_pocket_r(), top), (r_out, top)]
    return keep_clear(sloped_post(profile, socket_span(clear)), KEEP_CLEAR_RADII)


def spring_slot():
    r0, top = spring_inner_r(), Z_PCB
    tip = spring_deg() - arc_deg(BUMP_W / 2 + SPRING_SLOT_GAP, r0)
    arm = sector((r0 - SPRING_INNER_GAP, spring_outer_r() + SPRING_SLOT_GAP), (tip, root_deg(), slot_floor_z(), top))
    clear = FDM_HOLE_GROWTH / 2 + SOCKET_FIT_CLEAR
    socket = sector((r0 - clear, r0 + SPRING_ROOT_LEN + clear), socket_span(clear) + (part_bottom_z(), top))
    return arm.union(socket)


def make_fdm_plate():
    p = thick_plate().union(locating_lip()).union(tube_lip()).union(standoff_pads()).cut(cell_pocket())
    p = p.union(cell_lip()).union(strip_post()).union(socket_collar()).cut(spring_slot())
    p = p.cut(printed_hole((0, 0), L["bolt"]["clearance_dia"]))
    for xy in boss_xy():
        p = p.cut(blind_stud_hole(xy))
    return p


def main():
    os.makedirs(OUT, exist_ok=True)
    path = os.path.join(OUT, STL_NAME)
    cq.exporters.export(make_fdm_plate(), path, **STL_FINE)
    print(path)


if __name__ == "__main__":
    main()
