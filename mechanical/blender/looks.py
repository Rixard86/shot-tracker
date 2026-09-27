import bpy

LOOKS = {
    "anodised": {"Base Color": (0.035, 0.035, 0.04, 1.0), "Metallic": 0.5, "Roughness": 0.5},
    "brass": {"Base Color": (0.86, 0.63, 0.30, 1.0), "Metallic": 1.0, "Roughness": 0.25},
    "steel": {"Base Color": (0.78, 0.78, 0.80, 1.0), "Metallic": 1.0, "Roughness": 0.18},
    "nickel": {"Base Color": (0.72, 0.70, 0.64, 1.0), "Metallic": 1.0, "Roughness": 0.30},
    "gold": {"Base Color": (1.0, 0.76, 0.33, 1.0), "Metallic": 1.0, "Roughness": 0.20},
    "black_plastic": {"Base Color": (0.018, 0.018, 0.02, 1.0), "Roughness": 0.45},
    "rubber": {"Base Color": (0.012, 0.012, 0.012, 1.0), "Roughness": 0.5, "Specular": 0.2},
    "foam": {"Base Color": (0.16, 0.16, 0.17, 1.0), "Roughness": 1.0},
    "black_oxide": {"Base Color": (0.035, 0.035, 0.04, 1.0), "Metallic": 1.0, "Roughness": 0.42},
    "kapton": {"Base Color": (1.0, 0.42, 0.04, 1.0), "Roughness": 0.15, "Transmission": 0.7, "IOR": 1.7},
    "polycarbonate": {"Base Color": (0.92, 0.96, 1.0, 1.0), "Roughness": 0.04, "Transmission": 1.0,
                      "IOR": 1.586},
}
CLEAR_SHADOW_LOOKS = {"polycarbonate", "kapton"}
WORLD_STOPS = ((0.0, (0.004, 0.004, 0.005, 1.0)), (1.0, (0.30, 0.31, 0.34, 1.0)))
WORLD_Z_RANGE = (-0.3, 0.9)
SOLDERMASK = {"Base Color": (0.01, 0.06, 0.025, 1.0), "Roughness": 0.4, "Metallic": 0.0, "Alpha": 1.0}
SOLDERMASK_ALPHA_MAX = 0.9
OPAQUE_ALPHA_MIN = 0.95
METAL_MIN_LUMA = 0.3
IMPORTED_ROUGHNESS_MAX = 0.45


def clear_shadows(mat):
    nodes, links = mat.node_tree.nodes, mat.node_tree.links
    path = nodes.new("ShaderNodeLightPath")
    clear = nodes.new("ShaderNodeBsdfTransparent")
    mix = nodes.new("ShaderNodeMixShader")
    links.new(path.outputs["Is Shadow Ray"], mix.inputs["Fac"])
    links.new(nodes["Principled BSDF"].outputs["BSDF"], mix.inputs[1])
    links.new(clear.outputs["BSDF"], mix.inputs[2])
    links.new(mix.outputs["Shader"], nodes["Material Output"].inputs["Surface"])


def principled(name):
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    bsdf = mat.node_tree.nodes["Principled BSDF"]
    for key, value in LOOKS[name].items():
        bsdf.inputs[key].default_value = value
    if name in CLEAR_SHADOW_LOOKS:
        clear_shadows(mat)
    return mat


def world():
    studio = bpy.data.worlds.new("studio")
    bpy.context.scene.world = studio
    studio.use_nodes = True
    nodes, links = studio.node_tree.nodes, studio.node_tree.links
    coords = nodes.new("ShaderNodeTexCoord")
    split = nodes.new("ShaderNodeSeparateXYZ")
    span = nodes.new("ShaderNodeMapRange")
    ramp = nodes.new("ShaderNodeValToRGB")
    span.inputs["From Min"].default_value, span.inputs["From Max"].default_value = WORLD_Z_RANGE
    for element, (position, color) in zip(ramp.color_ramp.elements, WORLD_STOPS):
        element.position = position
        element.color = color
    links.new(coords.outputs["Generated"], split.inputs["Vector"])
    links.new(split.outputs["Z"], span.inputs["Value"])
    links.new(span.outputs["Result"], ramp.inputs["Fac"])
    links.new(ramp.outputs["Color"], nodes["Background"].inputs["Color"])


def tune_imported(mat):
    bsdf = mat.node_tree.nodes.get("Principled BSDF") if mat.use_nodes else None
    if bsdf is None:
        return
    color = bsdf.inputs["Base Color"].default_value
    alpha = bsdf.inputs["Alpha"].default_value
    if alpha < SOLDERMASK_ALPHA_MAX and color[1] > color[0]:
        for key, value in SOLDERMASK.items():
            bsdf.inputs[key].default_value = value
        mat.blend_method = "OPAQUE"
        return
    if alpha > OPAQUE_ALPHA_MIN:
        bsdf.inputs["Alpha"].default_value = 1.0
        mat.blend_method = "OPAQUE"
    if sum(color[:3]) / 3 < METAL_MIN_LUMA:
        bsdf.inputs["Metallic"].default_value = 0.0
    bsdf.inputs["Roughness"].default_value = min(bsdf.inputs["Roughness"].default_value, IMPORTED_ROUGHNESS_MAX)


def apply_all():
    mats = {}
    for obj in bpy.context.scene.objects:
        look = obj.get("look")
        if not look:
            continue
        if look not in mats:
            mats[look] = principled(look)
        obj.data.materials.clear()
        obj.data.materials.append(mats[look])
    for mat in bpy.data.materials:
        if mat.name not in LOOKS:
            tune_imported(mat)
    world()
