import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
BOARD = os.path.join(HERE, "kicad", "shotpuck.kicad_pcb")
PANEL_DIR = os.path.join(HERE, "kicad", "panel")
PANEL = os.path.join(PANEL_DIR, "shotpuck-panel.kicad_pcb")
LIB_TABLE = ('(fp_lib_table\n  (lib (name "ShotPuck")(type "KiCad")(uri "${KIPRJMOD}/../ShotPuck.pretty")'
             '(options "")(descr "ShotPuck custom footprints"))\n)\n')

SETTINGS = [
    "--layout", "grid; rows: 2; cols: 2; space: 3mm; renameref: {orig}_{n}",
    "--tabs", "fixed; width: 3mm; vcount: 2; hcount: 0",
    "--cuts", "mousebites; drill: 0.5mm; spacing: 0.8mm; offset: 0.2mm; prolong: 0.5mm",
    "--framing", "railstb; width: 5mm; space: 3mm",
    "--tooling", "3hole; hoffset: 2.5mm; voffset: 2.5mm; size: 1.152mm",
    "--fiducials", "3fid; hoffset: 5mm; voffset: 2.5mm; coppersize: 1mm; opening: 2mm",
    "--text", "simple; text: JLCJLCJLCJLC; anchor: mt; voffset: 2.5mm; hjustify: center; vjustify: center",
    "--post", "millradius: 1mm; refillzones: true",
]


def main():
    os.makedirs(PANEL_DIR, exist_ok=True)
    cmd = [sys.executable, "-m", "kikit.ui", "panelize"] + SETTINGS + [BOARD, PANEL]
    subprocess.run(cmd, check=True)
    with open(os.path.join(PANEL_DIR, "fp-lib-table"), "w") as f:
        f.write(LIB_TABLE)
    print("wrote", PANEL)


if __name__ == "__main__":
    main()
