"""Part helpers: build objects as lists of plain part dicts, then uk.place(name, parts, at) builds them as one object.

    from unreal_kit import parts as p
    P = [p.box([400, 300, 20], [0, 0, 0], m="concrete"),            # size, location (base pivot), material
         p.cyl(20, 300, [180, 130, 20], m="metal_dark")]
    P += p.transform(P, offset=(0, 500, 0), yaw=90)                  # copy, turned and moved
    uk.place("Table", P, at=[0, 0, 0])

Units cm, Z up, rotations [pitch, yaw, roll]. m is a PALETTE name (physical sRGB colours, never pure white/black)
or a dict like M("brick", (140, 87, 70), roughness=0.8). Every builder accepts extra part fields as keywords
(bevel, weathering, tile, style, emissive ...). Any shape not covered here: write the dict yourself
({"shape": "revolve", "profile": [[r, z], ...]}, see build.get_guide).
"""

import math
import random

# ------------------------------------------------------------------ materials (physical sRGB values -> linear)

def srgb(r, g, b):
    def lin(c):
        c /= 255.0
        return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4
    return [round(lin(r), 4), round(lin(g), 4), round(lin(b), 4)]


PRESETS = ["marble", "sandstone", "limestone", "granite", "stone", "brick", "terracotta", "concrete", "plaster", "wood", "slate_roof",
           "copper", "verdigris", "bronze", "gold", "steel", "iron", "painted_iron", "glass", "tinted_glass", "water", "grass", "sand",
           "asphalt", "roof_tile", "cobblestone", "light", "white", "black", "red", "green", "blue"]


def M(preset, rgb=None, **kw):
    """Material: a builder preset (PRESETS) or a PALETTE name, with an optional real sRGB colour 0-255 and fields."""
    if preset not in PRESETS and preset in globals().get("PALETTE", {}):
        d = dict(PALETTE[preset])
    else:
        d = {"material": preset}
    if rgb:
        d["color"] = srgb(*rgb)
    d.update(kw)
    return d


PALETTE = {
    # walls
    "brick_red": M("brick", (140, 87, 70)), "brick_brown": M("brick", (118, 80, 62)), "brick_buff": M("brick", (188, 160, 118)),
    "plaster_white": M("plaster", (222, 218, 208)), "plaster_cream": M("plaster", (218, 202, 166)), "plaster_ochre": M("plaster", (198, 160, 104)),
    "plaster_terracotta": M("plaster", (176, 112, 84)), "plaster_grey": M("plaster", (170, 168, 162)),
    "stone_light": M("limestone", (200, 190, 168)), "stone_warm": M("sandstone", (196, 168, 128)), "concrete": M("concrete", (160, 158, 150)),
    # trims and details
    "trim_white": M("plaster", (226, 224, 216), roughness=0.55), "trim_stone": M("limestone", (186, 178, 160)),
    "frame_dark": M("painted_iron", (46, 46, 49), roughness=0.45), "frame_white": M("plaster", (224, 222, 216), roughness=0.4),
    "frame_wood": M("wood", (96, 66, 44)), "door_wood": M("wood", (86, 56, 36)), "door_green": M("painted_iron", (40, 72, 56), roughness=0.4),
    "glass": M("tinted_glass"), "glass_dark": M("steel", (28, 34, 40), roughness=0.06, metallic=0.0),
    "metal": M("steel", (192, 194, 196), roughness=0.45), "metal_dark": M("painted_iron", (52, 54, 56), roughness=0.5),
    "aluminium": M("steel", (232, 232, 230), roughness=0.3),
    # roofs
    "roof_slate": M("slate_roof", (72, 74, 80)), "roof_tile": M("roof_tile", (150, 80, 58)), "roof_flat": M("concrete", (118, 116, 110)),
    "tar": M("asphalt", (48, 48, 50)),
    # ground
    "asphalt": M("asphalt", (56, 56, 58)), "sidewalk": M("concrete", (166, 162, 154), style="blocks", tile=60),
    "curb": M("granite", (150, 150, 146)), "marking": M("plaster", (226, 224, 210), roughness=0.6), "grass": M("grass", (88, 104, 54)),
    "soil": M("sand", (98, 80, 60)), "cobble": M("cobblestone", (130, 124, 116)),
    # nature
    "bark": M("wood", (78, 60, 46), roughness=0.9), "leaves": M("grass", (58, 84, 40), roughness=0.8), "leaves_light": M("grass", (82, 108, 52), roughness=0.8),
}

