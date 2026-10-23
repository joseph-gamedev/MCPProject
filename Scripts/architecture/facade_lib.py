"""Shared Blender building blocks for facade kits (Paris, Mediterranean, ...). Imported by make_style.py.

Conventions for every piece (Blender coordinates while building; finish() turns them for Unreal):
  x = along the facade, bay centred on 0; z = up from the storey floor; y = 0 on the wall's FRONT plane,
  wall body at +y (0..T), projections (balconies, sills, cornices) at -y. In Unreal the facade faces -Y.
Material slots are semantic, named S_<slot>: wall, trim, frame, glass, shutter, door, metal, roof, accent, brass,
shop, awning, soil. Each style maps the slots to its own material instances when importing.
"""

import math
import os

import bmesh
import bpy
from mathutils import Matrix, Vector

PREVIEW = {"wall": (0.85, 0.8, 0.68), "trim": (0.9, 0.87, 0.78), "frame": (0.9, 0.9, 0.88), "glass": (0.05, 0.06, 0.07), "shutter": (0.1, 0.25, 0.15),
           "door": (0.1, 0.15, 0.1), "metal": (0.02, 0.02, 0.02), "roof": (0.45, 0.48, 0.5), "accent": (0.7, 0.3, 0.2), "brass": (0.8, 0.6, 0.25),
           "shop": (0.05, 0.1, 0.12), "awning": (0.5, 0.1, 0.1), "soil": (0.05, 0.045, 0.03), "paving": (0.6, 0.6, 0.6), "road": (0.1, 0.1, 0.1),
           "kerb": (0.5, 0.5, 0.5)}


def mat(slot):
    name = "S_" + slot
    m = bpy.data.materials.get(name) or bpy.data.materials.new(name)
    m.diffuse_color = (*PREVIEW.get(slot, (0.5, 0.5, 0.5)), 1)
    return m


def reset():
    bpy.ops.wm.read_factory_settings(use_empty=True)


def _object(bm, slot, name="part"):
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    big = [f for f in bm.faces if len(f.verts) > 4]
    if big:
        bmesh.ops.triangulate(bm, faces=big, quad_method="BEAUTY", ngon_method="EAR_CLIP")
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    ob = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(ob)
    ob.data.materials.append(mat(slot))
    return ob


def bx(x0, x1, y0, y1, z0, z1, slot):
    """Axis-aligned box from min/max corners."""
    bm = bmesh.new()
    v = [bm.verts.new((x, y, z)) for x in (min(x0, x1), max(x0, x1)) for y in (min(y0, y1), max(y0, y1)) for z in (min(z0, z1), max(z0, z1))]
    for f in ((0, 1, 3, 2), (4, 6, 7, 5), (0, 4, 5, 1), (2, 3, 7, 6), (0, 2, 6, 4), (1, 5, 7, 3)):
        bm.faces.new([v[i] for i in f])
    return _object(bm, slot, "box")


def prism(pts, y0, y1, slot):
    """Polygon in the wall plane [(x, z)] extruded from y0 (front) to y1 (back)."""
    bm = bmesh.new()
    front = [bm.verts.new((x, y0, z)) for x, z in pts]
    back = [bm.verts.new((x, y1, z)) for x, z in pts]
    bm.faces.new(front)
    bm.faces.new(list(reversed(back)))
    n = len(pts)
    for i in range(n):
        bm.faces.new((front[i], front[(i + 1) % n], back[(i + 1) % n], back[i]))
    return _object(bm, slot, "prism")


def prism_yz(pts, x0, x1, slot):
    """Section [(y, z)] extruded along X from x0 to x1 (mouldings, roof slopes)."""
    bm = bmesh.new()
    left = [bm.verts.new((x0, y, z)) for y, z in pts]
    right = [bm.verts.new((x1, y, z)) for y, z in pts]
    bm.faces.new(left)
    bm.faces.new(list(reversed(right)))
    n = len(pts)
    for i in range(n):
        bm.faces.new((left[i], left[(i + 1) % n], right[(i + 1) % n], right[i]))
    return _object(bm, slot, "section")


def lathe(profile, x, y, z, slot, seg=16):
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
    return _object(bm, slot, "lathe")


