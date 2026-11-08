import os

import cadquery as cq

from geometry import L, OUT, R, T_PLATE, Z_KAPTON_TOP, Z_SEAL_TOP, boss_xy
from puck_parts import CUT_MARGIN, EDGE_CHAMFER, column, tube

PLATE_EXTRA_THICKNESS = 0.5
Z_UNDERSIDE = -PLATE_EXTRA_THICKNESS
FDM_HOLE_GROWTH = 0.2
M2_SELF_THREAD_HOLE = 1.7
STUD_LEN = Z_KAPTON_TOP - L["bosses"]["tip_inset"]
STUD_TIP_CLEAR = 0.2
LIP_FIT_CLEAR = 0.1
LIP_TOP_CLEAR = 0.05
CELL_FIT_CLEAR = 0.15
CELL_POCKET_DEPTH = 1.0
TUBE_FIT_CLEAR = 0.2
TUBE_LIP_WALL = 0.8
TUBE_LIP_HEIGHT = 1.0
TUBE_LIP_CELL_GAP = 0.5
STANDOFF_PAD_MARGIN = 0.2
STL_NAME = "plate_fdm.stl"
STL_FINE = {"tolerance": 0.005, "angularTolerance": 0.05}


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
    r_in = (L["sleeve"]["od"] + FDM_HOLE_GROWTH) / 2 + TUBE_FIT_CLEAR
    z_top = T_PLATE + TUBE_LIP_HEIGHT
    ring = tube((0, 0, r_in + TUBE_LIP_WALL, T_PLATE, z_top), r_in)
    cell_side = (c["x"], c["y"], cell_pocket_r() + TUBE_LIP_CELL_GAP, T_PLATE - CUT_MARGIN, z_top + CUT_MARGIN)
    return ring.cut(column(cell_side))


def standoff_pads():
    r = L["standoffs"]["od"] / 2 + STANDOFF_PAD_MARGIN
    pads = None
    for x, y in boss_xy():
        pad = column((x, y, r, T_PLATE, Z_KAPTON_TOP))
        pads = pad if pads is None else pads.union(pad)
    return pads


def make_fdm_plate():
    p = thick_plate().union(locating_lip()).union(tube_lip()).union(standoff_pads()).cut(cell_pocket())
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
