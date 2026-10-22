"""Georgian terrace kit (London / Dublin, 1720-1830) built in Blender, headless.

    blender -b --python Scripts/architecture/georgian.py -- [out_dir]

Every piece is one bay wide (BAY metres) and one storey tall, at real scale. Wall pieces are a solid 35 cm wall with
the opening cut through it: reveal, sash window set back 10 cm, stone sill, gauged-brick or arched head. A terrace is
these pieces stacked (basement, ground, first, second, third, cornice, parapet, mansard) bay by bay.

Pivot convention (in Unreal): x = bay centre, y = 0 on the wall's front plane, z = 0 at the piece's floor. The facade
faces -Y; the wall body is behind it (+Y). Free pieces (railings, steps, chimney) use the same origin rules.

Material slots GT_<kind>; the Unreal import maps them to scanned materials. GT_paint_door takes its colour from
per-instance custom data 1-3 and GT_stucco from 4-6 (so every house in a street can differ while sharing meshes).
"""

import math
import os
import sys

import addon_utils  # noqa: F401  (kept for parity with the other generators)
import bmesh
import bpy
from mathutils import Matrix

ARGS = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
OUT = os.path.abspath(ARGS[0]) if ARGS else os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "Saved", "Architecture", "Georgian"))
os.makedirs(OUT, exist_ok=True)

BAY = 1.9        # bay width (3-bay house = 5.7 m)
T = 0.35         # front wall thickness
H_BASE, H_GROUND, H_FIRST, H_SECOND, H_THIRD = 2.7, 3.3, 3.6, 3.0, 2.5
COURSE = 0.36    # rusticated stucco course height

COLORS = {"brick": (0.36, 0.16, 0.1), "brick_gauged": (0.55, 0.25, 0.14), "stucco": (0.85, 0.82, 0.74), "stone": (0.7, 0.67, 0.6),
          "granite": (0.5, 0.5, 0.48), "slate": (0.16, 0.17, 0.19), "lead": (0.2, 0.21, 0.22), "paint_frame": (0.9, 0.9, 0.87),
          "paint_door": (0.05, 0.12, 0.08), "metal": (0.02, 0.02, 0.02), "glass": (0.02, 0.025, 0.03), "brass": (0.8, 0.6, 0.25),
          "paving": (0.45, 0.42, 0.38), "asphalt": (0.08, 0.08, 0.08), "soil": (0.05, 0.045, 0.03)}


def mat(kind):
    name = "GT_" + kind
    m = bpy.data.materials.get(name) or bpy.data.materials.new(name)
    m.diffuse_color = (*COLORS[kind], 1)
    return m


def reset():
    bpy.ops.wm.read_factory_settings(use_empty=True)

# ------------------------------------------------------------------------------------------------ primitives


def _object(bm, kind, name="part"):
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    big = [f for f in bm.faces if len(f.verts) > 4]
    if big:
        bmesh.ops.triangulate(bm, faces=big, quad_method="BEAUTY", ngon_method="EAR_CLIP")
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    ob = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(ob)
    ob.data.materials.append(mat(kind))
    return ob


def bx(x0, x1, y0, y1, z0, z1, kind):
    """Axis-aligned box from min/max corners."""
    bm = bmesh.new()
    v = [bm.verts.new((x, y, z)) for x in (x0, x1) for y in (y0, y1) for z in (z0, z1)]
    for f in ((0, 1, 3, 2), (4, 6, 7, 5), (0, 4, 5, 1), (2, 3, 7, 6), (0, 2, 6, 4), (1, 5, 7, 3)):
        bm.faces.new([v[i] for i in f])
    return _object(bm, kind, "box")


