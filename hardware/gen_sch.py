#!/usr/bin/env python3
"""
gen_sch.py - build kicad/shotpuck.kicad_sch from design.py.

Every pin is connected with a net label at its endpoint (no wires), which
keeps the generated sheet readable and diff-able. Open it in KiCad 7+ and
edit normally; if you change the circuit in design.py instead, re-run this
and then "Update PCB from Schematic".
"""
import math
import os
import sys
import uuid

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import bmd340  # noqa: E402
import design  # noqa: E402
import sexp  # noqa: E402
from sexp import Str  # noqa: E402

SYMDIR = os.environ.get("KICAD10_SYMBOL_DIR", r"C:\Program Files\KiCad\10.0\share\kicad\symbols")
OUTDIR = os.path.join(HERE, "kicad")
PROJECT = "shotpuck"
PROJECT_SYMBOL_LIB = "ShotPuck"
ROOT_UUID = str(uuid.uuid5(uuid.NAMESPACE_URL, "shotpuck-root"))

_libs = {}


def U(seed=None):
    return str(uuid.uuid5(uuid.NAMESPACE_URL, seed)) if seed else str(uuid.uuid4())


def load_lib(name):
    if name not in _libs:
        folder = OUTDIR if name == PROJECT_SYMBOL_LIB else SYMDIR
        _libs[name] = sexp.parse(open(os.path.join(folder, name + ".kicad_sym")).read())[0]
    return _libs[name]


def lib_symbol(lib_id):
    """Flattened symbol definition (resolves 'extends') named 'Lib:Name'."""
    libn, name = lib_id.split(":")
    lib = load_lib(libn)
    syms = {s[1]: s for s in sexp.find(lib, "symbol")}
    child = syms[name]
    ext = sexp.find1(child, "extends")
    base = syms[ext[1]] if ext else child
    base_name = base[1]
    out = [ "symbol", Str(lib_id)]
    child_props = {p[1]: p for p in sexp.find(child, "property")}
    for item in base[2:]:
        if isinstance(item, list) and item and item[0] == "property" and item[1] in child_props:
            out.append(child_props.pop(item[1]))
        elif isinstance(item, list) and item and item[0] == "symbol":
            sub = list(item)
            sub[1] = Str(name + sub[1][len(base_name):])
            out.append(sub)
        elif isinstance(item, list) and item and item[0] == "extends":
            continue
        else:
            out.append(item)
    for p in child_props.values():
        out.insert(2, p)
    return out


def pins_of(symdef):
    """{unit: [(number, x, y, angle, name)]} ; unit 0 = common."""
    res = {}
    base = symdef[1].split(":")[1]
    for sub in sexp.find(symdef, "symbol"):
        suffix = sub[1][len(base) + 1:]
        unit = int(suffix.split("_")[0])
        for p in sexp.find(sub, "pin"):
            at = sexp.find1(p, "at")
            res.setdefault(unit, []).append(
                (sexp.find1(p, "number")[1], float(at[1]), float(at[2]), int(float(at[3])),
                 sexp.find1(p, "name")[1]))
    return res


def unit_pins(allpins, unit):
    return allpins.get(0, []) + allpins.get(unit, [])


def snap(v, g=2.54):
    return round(v / g) * g


def prop(name, value, x, y, hide=False, size=1.27):
    eff = ["effects", ["font", ["size", size, size]]]
    if hide:
        eff.append("hide")
    return ["property", Str(name), Str(value), ["at", round(x, 2), round(y, 2), 0], eff]