def arc(cx, cz, r, a0, a1, n=16, sx=1.0):
    return [(cx + sx * r * math.cos(a0 + (a1 - a0) * i / n), cz + r * math.sin(a0 + (a1 - a0) * i / n)) for i in range(n + 1)]


def ring(cx, cz, r0, r1, y0, y1, slot, n=20, a0=math.pi, a1=0.0):
    """Annulus sector (arch ring) between angles a0 and a1."""
    return prism(arc(cx, cz, r1, a0, a1, n) + arc(cx, cz, r0, a1, a0, n), y0, y1, slot)


def bar(x0, z0, x1, z1, w, y0, y1, slot):
    """Straight bar between two points in the wall plane."""
    dx, dz = x1 - x0, z1 - z0
    ln = math.hypot(dx, dz) or 1.0
    nx, nz = -dz / ln * w / 2, dx / ln * w / 2
    return prism([(x0 - nx, z0 - nz), (x1 - nx, z1 - nz), (x1 + nx, z1 + nz), (x0 + nx, z0 + nz)], y0, y1, slot)


def beam(p0, p1, w, h, slot):
    """Box of section w x h from point p0 to p1 (any direction)."""
    a, b = Vector(p0), Vector(p1)
    d = b - a
    bm = bmesh.new()
    bmesh.ops.create_cube(bm, size=1.0)
    bmesh.ops.scale(bm, vec=(d.length, w, h), verts=bm.verts)
    rot = d.normalized().to_track_quat("X", "Z").to_matrix().to_4x4()
    bmesh.ops.transform(bm, matrix=Matrix.Translation((a + b) / 2) @ rot, verts=bm.verts)
    return _object(bm, slot, "beam")


def grooves(objs, zs=(), depth=0.028, h=0.028, radial=None, verticals=()):
    """Cut joints into the front face: horizontal channels at zs, radial voussoir joints (cx, cz, r0, r1, n) and
    vertical joints [(x, z0, z1)] (ashlar)."""
    cutters = [bx(-50, 50, -0.3, depth, z - h / 2, z + h / 2, "wall") for z in zs]
    if radial:
        cx, cz, r0, r1, n = radial
        for i in range(1, n):
            a = math.pi * i / n
            cutters.append(bar(cx + r0 * math.cos(a), cz + r0 * math.sin(a), cx + r1 * math.cos(a), cz + r1 * math.sin(a), h, -0.3, depth, "wall"))
    for x, z0, z1 in verticals:
        cutters.append(bx(x - h / 2, x + h / 2, -0.3, depth, z0, z1, "wall"))
    if not cutters:
        return
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


def finish(name, out_dir):
    """Join everything in the scene, clean, turn to face -Y in Unreal and export <out_dir>/<name>.fbx."""
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
    me.transform(Matrix.Rotation(math.pi, 4, "Z"))  # the FBX import into Unreal mirrors Y: built facing -Y, turn to face -Y there
    for p in me.polygons:
        p.use_smooth = False
    ob.name = name
    os.makedirs(out_dir, exist_ok=True)
    bpy.ops.export_scene.fbx(filepath=os.path.join(out_dir, name + ".fbx"), use_selection=True, apply_unit_scale=True, bake_space_transform=True,
                             axis_forward="-Y", axis_up="Z", mesh_smooth_type="FACE")
    d = ob.dimensions
    print(f"PIECE {name}: {len(me.polygons)} faces, {d.x:.2f} x {d.y:.2f} x {d.z:.2f} m, slots {[m.name for m in me.materials]}")

# ------------------------------------------------------------------------------------------------ facade families


