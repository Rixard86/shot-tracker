#!/usr/bin/env python3
"""
plate_drawing.py - manufacturing drawing of the base plate (A4, 2:1) from ../layout.json.

    python plate_drawing.py   # writes out/plate_drawing.pdf (6061-T6) and the 5052 sheet-metal
                              # prototype variant out/plate_drawing_5052.pdf + out/plate_5052.step
"""
import datetime
import math
import os

import cadquery as cq
import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import Arc, Circle, Polygon, Rectangle

from geometry import L, OUT, R, T_PLATE, boss_xy
from puck_parts import EDGE_CHAMFER, make_plate

VARIANTS = {
    "plate_drawing.pdf": {"title": "ShotPuck base plate", "material": "6061-T6",
                          "process": "Laser or waterjet cut.", "chamfer": EDGE_CHAMFER, "step": None},
    "plate_drawing_5052.pdf": {"title": "ShotPuck base plate (prototype, sheet metal)", "material": "5052-H32",
                               "process": "Laser cut, flat sheet, no chamfer.", "chamfer": 0.0,
                               "step": "plate_5052.step"},
}

MM_PER_INCH = 25.4
PAGE_W, PAGE_H = 297.0, 210.0
PAGE_MARGIN = 8.0
SCALE = 2.0
TOP_VIEW_CENTRE = (80.0, 118.0)
SIDE_VIEW_CENTRE = (80.0, 40.0)
CENTRE_LINE_OVERHANG = 4.0
HOLE_MARK_HALF = 3.0
THREAD_ARC_DEG = 270.0
M2_MAJOR_DIA = 2.0
LABEL_OFFSET = (2.5, 2.5)
LEADER_DEG = 45.0
LEADER_TEXT_OFFSET = 14.0
VIEW_LABEL_GAP = 6.0
DIM_OFFSET = 6.0
TABLE_ORIGIN = (170.0, 196.0)
TABLE_COLUMNS = (0.0, 14.0, 34.0, 54.0)
ROW_STEP = 5.5
NOTES_ORIGIN = (170.0, 150.0)
NOTE_LINE = 4.2
NOTE_GAP = 1.6
TITLE_BOX = (170.0, 12.0, 119.0, 30.0)
TITLE_ROW_DROPS = (3.0, 13.0, 22.0)
TITLE_PAD = 3.0
THIN, THICK = 0.3, 0.8
SMALL, NORMAL, BIG = 7, 8, 12
HIDDEN = (0, (3, 2))
CENTRE = (0, (8, 2, 1, 2))


def paper(point, origin):
    return origin[0] + point[0] * SCALE, origin[1] + point[1] * SCALE


def holes():
    return [(f"H{i + 1}", xy) for i, xy in enumerate(boss_xy())]


def thread_callout():
    return f"M2×{L['bosses']['screw_pitch']:g} tapped through (tap drill Ø{L['bosses']['tap_drill']:g})"


def centre_mark(ax, spec):
    (x, y), half = spec
    ax.plot([x - half, x + half], [y, y], lw=THIN, ls=CENTRE, color="k")
    ax.plot([x, x], [y - half, y + half], lw=THIN, ls=CENTRE, color="k")


def tapped_hole(ax, centre):
    ax.add_patch(Circle(centre, L["bosses"]["tap_drill"] / 2 * SCALE, fill=False, lw=THICK))
    ax.add_patch(Arc(centre, M2_MAJOR_DIA * SCALE, M2_MAJOR_DIA * SCALE, theta1=0, theta2=THREAD_ARC_DEG, lw=THIN))
    centre_mark(ax, (centre, HOLE_MARK_HALF))


def diameter_label(ax, spec):
    text, radius = spec
    o = TOP_VIEW_CENTRE
    angle = math.radians(LEADER_DEG)
    tip = paper((radius * math.cos(angle), radius * math.sin(angle)), o)
    at = (tip[0] + LEADER_TEXT_OFFSET, tip[1] + LEADER_TEXT_OFFSET)
    ax.annotate(text, xy=tip, xytext=at, fontsize=NORMAL, arrowprops={"arrowstyle": "->", "lw": THIN})


def top_view(ax, variant):
    o = TOP_VIEW_CENTRE
    ax.add_patch(Circle(o, R * SCALE, fill=False, lw=THICK))
    if variant["chamfer"]:
        ax.add_patch(Circle(o, (R - variant["chamfer"]) * SCALE, fill=False, lw=THIN, ls=HIDDEN))
    bolt_r = L["bolt"]["clearance_dia"] / 2
    ax.add_patch(Circle(o, bolt_r * SCALE, fill=False, lw=THICK))
    centre_mark(ax, (o, (R + CENTRE_LINE_OVERHANG) * SCALE))
    for name, xy in holes():
        centre = paper(xy, o)
        tapped_hole(ax, centre)
        ax.text(centre[0] + LABEL_OFFSET[0], centre[1] + LABEL_OFFSET[1], name, fontsize=NORMAL, weight="bold")
    diameter_label(ax, (f"Ø{2 * R:g}", R))
    diameter_label(ax, (f"Ø{L['bolt']['clearance_dia']:g} through", bolt_r))
    ax.text(o[0], o[1] - (R * SCALE + VIEW_LABEL_GAP), "TOP VIEW (cap side, +x right, +y up)   2:1",
            fontsize=NORMAL, ha="center", va="top")


