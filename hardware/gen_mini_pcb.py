import os
import subprocess
import sys
from functools import lru_cache

import pcbnew
from shapely.geometry import Point, Polygon, box
from shapely.ops import unary_union

import bmd340
import board_shape
import design_mini as design
import sexp
from gen_pcb import BOTTOM, EDGE_WIDTH_MM, GND_NET, KI, LIB, MM, V
from gen_pcb import circle, layer_set, line, load_fp, new_fp, pad, poly_zone, set_model, setup_rules
from gen_sch import U

BOARD_PATH = os.path.join(KI, design.PROJECT + ".kicad_pcb")
SCH_PATH = os.path.join(KI, design.PROJECT + ".kicad_sch")
NET_PATH = os.path.join(KI, design.PROJECT + ".net")
KICAD_CLI = os.path.join(os.path.dirname(sys.executable), "kicad-cli")
SHEET_PREFIX = "/"
MECHANICAL_LIB = "Mechanical:"
BOARD_MM = 33.0
HALF = BOARD_MM / 2
BOARD_CORNERS = [(-HALF, -HALF), (HALF, -HALF), (HALF, HALF), (-HALF, HALF)]

HOLDER_FP = "BatteryHolder_MYOUNG_MY-2450-03_CR2450"
HOLDER_DESCR = ("MYOUNG MY-2450-03 SMD CR2450 + clip (LCSC C2988622), hand-soldered on the back. Pads 1 = the "
                "clip's two + tabs (joined by the clip), pad 2 = - contact under the cell, no paste. "
                "Clip 25.5 x 15.4 x 5.75 mm.")
HOLDER_MODEL = "${KIPRJMOD}/ShotPuck.3dshapes/" + HOLDER_FP + ".step"
HOLDER_MODEL_PLACEMENT = ((0.0, 0.0, 5.7), 0.0)
CLIP_PAD_X = 13.65
CLIP_PAD_W = 2.6
CLIP_PAD_H = 7.6
NEG_PAD_DIA = 8.0
CLIP_BODY_HALF = (12.75, 7.7)
CLIP_CHAMFER = 2.5
CELL_R = 12.25
CELL_ARC_SEGMENTS = 16
COURTYARD_MARGIN = 0.25
FAB_LINE = 0.1
COURTYARD_LINE = 0.05
SILK_LINE = 0.12
PLUS_MARK_HALF = 0.6
PLUS_MARK_GAP = 1.0
CELL_EDGE_GAP = 0.25
CLIP_EDGE_GAP = 0.3
HOLDER_X = -(HALF - CLIP_EDGE_GAP - CLIP_PAD_X - CLIP_PAD_W / 2)
HOLDER_Y = -HALF + CELL_R + CELL_EDGE_GAP

MODULE_EDGE_GAP = 0.4
MODULE_TOP_GAP = 1.0
MODULE_X = HALF - MODULE_EDGE_GAP - bmd340.BODY_LENGTH_MM / 2
MODULE_Y = HALF - MODULE_TOP_GAP - bmd340.BODY_WIDTH_MM / 2
ANTENNA_POINTS_RIGHT_DEG = 180
ANTENNA_INNER_X = HALF - MODULE_EDGE_GAP - bmd340.ANTENNA_KEEPOUT_MM
ANTENNA_BOTTOM_Y = MODULE_Y - board_shape.ANTENNA_HALF_W

HOLE_INSET = 2.5
HOLE = HALF - HOLE_INSET
REF_TEXT_MM = 0.5
REF_STROKE_MM = 0.08
LABEL = "ShotPuck mini rev A"
LABEL_POS = (0.0, -10.0)
LABEL_MM = 1.0