def prism(pts, y0, y1, kind):
    """Polygon in the wall plane [(x, z)] extruded from y0 (front) to y1 (back)."""
    bm = bmesh.new()
    front = [bm.verts.new((x, y0, z)) for x, z in pts]
    back = [bm.verts.new((x, y1, z)) for x, z in pts]
    bm.faces.new(front)
    bm.faces.new(list(reversed(back)))
    n = len(pts)
    for i in range(n):
        bm.faces.new((front[i], front[(i + 1) % n], back[(i + 1) % n], back[i]))
    return _object(bm, kind, "prism")


def prism_yz(pts, x0, x1, kind):
    """Section [(y, z)] extruded along X from x0 to x1 (mouldings, roof slopes)."""
    bm = bmesh.new()
    left = [bm.verts.new((x0, y, z)) for y, z in pts]
    right = [bm.verts.new((x1, y, z)) for y, z in pts]
    bm.faces.new(left)
    bm.faces.new(list(reversed(right)))
    n = len(pts)
    for i in range(n):
        bm.faces.new((left[i], left[(i + 1) % n], right[(i + 1) % n], right[i]))
    return _object(bm, kind, "section")


def lathe(profile, x, y, z, kind, seg=16):
    """[(radius, height)] spun around a vertical axis at (x, y, z)."""
    bm = bmesh.new()
    rings = [[bm.verts.new((x + r * math.cos(2 * math.pi * i / seg), y + r * math.sin(2 * math.pi * i / seg), z + h)) for i in range(seg)] for r, h in profile]
    for a, b in zip(rings, rings[1:]):
        for i in range(seg):
            bm.faces.new((a[i], a[(i + 1) % seg], b[(i + 1) % seg], b[i]))
    if profile[0][0] > 0:
        bm.faces.new(list(reversed(rings[0])))
    if profile[-1][0] > 0:
        bm.faces.new(rings[-1])
    return _object(bm, kind, "lathe")


def arc(cx, cz, r, a0, a1, n=16, sx=1.0):
    return [(cx + sx * r * math.cos(a0 + (a1 - a0) * i / n), cz + r * math.sin(a0 + (a1 - a0) * i / n)) for i in range(n + 1)]


def ring(cx, cz, r0, r1, y0, y1, kind, n=20):
    """Half annulus (arch ring) above the springing line."""
    return prism(arc(cx, cz, r1, math.pi, 0, n) + arc(cx, cz, r0, 0, math.pi, n), y0, y1, kind)


def beam(p0, p1, w, h, kind):
    """Box of section w x h running from point p0 to p1 (any direction)."""
    from mathutils import Vector
    a, b = Vector(p0), Vector(p1)
    d = b - a
    bm = bmesh.new()
    bmesh.ops.create_cube(bm, size=1.0)
    bmesh.ops.scale(bm, vec=(d.length, w, h), verts=bm.verts)
    rot = d.normalized().to_track_quat("X", "Z").to_matrix().to_4x4()
    bmesh.ops.transform(bm, matrix=Matrix.Translation((a + b) / 2) @ rot, verts=bm.verts)
    return _object(bm, kind, "beam")


def bar(x0, z0, x1, z1, w, y0, y1, kind):
    """Straight bar between two points in the wall plane."""
    dx, dz = x1 - x0, z1 - z0
    ln = math.hypot(dx, dz) or 1.0
    nx, nz = -dz / ln * w / 2, dx / ln * w / 2
    return prism([(x0 - nx, z0 - nz), (x1 - nx, z1 - nz), (x1 + nx, z1 + nz), (x0 + nx, z0 + nz)], y0, y1, kind)