def wall_bay(W, H, T, ox=0.0, sill=0.0, head=0.0, arch=None, slot="wall"):
    """A bay of wall W x H, T thick, with an opening ox wide from sill to head cut through it. arch: None (flat
    head), "round" (semicircle springing at head) or "segment" (shallow arc rising ox/6 above head). ox 0 = solid."""
    if ox <= 0:
        return [bx(-W / 2, W / 2, 0, T, 0, H, slot)]
    w = ox / 2
    parts = [bx(-W / 2, -w, 0, T, 0, H, slot), bx(w, W / 2, 0, T, 0, H, slot)]
    if sill > 0:
        parts.append(bx(-w, w, 0, T, 0, sill, slot))
    if arch == "round":
        parts.append(prism([(-w, head)] + arc(0, head, w, math.pi, 0, 24)[1:-1] + [(w, head), (w, H), (-w, H)], 0, T, slot))
    elif arch == "segment":
        rise = ox / 6
        r = (w * w + rise * rise) / (2 * rise)
        a = math.asin(w / r)
        pts = [(r * math.sin(t), head + rise - r + r * math.cos(t)) for t in [-a + 2 * a * i / 16 for i in range(17)]]
        parts.append(prism([(-w, head)] + pts[1:-1] + [(w, head), (w, H), (-w, H)], 0, T, slot))
    else:
        parts.append(bx(-w, w, 0, T, head, H, slot))
    return parts


def opening_top(ox, head, arch):
    return head + (ox / 2 if arch == "round" else ox / 6 if arch == "segment" else 0.0)


def glass_pane(ox, z0, z1, y, arch=None, head=None):
    """Glass filling an opening (with its arched top)."""
    w = ox / 2
    if arch == "round":
        return prism([(-w, z0), (w, z0), (w, head)] + arc(0, head, w, 0, math.pi, 24)[1:-1] + [(-w, head)], y, y + 0.01, "glass")
    return bx(-w, w, y, y + 0.01, z0, z1, "glass")


def casement(ox, z0, z1, yf=0.12, leaves=2, rows=3, cols=1, transom=None, slot="frame"):
    """Casement / French window: outer frame, `leaves` opening leaves with glazing bars (rows x cols per leaf) and an
    optional fixed transom light from `transom` up to z1."""
    w = ox / 2
    f, s = 0.06, 0.05
    parts = [glass_pane(ox, z0, z1, yf + 0.04)]
    parts += [bx(-w, -w + f, yf, yf + 0.1, z0, z1, slot), bx(w - f, w, yf, yf + 0.1, z0, z1, slot), bx(-w, w, yf, yf + 0.1, z1 - f, z1, slot),
              bx(-w, w, yf - 0.01, yf + 0.1, z0, z0 + 0.07, slot)]
    top = z1 - f
    if transom:
        parts.append(bx(-w, w, yf - 0.005, yf + 0.1, transom - 0.035, transom + 0.035, slot))
        top = transom - 0.035
    xi0, xi1 = -w + f, w - f
    lw = (xi1 - xi0) / leaves
    zb, zt = z0 + 0.07, top
    for k in range(leaves):
        a, b = xi0 + k * lw, xi0 + (k + 1) * lw
        y0, y1 = yf + 0.01, yf + 0.06
        parts += [bx(a, a + s, y0, y1, zb, zt, slot), bx(b - s, b, y0, y1, zb, zt, slot), bx(a, b, y0, y1, zb, zb + 0.09, slot), bx(a, b, y0, y1, zt - s, zt, slot)]
        for i in range(1, cols):
            x = a + s + (b - a - 2 * s) * i / cols
            parts.append(bx(x - 0.011, x + 0.011, y0 + 0.005, y1 - 0.005, zb + 0.09, zt - s, slot))
        for j in range(1, rows):
            z = zb + 0.09 + (zt - s - zb - 0.09) * j / rows
            parts.append(bx(a + s, b - s, y0 + 0.005, y1 - 0.005, z - 0.011, z + 0.011, slot))
    return parts


def shutters(ox, z0, z1, closed=False, louvre=0.06, slot="shutter", y=None):
    """Louvred shutters: open flat against the wall either side of the opening, or closed in front of it."""
    parts = []
    lw = ox / 2
    for side in (-1, 1):
        if closed:
            x0, x1 = (-ox / 2, 0) if side < 0 else (0, ox / 2)
            yb = 0.02 if y is None else y
        else:
            x0, x1 = (-ox / 2 - lw - 0.02, -ox / 2 - 0.02) if side < 0 else (ox / 2 + 0.02, ox / 2 + lw + 0.02)
            yb = -0.04 if y is None else y
        parts += [bx(x0, x1, yb, yb + 0.03, z0, z1, slot)]
        parts += [bx(x0 + 0.05, x1 - 0.05, yb - 0.012, yb, z0 + 0.06 + k * louvre, z0 + 0.06 + k * louvre + louvre * 0.45, slot)
                  for k in range(int((z1 - z0 - 0.12) / louvre))]
        parts += [bx(x0, x0 + 0.05, yb - 0.015, yb, z0, z1, slot), bx(x1 - 0.05, x1, yb - 0.015, yb, z0, z1, slot)]
    return parts


