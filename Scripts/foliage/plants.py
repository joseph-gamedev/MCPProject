"""Palms, bushes, ferns and grass in Blender (headless), exported for Unreal like the trees.

    blender -b --python Scripts/foliage/plants.py -- [out_dir] [kinds,...|all] [variants] [--preview]

Kinds: palm_coconut, palm_date, bush, boxwood, flower_bush, fern, grass, tall_grass, wildflowers.
Bushes use Sapling Tree Gen (dense, low, multi-stem); palms, ferns and grass are built here from curved strips
with alpha textures. Materials: Bark_<kind> and Leaves_<kind> (textures T_LeafSingle_<kind>.png), same as the trees.
"""

import math
import os
import random
import sys

import addon_utils
import bmesh
import bpy
import numpy as np
from mathutils import Vector

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
ARGS = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
PREVIEW = "--preview" in ARGS
ARGS = [a for a in ARGS if a != "--preview"]
OUT = os.path.abspath(ARGS[0]) if ARGS else os.path.abspath(os.path.join(HERE, "..", "..", "Saved", "Foliage"))
ONLY = ARGS[1].split(",") if len(ARGS) > 1 and ARGS[1] not in ("", "all") else None
VARIANTS = int(ARGS[2]) if len(ARGS) > 2 and ARGS[2].isdigit() else 3

import make_trees  # noqa: E402
import sapling_trees  # noqa: E402

make_trees.OUT = OUT
sapling_trees.OUT = OUT
os.makedirs(OUT, exist_ok=True)

# ------------------------------------------------------------------------------------------ textures


def tex_frond(kind, color, size=512):
    """Feather frond (coconut / date palm) or fern frond: rachis down the middle, leaflets both sides, V = along."""
    rng = random.Random(kind)
    img = np.zeros((size, size, 4), np.float32)
    c = np.array(color)
    ys, xs = np.mgrid[0:size, 0:size]
    rib = np.abs(xs - size / 2) < (4 if kind != "fern" else 2)
    img[rib, :3] = c * 0.7
    img[rib, 3] = 1
    n = 32 if kind != "fern" else 22  # leaflets per side, with gaps between them
    for i in range(n):
        y = size * (0.03 + 0.95 * i / n)
        env = math.sin(math.pi * min(1.0, 0.08 + 0.92 * i / n)) if kind == "fern" else math.sin(math.pi * (0.15 + 0.8 * i / n)) ** 0.6
        for side in (-1, 1):
            ln = size * 0.47 * env
            if ln < 6:
                continue
            ang = math.radians(side * (36 if kind != "fern" else 25))
            # one leaflet: thin pointed blade angled toward the tip
            ca, sa = math.cos(ang), math.sin(ang)
            u = ((xs - size / 2) * side * 1.0)
            v = (ys - y)
            along = u * math.cos(ang * side) + v * math.sin(ang * side)
            across = -u * math.sin(ang * side) + v * math.cos(ang * side)
            w = (size * (0.017 if kind != "fern" else 0.02)) * np.clip(1 - along / ln, 0, 1) ** 0.5
            m = (along > 0) & (along < ln) & (np.abs(across) < w)
            img[m, :3] = c * rng.uniform(0.85, 1.15)
            img[m, 3] = 1
    return make_trees._save_rgba(f"T_LeafSingle_{kind}", np.clip(img, 0, 1))


