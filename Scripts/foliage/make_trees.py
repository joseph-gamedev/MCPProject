"""Generates realistic tree meshes in Blender (headless) for the Unreal project.

    "C:/Program Files/Blender Foundation/Blender 5.2/blender.exe" -b --python Scripts/foliage/make_trees.py -- [out_dir] [species,...] [variants]

Each tree: tapered, curved branch skeleton (several levels, phototropism, gravity) as one bark mesh, plus leaf-cluster
cards (alpha-textured quads) at the branch ends. Two material slots, "Bark" and "Leaves_<kind>", and the leaf
textures (PNG with alpha) are written next to the FBX files. Units: metres in Blender, exported for Unreal (cm).
"""

import math
import os
import random
import sys

import bpy
import bmesh
import numpy as np
from mathutils import Matrix, Quaternion, Vector

ARGS = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
OUT = os.path.abspath(ARGS[0]) if ARGS else os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "Saved", "Foliage"))
ONLY = ARGS[1].split(",") if len(ARGS) > 1 and ARGS[1] else None
VARIANTS = int(ARGS[2]) if len(ARGS) > 2 and ARGS[2].isdigit() else 3
os.makedirs(OUT, exist_ok=True)

# --------------------------------------------------------------------------------------------- species
# levels: per branch level (0 = trunk): length (fraction of parent or metres for trunk), radius ratio, children count,
# down angle (deg from parent), curve (deg over length), start (fraction of parent where children begin)
SPECIES = {
    "oak": dict(height=(14, 19), trunk_r=0.32, leaf="broad", leaf_color=(0.20, 0.30, 0.09), card=1.3, cards_per_tip=5, gravity=0.06, up=0.1,
                levels=[dict(len=0.45, r=1.0, n=7, down=0, curve=8, start=0.0),
                        dict(len=0.62, r=0.42, n=6, down=55, curve=-25, start=0.35),
                        dict(len=0.5, r=0.5, n=5, down=45, curve=20, start=0.15),
                        dict(len=0.45, r=0.55, n=4, down=40, curve=15, start=0.1)]),
    "maple": dict(height=(12, 17), trunk_r=0.26, leaf="broad", leaf_color=(0.24, 0.34, 0.10), card=1.2, cards_per_tip=5, gravity=0.02, up=0.25,
                  levels=[dict(len=0.5, r=1.0, n=6, down=0, curve=5, start=0.0),
                          dict(len=0.55, r=0.4, n=6, down=40, curve=-15, start=0.4),
                          dict(len=0.5, r=0.5, n=5, down=40, curve=15, start=0.2),
                          dict(len=0.45, r=0.55, n=4, down=35, curve=10, start=0.1)]),
    "birch": dict(height=(12, 16), trunk_r=0.16, leaf="small", leaf_color=(0.33, 0.42, 0.14), card=0.9, cards_per_tip=4, gravity=0.25, up=0.0,
                  bark="birch",
                  levels=[dict(len=0.85, r=1.0, n=14, down=0, curve=6, start=0.0),
                          dict(len=0.32, r=0.3, n=6, down=55, curve=-10, start=0.3),
                          dict(len=0.55, r=0.5, n=5, down=45, curve=25, start=0.15),
                          dict(len=0.5, r=0.5, n=3, down=40, curve=25, start=0.1)]),
    "pine": dict(height=(16, 24), trunk_r=0.28, leaf="needle", leaf_color=(0.14, 0.24, 0.10), card=1.1, cards_per_tip=6, gravity=0.15, up=0.05,
                 conifer=True, crown_start=0.55,
                 levels=[dict(len=1.0, r=1.0, n=16, down=0, curve=4, start=0.55),
                         dict(len=0.25, r=0.28, n=6, down=70, curve=20, start=0.15),
                         dict(len=0.45, r=0.5, n=4, down=40, curve=-10, start=0.2)]),
    "spruce": dict(height=(14, 22), trunk_r=0.24, leaf="needle", leaf_color=(0.10, 0.20, 0.10), card=0.9, cards_per_tip=5, gravity=0.35, up=0.0,
                   conifer=True, crown_start=0.05,
                   levels=[dict(len=1.0, r=1.0, n=34, down=0, curve=2, start=0.05),
                           dict(len=0.3, r=0.22, n=7, down=78, curve=-15, start=0.1),
                           dict(len=0.4, r=0.5, n=4, down=45, curve=0, start=0.2)]),
    "palm": dict(height=(8, 13), trunk_r=0.17, leaf="frond", leaf_color=(0.26, 0.36, 0.12), palm=True),
    "cypress": dict(height=(10, 14), trunk_r=0.18, leaf="needle", leaf_color=(0.12, 0.22, 0.10), card=0.8, cards_per_tip=6, gravity=-0.2, up=0.5,
                    conifer=True, crown_start=0.08,
                    levels=[dict(len=1.0, r=1.0, n=40, down=0, curve=2, start=0.08),
                            dict(len=0.14, r=0.25, n=5, down=20, curve=5, start=0.2),
                            dict(len=0.5, r=0.5, n=3, down=20, curve=5, start=0.2)]),
    "willow": dict(height=(10, 13), trunk_r=0.3, leaf="willow", leaf_color=(0.36, 0.45, 0.16), card=1.6, cards_per_tip=5, gravity=0.9, up=0.0,
                   levels=[dict(len=0.35, r=1.0, n=7, down=0, curve=10, start=0.0),
                           dict(len=0.6, r=0.42, n=7, down=50, curve=-20, start=0.4),
                           dict(len=0.75, r=0.5, n=5, down=30, curve=40, start=0.2),
                           dict(len=0.8, r=0.5, n=3, down=25, curve=30, start=0.1)]),
    "shrub": dict(height=(1.2, 2.2), trunk_r=0.04, leaf="small", leaf_color=(0.22, 0.32, 0.10), card=0.5, cards_per_tip=4, gravity=0.0, up=0.2,
                  levels=[dict(len=0.15, r=1.0, n=9, down=0, curve=0, start=0.0),
                          dict(len=5.0, r=0.6, n=5, down=45, curve=15, start=0.0),
                          dict(len=0.5, r=0.5, n=4, down=35, curve=10, start=0.2)]),
}

