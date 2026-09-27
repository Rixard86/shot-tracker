#!/usr/bin/env python3
"""gen_bom.py - grouped BOM (kicad/fab/shotpuck-bom.csv) from design.py"""
import csv
import os

import design

MPN = {
    "MDBT50Q-1MV2": ("Raytac", "MDBT50Q-1MV2"),
    "LSM6DSO32TR": ("STMicroelectronics", "LSM6DSO32TR"),
    "MCP73831T-2ACI/OT": ("Microchip", "MCP73831T-2ACI/OT"),
    "DMN63D8LDW": ("Diodes Inc", "DMN63D8LDW-7"),
    "BAT54J": ("Nexperia", "BAT54J,115"),
    "PESD5V0S1BB": ("Nexperia", "PESD5V0S1BB,115"),
    "LED green 0402": ("Lite-On", "LTST-C193KGKT-5A"),
    "4.7u 10V": ("Murata", "GRM188R61A475KE15D"),
    "4.7u 6.3V": ("Murata", "GRM155R60J475ME47D"),
    "100n": ("Murata", "GRM155R71C104KA88D"),
    "20k 1%": ("Yageo", "RC0402FR-0720KL"),
    "1M": ("Yageo", "RC0402FR-071ML"),
    "100k": ("Yageo", "RC0402FR-07100KL"),
    "1k": ("Yageo", "RC0402FR-071KL"),
    "Varta CP1654 A3 (wire or tag version)": ("VARTA", "CP 1654 A3, wire or tag version - hand-solder, never reflow"),
    "Samzo PR5L4015-2P-C-F": ("Samzo", "PR5L4015-2P-C-F (Electrokit 41035992) - hand-solder"),
    "BQ29700DSER": ("Texas Instruments", "BQ29700DSER"),
    "DMN2004DWK": ("Diodes Inc", "DMN2004DWK-7"),
    "330R": ("Yageo", "RC0402FR-07330RL"),
    "2.2k": ("Yageo", "RC0402FR-072K2L"),
    "TC2030-NL": ("Tag-Connect", "pads only (cable TC2030-CTX-NL)"),
}

groups = {}
for (ref, lib, fp, val, pins, extra) in design.PARTS:
    if lib.startswith("Mechanical:"):
        continue
    key = (val, fp, bool(extra.get("dnp")))
    groups.setdefault(key, []).append(ref)

out = os.path.join(os.path.dirname(__file__), "kicad", "fab", "shotpuck-bom.csv")
with open(out, "w", newline="") as f:
    w = csv.writer(f)
    w.writerow(["Qty", "References", "Value", "Footprint", "Manufacturer", "MPN", "DNP"])
    for (val, fp, dnp), refs in sorted(groups.items(), key=lambda g: g[1][0]):
        man, mpn = MPN.get(val, ("", ""))
        w.writerow([0 if dnp else len(refs), " ".join(sorted(refs)), val, fp.split(":")[1],
                    man, mpn, "DNP" if dnp else ""])
print("wrote", out)
