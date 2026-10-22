"""Modular architecture pieces in Blender (headless): Archimesh doors / windows / steps + our own classical parts.

    blender -b --python Scripts/architecture/make_modules.py -- [out_dir] [--preview]

Every piece is real scale (metres here, cm in Unreal). Wall pieces (windows, doors, mouldings, balcony, pediment)
have their pivot on the wall plane at the bottom centre and sit in front of it, facing -Y (outside): place them at
the front face of the wall. Free-standing pieces (columns, balustrade, chimney, dormer, steps) pivot at the bottom. Material slots are named Arch_<kind>: wood, stone, marble,
brick, roof_tile, glass, metal_dark, paint_white, paint_dark; the Unreal import maps them to scanned materials.
"""

import math
import os
import sys

import addon_utils
import bmesh
import bpy
from mathutils import Matrix, Vector

ARGS = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
PREVIEW = "--preview" in ARGS
ARGS = [a for a in ARGS if a != "--preview"]
OUT = os.path.abspath(ARGS[0]) if ARGS else os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "Saved", "Architecture", "Modules"))
os.makedirs(OUT, exist_ok=True)

COLORS = {"wood": (0.25, 0.15, 0.08), "stone": (0.62, 0.58, 0.5), "marble": (0.85, 0.84, 0.8), "brick": (0.4, 0.17, 0.11), "roof_tile": (0.45, 0.2, 0.12),
          "glass": (0.3, 0.4, 0.45), "metal_dark": (0.03, 0.03, 0.035), "paint_white": (0.82, 0.81, 0.78), "paint_dark": (0.05, 0.08, 0.07)}


def mat(kind):
    name = "Arch_" + kind
    m = bpy.data.materials.get(name) or bpy.data.materials.new(name)
    m.diffuse_color = (*COLORS[kind], 1)
    if m.node_tree and m.node_tree.nodes.get("Principled BSDF"):
        m.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value = (*COLORS[kind], 1)
    return m


def reset():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    addon_utils.enable("bl_ext.blender_org.archimesh", default_set=True)


def finish(name, objs, remap=None, rotate_z=0.0):
    """Join meshes, map materials to Arch_ slots, pivot at bottom centre of the front face, export."""
    objs = [o for o in objs if o.type == "MESH" and not o.name.startswith("CTRL_")]
    for o in list(bpy.data.objects):
        if o.name.startswith("CTRL_") or o.type == "EMPTY":
            bpy.data.objects.remove(o, do_unlink=True)
    for o in objs:
        for slot in o.material_slots:
            src = slot.material.name if slot.material else ""
            kind = next((v for k, v in (remap or {}).items() if k.lower() in src.lower()), None)
            if kind:
                slot.material = mat(kind)
    bpy.ops.object.select_all(action="DESELECT")
    for o in objs:
        o.select_set(True)
    bpy.context.view_layer.objects.active = objs[0]
    bpy.ops.object.parent_clear(type="CLEAR_KEEP_TRANSFORM")
    if len(objs) > 1:
        bpy.ops.object.join()
    ob = bpy.context.view_layer.objects.active
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    # Built facing -Y in Blender; the FBX import into Unreal mirrors Y, so turn 180 degrees to face -Y (outside) there.
    ob.data.transform(Matrix.Rotation(math.radians(180 + rotate_z), 4, "Z"))
    vs = [v.co for v in ob.data.vertices]
    shift = Vector(((min(v.x for v in vs) + max(v.x for v in vs)) / 2, min(v.y for v in vs), min(v.z for v in vs)))
    ob.data.transform(Matrix.Translation(-shift))
    ob.name = name
    for p in ob.data.polygons:
        p.use_smooth = False
    path = os.path.join(OUT, name + ".fbx")
    bpy.ops.export_scene.fbx(filepath=path, use_selection=True, apply_unit_scale=True, bake_space_transform=True, axis_forward="-Y", axis_up="Z",
                             mesh_smooth_type="FACE")
    print(f"MODULE {name}: {len(ob.data.polygons)} faces, {ob.dimensions.x:.2f} x {ob.dimensions.y:.2f} x {ob.dimensions.z:.2f} m")
    return ob

# ------------------------------------------------------------------------------------- Archimesh pieces


