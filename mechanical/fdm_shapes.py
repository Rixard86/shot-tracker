import math

import cadquery as cq

from geometry import L, T_PLATE, Z_PCB, boss_xy
from puck_parts import CUT_MARGIN, column, placed, slab, tube

BACKING_RUN_PER_RISE = 1.0
FULL_TURN_DEG = 360.0
HALF_SPACE = 100.0


def polar(r, deg):
    a = math.radians(deg)
    return r * math.cos(a), r * math.sin(a)


def arc_deg(length, r):
    return math.degrees(length / r)


def at_cell(wp, deg):
    c = L["cell"]
    return placed(wp, (c["x"], c["y"], deg))


def box_between(xy, z_range):
    (x0, x1), (y0, y1) = xy
    return slab((x1 - x0, y1 - y0), z_range).translate(((x0 + x1) / 2, (y0 + y1) / 2, 0))


def outline(ring, z):
    (r0, r1), (a0, a1) = ring
    mid = (a0 + a1) / 2
    wp = cq.Workplane("XY").workplane(offset=z).moveTo(*polar(r0, a0)).lineTo(*polar(r1, a0))
    wp = wp.threePointArc(polar(r1, mid), polar(r1, a1)).lineTo(*polar(r0, a1))
    return wp.threePointArc(polar(r0, mid), polar(r0, a0)).close()


def sector(radii, span):
    a0, a1, z0, z1 = span
    return at_cell(outline((radii, (a0, a1)), z0).extrude(z1 - z0), 0)


def sector_loft(rings, z_range):
    (low, high), (z0, z1) = rings, z_range
    solid = cq.Solid.makeLoft([outline(low, z0).val(), outline(high, z1).val()], True)
    return at_cell(cq.Workplane("XY").add(solid), 0)


def backing_run(z_range):
    z0, z1 = z_range
    return (z1 - z0) * BACKING_RUN_PER_RISE


def backing_profile(profile):
    (r_in, r_out), (z0, z1) = profile
    pts = [(r_in, z0), (r_out + backing_run((z0, z1)), z0), (r_out, z1), (r_in, z1)]
    return cq.Workplane("XZ").polyline(pts).close()


def backed_ring(center, profile):
    ring = backing_profile(profile).revolve(FULL_TURN_DEG, (0, 0, 0), (0, 1, 0))
    return ring.translate((center[0], center[1], 0))


def flared_column(spec):
    x, y, r, z0, z1 = spec
    cone = cq.Solid.makeCone(r, r + backing_run((z0, z1)), z1 - z0, cq.Vector(x, y, z0), cq.Vector(0, 0, 1))
    return cq.Workplane("XY").add(cone)


def revolved(profile, span):
    a0, a1 = span
    wedge = cq.Workplane("XZ").polyline(profile).close().revolve(a1 - a0, (0, 0, 0), (0, 1, 0))
    return at_cell(wedge, a0)


def above_slope(edge):
    deg, z_top, sign = edge
    ux, uy = polar(1.0, deg)
    normal = cq.Vector(-uy * sign, ux * sign, BACKING_RUN_PER_RISE)
    plane = cq.Plane(origin=(0, 0, z_top), xDir=(ux, uy, 0), normal=normal)
    return at_cell(cq.Workplane(plane).rect(HALF_SPACE, HALF_SPACE).extrude(HALF_SPACE), 0)


def sloped_post(profile, span):
    a0, a1 = span
    (r_face, z0), (r_back, top) = profile[0], profile[-1]
    run = backing_run((z0, top))
    wide = math.degrees(math.atan2(run + CUT_MARGIN, r_face))
    post = revolved(profile + [(r_back + run, z0)], (a0 - wide, a1 + wide))
    return post.cut(above_slope((a1, top, 1))).cut(above_slope((a0, top, -1)))


def keep_clear(solid, radii):
    bore_r, rim_r, standoff_r = radii
    solid = solid.intersect(tube((0, 0, rim_r, T_PLATE - CUT_MARGIN, Z_PCB), bore_r))
    for x, y in boss_xy():
        solid = solid.cut(column((x, y, standoff_r, T_PLATE - CUT_MARGIN, Z_PCB)))
    return solid
