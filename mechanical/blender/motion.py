import math
import os

import bpy

from shapes import MM
from stack import L, OUT

FPS = 30
END_FRAME = 395
ASSEMBLED_FRAME = 310
DOLLY_FRAME = 205
EXPLODE_MM = {"plate": 0.0, "kapton": 9.0, "spacers": 14.0, "cell": 22.0, "board": 30.0,
              "foam": 38.0, "sleeve": 48.0, "cap": 56.0, "screws": 70.0, "bolt": 88.0}
CAP_LEVEL = EXPLODE_MM["cap"]
STEPS = (
    (("kapton",), (60, 80), 0.0),
    (("spacers",), (80, 100), 0.0),
    (("cell",), (100, 125), 0.0),
    (("board",), (125, 155), 0.0),
    (("foam",), (155, 180), 0.0),
    (("sleeve",), (180, DOLLY_FRAME), CAP_LEVEL),
    (("cap", "sleeve"), (DOLLY_FRAME, 245), 0.0),
    (("screws",), (245, 275), 0.0),
    (("bolt",), (275, ASSEMBLED_FRAME), 0.0),
)
RIGHT_ANGLE_DEG = 90.0
PORT_AXIS_DEG = math.degrees(math.atan2(L["connector"]["y"], L["connector"]["x"]))
PORT_VIEW_OFFSET_DEG = 15.0
ORBIT_DEG = 90.0
END_TURN = PORT_AXIS_DEG + RIGHT_ANGLE_DEG - PORT_VIEW_OFFSET_DEG
CAMERA_START = {"distance": 340.0, "target": 55.0, "turn": END_TURN - ORBIT_DEG}
CAMERA_END = {"distance": 190.0, "target": 3.0, "turn": END_TURN}
CAMERA_ELEVATION_DEG = 18.0
LENS_MM = 70.0
CLIP_START = 0.005
LIGHT_TARGET_Z = 0.01
LIGHTS = (
    ("key", (0.25, -0.30, 0.35), 8.0, 0.20),
    ("fill", (-0.35, -0.15, 0.12), 3.0, 0.30),
    ("rim", (-0.10, 0.40, 0.30), 8.0, 0.15),
)
RESOLUTION = (1080, 1920)
SAMPLES = 512
PREVIEW_SAMPLES = 32
PREVIEW_SCALE = 50
PREVIEW_FRAMES = (0, 130, 200, 290, END_FRAME)
GPU_BACKEND = "OPTIX"
TRANSMISSION_BOUNCES = 16
TRANSPARENT_BOUNCES = 16
CLAMP_INDIRECT = 10.0
FRAMES_DIR = os.path.join(OUT, "frames")
MOVIE_PATH = os.path.join(OUT, "shotpuck_assembly.mp4")
BLEND_PATH = os.path.join(OUT, "shotpuck_assembly.blend")


def key_z(obj, frame_dz):
    frame, dz = frame_dz
    obj.location.z = obj["rest_z"] + dz * MM
    obj.keyframe_insert(data_path="location", index=2, frame=frame)


def animate(groups):
    for name, obj in groups.items():
        obj["rest_z"] = obj.location.z
        key_z(obj, (0, EXPLODE_MM[name]))
    state = dict(EXPLODE_MM)
    for names, (start, end), target in STEPS:
        for name in names:
            key_z(groups[name], (start, state[name]))
            key_z(groups[name], (end, target))
            state[name] = target


def key_camera(rig_cam, frame_pose):
    rig, cam = rig_cam
    frame, pose = frame_pose
    el = math.radians(CAMERA_ELEVATION_DEG)
    cam.location = (0.0, -pose["distance"] * MM * math.cos(el), pose["distance"] * MM * math.sin(el))
    cam.keyframe_insert("location", frame=frame)
    rig.location.z = pose["target"] * MM
    rig.keyframe_insert("location", index=2, frame=frame)


def spin(rig):
    for frame, pose in ((0, CAMERA_START), (END_FRAME, CAMERA_END)):
        rig.rotation_euler.z = math.radians(pose["turn"])
        rig.keyframe_insert("rotation_euler", index=2, frame=frame)
    for point in rig.animation_data.action.fcurves.find("rotation_euler", index=2).keyframe_points:
        point.interpolation = "LINEAR"