def tex_grass(kind, color, size=256):
    """Grass blades on a card: thin tapering blades rising from the bottom edge (V up)."""
    rng = random.Random(kind)
    img = np.zeros((size, size, 4), np.float32)
    c = np.array(color)
    ys, xs = np.mgrid[0:size, 0:size]
    h = 1 - ys / size  # 0 bottom, 1 top
    for _ in range(26 if kind == "grass" else 18):
        x0 = rng.uniform(size * 0.08, size * 0.92)
        top = rng.uniform(0.55, 1.0)
        lean = rng.uniform(-0.25, 0.25) * size
        cx = x0 + lean * h ** 2
        w = 3.2 * np.clip(1 - h / top, 0, 1) ** 0.6 + 0.4
        m = (np.abs(xs - cx) < w) & (h < top)
        shade = (0.65 + 0.5 * h) * rng.uniform(0.85, 1.15)
        img[m, :3] = (c * shade[..., None])[m]
        img[m, 3] = 1
    if kind == "wildflowers":
        for _ in range(9):
            fx, fy = rng.uniform(size * 0.15, size * 0.85), rng.uniform(size * 0.05, size * 0.4)
            petal = np.array(rng.choice([(0.95, 0.85, 0.2), (0.9, 0.9, 0.95), (0.85, 0.3, 0.55), (0.5, 0.4, 0.9)]))
            m = (xs - fx) ** 2 + (ys - fy) ** 2 < (size * 0.03) ** 2
            img[m, :3] = petal
            img[m, 3] = 1
            centre = (xs - fx) ** 2 + (ys - fy) ** 2 < (size * 0.01) ** 2
            img[centre, :3] = (0.9, 0.7, 0.1)
    return make_trees._save_rgba(f"T_LeafSingle_{kind}", np.clip(img, 0, 1))


def tex_flower_leaf(color, size=256):
    """Leaf card with a cluster of blossoms (flowering bush): mostly leaf, petals in the middle."""
    rng = random.Random("flower")
    img = np.zeros((size, size, 4), np.float32)
    make_trees._leaf_shape(img, size / 2, size / 2, size * 0.9, size * 0.5, math.pi / 2, np.array(color), rng)
    ys, xs = np.mgrid[0:size, 0:size]
    for _ in range(5):
        fx, fy = rng.uniform(size * 0.3, size * 0.7), rng.uniform(size * 0.25, size * 0.75)
        m = (xs - fx) ** 2 + (ys - fy) ** 2 < (size * 0.07) ** 2
        img[m, :3] = (0.92, 0.42, 0.62)
        img[m, 3] = 1
    return make_trees._save_rgba("T_LeafSingle_flower", np.clip(img, 0, 1))

# ------------------------------------------------------------------------------------------ geometry


def strip(bm, uv, path, width_fn, up, flip_v=False):
    """A ribbon along a path (list of Vectors), width_fn(t) half-width, oriented by 'up'; U across, V along."""
    prev = None
    n = len(path)
    for i, p in enumerate(path):
        t = i / (n - 1)
        d = (path[min(i + 1, n - 1)] - path[max(i - 1, 0)]).normalized()
        side = d.cross(up).normalized() * width_fn(t)
        l, r = bm.verts.new(p - side), bm.verts.new(p + side)
        if prev:
            f = bm.faces.new((prev[0], prev[1], r, l))
            t0 = (i - 1) / (n - 1)
            for loop, (u, v) in zip(f.loops, [(0, t0), (1, t0), (1, t), (0, t)]):
                loop[uv].uv = (u, 1 - v if flip_v else v)
        prev = (l, r)


def frond_path(base, direction, length, arch, droop, segs=7):
    pts = []
    for s in range(segs + 1):
        t = s / segs
        pts.append(base + direction * length * t + Vector((0, 0, length * (arch * t - droop * t * t))))
    return pts