def grooves(objs, zs, depth=0.028, h=0.028, radial=None):
    """Rustication: cut horizontal channels (and optional radial voussoir joints) into the front face of objs."""
    cutters = [bx(-5, 5, -0.2, depth, z - h / 2, z + h / 2, "stucco") for z in zs]
    if radial:
        cx, cz, r0, r1, n = radial
        for i in range(1, n):
            a = math.pi * i / n
            cutters.append(bar(cx + r0 * math.cos(a), cz + r0 * math.sin(a), cx + r1 * math.cos(a), cz + r1 * math.sin(a), h, -0.2, depth, "stucco"))
    coll = bpy.data.collections.new("cutters")
    bpy.context.scene.collection.children.link(coll)
    for c in cutters:
        bpy.context.scene.collection.objects.unlink(c)
        coll.objects.link(c)
    for o in objs:
        mod = o.modifiers.new("cut", "BOOLEAN")
        mod.operation = "DIFFERENCE"
        mod.operand_type = "COLLECTION"
        mod.collection = coll
        mod.solver = "EXACT"
        bpy.context.view_layer.objects.active = o
        bpy.ops.object.modifier_apply(modifier=mod.name)
    for c in cutters:
        bpy.data.objects.remove(c, do_unlink=True)
    bpy.data.collections.remove(coll)


def finish(name):
    objs = [o for o in bpy.context.scene.objects if o.type == "MESH"]
    bpy.ops.object.select_all(action="DESELECT")
    for o in objs:
        o.select_set(True)
    bpy.context.view_layer.objects.active = objs[0]
    if len(objs) > 1:
        bpy.ops.object.join()
    ob = bpy.context.view_layer.objects.active
    me = ob.data
    bm = bmesh.new()
    bm.from_mesh(me)
    bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=0.0005)
    bm.to_mesh(me)
    bm.free()
    # built facing -Y; the FBX import into Unreal mirrors Y, so turn 180 degrees to face -Y there
    me.transform(Matrix.Rotation(math.pi, 4, "Z"))
    for p in me.polygons:
        p.use_smooth = False
    ob.name = name
    bpy.ops.export_scene.fbx(filepath=os.path.join(OUT, name + ".fbx"), use_selection=True, apply_unit_scale=True, bake_space_transform=True,
                             axis_forward="-Y", axis_up="Z", mesh_smooth_type="FACE")
    d = ob.dimensions
    print(f"GT {name}: {len(me.polygons)} faces, {d.x:.2f} x {d.y:.2f} x {d.z:.2f} m, slots {[m.name for m in me.materials]}")

# ------------------------------------------------------------------------------------------------ building blocks


def wall_with_opening(H, ox, sill, head, kind, arch=False, rusticate=False):
    """Bay of wall H tall with an opening ox wide from sill to head (head = springing line when arched)."""
    w = ox / 2
    parts = [bx(-BAY / 2, -w, 0, T, 0, H, kind), bx(w, BAY / 2, 0, T, 0, H, kind)]
    if sill > 0:
        parts.append(bx(-w, w, 0, T, 0, sill, kind))
    if arch:
        parts.append(prism([(-w, head)] + arc(0, head, w, math.pi, 0, 24)[1:-1] + [(w, head), (w, H), (-w, H)], 0, T, kind))
    else:
        parts.append(bx(-w, w, 0, T, head, H, kind))
    if rusticate:
        zs = [k * COURSE for k in range(1, int(H / COURSE) + 1) if k * COURSE < H - 0.05]
        if arch:  # courses stop at the arch; voussoir joints radiate from it
            zs = [z for z in zs if z < head or z > head + w + 0.45]
        grooves(parts, zs, radial=(0, head, w, w + 0.45, 7) if arch else None)
    return parts


def stone_sill(ox, z):
    w = ox / 2 + 0.06
    return [bx(-w, w, -0.06, 0.12, z - 0.08, z, "stone"), bx(-w + 0.02, w - 0.02, -0.075, -0.06, z - 0.03, z, "stone")]


def gauged_head(ox, z, h=0.32):
    """Flat arch of rubbed red brick over an opening (wider at the top), slightly proud of the wall."""
    w = ox / 2
    return [prism([(-w, z), (w, z), (w + 0.1, z + h), (-w - 0.1, z + h)], -0.006, 0.05, "brick_gauged")]