def railing(x0, x1, y, z0, h=1.0, spacing=0.11, motif=None, slot="metal"):
    """Iron railing: top and bottom rails, bars; motif "rings" adds a band of rings, "scroll" a band of S-curves."""
    parts = [bx(x0, x1, y - 0.022, y + 0.022, z0 + h - 0.045, z0 + h, slot), bx(x0, x1, y - 0.015, y + 0.015, z0, z0 + 0.04, slot)]
    n = max(2, int((x1 - x0) / spacing))
    for i in range(n + 1):
        x = x0 + (x1 - x0) * i / n
        parts.append(bx(x - 0.009, x + 0.009, y - 0.009, y + 0.009, z0 + 0.04, z0 + h - 0.045, slot))
    if motif:
        zc = z0 + h * 0.62
        parts.append(bx(x0, x1, y - 0.012, y + 0.012, zc - 0.13, zc - 0.11, slot))
        parts.append(bx(x0, x1, y - 0.012, y + 0.012, zc + 0.11, zc + 0.13, slot))
        for i in range(n):
            x = x0 + (x1 - x0) * (i + 0.5) / n
            if motif == "rings":
                for a0 in (0, math.pi):
                    prism(arc(x, zc, spacing * 0.45, a0, a0 + math.pi, 8) + arc(x, zc, spacing * 0.33, a0 + math.pi, a0, 8), y - 0.008, y + 0.008, slot)
            else:  # scroll: two half rings offset
                ring(x - spacing * 0.22, zc, spacing * 0.16, spacing * 0.24, y - 0.008, y + 0.008, slot, 8, 0, math.pi)
                ring(x + spacing * 0.22, zc, spacing * 0.16, spacing * 0.24, y - 0.008, y + 0.008, slot, 8, math.pi, 2 * math.pi)
    return parts


def balcony(x0, x1, depth, z, thick=0.16, corbels=2, rail_h=1.0, motif="scroll", slot="trim", returns=True):
    """Stone balcony slab on moulded corbels with an iron railing round its edge."""
    parts = [bx(x0, x1, -depth, 0, z - thick, z, slot), bx(x0 - 0.02, x1 + 0.02, -depth - 0.02, -depth + 0.04, z - thick, z - thick + 0.05, slot)]
    for i in range(corbels):
        cx = x0 + (x1 - x0) * (i + 0.5) / corbels
        prism_yz([(0, z - thick), (-depth * 0.8, z - thick), (-depth * 0.8, z - thick - 0.08), (-depth * 0.35, z - thick - 0.22), (0, z - thick - 0.42)],
                 cx - 0.09, cx + 0.09, slot)
    parts += railing(x0, x1, -depth + 0.05, z, rail_h, motif=motif)
    if returns:
        for x in (x0 + 0.03, x1 - 0.03):
            for k in range(int(depth / 0.11)):
                y = -0.06 - k * 0.11
                bx(x - 0.009, x + 0.009, y - 0.009, y + 0.009, z, z + rail_h - 0.04, "metal")
            bx(x - 0.02, x + 0.02, -depth + 0.05, 0, z + rail_h - 0.045, z + rail_h, "metal")
    return parts