def palm(kind, rng):
    h = rng.uniform(9, 14) if kind == "palm_coconut" else rng.uniform(6, 10)
    bend = rng.uniform(0.15, 0.4) if kind == "palm_coconut" else rng.uniform(0.0, 0.08)
    a = rng.uniform(0, 2 * math.pi)
    lean = Vector((math.cos(a), math.sin(a), 0))
    pts, radii = [], []
    for i in range(25):
        t = i / 24
        pts.append(lean * h * bend * t * t + Vector((0, 0, h * t)))
        base_r = 0.24 if kind == "palm_coconut" else 0.36
        radii.append(base_r * (1.25 - 0.35 * t) * (1 + 0.06 * math.sin(i * 2.7)))  # leaf-scar rings
    trunk = make_trees.bark_mesh([make_trees.Branch(pts, radii, 0)], kind + "_trunk")
    bm = bmesh.new()
    uv = bm.loops.layers.uv.new("UVMap")
    top = pts[-1]
    count = rng.randint(14, 18) if kind == "palm_coconut" else rng.randint(22, 28)
    for i in range(count):
        ang = 2 * math.pi * i / count + rng.uniform(-0.15, 0.15)
        tilt = rng.uniform(-0.1, 0.5)  # some fronds rise, some droop
        d = Vector((math.cos(ang), math.sin(ang), tilt)).normalized()
        ln = rng.uniform(4.0, 5.5) if kind == "palm_coconut" else rng.uniform(3.0, 4.2)  # real frond lengths (m)
        path = frond_path(top, Vector((d.x, d.y, 0)).normalized(), ln, arch=0.35 + tilt * 0.8, droop=rng.uniform(0.9, 1.3), segs=9)
        strip(bm, uv, path, lambda t, ln=ln: ln * 0.22 * math.sin(math.pi * min(1, 0.12 + t * 0.95)), Vector((0, 0, 1)))
    if kind == "palm_coconut":
        for k in range(rng.randint(3, 6)):  # coconuts
            c = top + Vector((rng.uniform(-0.25, 0.25), rng.uniform(-0.25, 0.25), -0.35))
            bmesh.ops.create_icosphere(bm, subdivisions=1, radius=0.13, matrix=__import__("mathutils").Matrix.Translation(c))
    leaves = bpy.data.meshes.new(kind + "_fronds")
    bm.to_mesh(leaves)
    bm.free()
    return trunk, leaves


def fern(rng):
    bm = bmesh.new()
    uv = bm.loops.layers.uv.new("UVMap")
    for i in range(rng.randint(10, 16)):
        a = 2 * math.pi * i / 13 + rng.uniform(-0.2, 0.2)
        d = Vector((math.cos(a), math.sin(a), 0))
        ln = rng.uniform(0.6, 1.1)
        path = frond_path(Vector((0, 0, 0.02)), d, ln, arch=rng.uniform(0.9, 1.4), droop=rng.uniform(1.0, 1.5))
        strip(bm, uv, path, lambda t: ln * 0.18 * math.sin(math.pi * min(1, 0.1 + t)), Vector((0, 0, 1)))
    me = bpy.data.meshes.new("fern")
    bm.to_mesh(me)
    bm.free()
    return None, me


def grass(kind, rng):
    """Clump of crossed, slightly bent grass cards (each card many blades in the texture)."""
    bm = bmesh.new()
    uv = bm.loops.layers.uv.new("UVMap")
    h = {"grass": (0.25, 0.45), "tall_grass": (0.7, 1.1), "wildflowers": (0.35, 0.6)}[kind]
    cards = {"grass": 7, "tall_grass": 9, "wildflowers": 7}[kind]
    for i in range(cards):
        a = math.pi * i / cards + rng.uniform(-0.2, 0.2)
        d = Vector((math.cos(a), math.sin(a), 0))
        off = Vector((rng.uniform(-0.12, 0.12), rng.uniform(-0.12, 0.12), 0))
        height = rng.uniform(*h)
        w = height * 0.55
        lean = Vector((-d.y, d.x, 0)) * rng.uniform(-0.15, 0.15)
        path = [off + Vector((0, 0, height * t)) + lean * t * t * height for t in (0, 0.33, 0.66, 1.0)]
        strip(bm, uv, path, lambda t: w / 2, d.cross(Vector((0, 0, 1))))
    me = bpy.data.meshes.new(kind)
    bm.to_mesh(me)
    bm.free()
    return None, me


BUSH_SAPLING = {  # kind -> (preset, overrides, leaf kind, leaf colour, height range m)
    "bush": ("small_maple", {"baseSize": 0.02, "baseSplits": 5, "ratio": 0.025, "_leaves": 3.0, "_leafScale": 2.2}, "broad", (0.22, 0.33, 0.10), (1.2, 2.2)),
    "boxwood": ("callistemon", {"baseSize": 0.0, "baseSplits": 6, "_leaves": 3.0, "_leafScale": 1.4}, "small", (0.16, 0.26, 0.08), (0.8, 1.4)),
    "flower_bush": ("small_maple", {"baseSize": 0.02, "baseSplits": 4, "ratio": 0.025, "_leaves": 2.5, "_leafScale": 2.2}, "flower", (0.24, 0.36, 0.12), (1.0, 1.8)),
}