def sash(ox, z0, z1, rows_up=2, rows_low=2, cols=3, split=0.5, yf=0.1):
    """Box-frame sash window inside the reveal: frame, two sashes (upper one forward), meeting rails, glazing bars."""
    w = ox / 2
    f, s = 0.065, 0.045      # frame and sash member widths
    parts = [bx(-w, w, yf + 0.07, yf + 0.08, z0, z1, "glass")]
    parts += [bx(-w, -w + f, yf, yf + 0.12, z0, z1, "paint_frame"), bx(w - f, w, yf, yf + 0.12, z0, z1, "paint_frame"),
              bx(-w, w, yf, yf + 0.12, z1 - f, z1, "paint_frame"), bx(-w, w, yf - 0.01, yf + 0.12, z0, z0 + 0.07, "paint_frame")]
    zi0, zi1 = z0 + 0.07, z1 - f
    zm = zi0 + (zi1 - zi0) * split
    xi0, xi1 = -w + f, w - f
    for (a, b, y, rows) in ((zi0, zm + 0.02, yf + 0.045, rows_low), (zm - 0.02, zi1, yf + 0.005, rows_up)):
        y1 = y + 0.04
        parts += [bx(xi0, xi0 + s, y, y1, a, b, "paint_frame"), bx(xi1 - s, xi1, y, y1, a, b, "paint_frame")]
        bottom = 0.075 if a == zi0 else 0.035
        top = 0.035 if a == zi0 else s
        parts += [bx(xi0, xi1, y, y1, a, a + bottom, "paint_frame"), bx(xi0, xi1, y, y1, b - top, b, "paint_frame")]
        ga, gb = a + bottom, b - top
        for i in range(1, cols):
            x = xi0 + s + (xi1 - xi0 - 2 * s) * i / cols
            parts.append(bx(x - 0.011, x + 0.011, y + 0.005, y1 - 0.005, ga, gb, "paint_frame"))
        for j in range(1, rows):
            z = ga + (gb - ga) * j / rows
            parts.append(bx(xi0 + s, xi1 - s, y + 0.005, y1 - 0.005, z - 0.011, z + 0.011, "paint_frame"))
    return parts


def fanlight(ox, z, yf, spokes=7):
    """Semicircular fanlight: frame ring, glass, radiating bars and a small hub."""
    w = ox / 2
    parts = [prism(arc(0, z, w, 0, math.pi, 24), yf + 0.03, yf + 0.04, "glass"), ring(0, z, w - 0.06, w + 0.005, yf, yf + 0.06, "paint_frame"),
             bx(-w, w, yf, yf + 0.06, z - 0.03, z + 0.03, "paint_frame"), ring(0, z, 0.1, 0.14, yf - 0.005, yf + 0.05, "paint_frame", 10)]
    for i in range(1, spokes):
        a = math.pi * i / spokes
        parts.append(bar(0.14 * math.cos(a), z + 0.14 * math.sin(a), (w - 0.05) * math.cos(a), z + (w - 0.05) * math.sin(a), 0.018, yf + 0.005, yf + 0.045,
                         "paint_frame"))
    for rr in (0.3,):
        parts.append(prism(arc(0, z, rr + 0.009, math.pi, 0, 16) + arc(0, z, rr - 0.009, 0, math.pi, 16), yf + 0.005, yf + 0.045, "paint_frame"))
    return parts