# --------------------------------------------------------------------------------------------- leaf textures


def _save_rgba(name, rgba):
    h, w, _ = rgba.shape
    img = bpy.data.images.new(name, w, h, alpha=True)
    img.pixels.foreach_set(np.flipud(rgba).astype(np.float32).ravel())
    path = os.path.join(OUT, name + ".png")
    img.filepath_raw = path
    img.file_format = "PNG"
    img.save()
    return path


def _leaf_shape(img, cx, cy, length, width, angle, color, rng, vein=True):
    """Draws one leaf (pointed ellipse with a midrib) into an RGBA float image."""
    h, w, _ = img.shape
    ys, xs = np.mgrid[0:h, 0:w]
    ca, sa = math.cos(angle), math.sin(angle)
    u = ((xs - cx) * ca + (ys - cy) * sa) / length          # along the leaf, -0.5..0.5
    v = (-(xs - cx) * sa + (ys - cy) * ca) / width           # across
    profile = np.clip(1 - (2 * u) ** 2, 0, 1) ** 0.75 * (1 - 0.35 * np.clip(u * 2, 0, 1))
    inside = (np.abs(v) * 2 < profile) & (np.abs(u) < 0.5)
    shade = 0.82 + 0.18 * (1 - np.abs(v) * 2) + rng.uniform(-0.08, 0.08)
    col = np.array(color) * shade[..., None]
    if vein:
        rib = (np.abs(v) < 0.04) & inside
        col[rib] *= 1.35
    img[inside, :3] = col[inside]
    img[inside, 3] = 1.0
    # stem
    stem = (np.abs(v) < 0.02) & (u < -0.5) & (u > -0.65)
    img[stem, :3] = np.array(color) * 0.6
    img[stem, 3] = 1.0


