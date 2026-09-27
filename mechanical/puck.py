#!/usr/bin/env python3
"""
puck.py - parametric ShotPuck enclosure (CadQuery 2.x) and balance.

Reads ../layout.json (geometry.py) and builds with puck_parts.py:
  plate.step / plate.stl   flat 6061 plate: bolt hole, 3x M2 tapped
  cap.step / cap.stl       polycarbonate cap: screw counterbores, sleeve bore, connector opening
  sleeve.stl               steel tube from the plate through the PCB to the cap top (bolt load path)
  standoffs.stl            3 M2 standoffs under the PCB
  assembly.step            all of the above + PCB, cell, module, connector
  mass_report.txt

    python puck.py            # writes into ./out
"""
import os

from geometry import L, OUT, Z_PCB_TOP
from puck_parts import (make_cap, make_cell, make_connector, make_module, make_pcb, make_plate, make_sleeve,
                        make_standoffs)

RHO = {"al6061": 2.70, "pc": 1.20, "fr4": 1.85}
MM3_PER_CM3 = 1000.0
OTHER_PARTS_DZ = 0.5


def props(wp, rho):
    s = wp.val()
    c = s.Center()
    return s.Volume() / MM3_PER_CM3 * rho, (c.x, c.y, c.z)


def mass_items(solids):
    items = [props(solids["plate"], RHO["al6061"]), props(solids["standoffs"], L["standoffs"]["density"]),
             props(solids["pcb"], RHO["fr4"]), props(solids["sleeve"], L["sleeve"]["density"])]
    for key in ("cell", "module", "connector"):
        c = solids[key].val().Center()
        items.append((L[key]["mass_g"], (c.x, c.y, c.z)))
    items.append((L["other_components_g"], (0.0, 0.0, Z_PCB_TOP + OTHER_PARTS_DZ)))
    items.append(props(solids["cap"], RHO["pc"]))
    return items


def main():
    import puck_report
    os.makedirs(OUT, exist_ok=True)
    solids = {"plate": make_plate(), "standoffs": make_standoffs(), "pcb": make_pcb(), "sleeve": make_sleeve(),
              "cell": make_cell(), "module": make_module(), "connector": make_connector(), "cap": make_cap()}
    puck_report.write_report(mass_items(solids))
    puck_report.export(solids)


if __name__ == "__main__":
    main()