PLACE = {
    "U1": (MODULE_X, MODULE_Y, ANTENNA_POINTS_RIGHT_DEG),
    "BT1": (HOLDER_X, HOLDER_Y, 0, BOTTOM),
    "U2": (-2.5, 11.0, 0), "C6": (-2.5, 13.2, 0), "C7": (-5.4, 11.0, 90),
    "C4": (-0.2, 14.3, 90), "C1": (-0.6, 7.6, 90), "JP1": (9.5, 3.6, 0),
    "R4": (-12.8, 9.0, 90), "D4": (-14.5, 9.0, 90), "JP2": (-9.0, 14.5, 0),
    "J2": (-9.5, 9.5, 180, BOTTOM),
    "H1": (-HOLE, HOLE, 0), "H2": (-HOLE, -HOLE, 0), "H3": (HOLE, -HOLE, 0),
}


def holder_body():
    w, h = CLIP_BODY_HALF
    c = CLIP_CHAMFER
    return Polygon([(c - w, h), (w - c, h), (w, h - c), (w, c - h), (w - c, -h), (c - w, -h), (-w, c - h), (-w, h - c)])


def holder_courtyard():
    pad_reach = CLIP_PAD_X + CLIP_PAD_W / 2
    pads = box(-pad_reach, -CLIP_PAD_H / 2, pad_reach, CLIP_PAD_H / 2)
    cell = Point(0.0, 0.0).buffer(CELL_R, quad_segs=CELL_ARC_SEGMENTS)
    outline = unary_union([holder_body(), pads, cell])
    return outline.buffer(COURTYARD_MARGIN, join_style="mitre")


def draw_polygon(fp, spec):
    layer, polygon, width = spec
    points = [(x, -y) for x, y in polygon.exterior.coords]
    for (x1, y1), (x2, y2) in zip(points, points[1:]):
        line(fp, layer, x1, y1, x2, y2, w=width)


def draw_plus_mark(fp):
    x, y = CLIP_PAD_X, -(CLIP_PAD_H / 2 + PLUS_MARK_GAP)
    line(fp, pcbnew.F_SilkS, x - PLUS_MARK_HALF, y, x + PLUS_MARK_HALF, y, w=SILK_LINE)
    line(fp, pcbnew.F_SilkS, x, y - PLUS_MARK_HALF, x, y + PLUS_MARK_HALF, w=SILK_LINE)


def holder_pads(fp):
    for x in (CLIP_PAD_X, -CLIP_PAD_X):
        pad(fp, "1", pcbnew.PAD_SHAPE_RECT, x, 0.0, CLIP_PAD_W, CLIP_PAD_H)
    contact = pad(fp, "2", pcbnew.PAD_SHAPE_CIRCLE, 0.0, 0.0, NEG_PAD_DIA, NEG_PAD_DIA)
    contact.SetLayerSet(layer_set([pcbnew.F_Cu, pcbnew.F_Mask]))
    fp.SetDuplicatePadNumbersAreJumpers(True)


def holder_footprint():
    fp = new_fp(HOLDER_FP, HOLDER_DESCR)
    holder_pads(fp)
    draw_polygon(fp, (pcbnew.F_Fab, holder_body(), FAB_LINE))
    circle(fp, pcbnew.F_Fab, CELL_R, w=FAB_LINE)
    draw_polygon(fp, (pcbnew.F_CrtYd, holder_courtyard(), COURTYARD_LINE))
    draw_plus_mark(fp)
    set_model(fp, (HOLDER_MODEL, *HOLDER_MODEL_PLACEMENT))
    pcbnew.FootprintSave(LIB, fp)


def add_outline(board):
    for a, b in zip(BOARD_CORNERS, BOARD_CORNERS[1:] + BOARD_CORNERS[:1]):
        edge = pcbnew.PCB_SHAPE(board)
        edge.SetShape(pcbnew.SHAPE_T_SEGMENT)
        edge.SetStart(V(*a))
        edge.SetEnd(V(*b))
        edge.SetLayer(pcbnew.Edge_Cuts)
        edge.SetWidth(MM(EDGE_WIDTH_MM))
        board.Add(edge)