def make_single_leaf_texture(kind, color, seed=1, size=256):
    """One leaf filling the card (for generators that place individual leaves, like Sapling)."""
    rng = random.Random(seed)
    img = np.zeros((size, size, 4), np.float32)
    c = np.array(color)
    if kind == "needle":
        ys, xs = np.mgrid[0:size, 0:size]
        twig = (np.abs(xs - size / 2) < 3) & (ys > size * 0.03)
        img[twig, :3] = c * 0.55
        img[twig, 3] = 1
        for k in range(70):  # dense needle spray along a twig: reads as a solid mass at distance
            y0 = size * rng.uniform(0.05, 0.9)
            a = math.radians(rng.choice([1, -1]) * rng.uniform(25, 70) + 90)
            d = np.abs((xs - size / 2) * math.sin(a) - (ys - y0) * math.cos(a))
            t = (xs - size / 2) * math.cos(a) + (ys - y0) * math.sin(a)
            m = (d < 3.0) & (t > 0) & (t < size * rng.uniform(0.25, 0.45))
            img[m, :3] = c * rng.uniform(0.8, 1.15)
            img[m, 3] = 1
    else:
        width = {"broad": 0.62, "small": 0.5, "willow": 0.16}.get(kind, 0.5)
        _leaf_shape(img, size / 2, size / 2, size * 0.92, size * 0.92 * width, math.pi / 2, c, rng)
    return _save_rgba(f"T_LeafSingle_{kind}", np.clip(img, 0, 1))


def make_leaf_texture(kind, color, seed=1, size=512):
    """A leaf-cluster card: many leaves around a twig, on transparent background."""
    rng = random.Random(seed)
    img = np.zeros((size, size, 4), np.float32)
    c = np.array(color)
    if kind == "needle":
        for _ in range(260):
            a = rng.uniform(-math.pi, math.pi)
            r0 = rng.uniform(0, size * 0.12)
            x0, y0 = size / 2 + math.cos(a) * r0, size / 2 + math.sin(a) * r0
            ln = rng.uniform(size * 0.18, size * 0.42)
            ys, xs = np.mgrid[0:size, 0:size]
            d = np.abs((xs - x0) * math.sin(a) - (ys - y0) * math.cos(a))
            t = (xs - x0) * math.cos(a) + (ys - y0) * math.sin(a)
            m = (d < 1.6) & (t > 0) & (t < ln)
            img[m, :3] = c * rng.uniform(0.75, 1.2)
            img[m, 3] = 1
    elif kind == "frond":
        ys, xs = np.mgrid[0:size, 0:size]
        rib = (np.abs(xs - size / 2) < 3)
        img[rib, :3] = c * 0.8
        img[rib, 3] = 1
        for i in range(48):
            y = size * (0.04 + 0.92 * i / 48)
            for side in (-1, 1):
                ln = size * 0.42 * math.sin(math.pi * (0.1 + 0.85 * i / 48))
                _leaf_shape(img, size / 2 + side * ln / 2, y, ln, size * 0.035, math.radians(side * 12) + (0 if side > 0 else math.pi), c * rng.uniform(0.85, 1.1), rng, vein=False)
    elif kind == "willow":
        for _ in range(70):
            x, y = rng.uniform(size * 0.15, size * 0.85), rng.uniform(size * 0.05, size * 0.95)
            _leaf_shape(img, x, y, size * rng.uniform(0.12, 0.2), size * 0.025, math.radians(rng.uniform(80, 100)), c * rng.uniform(0.85, 1.15), rng, vein=False)
    else:
        n, ln = (34, 0.2) if kind == "broad" else (60, 0.13)
        for _ in range(n):
            a = rng.uniform(0, 2 * math.pi)
            r = rng.uniform(0, size * 0.3)
            x, y = size / 2 + math.cos(a) * r, size / 2 + math.sin(a) * r
            _leaf_shape(img, x, y, size * ln * rng.uniform(0.8, 1.2), size * ln * rng.uniform(0.45, 0.6), a + rng.uniform(-0.5, 0.5),
                        c * rng.uniform(0.8, 1.2), rng)
    return _save_rgba(f"T_Leaf_{kind}", np.clip(img, 0, 1))

