import json
import math
import os

HERE = os.path.dirname(os.path.abspath(__file__))
L = json.load(open(os.path.join(HERE, "..", "layout.json")))
P = L["puck"]
OUT = os.path.join(HERE, "out")

R = P["od"] / 2
WALL = P["cap_wall"]
T_PLATE = P["plate_thickness"]
Z_KAPTON_TOP = T_PLATE + P["insulator"]
Z_PCB = Z_KAPTON_TOP + P["pcb_standoff"]
Z_PCB_TOP = Z_PCB + P["pcb_thickness"]
CAV_H = L["cell"]["swell_h"] + L["cell"]["tab_h"]
Z_CELL = Z_KAPTON_TOP
Z_CEIL = Z_CELL + CAV_H
Z_TOP = Z_CEIL + P["cap_top"]
Z_SLEEVE_TOP = Z_TOP + L["sleeve"]["proud"]
Z_CAP = T_PLATE
Z_SEAL_TOP = T_PLATE + L["seal"]["cs"] * (1 - L["seal"]["squeeze"])
Z_BOSS = Z_PCB_TOP + L["bosses"]["boss_gap"]
BOSS_D = L["bosses"]["boss_dia"]
COUNTERBORE_D = L["bosses"]["head_dia"] + L["bosses"]["counterbore_clear"]
COUNTERBORE_DEPTH = Z_TOP - L["bosses"]["screw_len"] - L["bosses"]["tip_inset"]
BOSS_HEAD_WALL = 0.8
SLEEVE_BORE_CLEAR = 0.1


def boss_xy():
    return [tuple(p) for p in L["bosses"]["positions"]]


def module_pose():
    m = L["module"]
    return m["x"], m["y"], m["antenna_dir_deg"]


def connector_pose():
    k = L["connector"]
    return k["x"], k["y"], k["rot_deg"]


def notch_r():
    c = L["cell"]
    return c["dia"] / 2 + c["pcb_hole_clear"]


def cell_dir_deg():
    c = L["cell"]
    return math.degrees(math.atan2(c["y"], c["x"]))