def main():
    os.makedirs(OUTDIR, exist_ok=True)
    bmd340.save_symbol(OUTDIR)
    libsyms = {}
    items = []
    placements = []  # (ref, lib_id, unit, x, y, pins, value, fp, extra)

    # ------------------------------------------------ placement (flow)
    # (title, refs, region x0, y0, x_max) - A3 frame is ~410 x 287 mm
    groups = [
        ("MCU / radio (nRF52840 high-voltage mode)", ["U1", "C4", "C5", "R9", "C9"], 22.86, 35.56, 190.5),
        ("Programming & status LED", ["J2", "R4", "D4"], 22.86, 170.18, 190.5),
        ("Charging input & charger", ["J1", "D3", "D1", "C1", "U3", "R1"],
         203.2, 35.56, 405.0),
        ("Charge inhibit / status", ["R2", "Q1", "R3", "D2"], 203.2, 81.28, 405.0),
        ("Battery + protection (PCM)", ["BT1", "JP1", "C2", "C3", "U4", "Q2", "R7", "R8", "C8"],
         203.2, 124.46, 405.0),
        ("IMU (accelerometer + gyroscope)", ["U2", "C6", "C7"], 203.2, 160.02, 405.0),
        ("Mechanical", ["H1", "H2", "H3", "H4"], 203.2, 203.2, 405.0),
    ]
    by_ref = {p[0]: p for p in design.PARTS}
    for title, refs, x0, y0, page_w in groups:
        x, y, row_h = x0, y0, 0.0
        items.append(["text", Str(title), ["at", x, y - 5.08, 0],
                      ["effects", ["font", ["size", 2.0, 2.0], "bold"], ["justify", "left", "bottom"]],
                      ["uuid", U()]])
        for ref in refs:
            _, lib_id, fp, value, pinmap, extra = by_ref[ref]
            if lib_id not in libsyms:
                libsyms[lib_id] = lib_symbol(lib_id)
            allp = pins_of(libsyms[lib_id])
            units = extra.get("units", 1)
            for unit in range(1, units + 1):
                up = unit_pins(allp, unit)
                xs = [p[1] for p in up] or [0]
                ys = [-p[2] for p in up] or [0]
                lab = max([len(pinmap.get(p[0], "")) for p in up] + [3]) * 1.0 + 4
                w = (max(xs) - min(xs)) + 2 * lab + 8
                h = (max(ys) - min(ys)) + 15
                if x + w > page_w:
                    x, y, row_h = x0, y + row_h + 2.54, 0.0
                ox = snap(x + lab - min(xs) + 2.54)
                oy = snap(y + 7.62 - min(ys))
                placements.append((ref, lib_id, unit, ox, oy, up, value, fp, extra, pinmap,
                                   (min(xs), max(xs), min(ys), max(ys))))
                x += w
                row_h = max(row_h, h)
    x0, y, row_h = 203.2, 215.9, 0.0

    # ----------------------------------------------------- instances
    used_points = set()
    for (ref, lib_id, unit, ox, oy, up, value, fp, extra, pinmap, bb) in placements:
        is_mh = lib_id.startswith("Mechanical:")
        dnp = extra.get("dnp", False)
        sym = ["symbol", ["lib_id", Str(lib_id)], ["at", ox, oy, 0], ["unit", unit],
               ["in_bom", "no" if is_mh else "yes"], ["on_board", "yes"],
               ["dnp", "yes" if dnp else "no"], ["uuid", U(f"sym-{ref}-{unit}")],
               prop("Reference", ref, ox + bb[0], oy + bb[2] - 2.54),
               prop("Value", value, ox + bb[0], oy + bb[3] + 3.81),
               prop("Footprint", fp, ox, oy, hide=True),
               prop("Datasheet", "", ox, oy, hide=True)]
        for (num, *_r) in up:
            sym.append(["pin", Str(num), ["uuid", U(f"pin-{ref}-{unit}-{num}")]])
        sym.append(["instances", ["project", Str(PROJECT),
                    ["path", Str("/" + ROOT_UUID), ["reference", Str(ref)], ["unit", unit]]]])
        items.append(sym)

        # labels / no-connects at pin endpoints
        for (num, px, py, ang, pname) in up:
            ax, ay = round(ox + px, 2), round(oy - py, 2)
            key = (ax, ay)
            net = pinmap.get(num)
            if key in used_points:
                continue  # stacked pins (e.g. several GND on one point)
            used_points.add(key)
            if net:
                la = (ang + 180) % 360
                just = {0: ["justify", "left", "bottom"], 180: ["justify", "right", "bottom"],
                        90: ["justify", "left", "bottom"], 270: ["justify", "right", "bottom"]}[la]
                items.append(["label", Str(net), ["at", ax, ay, la],
                              ["fields_autoplaced"],
                              ["effects", ["font", ["size", 1.27, 1.27]], just],
                              ["uuid", U(f"lab-{ref}-{unit}-{num}")]])
            else:
                items.append(["no_connect", ["at", ax, ay], ["uuid", U(f"nc-{ref}-{unit}-{num}")]])

    # ----------------------------------------------------- power flags
    libsyms["power:PWR_FLAG"] = lib_symbol("power:PWR_FLAG")
    fx = 22.86
    fy = 215.9
    items.append(["text", Str("Power flags (ERC)"), ["at", fx, fy - 7.62, 0],
                  ["effects", ["font", ["size", 2.0, 2.0], "bold"], ["justify", "left", "bottom"]],
                  ["uuid", U()]])
    for i, net in enumerate(design.POWER_FLAG_NETS, 1):
        px, py = snap(fx + (i - 1) * 25.4), snap(fy)
        items.append(["symbol", ["lib_id", Str("power:PWR_FLAG")], ["at", px, py, 0], ["unit", 1],
                      ["in_bom", "yes"], ["on_board", "yes"], ["dnp", "no"],
                      ["uuid", U(f"flg-{net}")],
                      prop("Reference", f"#FLG0{i}", px, py - 3.81, hide=True),
                      prop("Value", "PWR_FLAG", px, py - 3.81),
                      prop("Footprint", "", px, py, hide=True),
                      prop("Datasheet", "", px, py, hide=True),
                      ["pin", Str("1"), ["uuid", U(f"flgpin-{net}")]],
                      ["instances", ["project", Str(PROJECT),
                                     ["path", Str("/" + ROOT_UUID), ["reference", Str(f"#FLG0{i}")],
                                      ["unit", 1]]]]])
        items.append(["label", Str(net), ["at", px, py, 270], ["fields_autoplaced"],
                      ["effects", ["font", ["size", 1.27, 1.27]], ["justify", "right", "bottom"]],
                      ["uuid", U(f"flglab-{net}")]])

    # ----------------------------------------------------- notes
    note = design.__doc__.strip().replace('"', "'")
    items.append(["text", Str(note), ["at", 22.86, 228.6, 0],
                  ["effects", ["font", ["size", 1.27, 1.27]], ["justify", "left", "top"]],
                  ["uuid", U()]])

    sch = ["kicad_sch", ["version", 20230121], ["generator", "eeschema"],
           ["uuid", ROOT_UUID], ["paper", Str("A3")],
           ["title_block", ["title", Str("ShotPuck - arrow shot counter")],
            ["rev", Str("A")], ["company", Str("")],
            ["comment", 1, Str("Generated by hardware/gen_sch.py from design.py")]],
           ["lib_symbols"] + list(libsyms.values())]
    sch += items
    sch.append(["sheet_instances", ["path", Str("/"), ["page", Str("1")]]])
    path = os.path.join(OUTDIR, PROJECT + ".kicad_sch")
    open(path, "w").write(sexp.dump(sch) + "\n")
    print("wrote", path, f"({len(placements)} symbol units)")


if __name__ == "__main__":
    main()