# --------------------------------------------------------------------------------------------- skeleton


class Branch:
    def __init__(self, points, radii, level):
        self.points, self.radii, self.level = points, radii, level


def grow(start, direction, length, radius, level, sp, rng, out, tips):
    """Grows one branch as a polyline (gravity, phototropism, curve, noise), then its children."""
    segs = max(4, int(length * (8 if level == 0 else 5)))
    lv = sp["levels"][level]
    pts, rads = [Vector(start)], []
    d = Vector(direction).normalized()
    axis = d.orthogonal().normalized()
    bend = math.radians(lv["curve"]) / segs
    for i in range(segs):
        d = (Quaternion(axis, bend) @ d)
        d.z += (sp.get("up", 0) - sp.get("gravity", 0) * (level > 0)) / segs
        d += Vector((rng.uniform(-1, 1), rng.uniform(-1, 1), rng.uniform(-1, 1))) * 0.04
        d.normalize()
        pts.append(pts[-1] + d * (length / segs))
    for i in range(len(pts)):
        f = i / (len(pts) - 1)
        rads.append(max(0.006, radius * (1 - f * 0.85) ** 1.1))
    out.append(Branch(pts, rads, level))
    if level + 1 >= len(sp["levels"]) or radius < 0.008:
        tips.append((pts[-1], d))
        for p in pts[len(pts) // 2:-1]:
            if rng.random() < 0.35:
                tips.append((p, d))
        return
    child = sp["levels"][level + 1]
    n = child["n"]
    for k in range(n):
        f = child["start"] + (1 - child["start"]) * (k + rng.uniform(0.1, 0.9)) / n
        idx = min(len(pts) - 2, int(f * (len(pts) - 1)))
        base = pts[idx]
        pd = (pts[idx + 1] - pts[idx]).normalized()
        roll = k * 2.39996 + rng.uniform(-0.3, 0.3)          # golden angle spiral
        side = pd.orthogonal().normalized()
        side = Quaternion(pd, roll) @ side
        down = math.radians(child["down"] + rng.uniform(-10, 10))
        nd = (pd * math.cos(down) + side * math.sin(down)).normalized()
        clen = length * child["len"] * (1 - 0.55 * f if level > 0 or sp.get("conifer") else 1) * rng.uniform(0.8, 1.15)
        if sp.get("conifer") and level == 0:
            clen = sp["height"][1] * child["len"] * (1 - f) ** 0.9 + 0.3
        grow(base, nd, clen, rads[idx] * child["r"] * 1.6, level + 1, sp, rng, out, tips)


def bark_mesh(branches, name):
    """All branches as one mesh: rings along each polyline, joined into tubes (8 sides trunk, fewer for twigs)."""
    bm = bmesh.new()
    for b in branches:
        sides = 10 if b.level == 0 else (6 if b.level == 1 else 4)
        rings = []
        prev_n = None
        for i, p in enumerate(b.points):
            t = (b.points[min(i + 1, len(b.points) - 1)] - b.points[max(i - 1, 0)]).normalized()
            n = prev_n if prev_n is not None else t.orthogonal().normalized()
            n = (n - t * n.dot(t)).normalized()
            prev_n = n
            bn = t.cross(n)
            r = b.radii[i]
            ring = []
            for s in range(sides):
                a = 2 * math.pi * s / sides
                flare = 1 + (0.35 * max(0, 1 - i / 2) if b.level == 0 else 0)
                ring.append(bm.verts.new(p + (n * math.cos(a) + bn * math.sin(a)) * r * flare))
            rings.append(ring)
        for i in range(len(rings) - 1):
            for s in range(sides):
                a, bb = rings[i][s], rings[i][(s + 1) % sides]
                c, d = rings[i + 1][(s + 1) % sides], rings[i + 1][s]
                bm.faces.new((a, bb, c, d))
        bm.faces.new(list(reversed(rings[0]))) if b.level == 0 else None
    me = bpy.data.meshes.new(name)
    bm.normal_update()
    bm.to_mesh(me)
    bm.free()
    for poly in me.polygons:
        poly.use_smooth = True
    return me


def leaf_mesh(tips, sp, rng, name):
    """Leaf-cluster cards: crossed quads at branch tips, facing outward/upward with random roll, UV 0-1."""
    bm = bmesh.new()
    uv = bm.loops.layers.uv.new("UVMap")
    size = sp.get("card", 1.0)
    for pos, d in tips:
        for k in range(sp.get("cards_per_tip", 4)):
            s = size * rng.uniform(0.7, 1.25)
            c = pos + Vector((rng.uniform(-1, 1), rng.uniform(-1, 1), rng.uniform(-0.6, 0.8))) * size * 0.45
            normal = (Vector((rng.uniform(-1, 1), rng.uniform(-1, 1), rng.uniform(-0.2, 1.2))) + d * 0.5).normalized()
            if sp["leaf"] == "willow":
                normal = Vector((rng.uniform(-1, 1), rng.uniform(-1, 1), 0)).normalized()
            tangent = normal.orthogonal().normalized()
            tangent = Quaternion(normal, rng.uniform(0, 2 * math.pi)) @ tangent
            bit = normal.cross(tangent)
            if sp["leaf"] == "willow":
                tangent, bit = Vector((0, 0, -1)), normal.cross(Vector((0, 0, -1)))
                c = c + Vector((0, 0, -s * 0.5))
            h = s * (1.6 if sp["leaf"] == "willow" else 1.0) / 2
            corners = [c - tangent * h - bit * s / 2, c + tangent * h - bit * s / 2, c + tangent * h + bit * s / 2, c - tangent * h + bit * s / 2]
            vs = [bm.verts.new(v) for v in corners]
            f = bm.faces.new(vs)
            for loop, (u, v) in zip(f.loops, [(0, 0), (1, 0), (1, 1), (0, 1)]):
                loop[uv].uv = (u, v)
    me = bpy.data.meshes.new(name)
    bm.normal_update()
    bm.to_mesh(me)
    bm.free()
    return me


def palm_meshes(sp, rng, name):
    h = rng.uniform(*sp["height"])
    lean = Vector((rng.uniform(-0.25, 0.25), rng.uniform(-0.25, 0.25), 1)).normalized()
    pts = [Vector((0, 0, 0))]
    d = lean
    for i in range(16):
        d = (d + Vector((rng.uniform(-0.03, 0.03), rng.uniform(-0.03, 0.03), 0.05))).normalized()
        pts.append(pts[-1] + d * h / 16)
    radii = [sp["trunk_r"] * (1.3 - 0.4 * i / 16) * (1 + 0.08 * (i % 2)) for i in range(17)]
    trunk = bark_mesh([Branch(pts, radii, 0)], name + "_bark")
    bm = bmesh.new()
    uv = bm.loops.layers.uv.new("UVMap")
    top = pts[-1]
    for i in range(rng.randint(12, 16)):
        a = 2 * math.pi * i / 14 + rng.uniform(-0.2, 0.2)
        out = Vector((math.cos(a), math.sin(a), 0))
        ln = h * rng.uniform(0.35, 0.45)
        segs = 6
        prev = None
        for sgi in range(segs + 1):
            t = sgi / segs
            p = top + out * ln * t + Vector((0, 0, ln * (0.45 * t - 0.85 * t * t) + rng.uniform(-0.05, 0.05)))
            side = out.cross(Vector((0, 0, 1))).normalized() * ln * 0.18 * math.sin(math.pi * min(1, t * 1.2 + 0.05))
            l, r = bm.verts.new(p - side), bm.verts.new(p + side)
            if prev:
                f = bm.faces.new((prev[0], prev[1], r, l))
                for loop, (uu, vv) in zip(f.loops, [(0, (sgi - 1) / segs), (1, (sgi - 1) / segs), (1, t), (0, t)]):
                    loop[uv].uv = (uu, vv)
            prev = (l, r)
    leaves = bpy.data.meshes.new(name + "_leaves")
    bm.normal_update()
    bm.to_mesh(leaves)
    bm.free()
    return trunk, leaves

# --------------------------------------------------------------------------------------------- materials + export


def material(name, color, texture=None):
    m = bpy.data.materials.get(name) or bpy.data.materials.new(name)
    m.use_nodes = True
    bsdf = m.node_tree.nodes.get("Principled BSDF")
    bsdf.inputs["Base Color"].default_value = (*color, 1)
    if texture:
        tex = m.node_tree.nodes.new("ShaderNodeTexImage")
        tex.image = bpy.data.images.load(texture, check_existing=True)
        m.node_tree.links.new(tex.outputs["Color"], bsdf.inputs["Base Color"])
        m.node_tree.links.new(tex.outputs["Alpha"], bsdf.inputs["Alpha"])
        m.blend_method = "CLIP" if hasattr(m, "blend_method") else None
    return m


def build(species, variant):
    sp = SPECIES[species]
    rng = random.Random(f"{species}-{variant}")
    name = f"SM_{species.capitalize()}_{variant + 1:02d}"
    tex = make_leaf_texture(sp["leaf"], sp["leaf_color"], seed=3)
    if sp.get("palm"):
        bark, leaves = palm_meshes(sp, rng, name)
    else:
        h = rng.uniform(*sp["height"])
        branches, tips = [], []
        lean = Vector((rng.uniform(-0.06, 0.06), rng.uniform(-0.06, 0.06), 1)).normalized()
        trunk_len = h * sp["levels"][0]["len"] if not sp.get("conifer") else h
        grow((0, 0, 0), lean, trunk_len, sp["trunk_r"] * rng.uniform(0.9, 1.1), 0, dict(sp, height=(h, h)), rng, branches, tips)
        bark = bark_mesh(branches, name + "_bark")
        leaves = leaf_mesh(tips, sp, rng, name + "_leaves")
    ob_b = bpy.data.objects.new(name, bark)
    ob_l = bpy.data.objects.new(name + "_l", leaves)
    for ob in (ob_b, ob_l):
        bpy.context.scene.collection.objects.link(ob)
    bark_color = (0.62, 0.6, 0.55) if sp.get("bark") == "birch" else (0.16, 0.12, 0.09)
    ob_b.data.materials.append(material("Bark_" + (sp.get("bark") or "default"), bark_color))
    ob_l.data.materials.append(material("Leaves_" + sp["leaf"], sp["leaf_color"], tex))
    bpy.ops.object.select_all(action="DESELECT")
    ob_b.select_set(True)
    ob_l.select_set(True)
    bpy.context.view_layer.objects.active = ob_b
    bpy.ops.object.join()
    path = os.path.join(OUT, name + ".fbx")
    bpy.ops.export_scene.fbx(filepath=path, use_selection=True, apply_unit_scale=True, global_scale=1.0, bake_space_transform=True,
                             axis_forward="-Y", axis_up="Z", mesh_smooth_type="FACE", path_mode="STRIP")
    tris = sum(len(p.vertices) - 2 for p in ob_b.data.polygons)
    print(f"TREE {name}: {tris} triangles -> {path}")
    return ob_b


def main():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    made = []
    for species in SPECIES:
        if ONLY and species not in ONLY:
            continue
        for v in range(VARIANTS):
            made.append(build(species, v))
    print("DONE", len(made))


if __name__ == "__main__":
    main()