def six_panel_door(ox, h, yf):
    """Six-panel door (small top panels, tall middle, medium bottom) with fielded panels and brass furniture."""
    w = ox / 2
    parts = [bx(-w, w, yf + 0.015, yf + 0.06, 0, h, "paint_door")]
    st, rails = 0.13, [(0, 0.24), (0.78, 0.98), (1.95, 2.07), (h - 0.13, h)]
    parts += [bx(-w, -w + st, yf, yf + 0.015, 0, h, "paint_door"), bx(w - st, w, yf, yf + 0.015, 0, h, "paint_door"),
              bx(-0.06, 0.06, yf, yf + 0.015, 0, h, "paint_door")]
    for a, b in rails:
        parts.append(bx(-w, w, yf, yf + 0.015, a, b, "paint_door"))
    for (a, b), (c, d) in zip(rails, rails[1:]):
        for x0, x1 in ((-w + st, -0.06), (0.06, w - st)):
            parts.append(bx(x0 + 0.045, x1 - 0.045, yf + 0.005, yf + 0.015, b + 0.045, c - 0.045, "paint_door"))  # raised field
    knob = lathe([(0.0, 0.0), (0.028, 0.0), (0.032, 0.02), (0.012, 0.03), (0.012, 0.06), (0.0, 0.06)], 0, 0, 0, "brass", 12)
    knob.data.transform(Matrix.Rotation(-math.pi / 2, 4, "X"))
    knob.data.transform(Matrix.Translation((0, yf, 1.05)))
    parts += [knob, bx(-0.14, 0.14, yf - 0.008, yf, 0.86, 0.92, "brass"),             # letter plate on the lock rail
              bx(-0.035, 0.035, yf - 0.03, yf, 1.42, 1.58, "brass")]                # knocker
    return parts

# ------------------------------------------------------------------------------------------------ pieces


def basement(kind="stucco"):
    reset()
    ox, sill, head = 1.0, 0.75, 2.2
    wall_with_opening(H_BASE, ox, sill, head, kind, rusticate=(kind == "stucco"))
    stone_sill(ox, sill)
    sash(ox, sill, head, 2, 2, 3)
    if kind == "brick":
        gauged_head(ox, head)
    finish(f"SM_GT_Basement_{kind.title()}")


def ground_window(kind="stucco", arched=False):
    reset()
    ox, sill = 1.1, 0.75
    head = 2.3 if arched else 2.8
    wall_with_opening(H_GROUND, ox, sill, head, kind, arch=arched, rusticate=(kind == "stucco"))
    stone_sill(ox, sill)
    if arched:
        sash(ox, sill, head, 2, 2, 3, split=0.5)
        fanlight(ox, head, 0.1, spokes=6)
    else:
        sash(ox, sill, head, 2, 2, 3)
    if kind == "brick" and not arched:
        gauged_head(ox, head)
    finish(f"SM_GT_Ground_{kind.title()}{'_Arched' if arched else ''}")


def ground_door(kind="stucco", case=False):
    """Front door bay: 6-panel door 25 cm back in a deep reveal, semicircular fanlight, optional pilaster doorcase."""
    reset()
    ox, dh = 1.1, 2.35
    yf = 0.24
    wall_with_opening(H_GROUND, ox, 0, dh, kind, arch=True, rusticate=(kind == "stucco" and not case))
    six_panel_door(ox - 0.08, dh - 0.04, yf)
    bx(-ox / 2, -ox / 2 + 0.04, yf, T, 0, dh, "paint_frame")
    bx(ox / 2 - 0.04, ox / 2, yf, T, 0, dh, "paint_frame")
    fanlight(ox, dh, yf - 0.06)
    bx(-ox / 2 - 0.05, ox / 2 + 0.05, -0.02, T, -0.02, 0.0, "stone")                    # threshold
    if kind == "brick":  # Dublin: gauged-brick arch ring
        ring(0, dh, ox / 2, ox / 2 + 0.24, -0.006, 0.05, "brick_gauged", 24)
    if case:  # Doric pilasters, frieze and cornice in painted stucco / stone
        k = "stone" if kind == "brick" else "stucco"
        top = dh + ox / 2 + 0.12
        for s in (-1, 1):
            x0, x1 = s * (ox / 2 + 0.06), s * (ox / 2 + 0.3)
            x0, x1 = min(x0, x1), max(x0, x1)
            bx(x0, x1, -0.07, 0.0, 0, top, k)
            bx(x0 - 0.03, x1 + 0.03, -0.1, 0.0, 0, 0.25, k)                            # plinth
            bx(x0 - 0.03, x1 + 0.03, -0.1, 0.0, top - 0.12, top, k)                    # capital
        bx(-ox / 2 - 0.33, ox / 2 + 0.33, -0.08, 0.0, top, top + 0.28, k)               # frieze
        prism_yz([(0, top + 0.28), (-0.12, top + 0.28), (-0.2, top + 0.36), (-0.22, top + 0.44), (0, top + 0.44)], -ox / 2 - 0.42, ox / 2 + 0.42, k)
    finish(f"SM_GT_Door_{kind.title()}{'_Case' if case else ''}")


