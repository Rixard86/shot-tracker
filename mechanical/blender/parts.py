import os
import subprocess

import bpy

import cellconn
from shapes import MM, activate, attach, bevel, cone, cut, cylinder, empty, smooth, tag
from stack import (COUNTERBORE_DEPTH, L, OUT, P, R_CAVITY, ROOT, T_PLATE, Z_CAP, Z_CEIL, Z_KAPTON_TOP, Z_PCB,
                   Z_TOP, boss_xy)

KICAD_CLI = os.environ.get("KICAD_CLI", r"C:\Program Files\KiCad\10.0\bin\kicad-cli.exe")
BOARD_PCB = os.path.join(ROOT, "hardware", "kicad", "shotpuck.kicad_pcb")
BOARD_GLB = os.path.join(OUT, "board.glb")
GLB_OPTIONS = ["--include-tracks", "--include-pads", "--include-zones", "--include-silkscreen",
               "--include-soldermask", "--no-dnp"]
KAPTON_EDGE_CLEAR = 0.25
FOAM_DIA = 11.0
SCREW_DIA = 2.0
SOCKET_RADIUS = 0.8
SOCKET_DEPTH = 0.6
HEX_SIDES = 6
BOLT_LEN = 25.0
BOLT_SOCKET_RADIUS = 2.75
BOLT_SOCKET_DEPTH = 2.5
BUTTON_TOP_RATIO = 0.75
BUTTON_ROUND = 1.2
CUTTER_MARGIN = 0.5


def import_stl(name, look):
    bpy.ops.import_mesh.stl(filepath=os.path.join(OUT, name + ".stl"), global_scale=MM)
    obj = bpy.context.selected_objects[0]
    obj.name = name
    return tag(smooth(obj), look)


def import_board():
    subprocess.run([KICAD_CLI, "pcb", "export", "glb", "-f", "-o", BOARD_GLB] + GLB_OPTIONS + [BOARD_PCB],
                   check=True, stdout=subprocess.DEVNULL)
    before = set(bpy.data.objects)
    bpy.ops.import_scene.gltf(filepath=BOARD_GLB)
    roots = [o for o in bpy.data.objects if o not in before and o.parent is None]
    board = empty("board", Z_PCB)
    for obj in roots:
        obj.location.z += Z_PCB * MM
        attach(obj, board)
    attach(cellconn.build_connector(), board)
    cellconn.build_contacts(board)
    return board


def ring(spec, name):
    outer = cylinder({"r": spec[0], "z0": spec[2], "z1": spec[3]}, name)
    inner = cylinder({"r": spec[1], "z0": spec[2] - CUTTER_MARGIN, "z1": spec[3] + CUTTER_MARGIN}, "cutter")
    return cut(outer, inner)


def build_gasket():
    return tag(smooth(ring((P["od"] / 2, R_CAVITY, T_PLATE, Z_CAP), "gasket")), "rubber")


def build_kapton():
    s = L["sleeve"]
    disc = ring((R_CAVITY - KAPTON_EDGE_CLEAR, s["od"] / 2 + s["pcb_clear"], T_PLATE, Z_KAPTON_TOP), "kapton")
    for x, y in boss_xy():
        hole = cylinder({"r": L["bosses"]["pcb_hole"] / 2, "x": x, "y": y,
                         "z0": T_PLATE - CUTTER_MARGIN, "z1": Z_KAPTON_TOP + CUTTER_MARGIN}, "cutter")
        cut(disc, hole)
    return tag(disc, "kapton")


def build_spacers():
    group = empty("spacers", 0.0)
    attach(import_stl("standoffs", "steel"), group)
    return group


def build_foam():
    c = L["cell"]
    z0 = cellconn.Z_CAN_TOP + cellconn.CELL_CAP_RISE + cellconn.STRIP_T
    spec = {"r": FOAM_DIA / 2, "x": c["x"], "y": c["y"], "z0": z0, "z1": Z_CEIL}
    return tag(smooth(cylinder(spec, "foam")), "foam")


def socket(spec, name):
    x, y, z_top, r, depth = spec
    return cylinder({"r": r, "x": x, "y": y, "z0": z_top - depth, "z1": z_top + CUTTER_MARGIN, "seg": HEX_SIDES},
                    name)


def build_screw(xy):
    x, y = xy
    b = L["bosses"]
    z_head = Z_TOP - COUNTERBORE_DEPTH
    head = cylinder({"r": b["head_dia"] / 2, "x": x, "y": y, "z0": z_head, "z1": z_head + b["head_h"]}, "screw")
    cut(head, socket((x, y, z_head + b["head_h"], SOCKET_RADIUS, SOCKET_DEPTH), "cutter"))
    shank = cylinder({"r": SCREW_DIA / 2, "x": x, "y": y, "z0": z_head - b["screw_len"], "z1": z_head},
                     "screw_shank")
    attach(tag(smooth(shank), "black_oxide"), head)
    return tag(smooth(head), "black_oxide")


def build_screws():
    group = empty("screws", 0.0)
    for xy in boss_xy():
        attach(build_screw(xy), group)
    return group


def build_bolt():
    b = L["bolt"]
    z_head = Z_TOP + b["washer_t"]
    r = b["head_dia"] / 2
    head = cone({"r0": r, "r1": r * BUTTON_TOP_RATIO, "x": 0.0, "y": 0.0, "z0": z_head, "z1": z_head + b["head_h"]},
                "bolt")
    bevel(head, BUTTON_ROUND)
    activate(head)
    bpy.ops.object.modifier_apply(modifier="bevel")
    cut(head, socket((0.0, 0.0, z_head + b["head_h"], BOLT_SOCKET_RADIUS, BOLT_SOCKET_DEPTH), "cutter"))
    shank = cylinder({"r": b["dia"] / 2, "z0": z_head - BOLT_LEN, "z1": z_head}, "bolt_shank")
    attach(tag(smooth(shank), "black_oxide"), head)
    washer = ring((b["washer_od"] / 2, b["dia"] / 2, Z_TOP, z_head), "sealing_washer")
    attach(tag(smooth(washer), "steel"), head)
    return tag(smooth(head), "black_oxide")


def build_all():
    return {
        "plate": import_stl("plate", "anodised"),
        "cap": import_stl("cap", "polycarbonate"),
        "sleeve": import_stl("sleeve", "steel"),
        "spacers": build_spacers(),
        "board": import_board(),
        "cell": cellconn.build_cell(),
        "foam": build_foam(),
        "gasket": build_gasket(),
        "kapton": build_kapton(),
        "screws": build_screws(),
        "bolt": build_bolt(),
    }
