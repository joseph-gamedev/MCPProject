"""Realistic trees with Blender's Sapling Tree Gen add-on (extensions.blender.org, free), exported for Unreal.

    blender -b --python Scripts/foliage/sapling_trees.py -- [out_dir] [species,...] [variants] [--preview]

Species come from Sapling's presets (and a few tuned variants of them): branches become one bark mesh, leaves become
alpha-textured leaf cards (texture from make_trees.make_leaf_texture). Each FBX has two materials: Bark_<kind> and
Leaves_<kind>. --preview renders a lineup PNG (preview_trees.png) for checking before import.
Needs: sapling_tree_gen enabled (blender --online-mode -c extension install --enable sapling_tree_gen).
"""

import ast
import math
import os
import random
import sys

import addon_utils
import bpy

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
ARGS = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
PREVIEW = "--preview" in ARGS
ARGS = [a for a in ARGS if a != "--preview"]
OUT = os.path.abspath(ARGS[0]) if ARGS else os.path.abspath(os.path.join(HERE, "..", "..", "Saved", "Foliage"))
ONLY = ARGS[1].split(",") if len(ARGS) > 1 and ARGS[1] not in ("", "all") else None
VARIANTS = int(ARGS[2]) if len(ARGS) > 2 else 3
os.makedirs(OUT, exist_ok=True)

import make_trees  # noqa: E402  (leaf textures, palm)

make_trees.OUT = OUT

# species -> (sapling preset, overrides, leaf kind, leaf colour, bark colour)
SPECIES = {
    "aspen": ("quaking_aspen", {"_leaves": 3.5, "_leafScale": 1.8}, "small", (0.30, 0.40, 0.12), (0.55, 0.53, 0.47)),
    "birch": ("white_birch", {"_leaves": 1.6, "_leafScale": 1.7}, "small", (0.32, 0.42, 0.13), (0.70, 0.68, 0.62)),
    "maple": ("small_maple", {"_leaves": 2.2, "_leafScale": 1.9}, "broad", (0.24, 0.34, 0.10), (0.18, 0.14, 0.11)),
    "japanese_maple": ("japanese_maple", {"_leafScale": 1.6, "_leaves": 1.8}, "broad", (0.45, 0.10, 0.06), (0.16, 0.12, 0.10)),
    "oak": ("small_maple", {"scale": 15.0, "baseSize": 0.25, "ratio": 0.03, "downAngle": (90.0, 70.0, 50.0, 45.0), "curve": (0.0, -20.0, 20.0, 0.0),
                            "taper": (1.0, 1.0, 1.0, 1.0), "_leaves": 2.5, "_leafScale": 2.0}, "broad", (0.20, 0.30, 0.09), (0.16, 0.12, 0.09)),
    "pine": ("small_pine", {"scale": 16.0, "scaleV": 3.0, "_leafScale": 2.8, "_leaves": 2.0, "_nodecimate": True}, "needle", (0.14, 0.24, 0.10), (0.22, 0.15, 0.10)),
    "fir": ("douglas_fir", {"_leafScale": 2.4, "_nodecimate": True}, "needle", (0.10, 0.20, 0.10), (0.20, 0.14, 0.10)),
    "willow": ("weeping_willow", {}, "willow", (0.36, 0.45, 0.16), (0.18, 0.15, 0.11)),
    "bottlebrush": ("callistemon", {}, "small", (0.24, 0.32, 0.12), (0.20, 0.15, 0.11)),
}


# real-world heights (m): every variant is scaled into this range
HEIGHTS = {"aspen": (12, 18), "birch": (12, 16), "maple": (9, 14), "japanese_maple": (4, 7), "oak": (14, 20), "pine": (15, 22),
           "fir": (22, 34), "willow": (10, 14), "bottlebrush": (3, 6)}


def sapling_dir():
    for m in addon_utils.modules():
        if m.__name__.endswith("sapling_tree_gen"):
            return os.path.dirname(m.__file__)
    raise RuntimeError("sapling_tree_gen is not installed/enabled")


def load_preset(name):
    with open(os.path.join(sapling_dir(), "presets", name + ".py"), encoding="utf-8") as fh:
        text = fh.read()
    return ast.literal_eval(text[text.index("{"):])


def material(name, color, texture=None):
    return make_trees.material(name, color, texture)