def first_floor(kind="brick"):
    """Piano nobile: string course at the floor, tall 6-over-9 sash down to the floor (for a balconette)."""
    reset()
    ox, sill, head = 1.1, 0.3, 2.85
    wall_with_opening(H_FIRST, ox, sill, head, kind)
    bx(-BAY / 2, BAY / 2, -0.06, T, 0, 0.22, "stucco")
    bx(-BAY / 2, BAY / 2, -0.08, T, 0.16, 0.22, "stucco")
    stone_sill(ox, sill)
    sash(ox, sill, head, 2, 3, 3, split=0.58)
    if kind == "brick":
        gauged_head(ox, head)
    finish(f"SM_GT_First_{kind.title()}")


def upper_floor(level, kind="brick"):
    reset()
    if level == 2:
        H, ox, sill, head, rows = H_SECOND, 1.1, 0.8, 2.6, (2, 2)
    else:
        H, ox, sill, head, rows = H_THIRD, 1.05, 0.6, 1.75, (1, 1)
    wall_with_opening(H, ox, sill, head, kind)
    stone_sill(ox, sill)
    sash(ox, sill, head, rows[0], rows[1], 3)
    if kind == "brick":
        gauged_head(ox, head, 0.28 if level == 2 else 0.24)
    finish(f"SM_GT_{'Second' if level == 2 else 'Third'}_{kind.title()}")


def blank(H, name, kind="brick"):
    """Plain wall bay (end bays, blind bays, party-wall returns)."""
    reset()
    bx(-BAY / 2, BAY / 2, 0, T, 0, H, kind)
    if kind == "stucco":
        grooves(list(bpy.context.scene.objects), [k * COURSE for k in range(1, int(H / COURSE) + 1) if k * COURSE < H - 0.05])
    finish(name)


def cornice():
    """Stucco main cornice with dentils, one bay long; sits on top of the top floor."""
    reset()
    w = BAY / 2
    prism_yz([(T, 0), (0, 0), (0, 0.06), (-0.04, 0.08), (-0.04, 0.16), (-0.09, 0.19), (-0.22, 0.24), (-0.3, 0.3), (-0.34, 0.3), (-0.34, 0.38),
              (T, 0.38)], -w, w, "stucco")
    n = int(BAY / 0.13)
    for i in range(n):
        x = -w + (i + 0.5) * BAY / n
        bx(x - 0.035, x + 0.035, -0.1, 0, 0.1, 0.17, "stucco")
    finish("SM_GT_Cornice")


def parapet():
    reset()
    w = BAY / 2
    bx(-w, w, 0, 0.3, 0, 0.85, "brick")
    prism_yz([(-0.05, 0.85), (0.35, 0.85), (0.35, 0.93), (0.15, 0.97), (-0.05, 0.93)], -w, w, "stone")
    finish("SM_GT_Parapet")