def camera_rig():
    rig = bpy.data.objects.new("camera_rig", None)
    bpy.context.scene.collection.objects.link(rig)
    lens = bpy.data.cameras.new("camera")
    lens.lens = LENS_MM
    lens.clip_start = CLIP_START
    cam = bpy.data.objects.new("camera", lens)
    bpy.context.scene.collection.objects.link(cam)
    cam.parent = rig
    cam.rotation_euler = (math.radians(RIGHT_ANGLE_DEG - CAMERA_ELEVATION_DEG), 0.0, 0.0)
    bpy.context.scene.camera = cam
    for frame_pose in ((0, CAMERA_START), (DOLLY_FRAME, CAMERA_START), (ASSEMBLED_FRAME, CAMERA_END)):
        key_camera((rig, cam), frame_pose)
    spin(rig)
    return rig


def lights(rig):
    target = bpy.data.objects.new("light_target", None)
    target.location.z = LIGHT_TARGET_Z
    bpy.context.scene.collection.objects.link(target)
    for name, position, power, size in LIGHTS:
        data = bpy.data.lights.new(name, "AREA")
        data.energy = power
        data.size = size
        lamp = bpy.data.objects.new(name, data)
        lamp.location = position
        lamp.parent = rig
        bpy.context.scene.collection.objects.link(lamp)
        aim = lamp.constraints.new("TRACK_TO")
        aim.target = target
        aim.track_axis = "TRACK_NEGATIVE_Z"
        aim.up_axis = "UP_Y"


def stage(groups):
    animate(groups)
    lights(camera_rig())


def use_gpu():
    prefs = bpy.context.preferences.addons["cycles"].preferences
    prefs.compute_device_type = GPU_BACKEND
    prefs.get_devices()
    for device in prefs.devices:
        device.use = device.type == GPU_BACKEND
    bpy.context.scene.cycles.device = "GPU"


def render_settings(samples):
    scene = bpy.context.scene
    scene.render.engine = "CYCLES"
    use_gpu()
    scene.cycles.samples = samples
    scene.cycles.use_denoising = True
    scene.cycles.denoiser = GPU_BACKEND
    scene.cycles.transmission_bounces = TRANSMISSION_BOUNCES
    scene.cycles.transparent_max_bounces = TRANSPARENT_BOUNCES
    scene.cycles.sample_clamp_indirect = CLAMP_INDIRECT
    scene.render.resolution_x, scene.render.resolution_y = RESOLUTION
    scene.render.fps = FPS
    scene.frame_start, scene.frame_end = 0, END_FRAME
    scene.view_settings.view_transform = "Filmic"
    scene.view_settings.look = "Medium High Contrast"


def render_stills():
    render_settings(PREVIEW_SAMPLES)
    scene = bpy.context.scene
    scene.render.resolution_percentage = PREVIEW_SCALE
    for frame in PREVIEW_FRAMES:
        scene.frame_set(frame)
        scene.render.filepath = os.path.join(OUT, f"preview_{frame:03d}.png")
        bpy.ops.render.render(write_still=True)


def render_frames():
    render_settings(SAMPLES)
    scene = bpy.context.scene
    scene.render.use_overwrite = False
    scene.render.image_settings.file_format = "PNG"
    scene.render.filepath = os.path.join(FRAMES_DIR, "frame_")
    bpy.ops.wm.save_as_mainfile(filepath=BLEND_PATH)
    bpy.ops.render.render(animation=True)


def encode_movie():
    scene = bpy.context.scene
    render_settings(SAMPLES)
    files = sorted(f for f in os.listdir(FRAMES_DIR) if f.endswith(".png"))
    strips = scene.sequence_editor_create().sequences
    clip = strips.new_image("frames", os.path.join(FRAMES_DIR, files[0]), 1, 0)
    for name in files[1:]:
        clip.elements.append(name)
    scene.render.image_settings.file_format = "FFMPEG"
    scene.render.ffmpeg.format = "MPEG4"
    scene.render.ffmpeg.codec = "H264"
    scene.render.ffmpeg.constant_rate_factor = "PERC_LOSSLESS"
    scene.render.filepath = MOVIE_PATH
    bpy.ops.render.render(animation=True)
