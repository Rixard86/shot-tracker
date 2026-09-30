#!/usr/bin/env python3
"""gen_bom.py - grouped BOM (kicad/fab/shotpuck-bom.csv) from design.py"""
import csv
import os

import design

MPN = {
    "BMD-340-A-R": ("u-blox", "BMD-340-A-R-10"),
    "LSM6DSO32TR": ("STMicroelectronics", "LSM6DSO32TR"),
    "MCP73831T-2ACI/OT": ("Microchip", "MCP73831T-2ACI/OT"),
    "DMN63D8LDW": ("Diodes Inc", "DMN63D8LDW-7"),
    "BAT54J": ("Nexperia", "BAT54J,115"),
    "SMF5.0CA": ("Littelfuse", "SMF5.0CA (second sources: MDD / FUXINSEMI SMF5.0CA)"),
    "LED yellow-green 0402": ("XINGLIGHT", "XL-1005SYGC (AlGaInP 572 nm, Vf ~2 V)"),
    "4.7u 10V": ("Murata", "GRM188R61A475KE15D"),
    "10u 10V": ("Samsung", "CL10A106KP8NNNC"),
    "4.7u 6.3V": ("Murata", "GRM155R60J475ME47D"),
    "100n": ("Murata", "GRM155R71C104KA88D"),
    "47k 1%": ("UNI-ROYAL", "0402WGF4702TCE"),
    "1M": ("Yageo", "RC0402FR-071ML"),
    "100k": ("Yageo", "RC0402FR-07100KL"),
    "1k": ("Yageo", "RC0402FR-071KL"),
    "LIR1254 (plain, spring contacts)": ("generic", "LIR1254 3.6-3.7 V 45-65 mAh, no tabs - pick a maker with a datasheet"),
    "TYPE-C 6PFS 2JCB1.6-H6.7 IPX8": ("SHOU HAN", "waterproof USB-C 6P power-only (LCSC C3020041)"),
    "5.1k 1%": ("UNI-ROYAL", "0402WGF5101TCE"),
    "BQ29700DSER": ("Texas Instruments", "BQ29700DSER"),
    "DMN2004DWK": ("Diodes Inc", "DMN2004DWK-7"),
    "330R": ("Yageo", "RC0402FR-07330RL"),
    "2.2k": ("Yageo", "RC0402FR-072K2L"),
    "SWD pads": ("-", "4 bottom test pads (VDD, SWDIO, SWDCLK, GND) at 2.54 mm: program with a 4-pin pogo jig"),
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