def mat(name):
    """A PALETTE entry or a plain preset name."""
    return dict(PALETTE[name]) if name in PALETTE else {"material": name}


# ------------------------------------------------------------------ primitives (local frame)

def box(size, at, rot=None, m="concrete", **kw):
    d = {"shape": "box", "size": [round(float(v), 2) for v in size], "location": [round(float(v), 2) for v in at], "pivot": "base"}
    if rot:
        d["rotation"] = list(rot)
    d.update(mat(m) if isinstance(m, str) else m)
    d.update(kw)
    return d


def cyl(diameter, height, at, m="metal", **kw):
    d = {"shape": "cylinder", "size": [diameter, diameter, height], "location": list(at)}
    if diameter < 100:
        d["detail"] = "low"  # poles, pipes, wheels: low resolution is invisible and much cheaper
    d.update(mat(m) if isinstance(m, str) else m)
    d.update(kw)
    return d


def cone(diameter, height, at, m="leaves", top_ratio=0.0, **kw):
    d = {"shape": "cone", "size": [diameter, diameter, height], "location": list(at), "top_ratio": top_ratio, "detail": "low"}
    d.update(mat(m) if isinstance(m, str) else m)
    d.update(kw)
    return d


def sphere(diameter, at, m="leaves", scale=None, **kw):
    d = {"shape": "sphere", "size": [diameter, diameter, diameter], "location": list(at)}
    if diameter < 150:
        d["detail"] = "low"
    if scale:
        d["scale"] = list(scale)
    d.update(mat(m) if isinstance(m, str) else m)
    d.update(kw)
    return d


def gable(width, depth, height, at, rot=None, m="roof_tile", **kw):
    d = {"shape": "roof", "size": [width, depth, height], "location": list(at)}
    if rot:
        d["rotation"] = list(rot)
    d.update(mat(m) if isinstance(m, str) else m)
    d.update(kw)
    return d


def transform(parts, offset=(0, 0, 0), yaw=0.0):
    """Rotates parts around Z by yaw degrees, then moves them by offset (returns new dicts)."""
    a = math.radians(yaw)
    ca, sa = math.cos(a), math.sin(a)
    out = []
    for p in parts:
        q = dict(p)
        x, y, z = p.get("location", [0, 0, 0])
        q["location"] = [round(x * ca - y * sa + offset[0], 2), round(x * sa + y * ca + offset[1], 2), round(z + offset[2], 2)]
        if yaw:
            r = list(p.get("rotation", [0, 0, 0]))
            r[1] = (r[1] + yaw) % 360
            q["rotation"] = r
        out.append(q)
    return out


def part(shape, size, at, m="concrete", rot=None, **kw):
    """Any builder shape (pyramid, torus, wedge, capsule, dome, tube, arch, steps, tiers, column ...)."""
    d = {"shape": shape, "size": list(size), "location": list(at)}
    if rot:
        d["rotation"] = list(rot)
    d.update(mat(m) if isinstance(m, str) else m)
    d.update(kw)
    return d


def extrude(outline, height, at=(0, 0, 0), m="concrete", **kw):
    """Footprint [[x, y], ...] pushed up by height (any floor plan, walls with odd shapes)."""
    d = {"shape": "extrude", "profile": [list(q) for q in outline], "height": height, "location": list(at)}
    d.update(mat(m) if isinstance(m, str) else m)
    d.update(kw)
    return d


def sweep(profile, path, m="metal", closed=False, **kw):
    """2D profile [[x, y]] swept along a 3D path [[x, y, z]] (rails, pipes, cables, mouldings)."""
    d = {"shape": "sweep", "profile": [list(q) for q in profile], "path": [list(q) for q in path], "closed": closed}
    d.update(mat(m) if isinstance(m, str) else m)
    d.update(kw)
    return d