def build(species, variant):
    preset_name, overrides, leaf_kind, leaf_color, bark_color = SPECIES[species]
    props = load_preset(preset_name)
    tweaks = {k: v for k, v in overrides.items() if k.startswith("_")}
    props.update({k: v for k, v in overrides.items() if not k.startswith("_")})
    props["leaves"] = int(props.get("leaves", 25) * tweaks.get("_leaves", 1.0))
    props["leafScale"] = props.get("leafScale", 0.2) * tweaks.get("_leafScale", 1.0)
    rng = random.Random(f"{species}{variant}")
    if species == "aspen":  # this preset ignores the seed: vary its shape parameters instead
        props["baseSize"] = props.get("baseSize", 0.4) * rng.uniform(0.75, 1.25)
        props["ratio"] = props.get("ratio", 0.015) * rng.uniform(0.85, 1.2)
        props["branches"] = tuple(max(1, int(b * rng.uniform(0.8, 1.25))) for b in props.get("branches", (0, 50, 30, 10)))
    props.update(seed=rng.randint(0, 9999), showLeaves=True, leafShape="rect", makeMesh=False, bevel=True, armAnim=False, leafAnim=False,
                 useArm=False, prune=props.get("prune", False))
    props["scale"] = props.get("scale", 10) * rng.uniform(0.9, 1.1)
    for key in list(props):  # drop keys this version does not know
        if key not in bpy.ops.curve.tree_add.get_rna_type().properties.keys():
            props.pop(key)
    before = set(bpy.data.objects)
    bpy.ops.curve.tree_add(**props)
    new = [o for o in bpy.data.objects if o not in before]
    tree = next(o for o in new if o.type == "CURVE")
    leaves = next((o for o in new if o.type == "MESH" and "leaves" in o.name.lower()), None)
    name = f"SM_{species.title().replace('_', '')}_{variant + 1:02d}"
    # branches: curve -> mesh
    bpy.ops.object.select_all(action="DESELECT")
    tree.select_set(True)
    bpy.context.view_layer.objects.active = tree
    tree.data.bevel_resolution = 1
    tree.data.resolution_u = 2
    bpy.ops.object.convert(target="MESH")
    bark = bpy.context.view_layer.objects.active
    # keep the silhouette, drop the triangles thin twigs do not need (target ~40k)
    bark_tris = sum(len(p.vertices) - 2 for p in bark.data.polygons)
    if bark_tris > 40000 and not tweaks.get("_nodecimate"):
        dec = bark.modifiers.new("dec", "DECIMATE")
        dec.ratio = max(0.4, 40000 / bark_tris)  # stronger decimation breaks thin twigs
        bpy.ops.object.modifier_apply(modifier="dec")
    if not bark.data.uv_layers:
        bark.data.uv_layers.new(name="UVMap")
    bark.data.uv_layers[0].name = "UVMap"
    bark.data.materials.clear()
    bark.data.materials.append(material(f"Bark_{species}", bark_color))
    parts = [bark]
    if leaves:
        tex = make_trees.make_single_leaf_texture(leaf_kind, leaf_color, seed=3)
        mw = leaves.matrix_world.copy()
        leaves.parent = None
        leaves.matrix_world = mw
        for layer in list(leaves.data.uv_layers)[1:]:
            leaves.data.uv_layers.remove(layer)
        leaves.data.uv_layers[0].name = "UVMap"  # same name as the bark's, so the join keeps the leaf UVs
        leaves.data.materials.clear()
        leaves.data.materials.append(material(f"Leaves_{leaf_kind}", leaf_color, tex))
        parts.append(leaves)
    for o in new:
        if o not in parts and o.name in bpy.data.objects:
            bpy.data.objects.remove(o, do_unlink=True)
    bpy.ops.object.select_all(action="DESELECT")
    for o in parts:
        o.select_set(True)
    bpy.context.view_layer.objects.active = bark
    bpy.ops.object.join()
    bark.name = name
    target = random.Random(f"h{species}{variant}").uniform(*HEIGHTS[species])
    k = target / max(bark.dimensions.z, 0.1)
    bark.scale = (k, k, k)
    bpy.ops.object.transform_apply(location=False, rotation=True, scale=True)
    tris = sum(len(p.vertices) - 2 for p in bark.data.polygons)
    leaf_polys = sum(1 for p in bark.data.polygons if p.material_index == 1)
    path = os.path.join(OUT, name + ".fbx")
    bpy.ops.export_scene.fbx(filepath=path, use_selection=True, apply_unit_scale=True, bake_space_transform=True, axis_forward="-Y", axis_up="Z",
                             mesh_smooth_type="FACE", path_mode="STRIP")
    print(f"TREE {name}: {tris} triangles ({leaf_polys} leaf cards), height {bark.dimensions.z:.1f} m -> {path}")
    return bark


def preview(objects, path):
    scene = bpy.context.scene
    x = 0.0
    for o in objects:
        o.location = (x, 0, 0)
        x += max(o.dimensions.x, 6) + 3
    bpy.ops.object.camera_add(location=(x / 2, -x * 0.9, 6), rotation=(math.radians(86), 0, 0))
    scene.camera = bpy.context.object
    scene.camera.data.lens = 35
    bpy.ops.object.light_add(type="SUN", rotation=(math.radians(50), 0, math.radians(30)))
    bpy.context.object.data.energy = 4
    world = bpy.data.worlds.new("w")
    world.use_nodes = True
    world.node_tree.nodes["Background"].inputs[0].default_value = (0.55, 0.7, 0.9, 1)
    world.node_tree.nodes["Background"].inputs[1].default_value = 0.8
    scene.world = world
    bpy.ops.mesh.primitive_plane_add(size=400, location=(x / 2, 0, 0))
    bpy.context.object.data.materials.append(material("Ground", (0.18, 0.22, 0.1)))
    scene.render.engine = "BLENDER_EEVEE"
    scene.render.resolution_x, scene.render.resolution_y = 1600, 700
    scene.render.filepath = path
    bpy.ops.render.render(write_still=True)
    print("PREVIEW", path)


def main():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    addon_utils.enable("bl_ext.blender_org.sapling_tree_gen", default_set=True)
    made = []
    for species in SPECIES:
        if ONLY and species not in ONLY:
            continue
        for v in range(VARIANTS):
            made.append(build(species, v))
    if PREVIEW and made:
        preview(made, os.path.join(OUT, "preview_trees.png"))
    print("DONE", len(made))


if __name__ == "__main__":
    main()

