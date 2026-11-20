import math
import os

import cadquery as cq

from geometry import OUT, R, WALL, boss_xy
from plate_fdm import FDM_HOLE_GROWTH, STL_FINE
from puck_parts import tube

SPACER_COUNT = 5
SPACER_T = 0.6
M2_DIA = 2.0
SCREW_GRIP = 0.1
CAP_WALL_CLEAR = 0.2
PRINT_GAP = 3.0
STL_NAME = "spacer_fdm.stl"


def spacer_outer_r():
    hole_r = max(math.hypot(x, y) for x, y in boss_xy())
    return R - WALL - hole_r - CAP_WALL_CLEAR


def spacer_hole_r():
    return (M2_DIA - SCREW_GRIP + FDM_HOLE_GROWTH) / 2


def spacer(xy):
    x, y = xy
    return tube((x, y, spacer_outer_r(), 0, SPACER_T), spacer_hole_r())


def spacer_set():
    pitch = 2 * spacer_outer_r() + PRINT_GAP
    solids = [spacer((i * pitch, 0)).val() for i in range(SPACER_COUNT)]
    return cq.Compound.makeCompound(solids)


def main():
    os.makedirs(OUT, exist_ok=True)
    path = os.path.join(OUT, STL_NAME)
    cq.exporters.export(spacer_set(), path, **STL_FINE)
    print(path)


if __name__ == "__main__":
    main()
