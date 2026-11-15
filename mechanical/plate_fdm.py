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
CELL_FIT_CLEAR = -0.05
CELL_POCKET_DEPTH = 1.0
TUBE_FIT_CLEAR = 0.0
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
FULL_TURN_DEG = 360.0
POST_CHAMFER = 0.4
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
    return tube((0, 0, R, T_PLATE, Z_SEAL_TOP - LIP_TOP_CLEAR), r_in)


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
    ring = ring.cut(sector(wedge, (rim + far, rim + FULL_TURN_DEG - far) + z_span))
    return ring.cut(sector(wedge, post_span() + z_span))


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


def post_face_r():
    return L["cell"]["dia"] / 2 + STRIP_T + POST_STRIP_GAP


def post_span():
    a, half = pad_dir_deg(), arc_deg(POST_WIDTH / 2, post_face_r())
    return a - half, a + half


def strip_slot():
    radii = (cell_pocket_r() - CUT_MARGIN, post_face_r())
    return sector(radii, post_span() + (T_PLATE - CELL_POCKET_DEPTH, T_PLATE + CUT_MARGIN))


def strip_post():
    r_face = post_face_r()
    top = Z_PCB - POST_PCB_CLEAR
    profile = [(r_face, T_PLATE), (r_face, top - POST_CHAMFER), (r_face + POST_CHAMFER, top), (r_face + POST_DEPTH, top)]
    return keep_clear(sloped_post(profile, post_span()), KEEP_CLEAR_RADII)


def make_fdm_plate():
    p = thick_plate().union(locating_lip()).union(tube_lip()).union(standoff_pads()).cut(cell_pocket()).cut(strip_slot())
    p = p.union(cell_lip()).union(strip_post())
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
