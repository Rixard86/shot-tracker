import json
import math
import os
from functools import lru_cache

from shapely import affinity
from shapely.geometry import Point, Polygon, box
from shapely.ops import unary_union

HERE = os.path.dirname(os.path.abspath(__file__))
L = json.load(open(os.path.join(HERE, "..", "layout.json")))
ARC_SEGMENTS = 32
ANTENNA_INSET = 4.4
ANTENNA_HALF_W = 6.2
ANTENNA_CHAMFER = ANTENNA_HALF_W - L["module"]["w"] / 2
RIM_MARGIN = 2.0


def rim_r():
    return L["pcb_dia"] / 2


def boss_xy():
    return [tuple(p) for p in L["bosses"]["positions"]]


def disc(center, r):
    return Point(*center).buffer(r, quad_segs=ARC_SEGMENTS)


def notch_r():
    c = L["cell"]
    return c["dia"] / 2 + c["pcb_hole_clear"]


def cell_notch():
    c = L["cell"]
    r = notch_r()
    dist = math.hypot(c["x"], c["y"])
    reach = rim_r() + RIM_MARGIN - dist
    channel = box(0.0, -r, reach, r)
    channel = affinity.rotate(channel, math.atan2(c["y"], c["x"]), origin=(0, 0), use_radians=True)
    channel = affinity.translate(channel, c["x"], c["y"])
    return unary_union([disc((c["x"], c["y"]), r), channel])


def sleeve_hole():
    c = L["cell"]
    r = L["sleeve"]["od"] / 2 + L["sleeve"]["pcb_clear"]
    slot = box(0.0, -r, math.hypot(c["x"], c["y"]), r)
    slot = affinity.rotate(slot, math.atan2(c["y"], c["x"]), origin=(0, 0), use_radians=True)
    return unary_union([disc((0.0, 0.0), r), slot])


@lru_cache(maxsize=None)
def cutout():
    return unary_union([sleeve_hole(), cell_notch()])


@lru_cache(maxsize=None)
def board():
    return disc((0.0, 0.0), rim_r()).difference(cutout())


def outline_loops():
    shape = board()
    loops = [list(shape.exterior.coords)]
    loops += [list(ring.coords) for ring in shape.interiors]
    return loops


def module_rect():
    m = L["module"]
    rect = box(-m["l"] / 2, -m["w"] / 2, m["l"] / 2, m["w"] / 2)
    rect = affinity.rotate(rect, m["antenna_dir_deg"], origin=(0, 0))
    return affinity.translate(rect, m["x"], m["y"])


@lru_cache(maxsize=None)
def antenna_zone():
    m = L["module"]
    u0 = m["l"] / 2 - ANTENNA_INSET
    far = rim_r() + RIM_MARGIN + m["l"]
    zone = Polygon([(u0 + ANTENNA_CHAMFER, -ANTENNA_HALF_W), (far, -ANTENNA_HALF_W), (far, ANTENNA_HALF_W),
                    (u0, ANTENNA_HALF_W), (u0, ANTENNA_CHAMFER - ANTENNA_HALF_W)])
    zone = affinity.rotate(zone, m["antenna_dir_deg"], origin=(0, 0))
    return affinity.translate(zone, m["x"], m["y"])


def in_cutout(pt, margin):
    return cutout().distance(Point(*pt)) < margin


def in_antenna(pt, margin):
    return antenna_zone().distance(Point(*pt)) < margin


def polygon_points(shape):
    if isinstance(shape, Polygon):
        return list(shape.exterior.coords)[:-1]
    return list(max(shape.geoms, key=lambda g: g.area).exterior.coords)[:-1]