def door(model, name):
    reset()
    bpy.ops.mesh.archimesh_door()
    root = next(o for o in bpy.data.objects if o.name.startswith("Door") and hasattr(o, "DoorObjectGenerator") and len(o.DoorObjectGenerator))
    g = root.DoorObjectGenerator[0]
    try:
        g.model = model
    except Exception:
        pass
    bpy.context.view_layer.update()
    return finish(name, list(bpy.data.objects), {"Door": "wood", "Handle": "metal_dark"})


def window(width, height, name, panes="1"):
    reset()
    bpy.ops.mesh.archimesh_window()
    root = next((o for o in bpy.data.objects if hasattr(o, "WindowObjectGenerator") and len(o.WindowObjectGenerator)), None)
    if root:
        g = root.WindowObjectGenerator[0]
        for attr, val in (("width", width), ("depth", 0.1), ("height", height)):
            try:
                setattr(g, attr, val)
            except Exception:
                pass
        bpy.context.view_layer.update()
    return finish(name, list(bpy.data.objects), {"Window_material": "paint_white", "Glass": "glass", "Sill": "stone", "Handle": "metal_dark"})


def stairs(steps, width, name):
    reset()
    bpy.ops.mesh.archimesh_stairs(step_num=steps, max_width=width, min_width=width, depth=0.3, height=0.17, thickness=0.03, model="1")
    return finish(name, list(bpy.data.objects), {"Stairs": "stone"})

# ------------------------------------------------------------------------------------- our classical pieces


def lathe(profile, segments=24, name="lathe"):
    """profile [(radius, z)] spun around Z."""
    bm = bmesh.new()
    rings = []
    for r, z in profile:
        rings.append([bm.verts.new((r * math.cos(2 * math.pi * i / segments), r * math.sin(2 * math.pi * i / segments), z)) for i in range(segments)])
    for a, b in zip(rings, rings[1:]):
        for i in range(segments):
            bm.faces.new((a[i], a[(i + 1) % segments], b[(i + 1) % segments], b[i]))
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    return me


def box(size, at, kind):
    me = bpy.data.meshes.new("box")
    bm = bmesh.new()
    bmesh.ops.create_cube(bm, size=1.0)
    bmesh.ops.scale(bm, vec=Vector(size), verts=bm.verts)
    bmesh.ops.translate(bm, vec=Vector(at) + Vector((0, 0, size[2] / 2)), verts=bm.verts)
    bm.to_mesh(me)
    bm.free()
    ob = bpy.data.objects.new("box", me)
    bpy.context.scene.collection.objects.link(ob)
    ob.data.materials.append(mat(kind))
    return ob


def obj(me, kind, at=(0, 0, 0)):
    ob = bpy.data.objects.new(me.name, me)
    bpy.context.scene.collection.objects.link(ob)
    ob.location = at
    ob.data.materials.append(mat(kind))
    return ob


def column(order, height, name):
    """Tuscan / Doric column: plinth, torus base, tapering shaft (entasis), echinus and abacus."""
    reset()
    r = height / 14.0  # classical proportion: diameter = height / 7
    shaft = [(r, 0.0)]
    for i in range(1, 11):
        t = i / 10
        shaft.append((r * (1 - 0.17 * max(0, t - 0.33) / 0.67) * (1 + 0.02 * math.sin(math.pi * t)), t * height * 0.86))
    base = [(r * 1.45, 0), (r * 1.45, r * 0.25), (r * 1.3, r * 0.3), (r * 1.32, r * 0.45), (r * 1.15, r * 0.6), (r * 1.05, r * 0.65)]
    cap = [(r * 0.86, 0), (r * 0.9, r * 0.1), (r * 1.05, r * 0.3), (r * 1.25, r * 0.5), (r * 1.25, r * 0.55)]
    parts = [box((r * 3.0, r * 3.0, r * 0.4), (0, 0, 0), "stone"),
             obj(lathe(base, 28, "base"), "stone", (0, 0, r * 0.4)),
             obj(lathe(shaft, 28 if order == "tuscan" else 40, "shaft"), "stone", (0, 0, r * 1.05)),
             obj(lathe(cap, 28, "cap"), "stone", (0, 0, r * 1.05 + height * 0.86)),
             box((r * 2.7, r * 2.7, r * 0.35), (0, 0, r * 1.6 + height * 0.86), "stone")]
    ob = finish(name, parts)
    k = height / ob.dimensions.z
    ob.data.transform(Matrix.Scale(k, 4))
    return ob