def mansard(dormer=True):
    """Slate mansard behind the parapet (steep 70 degree pitch), optionally with a lead-cheeked dormer."""
    reset()
    w = BAY / 2
    y0, y1, top = 0.5, 1.45, 2.6
    prism_yz([(y0, 0), (y1 + 4, 0), (y1 + 4, top), (y1, top)], -w, w, "slate")   # slope + roof body behind
    bx(-w, w, y0 - 0.02, y1 + 4, top, top + 0.05, "lead")
    if dormer:
        dw, dz0, dz1 = 0.95, 0.75, 2.05
        yd = 0.62
        bx(-dw / 2, dw / 2, yd, y1 + 0.3, dz0, dz1, "lead")                       # dormer body (front at yd)
        bx(-dw / 2 - 0.05, dw / 2 + 0.05, yd - 0.06, y1 + 0.3, dz1, dz1 + 0.1, "lead")  # flat lead roof
        bx(-dw / 2 - 0.07, dw / 2 + 0.07, yd - 0.08, yd - 0.04, dz1 + 0.06, dz1 + 0.12, "paint_frame")
        sash(dw - 0.12, dz0 + 0.08, dz1 - 0.08, 1, 1, 3, yf=yd - 0.12)
        bx(-dw / 2, dw / 2, yd - 0.12, yd, dz0, dz0 + 0.08, "paint_frame")
        for s in (-1, 1):
            bx(s * dw / 2 - 0.06 * (s < 0), s * dw / 2 + 0.06 * (s > 0), yd - 0.12, yd + 0.02, dz0, dz1, "paint_frame")
    finish("SM_GT_Mansard_Dormer" if dormer else "SM_GT_Mansard")


def chimney():
    """Party-wall chimney stack (straddles the wall between two houses, runs front to back) with 6 pots."""
    reset()
    bx(-0.4, 0.4, 0, 2.6, 0, 1.7, "brick")
    bx(-0.46, 0.46, -0.06, 2.66, 1.7, 1.8, "stone")
    bx(-0.43, 0.43, -0.03, 2.63, 1.8, 1.88, "brick")
    for i in range(6):
        y = 0.25 + i * 2.1 / 5
        lathe([(0, 0), (0.13, 0), (0.13, 0.04), (0.1, 0.1), (0.11, 0.38), (0.13, 0.44), (0.13, 0.47), (0.1, 0.47), (0.1, 0.2), (0, 0.2)], 0, y, 1.88,
              "brick_gauged", 14)
    finish("SM_GT_Chimney")


def area_railing():
    """One bay of cast-iron area railing on a stone kerb: spear-headed bars, top and bottom rails, urn finial post."""
    reset()
    w = BAY / 2
    bx(-w, w, -0.15, 0.15, 0, 0.22, "granite")
    bx(-w, w, -0.03, 0.03, 0.3, 0.33, "metal")
    bx(-w, w, -0.025, 0.025, 1.0, 1.04, "metal")
    n = int(BAY / 0.125)
    for i in range(n):
        x = -w + (i + 0.5) * BAY / n
        bx(x - 0.011, x + 0.011, -0.011, 0.011, 0.22, 1.12, "metal")
        lathe([(0, 0), (0.025, 0.02), (0.012, 0.09), (0, 0.11)], x, 0, 1.11, "metal", 6)
    lathe([(0, 0), (0.04, 0), (0.035, 1.05), (0.06, 1.1), (0.06, 1.16), (0.03, 1.2), (0.05, 1.27), (0.0, 1.33)], -w, 0, 0.22, "metal", 10)
    finish("SM_GT_Area_Railing")


