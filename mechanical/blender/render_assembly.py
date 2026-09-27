import os
import sys

import bpy

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import looks
import motion
import parts


def flags():
    return sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []


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
    motion.render_frames()
    motion.encode_movie()


main()
