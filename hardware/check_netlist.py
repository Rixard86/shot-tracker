#!/usr/bin/env python3
"""check_netlist.py - verify KiCad's netlist of the generated schematic
matches design.py exactly (every pin on the intended net, NC pins alone)."""
import os
import subprocess
import sys

import design
import sexp

HERE = os.path.dirname(os.path.abspath(__file__))
SCH = os.path.join(HERE, "kicad", "shotpuck.kicad_sch")
NET = os.path.join(HERE, "kicad", "shotpuck.net")


def main():
    subprocess.check_call(["kicad-cli", "sch", "export", "netlist", "-o", NET, SCH],
                          stdout=subprocess.DEVNULL)
    tree = sexp.parse(open(NET).read())[0]
    nets = next(x for x in tree if isinstance(x, list) and x and x[0] == "nets")
    got = {}          # (ref, pin) -> net name as KiCad resolved it
    for n in nets[1:]:
        name = next(x[1] for x in n if isinstance(x, list) and x[0] == "name")
        for node in (x for x in n if isinstance(x, list) and x[0] == "node"):
            ref = next(x[1] for x in node if isinstance(x, list) and x[0] == "ref")
            pin = next(x[1] for x in node if isinstance(x, list) and x[0] == "pin")
            got[(str(ref), str(pin))] = str(name).lstrip("/")
    errors = 0
    checked = 0
    for (ref, _lib, _fp, _val, pinmap, _extra) in design.PARTS:
        for pin, net in pinmap.items():
            checked += 1
            g = got.get((ref, pin))
            if g != net:
                print(f"MISMATCH {ref}.{pin}: want {net!r}, KiCad has {g!r}")
                errors += 1
    # every symbol pin not in the design must be unconnected (alone)
    wanted = {(r, p) for (r, _l, _f, _v, pm, _e) in design.PARTS for p in pm}
    for (ref, pin), name in got.items():
        if (ref, pin) not in wanted and not ref.startswith("#") and \
                not name.startswith("unconnected-"):
            print(f"UNEXPECTED {ref}.{pin} on {name}")
            errors += 1
    nets_used = sorted({v for (r, p), v in got.items() if not v.startswith("unconnected-")})
    print(f"checked {checked} pin assignments, {len(nets_used)} nets: {', '.join(nets_used)}")
    print("NETLIST CHECK:", "PASS" if errors == 0 else f"FAIL ({errors})")
    sys.exit(1 if errors else 0)


if __name__ == "__main__":
    main()
