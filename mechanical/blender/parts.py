import os
import re
import subprocess

import bpy

import cellconn
from shapes import MM, activate, attach, bevel, cone, cut, cylinder, empty, smooth, tag, thread, torus, torx
from stack import (COUNTERBORE_DEPTH, L, OUT, R_CAVITY, ROOT, T_PLATE, Z_CEIL, Z_KAPTON_TOP, Z_PCB, Z_PCB_TOP,
                   Z_SLEEVE_TOP, Z_TOP, boss_xy)

KICAD_CLI = os.environ.get("KICAD_CLI", r"C:\Program Files\KiCad\10.0\bin\kicad-cli.exe")
BOARD_PCB = os.path.join(ROOT, "hardware", "kicad", "shotpuck.kicad_pcb")
BOARD_GLB = os.path.join(OUT, "board.glb")
GLB_OPTIONS = ["--include-tracks", "--include-pads", "--include-zones", "--include-silkscreen",
               "--include-soldermask", "--no-dnp"]
EDGE_BLOCK = re.compile(r"\n\t\(gr_\w+\s*\n(.*?)\n\t\)", re.S)
EDGE_POINT = re.compile(r"\((?:start|end|mid) ([-\d.]+) ([-\d.]+)\)")
KAPTON_EDGE_CLEAR = 0.25
FOAM_DIA = 11.0
SCREW_DIA = 2.0
TORX_T6 = (1.75, 1.27)
CHEESE_EDGE = 0.2
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


def board_origin():
    text = open(BOARD_PCB, encoding="utf-8").read()
    blocks = [b for b in EDGE_BLOCK.findall(text) if '"Edge.Cuts"' in b]
    xs, ys = zip(*[(float(x), float(y)) for b in blocks for x, y in EDGE_POINT.findall(b)])
    return (min(xs) + max(xs)) / 2, min(ys) + L["pcb_dia"] / 2


def import_board():
    x, y = board_origin()
    origin = ["--user-origin", f"{x:.4f}x{y:.4f}mm"]
    subprocess.run([KICAD_CLI, "pcb", "export", "glb", "-f", "-o", BOARD_GLB] + GLB_OPTIONS + origin + [BOARD_PCB],
                   check=True, stdout=subprocess.DEVNULL)
    before = set(bpy.data.objects)
    bpy.ops.import_scene.gltf(filepath=BOARD_GLB)
    roots = [o for o in bpy.data.objects if o not in before and o.parent is None]
    board = empty("board", Z_PCB)
    for obj in roots:
        obj.location.z += Z_PCB * MM
        attach(obj, board)
    cellconn.build_contacts(board)
    return board


def ring(spec, name):
    outer = cylinder({"r": spec[0], "z0": spec[2], "z1": spec[3]}, name)
    inner = cylinder({"r": spec[1], "z0": spec[2] - CUTTER_MARGIN, "z1": spec[3] + CUTTER_MARGIN}, "cutter")
    return cut(outer, inner)


def squashed_ring(spec, name):
    ring_id, cs, height, z0 = spec
    obj = torus({"R": ring_id / 2 + cs / 2, "r": cs / 2, "z": z0 + height / 2}, name)
    obj.scale.z = height / cs
    return tag(smooth(obj), "rubber")


def build_cap():
    cap = import_stl("cap", "polycarbonate")
    s, b = L["seal"], L["bosses"]
    attach(squashed_ring((s["ledge_od"], s["cs"], s["cs"] * (1 - s["squeeze"]), T_PLATE), "seal"), cap)
    o = b["clamp_oring"]
    for x, y in boss_xy():
        clamp = squashed_ring((o["id"], o["cs"], b["boss_gap"], Z_PCB_TOP), "clamp_oring")
        clamp.location.x, clamp.location.y = x * MM, y * MM
        attach(clamp, cap)
    return cap


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


def threaded(spec, head):
    helix, core = thread(spec, head.name + "_thread")
    tag(smooth(core), "black_oxide")
    attach(tag(smooth(helix), "black_oxide"), head)
    return head


def build_screw(xy):
    x, y = xy
    b = L["bosses"]
    z_head = Z_TOP - COUNTERBORE_DEPTH
    head = cylinder({"r": b["head_dia"] / 2, "x": x, "y": y, "z0": z_head, "z1": z_head + b["head_h"]}, "screw")
    bevel(head, CHEESE_EDGE)
    activate(head)
    bpy.ops.object.modifier_apply(modifier="bevel")
    cut(head, torx((x, y, z_head + b["head_h"], TORX_T6, SOCKET_DEPTH), "cutter"))
    shank = {"r": SCREW_DIA / 2, "pitch": b["screw_pitch"], "x": x, "y": y, "z0": z_head - b["screw_len"], "z1": z_head}
    return tag(smooth(threaded(shank, head)), "black_oxide")


def build_screws():
    group = empty("screws", 0.0)
    for xy in boss_xy():
        attach(build_screw(xy), group)
    return group


def build_bolt():
    b = L["bolt"]
    z_head = Z_SLEEVE_TOP + b["washer_t"]
    r = b["head_dia"] / 2
    head = cone({"r0": r, "r1": r * BUTTON_TOP_RATIO, "x": 0.0, "y": 0.0, "z0": z_head, "z1": z_head + b["head_h"]},
                "bolt")
    bevel(head, BUTTON_ROUND)
    activate(head)
    bpy.ops.object.modifier_apply(modifier="bevel")
    cut(head, socket((0.0, 0.0, z_head + b["head_h"], BOLT_SOCKET_RADIUS, BOLT_SOCKET_DEPTH), "cutter"))
    threaded({"r": b["dia"] / 2, "pitch": b["pitch"], "z0": z_head - BOLT_LEN, "z1": z_head}, head)
    washer = ring((b["washer_od"] / 2, b["dia"] / 2, Z_SLEEVE_TOP, z_head), "sealing_washer")
    attach(tag(smooth(washer), "steel"), head)
    return tag(smooth(head), "black_oxide")


def build_all():
    return {
        "plate": import_stl("plate", "anodised"),
        "cap": build_cap(),
        "sleeve": import_stl("sleeve", "steel"),
        "spacers": build_spacers(),
        "board": import_board(),
        "cell": cellconn.build_cell(),
        "foam": build_foam(),
        "kapton": build_kapton(),
        "screws": build_screws(),
        "bolt": build_bolt(),
    }