def surround(ox, sill, top, width=0.16, proud=0.04, slot="trim", hood=None, ears=True, keystone=False):
    """Architrave round an opening (flat-headed), with optional hood: "cornice" (moulded cornice on consoles) or
    "pediment" (triangular)."""
    w = ox / 2
    parts = [bx(-w - width, -w, -proud, 0.01, sill, top, slot), bx(w, w + width, -proud, 0.01, sill, top, slot),
             bx(-w - width, w + width, -proud, 0.01, top, top + width, slot)]
    if ears:
        parts += [bx(-w - width - 0.04, -w - width + 0.02, -proud, 0.01, top - 0.1, top + width, slot),
                  bx(w + width - 0.02, w + width + 0.04, -proud, 0.01, top - 0.1, top + width, slot)]
    if keystone:
        parts.append(prism([(-0.08, top - 0.04), (0.08, top - 0.04), (0.11, top + width + 0.06), (-0.11, top + width + 0.06)], -proud - 0.03, 0.01, slot))
    if hood == "cornice":
        zt = top + width
        for s in (-1, 1):  # consoles
            x = s * (w + width / 2)
            prism_yz([(0.0, zt - 0.3), (0, zt), (-0.16, zt), (-0.14, zt - 0.06), (-0.06, zt - 0.16), (-0.05, zt - 0.3)], x - 0.06, x + 0.06, slot)
        prism_yz([(0.01, zt), (-0.14, zt), (-0.2, zt + 0.06), (-0.22, zt + 0.14), (0.01, zt + 0.14)], -w - width - 0.1, w + width + 0.1, slot)
    elif hood == "pediment":
        zt = top + width
        x = w + width + 0.1
        prism([(-x, zt), (x, zt), (0, zt + x * 0.32)], -0.12, 0.01, slot)
        prism([(-x - 0.05, zt - 0.06), (x + 0.05, zt - 0.06), (x + 0.05, zt), (-x - 0.05, zt)], -0.16, 0.01, slot)
    return parts


def sill(ox, z, depth=0.06, slot="trim"):
    w = ox / 2 + 0.06
    return [bx(-w, w, -depth, 0.15, z - 0.07, z, slot)]


def door_leaves(ox, h, yf, leaves=2, rows=(0.35, 0.3, 0.35), slot="door", arch_head=None):
    """Panelled door leaves filling an opening ox x h, set back yf; rows = panel row height fractions."""
    w = ox / 2
    parts = [bx(-w, w, yf + 0.015, yf + 0.07, 0, h, slot)]
    lw = ox / leaves
    st = 0.11
    for k in range(leaves):
        a, b = -w + k * lw, -w + (k + 1) * lw
        parts += [bx(a, a + st, yf, yf + 0.015, 0, h, slot), bx(b - st, b, yf, yf + 0.015, 0, h, slot)]
        z = 0.0
        rails = [z]
        for r in rows:
            z += r * h
            rails.append(z)
        for i in range(len(rails) - 1):
            parts.append(bx(a, b, yf, yf + 0.015, rails[i], rails[i] + 0.12, slot))
            parts.append(bx(a + st + 0.05, b - st - 0.05, yf + 0.004, yf + 0.015, rails[i] + 0.17, rails[i + 1] - 0.05, slot))  # raised field
        parts.append(bx(a, b, yf, yf + 0.015, h - 0.1, h, slot))
    return parts


def shopfront(ox, h, yf=0.15, door_w=0.95, fascia=0.5, slot="shop"):
    """Shop front in an opening: stall riser, big lights with mullions, glazed door, transom, painted fascia."""
    w = ox / 2
    parts = [glass_pane(ox, 0.45, h - fascia, yf + 0.06)]
    parts += [bx(-w, w, yf, yf + 0.1, 0, 0.45, slot), bx(-w, w, yf - 0.02, yf + 0.1, h - fascia, h, slot),        # stall riser, fascia
              bx(-w, w, yf - 0.03, yf + 0.1, h - fascia - 0.04, h - fascia, slot),
              bx(-w, -w + 0.08, yf, yf + 0.1, 0, h, slot), bx(w - 0.08, w, yf, yf + 0.1, 0, h, slot)]
    dx = w - door_w / 2 - 0.08
    parts += [bx(dx - door_w / 2 - 0.05, dx - door_w / 2, yf, yf + 0.1, 0, h - fascia, slot), bx(-w, dx - door_w / 2, yf, yf + 0.1, 0.42, 0.5, slot)]
    tz = h - fascia - 0.5
    parts.append(bx(-w, w, yf, yf + 0.1, tz - 0.04, tz, slot))
    for x in (-w + (dx - door_w / 2 + w) / 2,):
        parts.append(bx(x - 0.025, x + 0.025, yf, yf + 0.1, 0.5, tz, slot))
    parts += [bx(dx - door_w / 2, dx + door_w / 2, yf + 0.04, yf + 0.06, 0, 0.12, slot), bx(dx - 0.03, dx + 0.03, yf - 0.02, yf + 0.04, 0.9, 1.2, "brass")]
    return parts


