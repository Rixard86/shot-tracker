import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, os.path.dirname(HERE))

from geometry import (COUNTERBORE_DEPTH, L, OUT, P, T_PLATE, Z_CAP, Z_CEIL, Z_CELL, Z_KAPTON_TOP, Z_PCB,
                      Z_PCB_TOP, Z_TOP, boss_xy)

R_CAVITY = P["od"] / 2 - P["cap_wall"]
