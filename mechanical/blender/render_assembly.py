import glob
import hashlib
import os
import sys

import bpy

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import looks
import motion
import parts
from stack import HERE, OUT, ROOT

SCENE_INPUTS = (os.path.join(HERE, "*.py"), os.path.join(OUT, "*.stl"), os.path.join(ROOT, "layout.json"),
                parts.BOARD_PCB, os.path.join(ROOT, "hardware", "kicad", "ShotPuck.3dshapes", "*"))
STAMP_PATH = os.path.join(motion.FRAMES_DIR, "scene.sha256")


def flags():
    return sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []


def scene_digest():
    digest = hashlib.sha256()
    for path in sorted(p for pattern in SCENE_INPUTS for p in glob.glob(pattern)):
        with open(path, "rb") as f:
            digest.update(f.read())
    return digest.hexdigest()


def read_stamp():
    if not os.path.exists(STAMP_PATH):
        return None
    with open(STAMP_PATH) as f:
        return f.read()


def clear_stale_frames():
    os.makedirs(motion.FRAMES_DIR, exist_ok=True)
    digest = scene_digest()
    if read_stamp() == digest:
        return
    for frame in glob.glob(os.path.join(motion.FRAMES_DIR, "*.png")):
        os.remove(frame)
    with open(STAMP_PATH, "w") as f:
        f.write(digest)


def main():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    if "--encode" in flags():
        motion.encode_movie()
        return
    groups = parts.build_all()
    looks.apply_all()
    motion.stage(groups)
    if "--preview" in flags():
        motion.render_stills()
        return
    clear_stale_frames()
    motion.render_frames()
    motion.encode_movie()


main()
