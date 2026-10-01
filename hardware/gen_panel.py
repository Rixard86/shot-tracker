import math
import os
import shutil
import subprocess
import sys

import kikit
import pcbnew

import board_shape as shape

HERE = os.path.dirname(os.path.abspath(__file__))
KI = os.path.join(HERE, "kicad")
BOARD = os.path.join(KI, "shotpuck.kicad_pcb")
PROJECT = os.path.join(KI, "shotpuck.kicad_pro")
PANEL_DIR = os.path.join(HERE, "kicad", "panel")
PANEL = os.path.join(PANEL_DIR, "shotpuck-panel.kicad_pcb")
TABBED = os.path.join(KI, "shotpuck-tabbed.kicad_pcb")
TABBED_PROJECT = os.path.join(KI, "shotpuck-tabbed.kicad_pro")
KIKIT_FOOTPRINTS = os.path.join(os.path.dirname(kikit.__file__), "resources", "kikit.pretty")
LIB_TABLE = ('(fp_lib_table\n  (lib (name "ShotPuck")(type "KiCad")(uri "${KIPRJMOD}/../ShotPuck.pretty")'
             '(options "")(descr "ShotPuck custom footprints"))\n)\n')
KIKIT_PREFIX = "KIKIT:"
TAB_WIDTH_MM = 3.0
TAB_ORIGIN_OUT_MM = 0.5
TOP_TAB_X = 1.2
TOP_TAB_WIDTH_MM = 1.5
TOP = (0, 1)
BOTTOM_TAB_X = -8.5
SIDE_TAB_Y = -4.0
TABS = [(TOP, TOP_TAB_X), ((0, -1), BOTTOM_TAB_X), ((-1, 0), SIDE_TAB_Y), ((1, 0), SIDE_TAB_Y)]

SETTINGS = [
    "--layout", "grid; rows: 2; cols: 2; hspace: 3mm; vspace: 2.5mm; hbackbone: 2mm; "
    "rotation: 180deg; alternation: rows; renameref: {orig}_{n}",
    "--tabs", "annotation",
    "--cuts", "mousebites; drill: 0.5mm; spacing: 0.8mm; offset: 0.2mm; prolong: 0.5mm",
    "--framing", "frame; width: 5mm; space: 3mm",
    "--tooling", "3hole; hoffset: 2.5mm; voffset: 2.5mm; size: 1.152mm",
    "--fiducials", "3fid; hoffset: 5mm; voffset: 2.5mm; coppersize: 1mm; opening: 2mm",
    "--text", "simple; text: JLCJLCJLCJLC; anchor: mt; voffset: 2.5mm; hjustify: center; vjustify: center",
    "--post", "millradius: 0.5mm; refillzones: true",
]


def tab_width(tab):
    return TOP_TAB_WIDTH_MM if tab[0] == TOP else TAB_WIDTH_MM


def tab_origin(tab):
    outward, offset = tab
    nearest_edge = max(0.0, abs(offset) - tab_width(tab) / 2)
    reach = math.sqrt(shape.rim_r() ** 2 - nearest_edge ** 2) + TAB_ORIGIN_OUT_MM
    return outward[0] * reach + abs(outward[1]) * offset, outward[1] * reach + abs(outward[0]) * offset


def design_origin(board):
    centre = board.GetBoardEdgesBoundingBox().Centre()
    minx, miny, maxx, maxy = shape.board().bounds
    return pcbnew.ToMM(centre.x) - (minx + maxx) / 2, pcbnew.ToMM(centre.y) + (miny + maxy) / 2


def tab_footprint(board, tab):
    outward = tab[0]
    x, y = tab_origin(tab)
    origin_x, origin_y = design_origin(board)
    fp = pcbnew.FootprintLoad(KIKIT_FOOTPRINTS, "Tab")
    fp.SetFPID(pcbnew.LIB_ID("kikit", "Tab"))
    for item in fp.GraphicalItems():
        if isinstance(item, pcbnew.PCB_TEXT) and item.GetText().startswith(KIKIT_PREFIX):
            item.SetText(f"{KIKIT_PREFIX} width: {tab_width(tab)}mm")
    board.Add(fp)
    fp.SetPosition(pcbnew.VECTOR2I(pcbnew.FromMM(origin_x + x), pcbnew.FromMM(origin_y - y)))
    fp.SetOrientationDegrees(math.degrees(math.atan2(-outward[1], -outward[0])))


def write_tabbed_board():
    board = pcbnew.LoadBoard(BOARD)
    for tab in TABS:
        tab_footprint(board, tab)
    pcbnew.SaveBoard(TABBED, board)
    shutil.copyfile(PROJECT, TABBED_PROJECT)


def main():
    os.makedirs(PANEL_DIR, exist_ok=True)
    write_tabbed_board()
    cmd = [sys.executable, "-m", "kikit.ui", "panelize"] + SETTINGS + [TABBED, PANEL]
    try:
        subprocess.run(cmd, check=True)
    finally:
        for path in (TABBED, TABBED_PROJECT):
            os.remove(path)
    with open(os.path.join(PANEL_DIR, "fp-lib-table"), "w") as f:
        f.write(LIB_TABLE)
    print("wrote", PANEL)


if __name__ == "__main__":
    main()
