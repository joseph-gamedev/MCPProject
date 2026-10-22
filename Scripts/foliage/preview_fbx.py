"""Renders a close lineup of exported FBX plants for review.

    blender -b --python Scripts/foliage/preview_fbx.py -- <out.png> <file.fbx> [file.fbx ...]
"""

import math
import sys

import bpy

args = sys.argv[sys.argv.index("--") + 1:]
out, files = args[0], args[1:]
bpy.ops.wm.read_factory_settings(use_empty=True)
objs = []
for f in files:
    before = set(bpy.data.objects)
    bpy.ops.import_scene.fbx(filepath=f)
    objs += [o for o in bpy.data.objects if o not in before and o.type == "MESH"]
x, tallest = 0.0, 0.0
for o in objs:
    w = max(o.dimensions.x, o.dimensions.y, 0.6)
    o.location = (x + w / 2, 0, 0)
    x += w * 1.15
    tallest = max(tallest, o.dimensions.z)
for img in bpy.data.images:  # textures sit next to the FBX files
    if img.filepath and not img.has_data:
        img.filepath = bpy.path.abspath("//") + img.name
for m in bpy.data.materials:
    if m.node_tree:
        for n in m.node_tree.nodes:
            if n.type == "TEX_IMAGE" and n.image is None:
                pass
scene = bpy.context.scene
dist = max(x, tallest * 1.6)
bpy.ops.object.camera_add(location=(x / 2, -dist * 1.15, tallest * 0.5), rotation=(math.radians(88), 0, 0))
scene.camera = bpy.context.object
scene.camera.data.lens = 32
bpy.ops.object.light_add(type="SUN", rotation=(math.radians(45), 0, math.radians(35)))
bpy.context.object.data.energy = 4
world = bpy.data.worlds.new("w")
world.use_nodes = True
world.node_tree.nodes["Background"].inputs[0].default_value = (0.55, 0.7, 0.9, 1)
scene.world = world
bpy.ops.mesh.primitive_plane_add(size=500, location=(x / 2, 0, 0))
scene.render.engine = "BLENDER_EEVEE"
scene.render.resolution_x, scene.render.resolution_y = 1600, 800
scene.render.filepath = out
bpy.ops.render.render(write_still=True)
print("PREVIEW", out)
