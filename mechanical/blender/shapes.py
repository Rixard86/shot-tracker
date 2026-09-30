import math

import bpy

MM = 0.001
ROUND_SEGMENTS = 64
TORUS_MAJOR_SEGMENTS = 128
TORUS_MINOR_SEGMENTS = 16
SMOOTH_ANGLE_DEG = 35
BEVEL_SEGMENTS = 2
THREAD_STEPS = 32
THREAD_DEPTH = 0.6134
THREAD_CREST_FLAT = 0.125
LOBES = 6
LOBE_POINTS = 96
CUTTER_ABOVE = 1.0


def tag(obj, look):
    obj["look"] = look
    return obj


def named(name):
    obj = bpy.context.active_object
    obj.name = name
    return obj


def cylinder(spec, name):
    bpy.ops.mesh.primitive_cylinder_add(
        vertices=spec.get("seg", ROUND_SEGMENTS), radius=spec["r"] * MM,
        depth=(spec["z1"] - spec["z0"]) * MM,
        location=(spec.get("x", 0.0) * MM, spec.get("y", 0.0) * MM, (spec["z0"] + spec["z1"]) / 2 * MM))
    return named(name)


def cone(spec, name):
    bpy.ops.mesh.primitive_cone_add(
        vertices=ROUND_SEGMENTS, radius1=spec["r0"] * MM, radius2=spec["r1"] * MM,
        depth=(spec["z1"] - spec["z0"]) * MM,
        location=(spec["x"] * MM, spec["y"] * MM, (spec["z0"] + spec["z1"]) / 2 * MM))
    return named(name)


def box(corners, name):
    lo = [min(a, b) for a, b in zip(*corners)]
    hi = [max(a, b) for a, b in zip(*corners)]
    bpy.ops.mesh.primitive_cube_add(size=1.0, location=[(a + b) / 2 * MM for a, b in zip(lo, hi)])
    obj = named(name)
    obj.scale = [(b - a) * MM for a, b in zip(lo, hi)]
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    return obj


def torus(spec, name):
    bpy.ops.mesh.primitive_torus_add(
        major_radius=spec["R"] * MM, minor_radius=spec["r"] * MM,
        major_segments=TORUS_MAJOR_SEGMENTS, minor_segments=TORUS_MINOR_SEGMENTS,
        location=(0.0, 0.0, spec["z"] * MM))
    return named(name)


def mesh_object(data, name):
    verts, faces = data
    mesh = bpy.data.meshes.new(name)
    mesh.from_pydata([tuple(c * MM for c in v) for v in verts], [], faces)
    obj = bpy.data.objects.new(name, mesh)
    bpy.context.scene.collection.objects.link(obj)
    return obj


def thread(spec, name):
    pitch, r_major = spec["pitch"], spec["r"]
    r_root = r_major - THREAD_DEPTH * pitch
    flank = (1.0 - THREAD_CREST_FLAT) / 2 * pitch
    profile = [(r_root, 0.0, 0.0), (r_major, 0.0, flank), (r_major, 0.0, pitch - flank), (r_root, 0.0, pitch)]
    mesh = bpy.data.meshes.new(name)
    mesh.from_pydata([tuple(c * MM for c in v) for v in profile], [(0, 1), (1, 2), (2, 3)], [])
    helix = bpy.data.objects.new(name, mesh)
    bpy.context.scene.collection.objects.link(helix)
    helix.location = (spec.get("x", 0.0) * MM, spec.get("y", 0.0) * MM, spec["z0"] * MM)
    mod = helix.modifiers.new("helix", "SCREW")
    mod.axis = "Z"
    mod.screw_offset = pitch * MM
    mod.iterations = int((spec["z1"] - spec["z0"]) / pitch) - 1
    mod.steps = mod.render_steps = THREAD_STEPS
    mod.use_normal_calculate = True
    activate(helix)
    bpy.ops.object.modifier_apply(modifier=mod.name)
    core = cylinder(dict(spec, r=r_root), name + "_core")
    attach(core, helix)
    return helix, core


def lobe_ring(centre, size):
    mean, swing = (size[0] + size[1]) / 4, (size[0] - size[1]) / 4
    points = []
    for i in range(LOBE_POINTS):
        a = 2 * math.pi * i / LOBE_POINTS
        r = mean + swing * math.cos(LOBES * a)
        points.append((centre[0] + r * math.cos(a), centre[1] + r * math.sin(a)))
    return points


def torx(spec, name):
    x, y, z_top, size, depth = spec
    ring = lobe_ring((x, y), size)
    verts = [(px, py, z_top - depth) for px, py in ring] + [(px, py, z_top + CUTTER_ABOVE) for px, py in ring]
    n = LOBE_POINTS
    sides = [(i, (i + 1) % n, n + (i + 1) % n, n + i) for i in range(n)]
    return mesh_object((verts, [tuple(reversed(range(n))), tuple(range(n, 2 * n))] + sides), name)


def activate(obj):
    bpy.ops.object.select_all(action="DESELECT")
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    return obj


def cut(obj, cutter):
    mod = obj.modifiers.new("cut", "BOOLEAN")
    mod.operation = "DIFFERENCE"
    mod.object = cutter
    activate(obj)
    bpy.ops.object.modifier_apply(modifier=mod.name)
    bpy.data.objects.remove(cutter, do_unlink=True)
    return obj


def smooth(obj):
    activate(obj)
    bpy.ops.object.shade_smooth()
    obj.data.use_auto_smooth = True
    obj.data.auto_smooth_angle = math.radians(SMOOTH_ANGLE_DEG)
    return obj


def bevel(obj, width):
    mod = obj.modifiers.new("bevel", "BEVEL")
    mod.width = width * MM
    mod.segments = BEVEL_SEGMENTS
    mod.limit_method = "ANGLE"
    return obj


def attach(child, parent):
    bpy.context.view_layer.update()
    child.parent = parent
    child.matrix_parent_inverse = parent.matrix_world.inverted()
    return child


def empty(name, z):
    bpy.ops.object.empty_add(location=(0.0, 0.0, z * MM))
    return named(name)