def baluster_profile(h):
    return [(0.075, 0), (0.075, 0.05), (0.055, 0.07), (0.05, 0.12), (0.075, 0.3), (0.085, 0.42), (0.06, 0.55), (0.04, 0.62), (0.05, 0.66),
            (0.065, 0.7), (0.065, 0.74)]


def balustrade(length, name):
    reset()
    h = 0.95
    parts = [box((length, 0.24, 0.12), (0, 0.12, 0), "stone"), box((length + 0.06, 0.3, 0.12), (0, 0.12, h - 0.12), "stone")]
    prof = [(rr, z * (h - 0.24) / 0.74) for rr, z in baluster_profile(h)]
    n = int(length / 0.22)
    for i in range(n):
        x = -length / 2 + (i + 0.5) * length / n
        parts.append(obj(lathe(prof, 16, "bal"), "stone", (x, 0.12, 0.12)))
    return finish(name, parts)


def iron_railing(length, name):
    reset()
    h = 1.05
    parts = [box((length, 0.04, 0.04), (0, 0.02, h - 0.04), "metal_dark"), box((length, 0.03, 0.03), (0, 0.02, 0.08), "metal_dark")]
    n = int(length / 0.12)
    for i in range(n + 1):
        x = -length / 2 + i * length / n
        parts.append(box((0.018, 0.018, h - 0.08), (x, 0.02, 0.06), "metal_dark"))
        if i % 4 == 0:
            parts.append(obj(lathe([(0.0, 0.0), (0.03, 0.0), (0.0, 0.06)], 8, "finial"), "metal_dark", (x, 0.02, h)))
    return finish(name, parts)


def balcony(width, name):
    reset()
    depth = 0.9
    parts = [box((width, depth, 0.14), (0, -depth / 2, 0), "stone")]
    for x in (-width / 2 + 0.15, 0, width / 2 - 0.15):  # stone consoles under the slab
        parts.append(box((0.14, depth * 0.75, 0.24), (x, -depth * 0.375, -0.24), "stone"))
    n = int(width / 0.12)
    for i in range(n + 1):
        parts.append(box((0.018, 0.018, 0.95), (-width / 2 + i * width / n, -depth + 0.04, 0.14), "metal_dark"))
    parts.append(box((width, 0.04, 0.04), (0, -depth + 0.04, 1.06), "metal_dark"))
    for side in (-1, 1):
        for j in range(int(depth / 0.12)):
            parts.append(box((0.018, 0.018, 0.95), (side * (width / 2 - 0.02), -0.06 - j * 0.12, 0.14), "metal_dark"))
        parts.append(box((0.04, depth, 0.04), (side * (width / 2 - 0.02), -depth / 2, 1.06), "metal_dark"))
    return finish(name, parts)


def window_classic(w, h, name, panes=(3, 2), shutters=False, surround="stone"):
    """Sash window: stone surround (architrave), projecting sill, lintel with keystone, painted frame, two sashes
    with glazing bars (panes = columns, rows per sash), dark glass; optional louvred shutters."""
    reset()
    t, d = 0.06, 0.05  # frame bar width, frame depth (in front of the wall)
    parts = [box((w, 0.02, h), (0, -0.01, 0), "glass")]
    for x in (-w / 2 + t / 2, w / 2 - t / 2):
        parts.append(box((t, d, h), (x, -d / 2, 0), "paint_white"))
    for z in (0, h - t, h / 2 - t / 2):
        parts.append(box((w, d, t), (0, -d / 2, z), "paint_white"))
    cols, rows = panes
    for half in (0, 1):
        z0, z1 = (t, h / 2 - t / 2) if half == 0 else (h / 2 + t / 2, h - t)
        for i in range(1, cols):
            parts.append(box((0.022, d * 0.8, z1 - z0), (-w / 2 + t + i * (w - 2 * t) / cols, -d * 0.4, z0), "paint_white"))
        for j in range(1, rows):
            parts.append(box((w - 2 * t, d * 0.8, 0.022), (0, -d * 0.4, z0 + j * (z1 - z0) / rows), "paint_white"))
    a = 0.13
    parts += [box((a, 0.07, h + a), (-w / 2 - a / 2, -0.035, 0), surround), box((a, 0.07, h + a), (w / 2 + a / 2, -0.035, 0), surround),
              box((w + 2 * a, 0.07, a), (0, -0.035, h), surround),
              box((w + 2 * a + 0.12, 0.2, 0.08), (0, -0.1, -0.08), surround),
              box((w + 2 * a + 0.16, 0.1, 0.16), (0, -0.05, h + a), surround),
              box((0.18, 0.13, 0.26), (0, -0.065, h + a - 0.04), surround)]
    if shutters:
        sw = w / 2 + 0.02
        for side in (-1, 1):
            cx = side * (w / 2 + a + sw / 2)
            parts.append(box((sw, 0.04, h), (cx, -0.02, 0), "paint_dark"))
            for k in range(int(h / 0.07)):
                parts.append(box((sw - 0.08, 0.02, 0.025), (cx, -0.045, 0.06 + k * 0.07), "paint_dark"))
    return finish(name, parts)