def sapling_bush(kind, variant):
    preset, overrides, leaf_kind, leaf_color, heights = BUSH_SAPLING[kind]
    sapling_trees.SPECIES[kind] = (preset, overrides, leaf_kind, leaf_color, (0.18, 0.14, 0.1))
    sapling_trees.HEIGHTS[kind] = heights
    if leaf_kind == "flower":
        tex_flower_leaf(leaf_color)
        orig = make_trees.make_single_leaf_texture
        make_trees.make_single_leaf_texture = lambda k, c, seed=1, size=256: os.path.join(OUT, "T_LeafSingle_flower.png") if k == "flower" else orig(k, c, seed, size)
    ob = sapling_trees.build(kind, variant)
    ob.name = f"SM_{kind.title().replace('_', '')}_{variant + 1:02d}"
    return ob


def export(name, bark_me, leaf_me, leaf_kind, leaf_color, texture, bark_color=(0.18, 0.14, 0.1)):
    objs = []
    if bark_me:
        ob = bpy.data.objects.new(name, bark_me)
        ob.data.materials.append(make_trees.material(f"Bark_{leaf_kind}", bark_color))
        if not ob.data.uv_layers:
            ob.data.uv_layers.new(name="UVMap")
        objs.append(ob)
    lo = bpy.data.objects.new(name + "_l", leaf_me)
    lo.data.materials.append(make_trees.material(f"Leaves_{leaf_kind}", leaf_color, texture))
    objs.append(lo)
    for o in objs:
        bpy.context.scene.collection.objects.link(o)
    bpy.ops.object.select_all(action="DESELECT")
    for o in objs:
        o.select_set(True)
    bpy.context.view_layer.objects.active = objs[0]
    if len(objs) > 1:
        bpy.ops.object.join()
    main = bpy.context.view_layer.objects.active
    main.name = name
    path = os.path.join(OUT, name + ".fbx")
    bpy.ops.export_scene.fbx(filepath=path, use_selection=True, apply_unit_scale=True, bake_space_transform=True, axis_forward="-Y", axis_up="Z",
                             mesh_smooth_type="FACE", path_mode="STRIP")
    tris = sum(len(p.vertices) - 2 for p in main.data.polygons)
    print(f"PLANT {name}: {tris} triangles, height {main.dimensions.z:.2f} m")
    return main


def build(kind, variant):
    rng = random.Random(f"{kind}{variant}")
    name = f"SM_{kind.title().replace('_', '')}_{variant + 1:02d}"
    if kind in BUSH_SAPLING:
        return sapling_bush(kind, variant)
    if kind.startswith("palm"):
        color = (0.25, 0.36, 0.11) if kind == "palm_coconut" else (0.22, 0.30, 0.12)
        tex = tex_frond(kind, color)
        bark, leaves = palm(kind, rng)
        return export(name, bark, leaves, kind, color, tex, bark_color=(0.13, 0.10, 0.075))
    if kind == "fern":
        color = (0.18, 0.32, 0.09)
        tex = tex_frond("fern", color)
        _, leaves = fern(rng)
        return export(name, None, leaves, "fern", color, tex)
    color = {"grass": (0.24, 0.36, 0.10), "tall_grass": (0.42, 0.44, 0.18), "wildflowers": (0.26, 0.38, 0.11)}[kind]
    tex = tex_grass(kind, color)
    _, leaves = grass(kind, rng)
    return export(name, None, leaves, kind, color, tex)


KINDS = ["palm_coconut", "palm_date", "bush", "boxwood", "flower_bush", "fern", "grass", "tall_grass", "wildflowers"]


def main():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    addon_utils.enable("bl_ext.blender_org.sapling_tree_gen", default_set=True)
    made = []
    for kind in KINDS:
        if ONLY and kind not in ONLY:
            continue
        for v in range(VARIANTS):
            made.append(build(kind, v))
    if PREVIEW and made:
        sapling_trees.preview([m for m in made if m], os.path.join(OUT, "preview_plants.png"))
    print("DONE", len(made))


if __name__ == "__main__":
    main()