def beam(a, b, thickness, m="metal", **kw):
    """Square member from point a to point b (trusses, struts, railings, branches)."""
    dx, dy, dz = b[0] - a[0], b[1] - a[1], b[2] - a[2]
    length = math.sqrt(dx * dx + dy * dy + dz * dz)
    yaw = math.degrees(math.atan2(dy, dx))
    pitch = math.degrees(math.atan2(dz, math.hypot(dx, dy)))
    mid = [(a[0] + b[0]) / 2, (a[1] + b[1]) / 2, (a[2] + b[2]) / 2]
    return box([length + thickness * 0.5, thickness, thickness], mid, rot=[pitch, yaw, 0], m=m, pivot="center", **kw)


def grid(count_x, count_y, spacing_x, spacing_y, make):
    """Calls make(x, y, i, j) on a centred grid and concatenates the part lists it returns."""
    out = []
    for i in range(count_x):
        for j in range(count_y):
            out += make((i - (count_x - 1) / 2) * spacing_x, (j - (count_y - 1) / 2) * spacing_y, i, j)
    return out


def ring(count, radius, make, start=0.0):
    """Calls make(x, y, angle_degrees, i) around a circle and concatenates the results."""
    out = []
    for i in range(count):
        a = start + 360.0 * i / count
        out += make(math.cos(math.radians(a)) * radius, math.sin(math.radians(a)) * radius, a, i)
    return out


# ------------------------------------------------------------------ organic shapes (trees, rocks, bushes, creatures)

def _noise(x, y, z, seed):
    """Smooth pseudo-random field in [-1, 1] (sum of seeded sines; cheap and deterministic)."""
    r = (seed * 9301 + 49297) % 233280 / 233280.0
    v = 0.0
    for k, (fx, fy, fz) in enumerate(((1.3, 0.7, 1.1), (2.9, 3.3, 2.1), (5.7, 4.9, 6.3))):
        ph = r * 6.283 * (k + 1)
        v += math.sin(x * fx + ph) * math.sin(y * fy + ph * 1.7) * math.sin(z * fz + ph * 2.3) / (k + 1)
    return v / 1.8


def _icosphere(level):
    t = (1 + 5 ** 0.5) / 2
    V = [[-1, t, 0], [1, t, 0], [-1, -t, 0], [1, -t, 0], [0, -1, t], [0, 1, t], [0, -1, -t], [0, 1, -t], [t, 0, -1], [t, 0, 1], [-t, 0, -1], [-t, 0, 1]]
    V = [[c / math.sqrt(sum(q * q for q in v)) for c in v] for v in V]
    F = [[0, 11, 5], [0, 5, 1], [0, 1, 7], [0, 7, 10], [0, 10, 11], [1, 5, 9], [5, 11, 4], [11, 10, 2], [10, 7, 6], [7, 1, 8],
         [3, 9, 4], [3, 4, 2], [3, 2, 6], [3, 6, 8], [3, 8, 9], [4, 9, 5], [2, 4, 11], [6, 2, 10], [8, 6, 7], [9, 8, 1]]
    for _ in range(level):
        cache, F2 = {}, []

        def mid(a, b):
            key = (min(a, b), max(a, b))
            if key not in cache:
                m = [(V[a][i] + V[b][i]) / 2 for i in range(3)]
                n = math.sqrt(sum(q * q for q in m))
                V.append([q / n for q in m])
                cache[key] = len(V) - 1
            return cache[key]
        for a, b, c in F:
            ab, bc, ca = mid(a, b), mid(b, c), mid(c, a)
            F2 += [[a, ab, ca], [b, bc, ab], [c, ca, bc], [ab, bc, ca]]
        F = F2
    return V, F


_ICO = {}