def door_classic(w, h, name, leaf="wood", double=False, fanlight=True):
    """Panelled door (raised panels) in a stone surround, optional fanlight with glazing bars, handle, step."""
    reset()
    parts = []
    leaves = 2 if double else 1
    lw = w / leaves
    for k in range(leaves):
        cx = -w / 2 + lw * (k + 0.5)
        parts.append(box((lw - 0.01, 0.05, h), (cx, -0.03, 0), leaf))
        ph = (h - 0.5) / 3
        for r in range(3):
            for c in range(2):
                px = cx - lw / 4 + c * lw / 2
                parts.append(box((lw / 2 - 0.12, 0.025, ph - 0.12), (px, -0.065, 0.2 + r * ph + 0.06), leaf))
        side = 1 if (k == 0 and not double) or (double and k == 0) else -1
        parts.append(box((0.03, 0.06, 0.12), (cx + side * (lw / 2 - 0.1), -0.09, h * 0.47), "metal_dark"))
    top = h
    if fanlight:
        fh = 0.45
        parts.append(box((w, 0.02, fh), (0, -0.01, h + 0.05), "glass"))
        parts.append(box((w, 0.05, 0.05), (0, -0.025, h), "paint_white"))
        for i in range(1, 5):
            parts.append(box((0.02, 0.04, fh), (-w / 2 + i * w / 5, -0.02, h + 0.05), "paint_white"))
        top = h + fh + 0.05
    a = 0.18
    parts += [box((a, 0.08, top + a), (-w / 2 - a / 2, -0.04, 0), "stone"), box((a, 0.08, top + a), (w / 2 + a / 2, -0.04, 0), "stone"),
              box((w + 2 * a + 0.2, 0.14, 0.2), (0, -0.07, top + a), "stone"),
              box((w + 2 * a + 0.3, 0.35, 0.16), (0, -0.175, -0.16), "stone")]
    return finish(name, parts)


def moulding(profile, length, name, kind="stone"):
    """A straight moulding: 2D profile [(y out, z)] extruded along X."""
    reset()
    bm = bmesh.new()
    left = [bm.verts.new((-length / 2, -y, z)) for y, z in profile]
    right = [bm.verts.new((length / 2, -y, z)) for y, z in profile]
    for i in range(len(profile) - 1):
        bm.faces.new((left[i + 1], right[i + 1], right[i], left[i]))
    bm.faces.new(list(reversed(left)))
    bm.faces.new(right)
    me = bpy.data.meshes.new(name)
    bm.normal_update()
    bm.to_mesh(me)
    bm.free()
    return finish(name, [obj(me, kind)])


CORNICE = [(0, 0), (0.06, 0), (0.06, 0.04), (0.1, 0.06), (0.12, 0.1), (0.2, 0.14), (0.26, 0.2), (0.38, 0.24), (0.42, 0.28), (0.42, 0.34), (0, 0.34)]
STRING = [(0, 0), (0.04, 0), (0.07, 0.03), (0.07, 0.09), (0.05, 0.11), (0, 0.11)]