@lru_cache(maxsize=None)
def schematic_nets():
    subprocess.check_call([KICAD_CLI, "sch", "export", "netlist", "-o", NET_PATH, SCH_PATH],
                          stdout=subprocess.DEVNULL)
    nets = sexp.find1(sexp.parse(open(NET_PATH).read())[0], "nets")
    pad_nets = {}
    for net in sexp.find(nets, "net"):
        name = str(sexp.find1(net, "name")[1])
        for node in sexp.find(net, "node"):
            pad_nets[(str(sexp.find1(node, "ref")[1]), str(sexp.find1(node, "pin")[1]))] = name
    return pad_nets


def add_nets(board):
    for name in sorted(set(schematic_nets().values())):
        board.Add(pcbnew.NETINFO_ITEM(board, name))


def match_bom_flag(fp, lib_id):
    attributes = fp.GetAttributes() & ~pcbnew.FP_EXCLUDE_FROM_BOM
    if lib_id.startswith(MECHANICAL_LIB):
        attributes |= pcbnew.FP_EXCLUDE_FROM_BOM
    fp.SetAttributes(attributes)


def style_reference(fp):
    fp.Value().SetVisible(False)
    fp.Reference().SetLayer(pcbnew.F_Fab)
    fp.Reference().SetTextSize(pcbnew.VECTOR2I(MM(REF_TEXT_MM), MM(REF_TEXT_MM)))
    fp.Reference().SetTextThickness(MM(REF_STROKE_MM))


def place_part(board, part):
    ref, lib_id, fpid, value, _pins, _extra = part
    x, y, rotation, *side = PLACE[ref]
    fp = load_fp(fpid)
    fp.SetReference(ref)
    fp.SetValue(value)
    fp.SetPosition(V(x, y))
    fp.SetOrientationDegrees(rotation)
    fp.SetPath(pcbnew.KIID_PATH("/" + U(f"sym-{ref}-1")))
    style_reference(fp)
    match_bom_flag(fp, lib_id)
    for p in fp.Pads():
        net = schematic_nets().get((ref, p.GetNumber()))
        if net:
            p.SetNet(board.FindNet(net))
    board.Add(fp)
    if side == [BOTTOM]:
        fp.Flip(fp.GetPosition(), pcbnew.FLIP_DIRECTION_LEFT_RIGHT)


def add_antenna_keepout(board):
    corners = [(ANTENNA_INNER_X, ANTENNA_BOTTOM_Y), (HALF, ANTENNA_BOTTOM_Y), (HALF, HALF), (ANTENNA_INNER_X, HALF)]
    copper = layer_set([pcbnew.F_Cu, pcbnew.B_Cu])
    poly_zone(board, copper, corners, rule="tracks vias pour", name="rf_keepout")


def add_ground_pours(board):
    for layer in (pcbnew.F_Cu, pcbnew.B_Cu):
        poly_zone(board, layer_set([layer]), BOARD_CORNERS, net=board.FindNet(SHEET_PREFIX + GND_NET))


def add_label(board):
    text = pcbnew.PCB_TEXT(board)
    text.SetText(LABEL)
    text.SetPosition(V(*LABEL_POS))
    text.SetLayer(pcbnew.B_SilkS)
    text.SetMirrored(True)
    text.SetTextSize(pcbnew.VECTOR2I(MM(LABEL_MM), MM(LABEL_MM)))
    board.Add(text)


def build():
    board = pcbnew.BOARD()
    setup_rules(board)
    add_nets(board)
    add_outline(board)
    for part in design.PARTS:
        place_part(board, part)
    add_antenna_keepout(board)
    add_ground_pours(board)
    add_label(board)
    pcbnew.ZONE_FILLER(board).Fill(board.Zones())
    return board


def main():
    if os.path.exists(BOARD_PATH):
        sys.exit(f"{BOARD_PATH} exists and is placed and routed by hand: not overwritten. Delete it to start over.")
    holder_footprint()
    pcbnew.SaveBoard(BOARD_PATH, build())
    print("wrote", BOARD_PATH)


if __name__ == "__main__":
    main()
