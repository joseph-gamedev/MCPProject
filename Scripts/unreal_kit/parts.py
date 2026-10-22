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