def pediment(width, name):
    reset()
    h = width * 0.28
    bm = bmesh.new()
    pts = [(-width / 2, 0), (width / 2, 0), (0, h)]
    front = [bm.verts.new((x, -0.12, z)) for x, z in pts]
    back = [bm.verts.new((x, 0.0, z)) for x, z in pts]
    bm.faces.new(front)
    bm.faces.new(list(reversed(back)))
    for i in range(3):
        bm.faces.new((front[i], back[i], back[(i + 1) % 3], front[(i + 1) % 3]))
    me = bpy.data.meshes.new(name)
    bm.normal_update()
    bm.to_mesh(me)
    bm.free()
    parts = [obj(me, "stone", (0, 0, 0.16)), box((width + 0.1, 0.16, 0.16), (0, 0.0, 0), "stone")]
    return finish(name, parts)


def chimney(name):
    reset()
    parts = [box((0.9, 0.6, 1.6), (0, 0.3, 0), "brick"), box((1.0, 0.7, 0.12), (0, 0.3, 1.6), "stone")]
    for x in (-0.22, 0.22):
        parts.append(obj(lathe([(0.0, 0), (0.12, 0), (0.12, 0.05), (0.09, 0.12), (0.1, 0.35), (0.12, 0.4), (0.12, 0.42), (0.0, 0.42)], 16, "pot"),
                         "roof_tile", (x, 0.3, 1.72)))
    return finish(name, parts)


def dormer(name):
    reset()
    w, d, h = 1.3, 1.4, 1.3
    parts = [box((w, d, h), (0, d / 2, 0), "paint_white")]
    bm = bmesh.new()
    pts = [(-w / 2 - 0.1, h), (w / 2 + 0.1, h), (0, h + 0.75)]
    front = [bm.verts.new((x, -0.1, z)) for x, z in pts]
    back = [bm.verts.new((x, d, z)) for x, z in pts]
    bm.faces.new(front)
    bm.faces.new(list(reversed(back)))
    for i in range(3):
        bm.faces.new((front[i], back[i], back[(i + 1) % 3], front[(i + 1) % 3]))
    me = bpy.data.meshes.new("dormer_roof")
    bm.normal_update()
    bm.to_mesh(me)
    bm.free()
    parts.append(obj(me, "roof_tile"))
    parts += [box((0.7, 0.04, 0.85), (0, -0.02, 0.25), "glass"), box((0.8, 0.06, 0.06), (0, -0.03, 1.1), "paint_white"),
              box((0.8, 0.06, 0.06), (0, -0.03, 0.22), "paint_white"), box((0.05, 0.06, 0.9), (-0.38, -0.03, 0.22), "paint_white"),
              box((0.05, 0.06, 0.9), (0.38, -0.03, 0.22), "paint_white")]
    return finish(name, parts)


def main():
    made = [door_classic(1.0, 2.2, "SM_Door_Panel", leaf="wood"),
            door_classic(1.0, 2.2, "SM_Door_Painted", leaf="paint_dark"),
            door_classic(1.7, 2.5, "SM_Door_Double", leaf="wood", double=True),
            window_classic(1.0, 1.7, "SM_Window_Sash", panes=(3, 2)),
            window_classic(0.9, 1.5, "SM_Window_Sash_Small", panes=(2, 1)),
            window_classic(1.0, 1.7, "SM_Window_Shutters", panes=(3, 2), shutters=True),
            window_classic(1.0, 2.4, "SM_Window_Tall", panes=(3, 3)),
            window_classic(1.6, 1.5, "SM_Window_Wide", panes=(4, 2)),
            stairs(4, 1.6, "SM_Steps_Entrance"),
            column("tuscan", 4.0, "SM_Column_Tuscan"),
            column("doric", 6.0, "SM_Column_Doric"),
            balustrade(2.0, "SM_Balustrade_2m"),
            iron_railing(2.0, "SM_Railing_Iron_2m"),
            balcony(1.8, "SM_Balcony"),
            moulding(CORNICE, 2.0, "SM_Cornice_2m"),
            moulding(STRING, 2.0, "SM_StringCourse_2m"),
            pediment(1.6, "SM_Pediment"),
            chimney("SM_Chimney"),
            dormer("SM_Dormer")]
    print("DONE", len(made))


if __name__ == "__main__":
    main()
