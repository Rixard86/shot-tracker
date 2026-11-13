import os

import cadquery as cq

from fdm_shapes import arc_deg, at_cell, box_between, sector, sector_loft
from geometry import L, OUT, T_PLATE
from plate_fdm import (BUMP_W, CELL_POCKET_DEPTH, SPRING_ROOT_LEN, STL_FINE, part_bottom_z, root_deg, spring_deg,
                       spring_inner_r, spring_outer_r, spring_top_z, tab_deg)

BUMP_REACH = 0.35
BUMP_FLOOR_CLEAR = 0.3
BUMP_OVERLAP = 0.1
BUMP_CHAMFER = 0.6
TAB_CHAMFER = 0.3
TAB_GROWTHS = (0.05, 0.10, 0.15)
PRINT_GAP = 3.0
STL_NAME = "cell_spring_fdm.stl"


def spring_arm():
    tip = spring_deg() - arc_deg(BUMP_W / 2, spring_inner_r())
    return sector((spring_inner_r(), spring_outer_r()), (tip, root_deg(), part_bottom_z(), spring_top_z()))


def spring_root_tab(grow):
    r0, grow_deg = spring_inner_r(), arc_deg(grow, spring_inner_r())
    r1, a0, a1 = r0 + SPRING_ROOT_LEN, root_deg() - grow_deg, root_deg() + tab_deg() + grow_deg
    z0, z_lead = part_bottom_z(), part_bottom_z() + TAB_CHAMFER
    narrow = ((r0, r1 - TAB_CHAMFER), (a0, a1 - arc_deg(TAB_CHAMFER, r1)))
    lead_in = sector_loft((narrow, ((r0, r1), (a0, a1))), (z0, z_lead))
    return lead_in.union(sector((r0, r1), (a0, a1, z_lead, spring_top_z())))


def spring_bump():
    r_in = L["cell"]["dia"] / 2 - BUMP_REACH
    z0 = T_PLATE - CELL_POCKET_DEPTH + BUMP_FLOOR_CLEAR
    xy = ((r_in, spring_inner_r() + BUMP_OVERLAP), (-BUMP_W / 2, BUMP_W / 2))
    bump = box_between(xy, (z0, spring_top_z())).faces("<X").edges("|Y").chamfer(BUMP_CHAMFER)
    return at_cell(bump, spring_deg())


def cell_spring(grow):
    return spring_arm().union(spring_root_tab(grow)).union(spring_bump())


def spring_set():
    solids, x = [], 0.0
    for grow in TAB_GROWTHS:
        part = cell_spring(grow)
        bb = part.val().BoundingBox()
        solids.append(part.translate((x - bb.xmin, -bb.center.y, -bb.zmin)).val())
        x += bb.xlen + PRINT_GAP
    return cq.Compound.makeCompound(solids)


def main():
    os.makedirs(OUT, exist_ok=True)
    path = os.path.join(OUT, STL_NAME)
    cq.exporters.export(spring_set(), path, **STL_FINE)
    print(path)


if __name__ == "__main__":
    main()