def cornice(W, pts, slot="trim", modillions=0, mod_depth=0.3, mod_z=None):
    """Moulded cornice along a bay: section [(y, z)] (y negative = projecting) plus optional modillions (brackets)."""
    parts = [prism_yz(pts, -W / 2, W / 2, slot)]
    if modillions:
        z = mod_z if mod_z is not None else max(z for _, z in pts) * 0.55
        for i in range(modillions):
            x = -W / 2 + (i + 0.5) * W / modillions
            parts.append(prism_yz([(0, z - 0.18), (0, z), (-mod_depth, z), (-mod_depth, z - 0.06), (-mod_depth * 0.4, z - 0.1), (-0.05, z - 0.18)],
                                  x - 0.05, x + 0.05, slot))
    return parts


def chimney_pots(x, y, z, n, along="y", spacing=0.32, slot="accent", r=0.1, h=0.45):
    parts = []
    for i in range(n):
        d = (i - (n - 1) / 2) * spacing
        px, py = (x + d, y) if along == "x" else (x, y + d)
        parts.append(lathe([(0, 0), (r * 1.2, 0), (r * 1.2, 0.04), (r, 0.1), (r * 1.05, h * 0.85), (r * 1.25, h), (r * 0.9, h), (r * 0.9, h * 0.4), (0, h * 0.4)],
                           px, py, z, slot, 12))
    return parts


def blocks(prefix, slots, out_dir):
    """1 m unit blocks per slot (pivot bottom centre) for shells, roofs and ground; instances scale them."""
    for slot in slots:
        reset()
        bx(-0.5, 0.5, -0.5, 0.5, 0, 1, slot)
        finish(f"{prefix}Block_{slot.title()}", out_dir)


def double_hung(ox, z0, z1, yf=0.12, cols=1, rows=1, pair=False, slot="frame"):
    """Double-hung sash window (upper sash slides in front of the lower); pair=True puts two side by side with a
    centre mullion (typical American commercial window). cols x rows panes per sash."""
    parts = []
    units = [(-ox / 4 - 0.02, ox / 2 - 0.08), (ox / 4 + 0.02, ox / 2 - 0.08)] if pair else [(0.0, ox)]
    if pair:
        parts.append(bx(-0.08, 0.08, yf - 0.02, yf + 0.12, z0, z1, slot))
    for cx, w in units:
        hw = w / 2
        f, s = 0.06, 0.045
        parts.append(bx(cx - hw, cx + hw, yf + 0.07, yf + 0.08, z0, z1, "glass"))
        parts += [bx(cx - hw, cx - hw + f, yf, yf + 0.12, z0, z1, slot), bx(cx + hw - f, cx + hw, yf, yf + 0.12, z0, z1, slot),
                  bx(cx - hw, cx + hw, yf, yf + 0.12, z1 - f, z1, slot), bx(cx - hw, cx + hw, yf - 0.01, yf + 0.12, z0, z0 + 0.07, slot)]
        zi0, zi1 = z0 + 0.07, z1 - f
        zm = (zi0 + zi1) / 2
        xi0, xi1 = cx - hw + f, cx + hw - f
        for a, b, y in ((zi0, zm + 0.02, yf + 0.045), (zm - 0.02, zi1, yf + 0.005)):
            parts += [bx(xi0, xi0 + s, y, y + 0.04, a, b, slot), bx(xi1 - s, xi1, y, y + 0.04, a, b, slot), bx(xi0, xi1, y, y + 0.04, a, a + 0.05, slot),
                      bx(xi0, xi1, y, y + 0.04, b - 0.04, b, slot)]
            for i in range(1, cols):
                x = xi0 + s + (xi1 - xi0 - 2 * s) * i / cols
                parts.append(bx(x - 0.011, x + 0.011, y + 0.005, y + 0.035, a + 0.05, b - 0.04, slot))
            for j in range(1, rows):
                z = a + 0.05 + (b - a - 0.09) * j / rows
                parts.append(bx(xi0 + s, xi1 - s, y + 0.005, y + 0.035, z - 0.011, z + 0.011, slot))
    return parts