def side_view(ax, variant):
    x0, y0 = SIDE_VIEW_CENTRE
    half, t, c = R * SCALE, T_PLATE * SCALE, variant["chamfer"] * SCALE
    outline = [(x0 - half + c, y0), (x0 + half - c, y0), (x0 + half, y0 + c), (x0 + half, y0 + t),
               (x0 - half, y0 + t), (x0 - half, y0 + c)]
    ax.add_patch(Polygon(outline, closed=True, fill=False, lw=THICK))
    edges = [(0.0, L["bolt"]["clearance_dia"] / 2)] + [(xy[0], L["bosses"]["tap_drill"] / 2) for xy in boss_xy()]
    for x, r in edges:
        for side in (x - r, x + r):
            xs = x0 + side * SCALE
            ax.plot([xs, xs], [y0, y0 + t], lw=THIN, ls=HIDDEN, color="k")
    xd = x0 + half + DIM_OFFSET
    ax.annotate("", xy=(xd, y0), xytext=(xd, y0 + t), arrowprops={"arrowstyle": "<->", "lw": THIN})
    ax.text(xd + LABEL_OFFSET[0], y0 + t / 2, f"{T_PLATE:.1f}", fontsize=NORMAL, va="center")
    label = "SIDE VIEW (underside down)   2:1"
    if variant["chamfer"]:
        label += f"   chamfer {variant['chamfer']:g} × 45° on the underside edge"
    ax.text(x0, y0 - VIEW_LABEL_GAP, label, fontsize=NORMAL, ha="center", va="top")


def table_rows():
    rows = [("Hole", "X", "Y", "Feature")]
    rows += [(name, f"{x:.3f}", f"{y:.3f}", thread_callout()) for name, (x, y) in holes()]
    rows.append(("C", "0.000", "0.000", f"Ø{L['bolt']['clearance_dia']:g} through (bolt clearance)"))
    return rows


def hole_table(ax):
    x0, y0 = TABLE_ORIGIN
    ax.text(x0, y0, "HOLE TABLE (mm, from the plate centre, top view)", fontsize=NORMAL, weight="bold")
    for i, row in enumerate(table_rows()):
        y = y0 - (i + 1) * ROW_STEP
        for col, text in zip(TABLE_COLUMNS, row):
            ax.text(x0 + col, y, text, fontsize=SMALL, weight="bold" if i == 0 else "normal")


def notes(variant):
    chamfer = variant["chamfer"]
    items = [
        f"Material: aluminium {variant['material']}, {T_PLATE:.1f} mm sheet. {variant['process']}",
        f"4× {thread_callout()}\n    at H1–H4. The threads are required: do not leave plain holes.",
        f"Centre hole Ø{L['bolt']['clearance_dia']:g} through, for a {L['bolt']['thread']} bolt.",
        f"{chamfer:g} × 45° chamfer on the outer edge of the underside only\n"
        "    (the face that seats on the weight)." if chamfer else None,
        "Deburr all edges and holes. The top face seals against an O-ring near\n"
        "    the rim: keep it flat and free of scratches and burrs.",
        "General tolerances ISO 2768-m.",
        "Finish: none required. If anodised, mask the threads.",
    ]
    return [f"{i}. {text}" for i, text in enumerate((t for t in items if t), start=1)]


def notes_block(ax, variant):
    x0, y = NOTES_ORIGIN
    ax.text(x0, y, "NOTES", fontsize=NORMAL, weight="bold")
    y -= NOTE_LINE + NOTE_GAP
    for note in notes(variant):
        ax.text(x0, y, note, fontsize=SMALL, va="top", linespacing=1.3)
        y -= NOTE_LINE * (note.count("\n") + 1) + NOTE_GAP


def title_block(ax, variant):
    x, y, w, h = TITLE_BOX
    ax.add_patch(Rectangle((x, y), w, h, fill=False, lw=THICK))
    lines = [(variant["title"], BIG, "bold"),
             (f"{variant['material']}, t = {T_PLATE:.1f} mm   Ø{2 * R:g}   Scale 2:1 on A4   Units mm", NORMAL, "normal"),
             (f"Generated {datetime.date.today().isoformat()} by mechanical/plate_drawing.py from layout.json",
              SMALL, "normal")]
    for drop, (text, size, weight) in zip(TITLE_ROW_DROPS, lines):
        ax.text(x + TITLE_PAD, y + h - drop, text, fontsize=size, weight=weight, va="top")


def build_figure(variant):
    fig = plt.figure(figsize=(PAGE_W / MM_PER_INCH, PAGE_H / MM_PER_INCH))
    ax = fig.add_axes((0, 0, 1, 1))
    ax.set_xlim(0, PAGE_W)
    ax.set_ylim(0, PAGE_H)
    ax.set_aspect("equal")
    ax.axis("off")
    frame = (PAGE_MARGIN, PAGE_MARGIN)
    ax.add_patch(Rectangle(frame, PAGE_W - 2 * PAGE_MARGIN, PAGE_H - 2 * PAGE_MARGIN, fill=False, lw=THICK))
    top_view(ax, variant)
    side_view(ax, variant)
    hole_table(ax)
    notes_block(ax, variant)
    title_block(ax, variant)
    return fig


def main():
    os.makedirs(OUT, exist_ok=True)
    for name, variant in VARIANTS.items():
        build_figure(variant).savefig(os.path.join(OUT, name))
        print("wrote", os.path.join(OUT, name))
        if variant["step"]:
            cq.exporters.export(make_plate(variant["chamfer"]), os.path.join(OUT, variant["step"]))
            print("wrote", os.path.join(OUT, variant["step"]))


if __name__ == "__main__":
    main()