def balconette():
    """Cast-iron balconette for a piano nobile window (sits on the sill, 1.3 m wide)."""
    reset()
    w, d, h = 0.68, 0.24, 0.8
    for y in (-d,):
        bx(-w, w, y - 0.02, y + 0.02, 0, 0.04, "metal")
        bx(-w, w, y - 0.025, y + 0.025, h - 0.04, h, "metal")
    for s in (-1, 1):
        bx(s * w - 0.02, s * w + 0.02, -d, 0.05, 0, 0.04, "metal")
        bx(s * w - 0.02, s * w + 0.02, -d, 0.05, h - 0.04, h, "metal")
        bx(s * w - 0.015, s * w + 0.015, -d - 0.015, -d + 0.015, 0, h, "metal")
    n = 11
    for i in range(n):
        x = -w + (i + 0.5) * 2 * w / n
        bx(x - 0.009, x + 0.009, -d - 0.009, -d + 0.009, 0.04, h - 0.04, "metal")
        if i % 2 == 0:  # ring motif in the middle band
            for a0 in (0, math.pi):  # a ring = two half annuli
                prism(arc(x, h * 0.55, 0.06, a0, a0 + math.pi, 10) + arc(x, h * 0.55, 0.045, a0 + math.pi, a0, 10), -d - 0.008, -d + 0.008, "metal")
    finish("SM_GT_Balconette")


def steps():
    """Entrance steps bridging the area: 5 granite steps from the pavement (z 0) up to the door (z 0.9),
    railings with a scrolled end both sides. Origin on the wall plane at pavement level."""
    reset()
    w, run, rise, n = 0.75, 0.28, 0.18, 5
    depth = n * run
    for i in range(n):
        y0 = -depth + i * run
        bx(-w, w, y0 - 0.03, 0.0, -1.8, (i + 1) * rise, "granite")
        bx(-w - 0.02, w + 0.02, y0 - 0.035, y0 + 0.02, (i + 1) * rise - 0.04, (i + 1) * rise, "granite")  # nosing
    for s in (-1, 1):
        x = s * (w - 0.04)
        for i in range(n):
            y = -depth + (i + 0.5) * run
            top = rise + 0.95 + (y + depth - 0.06) / (depth - 0.06 - run * 0.5) * (n - 1) * rise
            bx(x - 0.011, x + 0.011, y - 0.011, y + 0.011, (i + 1) * rise, min(top, n * rise + 0.95), "metal")
        # handrail: one sloped rail over the nosings, level at the top landing
        beam((x, -depth + 0.06, rise + 0.95), (x, -run * 0.5, n * rise + 0.95), 0.045, 0.035, "metal")
        beam((x, -run * 0.5, n * rise + 0.95), (x, -0.02, n * rise + 0.95), 0.045, 0.035, "metal")
        lathe([(0, 0), (0.045, 0), (0.04, 1.0), (0.065, 1.05), (0.065, 1.1), (0, 1.16)], x, -depth + 0.06, rise, "metal", 10)
    finish("SM_GT_Steps")


def blocks():
    """1 m unit blocks per material (pivot bottom centre) for end walls, backs, roofs, pavements: instances scale them;
    the world-aligned materials keep brick and slate at true size whatever the scale."""
    for kind in ("brick", "stucco", "stone", "granite", "slate", "lead", "paving", "asphalt", "soil"):
        reset()
        bx(-0.5, 0.5, -0.5, 0.5, 0, 1, kind)
        finish(f"SM_GT_Block_{kind.title()}")


def main():
    blocks()
    for k in ("stucco", "brick"):
        basement(k)
        ground_window(k)
        ground_window(k, arched=True)
        ground_door(k)
        ground_door(k, case=True)
        blank(H_GROUND, f"SM_GT_Ground_Blank_{k.title()}", k)
    first_floor("brick")
    first_floor("stucco")
    upper_floor(2, "brick")
    upper_floor(3, "brick")
    upper_floor(2, "stucco")
    upper_floor(3, "stucco")
    for H, n in ((H_FIRST, "First"), (H_SECOND, "Second"), (H_THIRD, "Third")):
        blank(H, f"SM_GT_{n}_Blank_Brick", "brick")
    cornice()
    parapet()
    mansard(True)
    mansard(False)
    chimney()
    area_railing()
    balconette()
    steps()
    print("GT DONE")


if __name__ == "__main__":
    main()
