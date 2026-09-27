import math

import bpy

MM = 0.001
ROUND_SEGMENTS = 64
TORUS_MAJOR_SEGMENTS = 128
TORUS_MINOR_SEGMENTS = 16
SMOOTH_ANGLE_DEG = 35
BEVEL_SEGMENTS = 2


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