def blob(size, at, m="leaves", seed=0, bumpiness=0.25, detail=2, flat_bottom=0.0, smooth=60, yaw=0.0, **kw):
    """Irregular organic lump: foliage clumps, bushes, rocks, clouds, snow piles. size = [x, y, z] diameter (or one number);
    bumpiness 0 = smooth ellipsoid, 0.4 = very lumpy; flat_bottom 0-1 flattens the underside (rocks, bushes on ground).
    Every seed gives a different shape. at = centre; yaw turns it around Z (stretch along a branch with size x > y)."""
    sx, sy, sz = (size, size, size) if isinstance(size, (int, float)) else size
    if detail not in _ICO:
        _ICO[detail] = _icosphere(detail)
    V, F = _ICO[detail]
    verts = []
    for x, y, z in V:
        d = 1 + bumpiness * _noise(x * 1.7, y * 1.7, z * 1.7, seed)
        zz = z * d
        if flat_bottom and zz < -1 + flat_bottom * 2:
            zz = -1 + flat_bottom * 2 + (zz + 1 - flat_bottom * 2) * 0.15
        px, py = x * d * sx / 2, y * d * sy / 2
        if yaw:
            ca, sa = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))
            px, py = px * ca - py * sa, px * sa + py * ca
        verts.append([round(at[0] + px, 1), round(at[1] + py, 1), round(at[2] + zz * sz / 2, 1)])
    d = {"shape": "raw", "vertices": verts, "triangles": [list(f) for f in F], "smooth_angle": smooth}
    d.update(mat(m) if isinstance(m, str) else m)
    d.update(kw)
    return d


def limb(path, r0, r1=None, m="bark", sides=7, flat=1.0, **kw):
    """Tapered tube along a path [[x, y, z], ...]: trunks, branches, roots, horns, tentacles, vines, fronds.
    r0 = radius at the start, r1 at the end (default r0 * 0.3); flat < 1 squashes the section (leaves, fronds, blades)."""
    r1 = r0 * 0.3 if r1 is None else r1
    n = len(path)
    rings = []
    up_ref = [0.0, 0.0, 1.0]
    for i, p in enumerate(path):
        a = path[max(0, i - 1)]
        b = path[min(n - 1, i + 1)]
        t = [b[k] - a[k] for k in range(3)]
        tl = math.sqrt(sum(q * q for q in t)) or 1.0
        t = [q / tl for q in t]
        ref = up_ref if abs(t[2]) < 0.9 else [1.0, 0.0, 0.0]
        u = [t[1] * ref[2] - t[2] * ref[1], t[2] * ref[0] - t[0] * ref[2], t[0] * ref[1] - t[1] * ref[0]]
        ul = math.sqrt(sum(q * q for q in u)) or 1.0
        u = [q / ul for q in u]
        v = [t[1] * u[2] - t[2] * u[1], t[2] * u[0] - t[0] * u[2], t[0] * u[1] - t[1] * u[0]]
        r = r0 + (r1 - r0) * (i / (n - 1) if n > 1 else 0)
        rings.append([[round(p[k] + r * (math.cos(2 * math.pi * j / sides) * u[k] + flat * math.sin(2 * math.pi * j / sides) * v[k]), 1) for k in range(3)]
                      for j in range(sides)])
    d = {"shape": "loft", "sections": rings, "closed": True, "cap": True, "smooth_angle": 70}
    d.update(mat(m) if isinstance(m, str) else m)
    d.update(kw)
    return d


def curve(start, direction, length, bend=(0, 0, 0), segments=5, wobble=0.0, seed=0):
    """Points along a bending path: start, initial direction [x, y, z], length; bend is added to the direction a bit at
    every step (gravity droop = (0, 0, -1), wind = (1, 0, 0)); wobble adds natural irregularity."""
    rnd = random.Random(seed)
    dl = math.sqrt(sum(q * q for q in direction)) or 1.0
    dvec = [q / dl for q in direction]
    pts = [list(start)]
    step = length / segments
    for _ in range(segments):
        dvec = [dvec[k] + bend[k] / segments + rnd.uniform(-wobble, wobble) for k in range(3)]
        dl = math.sqrt(sum(q * q for q in dvec)) or 1.0
        dvec = [q / dl for q in dvec]
        pts.append([pts[-1][k] + dvec[k] * step for k in range(3)])
    return pts
