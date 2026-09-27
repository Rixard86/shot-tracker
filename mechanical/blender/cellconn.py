import math

import bpy

from shapes import MM, attach, bevel, box, cylinder, smooth, tag
from stack import L, Z_CELL, Z_PCB_TOP

C = L["cell"]
K = L["connector"]
Z_CAN = Z_CELL + C["tab_h"]
Z_CAN_TOP = Z_CAN + C["h"]
STRIP_T = 0.15
STRIP_W = 2.0
FINGER_H = 2.0
STRAP_GAP = 0.35
PAD_HALF = 1.0
CELL_CAP_RATIO = 0.78
GASKET_RATIO = 0.86
CELL_CAP_RISE = 0.05
GASKET_RISE = 0.025
CELL_BEVEL = 0.2
BODY_BEVEL = 0.3
CONTACT_DIA = 1.0
CONTACT_RISE = 0.3
MAGNET_DIA = 2.5
MAGNET_RISE = 0.02


def radial(corners, key):
    c = C[key]
    angle = math.atan2(c["y"] - C["y"], c["x"] - C["x"])
    strip = box(corners, "cell_contact")
    bpy.ops.object.transform_apply(location=True, rotation=False, scale=False)
    strip.rotation_euler.z = angle
    strip.location = (C["x"] * MM, C["y"] * MM, 0.0)
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=False)
    return tag(strip, "nickel")


def pad_distance(key):
    return math.hypot(C[key]["x"] - C["x"], C[key]["y"] - C["y"])


def plus_contact():
    r, pad = C["dia"] / 2, pad_distance("pos_pad")
    half = STRIP_W / 2
    return [radial(((pad - PAD_HALF, -half, Z_PCB_TOP), (pad + PAD_HALF, half, Z_PCB_TOP + STRIP_T)), "pos_pad"),
            radial(((r, -half, Z_PCB_TOP), (pad - PAD_HALF, half, Z_PCB_TOP + STRIP_T)), "pos_pad"),
            radial(((r, -half, Z_PCB_TOP - FINGER_H), (r + STRIP_T, half, Z_PCB_TOP + STRIP_T)), "pos_pad")]


def minus_strap():
    r, pad = C["dia"] / 2 + STRAP_GAP, pad_distance("neg_pad")
    half = STRIP_W / 2
    top = Z_CAN_TOP + CELL_CAP_RISE
    return [radial(((r, -half, Z_PCB_TOP), (pad + PAD_HALF, half, Z_PCB_TOP + STRIP_T)), "neg_pad"),
            radial(((r, -half, Z_PCB_TOP), (r + STRIP_T, half, top + STRIP_T)), "neg_pad"),
            radial(((0.0, -half, top), (r + STRIP_T, half, top + STRIP_T)), "neg_pad")]


def cell_disc(ratio, zs):
    return {"r": C["dia"] / 2 * ratio, "x": C["x"], "y": C["y"], "z0": zs[0], "z1": zs[1]}


def build_cell():
    can = cylinder(cell_disc(1.0, (Z_CAN, Z_CAN_TOP)), "cell")
    tag(smooth(bevel(can, CELL_BEVEL)), "steel")
    gasket = cylinder(cell_disc(GASKET_RATIO, (Z_CAN_TOP - CELL_CAP_RISE, Z_CAN_TOP + GASKET_RISE)), "cell_gasket")
    attach(tag(smooth(gasket), "black_plastic"), can)
    cap = cylinder(cell_disc(CELL_CAP_RATIO, (Z_CAN_TOP, Z_CAN_TOP + CELL_CAP_RISE)), "cell_cap")
    attach(tag(smooth(cap), "steel"), can)
    return can


def build_contacts(board):
    for strip in plus_contact() + minus_strap():
        attach(strip, board)


def top_disc(spec, name):
    top = Z_PCB_TOP + K["body_h"]
    return cylinder({"r": spec[0] / 2, "x": spec[1], "y": 0.0, "z0": top - spec[2], "z1": top + spec[2]}, name)


def build_connector():
    corner0 = (-K["body_w"] / 2, -K["body_l"] / 2, Z_PCB_TOP)
    corner1 = (K["body_w"] / 2, K["body_l"] / 2, Z_PCB_TOP + K["body_h"])
    body = tag(bevel(box((corner0, corner1), "connector"), BODY_BEVEL), "black_plastic")
    for side in (-1, 1):
        contact = top_disc((CONTACT_DIA, side * K["pitch"] / 2, CONTACT_RISE), "contact")
        attach(tag(smooth(contact), "gold"), body)
        magnet = top_disc((MAGNET_DIA, side * K["magnet_pitch"] / 2, MAGNET_RISE), "magnet")
        attach(tag(smooth(magnet), "nickel"), body)
    body.rotation_euler.z = math.radians(K["rot_deg"])
    body.location.x, body.location.y = K["x"] * MM, K["y"] * MM
    return body
