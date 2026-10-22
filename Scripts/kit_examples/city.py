"""EXAMPLE: realistic buildings, streets, props and a whole district, built from unreal_kit.parts.

Read it, copy what you need, change it. Run from the kit folder's parent:
    import sys; sys.path.insert(0, r"<build folder>/unreal_kit.zip"); sys.path.insert(0, r"<build folder>/kit_examples")
    import unreal_kit as uk, city
    uk.place("Corner", city.building(1800, 1400, 5, "brick", seed=3))
    city.city(blocks_x=3, blocks_y=2, seed=4)          # about 30 s, 50000 parts

Make it SPECIFIC, not a copy: decide the place's signature features first, then plug them in with small functions:
    city.add_style("brownstone", wall=[M("sandstone", (110, 72, 56))], trim=["trim_stone"], frame=["frame_dark"])
    def nyc_building(w, d, rng, tall):
        if tall: return city.building(w, d, rng.randint(30, 60), "glass", setbacks=[(0.55, 300), (0.8, 550)], crown="spire")
        return city.building(w, d, rng.randint(4, 6), "brownstone", bay=260, win_h=200, seed=rng.randint(0, 9999))
    city.city(blocks_x=4, blocks_y=3, block_w=8000, block_d=2400, make_building=nyc_building, extras=my_landmarks)
Only the hooks are new code; layout, streets, batching and materials are reused.

Shows: real dimensions (storey 3.2 m, door 2.3 m, sill 0.9 m, curb 15 cm), recessed windows with frames, sills and
lintels, cornices, rooftop clutter, varied colours per building, street furniture, one object per building.
"""

import math
import random

import unreal_kit as uk
from unreal_kit.parts import M, PALETTE, beam, box, cone, cyl, extrude, gable, mat, part, sphere, transform  # noqa: F401

place, place_many = uk.place, uk.place_many

STYLES = {
    "brick": {"wall": ["brick_red", "brick_brown", "brick_buff"], "trim": ["trim_stone", "trim_white"], "frame": ["frame_white", "frame_dark"],
              "roof": ["roof_flat", "roof_slate"], "door": ["door_wood", "door_green"]},
    "plaster": {"wall": ["plaster_white", "plaster_cream", "plaster_ochre", "plaster_terracotta"], "trim": ["trim_white", "trim_stone"],
                "frame": ["frame_wood", "frame_white", "frame_dark"], "roof": ["roof_tile", "roof_slate"], "door": ["door_wood", "door_green"]},
    "stone": {"wall": ["stone_light", "stone_warm"], "trim": ["trim_stone"], "frame": ["frame_dark", "frame_wood"], "roof": ["roof_slate", "roof_flat"],
              "door": ["door_wood"]},
    "office": {"wall": ["concrete", "plaster_grey", "stone_light"], "trim": ["metal_dark", "aluminium"], "frame": ["frame_dark", "aluminium"],
               "roof": ["roof_flat"], "door": ["glass_dark"]},
    "glass": {"wall": ["glass_dark"], "trim": ["aluminium", "metal_dark"], "frame": ["aluminium", "metal_dark"], "roof": ["roof_flat"],
              "door": ["glass_dark"]},
    # industrial brick loft: big steel windows
    "loft": {"wall": ["brick_red", "brick_brown", M("brick", (96, 62, 50))], "trim": ["concrete", "trim_stone"],
             "frame": ["frame_dark", M("painted_iron", (40, 58, 50), roughness=0.45)], "roof": ["roof_flat"], "door": ["frame_dark"]},
    # art deco: pale stone, projecting piers, bronze frames
    "deco": {"wall": [M("limestone", (214, 200, 172)), M("sandstone", (192, 164, 124)), M("limestone", (190, 186, 176))], "trim": ["trim_stone"],
             "frame": [M("bronze", (110, 86, 60), roughness=0.4), "frame_dark"], "roof": ["roof_flat"], "door": [M("bronze", (110, 86, 60))]},
    # modern residential: coloured cladding, balconies
    "residential": {"wall": [M("painted_iron", (214, 212, 204), roughness=0.5), M("painted_iron", (56, 58, 62), roughness=0.5),
                             M("painted_iron", (160, 84, 58), roughness=0.5), M("wood", (150, 108, 70)), M("plaster", (226, 222, 212)),
                             M("painted_iron", (120, 130, 116), roughness=0.5), M("brick", (180, 170, 160))],
                    "trim": ["aluminium", "trim_white", "concrete"], "frame": ["aluminium", "metal_dark"], "roof": ["roof_flat"], "door": ["glass_dark"]},
    # brutalist / late-modern concrete
    "brutalist": {"wall": ["concrete", M("concrete", (182, 178, 168)), M("concrete", (132, 130, 126))], "trim": ["concrete"],
                  "frame": ["metal_dark", "aluminium"], "roof": ["roof_flat"], "door": ["glass_dark"]},
    # painted brick
    "painted": {"wall": [M("brick", (226, 224, 216)), M("brick", (62, 64, 68)), M("brick", (116, 128, 112)), M("brick", (176, 70, 56)),
                         M("brick", (196, 184, 150))], "trim": ["trim_white", "frame_dark"], "frame": ["frame_dark", "frame_white"],
                "roof": ["roof_flat"], "door": ["door_green", "door_wood"]},
}

# Facade recipe choices per style (window types, how often features appear).
FACADE_PREFS = {
    "brick": {"windows": ["punched", "punched", "paired", "tall"], "piers": 0.15, "oriels": 0.25, "fire_escape": 0.5, "balconies": 0.05},
    "plaster": {"windows": ["punched", "tall", "paired", "square"], "piers": 0.05, "oriels": 0.15, "fire_escape": 0.0, "balconies": 0.35},
    "stone": {"windows": ["punched", "tall", "paired", "square"], "piers": 0.35, "oriels": 0.2, "fire_escape": 0.0, "balconies": 0.15},
    "office": {"windows": ["ribbon", "punched", "square", "wide"], "piers": 0.3, "oriels": 0.0, "fire_escape": 0.0, "balconies": 0.0},
    "loft": {"windows": ["wide", "wide", "paired"], "piers": 0.4, "oriels": 0.0, "fire_escape": 0.6, "balconies": 0.0},
    "deco": {"windows": ["tall", "paired", "punched"], "piers": 1.0, "oriels": 0.0, "fire_escape": 0.0, "balconies": 0.0},
    "residential": {"windows": ["tall", "wide", "punched"], "piers": 0.1, "oriels": 0.1, "fire_escape": 0.0, "balconies": 0.85},
    "brutalist": {"windows": ["ribbon", "square", "wide"], "piers": 0.5, "oriels": 0.0, "fire_escape": 0.0, "balconies": 0.2},
    "painted": {"windows": ["punched", "paired", "tall"], "piers": 0.05, "oriels": 0.2, "fire_escape": 0.35, "balconies": 0.1},
}



# ------------------------------------------------------------------ facade details (facade plane y = 0, outside -Y)

def add_style(name, wall, trim, frame, roof=("roof_flat",), door=("door_wood",)):
    """Registers a facade style: lists of PALETTE names or M(...) dicts, e.g.
    add_style("brownstone", wall=[M("sandstone", (120, 82, 62))], trim=["trim_stone"], frame=["frame_dark"])."""
    STYLES[name] = {"wall": list(wall), "trim": list(trim), "frame": list(frame), "roof": list(roof), "door": list(door)}


def window(x, z, w=120, h=160, recess=18, frame="frame_white", sill="trim_stone", lintel="trim_stone", glass="glass", wall_t=30, mullion=True):
    """Window opening centred at x with its sill at height z: recessed glass, frame, stone sill and lintel."""
    P = []
    f = 6
    P.append(box([w, 4, h], [x, recess, z], m=glass))                                   # glass set back
    P.append(box([w + 2 * f, 7, f], [x, recess - 4, z + h], m=frame))                    # head
    P.append(box([w + 2 * f, 7, f], [x, recess - 4, z - f], m=frame))                    # bottom rail
    P.append(box([f, 7, h], [x - w / 2 - f / 2, recess - 4, z], m=frame))                # jambs
    P.append(box([f, 7, h], [x + w / 2 + f / 2, recess - 4, z], m=frame))
    if mullion and w >= 90:
        P.append(box([4, 6, h], [x, recess - 3, z], m=frame))
        P.append(box([w, 6, 4], [x, recess - 3, z + h * 0.62], m=frame))
    P.append(box([w + 24, recess + 8, 6], [x, (recess - 8) / 2, z - 6 - f], m=sill))     # projecting sill
    P.append(box([w + 30, 6, 14], [x, -3, z + h + f], m=lintel))                          # lintel on the face
    return P


def door(x, w=110, h=230, recess=22, m="door_wood", frame="trim_stone", step=True):
    P = [box([w, 5, h], [x, recess, 0], m=m),
         box([w + 24, 8, 16], [x, -4, h], m=frame),
         box([12, 8, h], [x - w / 2 - 6, -4, 0], m=frame), box([12, 8, h], [x + w / 2 + 6, -4, 0], m=frame),
         box([6, 3, 6], [x + w * 0.35, recess - 3, h * 0.45], m="metal")]
    if step:
        P.append(box([w + 40, 40, 15], [x, -20, 0], m="curb"))
    return P


def shopfront(x, w, h=320, m_frame="frame_dark", sign=None):
    """Ground-floor shop: large recessed glazing, stall riser, sign band and an awning."""
    P = [box([w - 40, 40, 50], [x, 20, 0], m=m_frame),                                   # stall riser
         box([w - 40, 4, h - 130], [x, 28, 50], m="glass"),
         box([w, 12, 60], [x, -6, h - 80], m=sign or "metal_dark"),                        # sign band
         box([w - 30, 110, 6], [x, -55, h - 92], rot=[-8, 0, 0], m=random.choice(["plaster_terracotta", "frame_dark", "door_green"]))]
    for k in range(3):
        P.append(box([6, 8, h - 130], [x - (w - 40) / 2 + (w - 40) * (k + 1) / 4, 24, 50], m=m_frame))
    return P


def _windows_in_bay(wtype, bay, floor_h):
    """[(dx, w)], height, sill for one bay of a typical floor."""
    if wtype == "paired":
        w = min(110, bay * 0.3)
        return [(-w / 2 - 12, w), (w / 2 + 12, w)], min(180, floor_h * 0.52), 90
    if wtype == "ribbon":
        return [(0, bay - 26)], floor_h * 0.42, 100
    if wtype == "tall":
        return [(0, min(120, bay * 0.38))], floor_h * 0.7, 40
    if wtype == "square":
        w = min(110, bay * 0.34)
        return [(0, w)], w, 110
    if wtype == "wide":
        return [(0, bay * 0.66)], floor_h * 0.55, 80
    return [(0, min(140, bay * 0.45))], min(190, floor_h * 0.52), 90


def _wall_around(cx, bay, z0, fh, wins, wz, wh, wall, wall_t):
    """Wall boxes filling a bay of one floor around window openings (piers, spandrels, headers)."""
    P = []
    edges = [-bay / 2] + [e for dx, w in sorted(wins) for e in (dx - w / 2, dx + w / 2)] + [bay / 2]
    for i in range(0, len(edges), 2):                                      # piers between openings
        x0, x1 = edges[i], edges[i + 1]
        if x1 - x0 > 1:
            P.append(box([x1 - x0, wall_t, fh], [cx + (x0 + x1) / 2, wall_t / 2, z0], m=wall))
    for dx, w in wins:
        P.append(box([w, wall_t, wz - z0], [cx + dx, wall_t / 2, z0], m=wall))            # spandrel
        top = z0 + fh - (wz + wh)
        if top > 0:
            P.append(box([w, wall_t, top], [cx + dx, wall_t / 2, wz + wh], m=wall))      # header
    return P


def _balcony(cx, w, z, rail, trim):
    P = [box([w, 130, 16], [cx, -65, z - 16], m=trim)]
    if rail == "glass":
        P += [box([w, 3, 105], [cx, -128, z], m="glass"), box([w, 6, 5], [cx, -128, z + 103], m="aluminium")]
        P += [box([3, 125, 105], [cx + s * (w / 2 - 2), -65, z], m="glass") for s in (-1, 1)]
    else:
        P += [box([w, 5, 5], [cx, -128, z + 100], m="metal_dark"), box([w, 4, 4], [cx, -128, z + 10], m="metal_dark")]
        P += [box([3, 3, 100], [cx - w / 2 + i * w / 10, -128, z], m="metal_dark") for i in range(11)]
    return P


def _oriel(cx, w, z, h, wall, frame):
    """Bay window projecting 70 cm: frame boxes and three glazed faces."""
    d = 70
    return [box([w + 40, d + 10, 18], [cx, -d / 2, z - 18], m=wall), box([w + 40, d + 10, 24], [cx, -d / 2, z + h], m=wall),
            box([w + 36, 4, h], [cx, -d, z], m="glass"), box([4, d, h], [cx - w / 2 - 16, -d / 2, z], m="glass"),
            box([4, d, h], [cx + w / 2 + 16, -d / 2, z], m="glass"),
            box([8, 8, h], [cx - w / 2 - 18, -d, z], m=frame), box([8, 8, h], [cx + w / 2 + 18, -d, z], m=frame),
            box([6, 6, h], [cx - w / 6, -d - 2, z], m=frame), box([6, 6, h], [cx + w / 6, -d - 2, z], m=frame)]


def _fire_escape(cx, bay, z0, fh, last):
    P = [box([bay - 30, 100, 8], [cx, -55, z0], m="metal_dark"), box([bay - 30, 4, 4], [cx, -104, z0 + 95], m="metal_dark")]
    P += [box([3, 3, 95], [cx - (bay - 30) / 2 + i * (bay - 30) / 8, -104, z0], m="metal_dark") for i in range(9)]
    if not last:
        P.append(beam([cx - bay / 2 + 40, -60, z0 + 8], [cx + bay / 2 - 80, -60, z0 + fh], 10, m="metal_dark"))
    return P


def facade(width, floors, floor_h=320, ground_h=420, bays=None, wall="brick_red", trim="trim_stone", frame="frame_white", door_bay=None,
           shop=False, win_w=None, win_h=None, wall_t=30, rng=None, wtype="punched", bands="every", piers=False, cornice="cornice", attic=False,
           balconies=(), rail="metal", oriels=(), fire_escape=None):
    """One wall with real openings. Recipe knobs (so neighbours differ):
    wtype punched | paired | ribbon | tall | square | wide; bands every | base | thick | none; piers (projecting pilasters);
    cornice cornice | heavy | parapet | none; attic (short top floor); balconies / oriels: bay indices; fire_escape: bay index."""
    rng = rng or random
    bays = bays or max(1, int(width / 330))
    bay = width / bays
    P = []
    height = ground_h + (floors - 1) * floor_h
    P.append(box([width + 6, wall_t + 8, 60], [0, (wall_t - 8) / 2 - 4, 0], m=trim))                  # plinth
    for f in range(floors):
        z0 = 0 if f == 0 else ground_h + (f - 1) * floor_h
        fh = ground_h if f == 0 else floor_h
        top_floor = f == floors - 1
        for b in range(bays):
            cx = -width / 2 + bay * (b + 0.5)
            if f == 0 and shop:
                P += shopfront(cx, bay - 30, fh, sign=rng.choice(["metal_dark", "frame_dark", "door_green", "plaster_terracotta"]))
                P.append(box([30, wall_t, fh], [cx + bay / 2 - 15, wall_t / 2, z0], m=wall))
                continue
            if f == 0 and door_bay == b:
                P += door(cx, recess=wall_t * 0.7)
                P.append(box([(bay - 134) / 2, wall_t, fh], [cx - bay / 2 + (bay - 134) / 4, wall_t / 2, z0], m=wall))
                P.append(box([(bay - 134) / 2, wall_t, fh], [cx + bay / 2 - (bay - 134) / 4, wall_t / 2, z0], m=wall))
                P.append(box([134, wall_t, fh - 246], [cx, wall_t / 2, z0 + 246], m=wall))
                continue
            if f == 0:
                wins, wh, sill = [(0, win_w or min(140, bay * 0.45))], win_h or min(190, fh * 0.52), None
                wz = z0 + max(90, (fh - wh) * 0.55)
            else:
                wins, wh, sill = _windows_in_bay(wtype, bay, fh)
                if win_w and wtype == "punched":
                    wins = [(0, win_w)]
                if win_h and wtype == "punched":
                    wh = win_h
                balcony = b in balconies and not (attic and top_floor)
                if balcony:                                                        # French door onto the balcony
                    wins, wh, sill = [(0, min(150, bay * 0.5))], fh * 0.72, 8
                if attic and top_floor:
                    wh, sill = wh * 0.55, 70
                wz = z0 + sill
            P += _wall_around(cx, bay, z0, fh, wins, wz, wh, wall, wall_t)
            for dx, w in wins:
                P += window(cx + dx, wz, w, wh, recess=min(20, wall_t * 0.7), frame=frame, sill=trim, lintel=trim, wall_t=wall_t,
                            mullion=wtype != "ribbon" or w < 200)
                if wtype == "ribbon" and f > 0 and w >= 200:                        # ribbon mullions
                    P += [box([5, 6, wh], [cx + dx - w / 2 + k * w / int(w / 140), min(20, wall_t * 0.7) - 3, wz], m=frame)
                          for k in range(1, int(w / 140))]
            if f > 0 and b in balconies and not (attic and top_floor):
                P += _balcony(cx, min(bay - 20, 260), z0, rail, trim)
            if f > 0 and b in oriels and not top_floor:
                P += _oriel(cx, wins[0][1] if len(wins) == 1 else bay * 0.6, wz, wh, wall, frame)
            if f > 0 and b == fire_escape:
                P += _fire_escape(cx, bay, z0, fh, top_floor)
        if f > 0:
            if bands == "every" or (bands == "base" and f == 1):
                P.append(box([width + 4, 8, 12], [0, -4, z0 - 6], m=trim))                               # string course
            elif bands == "thick":
                P.append(box([width + 10, 14, 46], [0, -7, z0 - 23], m=trim))                            # spandrel band
            if attic and top_floor:
                P.append(box([width + 20, 20, 30], [0, -10, z0 - 15], m=trim))
    if piers:                                                                                     # pilasters up the facade
        for b in range(bays + 1):
            x = -width / 2 + b * bay
            P.append(box([44, 18, height - ground_h + 20], [max(-width / 2 + 22, min(width / 2 - 22, x)), -9, ground_h - 20], m=trim))
    if cornice == "heavy":
        P.append(box([width + 90, wall_t + 100, 46], [0, (wall_t - 100) / 2 - 6, height], m=trim))
        P += [box([18, 70, 36], [-width / 2 + 40 + i * (width - 80) / max(1, int(width / 140)), -40, height - 36], m=trim)
              for i in range(int(width / 140) + 1)]
    elif cornice == "parapet":
        P.append(box([width, wall_t, 100], [0, wall_t / 2, height], m=wall))
        P.append(box([width + 12, wall_t + 12, 10], [0, wall_t / 2, height + 100], m=trim))
    elif cornice == "none":
        P.append(box([width + 4, wall_t + 4, 8], [0, wall_t / 2, height], m="metal_dark"))
    else:
        P.append(box([width + 30, wall_t + 40, 28], [0, (wall_t - 40) / 2 - 6, height], m=trim))               # cornice
        P.append(box([width + 10, wall_t + 14, 14], [0, wall_t / 2 - 7, height + 28], m=trim))
    return P


# ------------------------------------------------------------------ buildings

def building(width=1600, depth=1200, floors=4, style="brick", roof="flat", seed=None, shop=None, floor_h=320, ground_h=None,
             win_w=None, win_h=None, bay=340, wall_t=30, setbacks=None, crown=None):
    """A complete building centred on its footprint: four facades, roof (flat with parapet + rooftop units, gable or hip),
    downspouts. style: a STYLES name (brick | plaster | stone | office | glass | your add_style) or a style dict.
    Proportions: win_w / win_h (window size cm), bay (window spacing cm), wall_t (wall thickness).
    Glass towers also take setbacks [(height fraction, inset cm), ...] and crown "spire" | "flat". Returns parts."""
    rng = random.Random(seed)
    S = style if isinstance(style, dict) else STYLES[style]
    wall = rng.choice(S["wall"])
    trim = rng.choice(S["trim"])
    frame = rng.choice(S["frame"])
    shop = (rng.random() < 0.5) if shop is None else shop
    ground_h = ground_h or (420 if shop else 340)
    height = ground_h + (floors - 1) * floor_h
    t = wall_t
    if style == "glass":
        return glass_tower(width, depth, floors, floor_h, rng, setbacks=setbacks, crown=crown)
    P = []
    # facade recipe: one set of choices per building, used on all four sides
    prefs = FACADE_PREFS.get(style if isinstance(style, str) else "", FACADE_PREFS["brick"])
    wtype = rng.choice(prefs["windows"])
    bands = rng.choice(["every", "every", "base", "thick", "none"]) if wtype != "ribbon" else rng.choice(["thick", "none"])
    piers = rng.random() < prefs["piers"] and wtype not in ("ribbon",)
    cornice = rng.choice(["cornice", "heavy", "parapet", "none"]) if style not in ("office", "residential", "brutalist") else rng.choice(["parapet", "none"])
    attic = floors >= 5 and rng.random() < 0.35
    rail = rng.choice(["glass", "metal"]) if style in ("residential", "brutalist", "office") else "metal"
    bal_mode = rng.choice(["all", "alternate", "centre"]) if rng.random() < prefs["balconies"] else None
    oriel_mode = rng.choice(["alternate", "pair"]) if rng.random() < prefs["oriels"] and wtype != "ribbon" else None
    fire = rng.random() < prefs["fire_escape"] and floors >= 4
    sides = [(width, (0, -depth / 2, 0), 0), (depth, (width / 2, 0, 0), 90), (width, (0, depth / 2, 0), 180), (depth, (-width / 2, 0, 0), 270)]
    for k, (w, off, yaw) in enumerate(sides):
        front = k == 0
        bays = max(1, int(w / bay))
        long_side = k % 2 == 0
        balconies = ()
        if bal_mode and long_side:
            balconies = set(range(bays)) if bal_mode == "all" else {b for b in range(bays) if b % 2 == 1} if bal_mode == "alternate" else {bays // 2}
        oriels = ()
        if oriel_mode and long_side:
            oriels = {b for b in range(bays) if b % 2 == 0} if oriel_mode == "alternate" else {0, bays - 1}
            oriels -= set(balconies)
        f = facade(w - (0 if k % 2 == 0 else 2 * t), floors, floor_h, ground_h, bays, wall, trim, frame,
                   door_bay=(bays // 2 if front and not shop else None), shop=shop and front, win_w=win_w, win_h=win_h, wall_t=t, rng=rng,
                   wtype=wtype, bands=bands, piers=piers, cornice=cornice, attic=attic, balconies=balconies, rail=rail, oriels=oriels,
                   fire_escape=(1 if bays > 2 else 0) if fire and front else None)
        P += transform(f, off, yaw)
    P.append(box([width - 2 * t, depth - 2 * t, 20], [0, 0, height - 20], m="roof_flat"))                     # roof slab
    if roof == "gable":
        P.append(gable(width + 60, depth + 60, min(width, depth) * 0.35, [0, 0, height + 28], m=rng.choice(["roof_tile", "roof_slate"])))
    elif roof == "hip":
        P.append({"shape": "pyramid", "size": [width + 60, depth + 60, min(width, depth) * 0.3], "location": [0, 0, height + 28],
                  **mat(rng.choice(["roof_slate", "roof_tile"]))})
    else:
        P.append(box([width - 2 * t, depth - 2 * t, 4], [0, 0, height], m="tar"))
        for _ in range(rng.randint(1, 3)):                                                                   # rooftop units
            ux, uy = rng.uniform(-width / 3, width / 3), rng.uniform(-depth / 3, depth / 3)
            P.append(box([rng.choice([120, 160, 220]), rng.choice([90, 120]), rng.choice([90, 120, 150])], [ux, uy, height + 4], m="metal"))
        if rng.random() < 0.4:
            P.append(cyl(220, 260, [rng.uniform(-width / 4, width / 4), rng.uniform(-depth / 4, depth / 4), height + 4], m="frame_wood"))
        P.append(box([180, 140, 260], [width / 2 - 200, depth / 2 - 160, height + 4], m=wall))             # stair head
        extra = rng.random()
        if floors >= 5 and extra < 0.25 and width > 900 and depth > 900:                                     # set-back penthouse
            pw, pd = width - 400, depth - 400
            P.append(box([pw, pd, floor_h - 20], [0, 0, height + 4], m=rng.choice(["glass_dark", wall, "aluminium"])))
            P.append(box([pw + 60, pd + 60, 20], [0, 0, height + floor_h - 16], m=trim))
            P.append(box([width - 40, 4, 100], [0, -depth / 2 + 20, height + 4], m="glass"))                 # terrace railing
        elif extra < 0.45:                                                                                   # roof garden + pergola
            P.append(box([width * 0.5, depth * 0.4, 40], [-width * 0.1, 0, height + 4], m="soil"))
            P.append(box([width * 0.48, depth * 0.38, 12], [-width * 0.1, 0, height + 44], m="grass"))
            for i in range(5):
                P.append(box([10, depth * 0.4, 10], [-width * 0.35 + i * width * 0.12, 0, height + 260], m="frame_wood"))
            for sx in (-1, 1):
                for sy in (-1, 1):
                    P.append(box([12, 12, 256], [-width * 0.1 + sx * width * 0.24, sy * depth * 0.18, height + 4], m="frame_wood"))
    for x in (-width / 2 + 25, width / 2 - 25):                                                               # downspouts
        P.append(cyl(10, height, [x, -depth / 2 - 10, 0], m="metal_dark"))
    return P


# Tower vocabulary: every tower picks a tint, a skin, a shape, a top and a podium, so no two look alike.
TOWER_TINTS = {"blue": (38, 66, 96), "green": (40, 84, 78), "grey": (62, 66, 72), "bronze": (96, 74, 52), "black": (20, 22, 26),
               "silver": (126, 134, 142), "teal": (32, 78, 92), "champagne": (150, 134, 108), "forest": (34, 64, 50), "smoke": (44, 46, 50)}
TOWER_SKINS = ["grid", "unitized", "fins", "bands", "ribbon", "frame", "diagrid"]
TOWER_SHAPES = ["box", "setback", "taper", "chamfer", "round"]
TOWER_TOPS = ["mech", "spire", "slant", "pyramid", "lattice", "stepped", "helipad"]
TOWER_PODIUMS = ["stone", "glass", "plaza"]


def _glass(tint):
    r, g, b = TOWER_TINTS[tint] if isinstance(tint, str) else tint
    return M("steel", (r, g, b), roughness=0.05, metallic=0.0)


def _face_skin(skin, w, h, floor_h, metal, solid, rng):
    """Skin parts for one rectangular face in face space: x along the face (centred), y outward is negative, z from 0."""
    P = []
    floors = max(1, int(h // floor_h))
    if skin in ("grid", "unitized", "fins", "bands"):
        step = {"grid": 150, "unitized": 300, "fins": 300, "bands": 300}[skin]
        n = max(2, int(w / step))
        depth, thick = (70, 14) if skin == "fins" else (14, 8 if skin == "unitized" else 10)
        P += [box([thick, depth, h], [-w / 2 + i * w / n, -depth / 2 + 2, 0], m=metal) for i in range(n + 1)]
    if skin == "bands":   # opaque spandrel band on every floor
        P += [box([w + 4, 10, floor_h * 0.32], [0, -5, f * floor_h], m=solid) for f in range(floors)]
    elif skin == "ribbon":  # thick white bands, glass ribbons between
        P += [box([w + 16, 26, floor_h * 0.45], [0, -12, f * floor_h], m=solid) for f in range(floors)]
    elif skin == "frame":   # concrete / stone grid: piers and spandrels, punched glass
        bay = rng.choice((300, 360))
        n = max(2, int(w / bay))
        P += [box([60, 34, h], [-w / 2 + i * w / n, -15, 0], m=solid) for i in range(n + 1)]
        P += [box([w, 30, floor_h * 0.3], [0, -13, f * floor_h], m=solid) for f in range(floors)]
    elif skin == "diagrid":  # steel diagonals between ring beams every 4 floors
        span = 4 * floor_h
        n = max(2, int(w / 600))
        for k in range(int(h // span)):
            z = k * span
            P.append(box([w + 10, 30, 40], [0, -14, z], m=metal))
            for i in range(n):
                x0, x1 = -w / 2 + i * w / n, -w / 2 + (i + 1) * w / n
                a, b2 = ((x0, z), (x1, z + span)) if (i + k) % 2 == 0 else ((x1, z), (x0, z + span))
                P.append(beam([a[0], -16, a[1]], [b2[0], -16, b2[1]], 34, m=metal))
    else:  # thin floor lines for the mullion skins
        P += [box([w + 6, 12, 12], [0, -6, f * floor_h - 6], m=metal) for f in range(1, floors)]
    return P


def _rect_segment(w, d, z0, h, floor_h, glass, skin, metal, solid, rng):
    P = [box([w, d, h], [0, 0, z0], m=glass)]
    for sw, off, yaw in ((w, (0, -d / 2, z0), 0), (d, (w / 2, 0, z0), 90), (w, (0, d / 2, z0), 180), (d, (-w / 2, 0, z0), 270)):
        P += transform(_face_skin(skin, sw, h, floor_h, metal, solid, rng), off, yaw)
    P.append(box([w + 30, d + 30, 50], [0, 0, z0 + h - 50], m=metal))            # cornice band at the top of the segment
    return P


def _octagon(w, d, c):
    return [[-w / 2 + c, -d / 2], [w / 2 - c, -d / 2], [w / 2, -d / 2 + c], [w / 2, d / 2 - c], [w / 2 - c, d / 2], [-w / 2 + c, d / 2],
            [-w / 2, d / 2 - c], [-w / 2, -d / 2 + c]]


def glass_tower(width, depth, floors, floor_h, rng, setbacks=None, crown=None, skin=None, shape=None, tint=None, podium=None):
    """Curtain-wall tower with a random (or given) tint, skin, shape, top and podium:
    tint   TOWER_TINTS name or (r, g, b); skin grid | unitized | fins | bands | ribbon | frame | diagrid;
    shape  box | setback | taper | chamfer | round; crown mech | spire | slant | pyramid | lattice | stepped | helipad
           (old names: "flat" = mech); podium stone | glass | plaza; setbacks [(height fraction, inset cm)].
    Returns parts (centred on the footprint, ground at z 0)."""
    tint = tint or rng.choice(list(TOWER_TINTS))
    skin = skin or rng.choice(TOWER_SKINS)
    shape = shape or ("setback" if setbacks else rng.choice(TOWER_SHAPES))
    crown = {"flat": "mech", None: None}.get(crown, crown) or rng.choice(TOWER_TOPS)
    podium = podium or rng.choice(TOWER_PODIUMS)
    glass = _glass(tint)
    metal = rng.choice(["aluminium", "aluminium", "metal_dark", M("bronze", (110, 86, 60), roughness=0.4)])
    solid = rng.choice(["concrete", "stone_light", "trim_white", M("limestone", (214, 206, 190)), M("granite", (90, 90, 92))])
    if skin == "frame":
        glass = _glass(rng.choice(("black", "grey", "blue")))
    P = []
    # --- podium: 2-3 storeys covering the lot, or an open plaza with planters
    pod_h = 0.0
    if podium == "stone":
        pod_h = rng.choice((900, 1200))
        P.append(box([width + 300, depth + 300, pod_h], [0, 0, 0], m=rng.choice(["stone_light", "stone_warm", M("granite", (110, 108, 104))])))
        P += [box([width + 320, 30, 340], [0, -(depth + 300) / 2 - 10, 60], m=_glass("black"))]                  # shop glazing
        P.append(box([width + 330, depth + 330, 40], [0, 0, pod_h - 40], m=metal))
    elif podium == "glass":
        pod_h = 600
        P.append(box([width + 400, depth + 400, pod_h], [0, 0, 0], m=_glass(rng.choice(("grey", "smoke", "black")))))
        P.append(box([width + 420, depth + 420, 30], [0, 0, pod_h - 30], m=metal))
        P.append(box([800, 300, 25], [0, -(depth + 400) / 2 - 150, 420], m=metal))                                 # entrance canopy
    else:
        P.append(box([width + 1200, depth + 1200, 15], [0, 0, 0], m=M("granite", (150, 148, 142))))                # plaza paving
        for sx in (-1, 1):
            P.append(box([300, 300, 60], [sx * (width / 2 + 400), -(depth / 2 + 400), 15], m="concrete"))
            P.append(box([280, 280, 10], [sx * (width / 2 + 400), -(depth / 2 + 400), 75], m="grass"))
        P.append(box([width + 60, depth + 60, 500], [0, 0, 0], m=_glass("black")))                                 # double-height lobby
    height = pod_h + floors * floor_h
    # --- shaft
    if shape in ("box", "setback", "taper"):
        if shape == "taper":
            steps = [(k / 5, k * rng.choice((80, 120))) for k in range(5)]
        else:
            steps = [(0.0, 0.0)] + sorted(setbacks or ([(rng.uniform(0.5, 0.7), rng.choice((200, 300)))] if shape == "setback" else []))
        for k, (frac, inset) in enumerate(steps):
            z0 = pod_h + floors * floor_h * frac
            z1 = pod_h + floors * floor_h * (steps[k + 1][0] if k + 1 < len(steps) else 1.0)
            w, d = max(400, width - 2 * inset), max(400, depth - 2 * inset)
            P += _rect_segment(w, d, z0, z1 - z0, floor_h, glass, skin, metal, solid, rng)
        top_w, top_d = max(400, width - 2 * steps[-1][1]), max(400, depth - 2 * steps[-1][1])
    elif shape == "chamfer":
        c = min(width, depth) * rng.uniform(0.15, 0.25)
        h = floors * floor_h
        P.append(extrude(_octagon(width, depth, c), h, [0, 0, pod_h], m=glass))
        for f in range(1, floors):  # floor lines: thin slightly larger octagons
            if f % (1 if skin in ("bands", "ribbon", "frame") else 1) == 0:
                hh = floor_h * (0.32 if skin in ("bands", "ribbon", "frame") else 0.04)
                P.append(extrude(_octagon(width + 16, depth + 16, c + 3), hh, [0, 0, pod_h + f * floor_h], m=solid if skin in ("bands", "ribbon", "frame") else metal))
        for x, y in _octagon(width + 10, depth + 10, c + 2):
            P.append(box([16, 16, h], [x, y, pod_h], m=metal))
        top_w, top_d = width, depth
    else:  # round
        dia = min(width, depth)
        h = floors * floor_h
        P.append(cyl(dia, h, [0, 0, pod_h], m=glass))
        heavy = skin in ("bands", "ribbon", "frame")
        for f in range(1, floors):
            P.append(cyl(dia + 20, floor_h * (0.3 if heavy else 0.04), [0, 0, pod_h + f * floor_h], m=solid if heavy else metal))
        n = max(12, int(math.pi * dia / 300))
        for i in range(n):
            a = 2 * math.pi * i / n
            P.append(box([14, 40 if skin == "fins" else 14, h], [math.cos(a) * dia / 2, math.sin(a) * dia / 2, pod_h], rot=[0, math.degrees(a), 0], m=metal))
        top_w = top_d = dia * 0.75
    # --- top
    if crown == "spire":
        P.append(box([top_w * 0.5, top_d * 0.5, 600], [0, 0, height], m=metal))
        P.append(cone(min(top_w, top_d) * 0.3, height * 0.15, [0, 0, height + 600], m="aluminium", top_ratio=0.02))
    elif crown == "slant":
        P.append(part("wedge", [top_w, top_d, min(top_w, top_d) * rng.uniform(0.35, 0.6)], [0, 0, height], m=glass, rot=[0, rng.choice((0, 90, 180, 270)), 0]))
    elif crown == "pyramid":
        P.append(box([top_w * 0.8, top_d * 0.8, 400], [0, 0, height], m=solid))
        P.append(part("pyramid", [top_w * 0.8, top_d * 0.8, max(top_w, top_d) * 0.45], [0, 0, height + 400], m=rng.choice([metal, M("verdigris", (90, 140, 120)), M("copper", (170, 110, 70))])))
    elif crown == "lattice":  # the facade fins carry on 8 m above the roof as an open screen
        for sw, off, yaw in ((top_w, (0, -top_d / 2, height), 0), (top_d, (top_w / 2, 0, height), 90), (top_w, (0, top_d / 2, height), 180),
                             (top_d, (-top_w / 2, 0, height), 270)):
            n = max(3, int(sw / 200))
            P += transform([box([14, 60, 800], [-sw / 2 + i * sw / n, -20, 0], m=metal) for i in range(n + 1)] + [box([sw, 40, 30], [0, -20, 780], m=metal)], off, yaw)
        P.append(box([top_w * 0.5, top_d * 0.5, 450], [0, 0, height], m="metal_dark"))
    elif crown == "stepped":
        for k, f in enumerate((0.8, 0.6, 0.4)):
            P.append(box([top_w * f, top_d * f, 350], [0, 0, height + k * 350], m=glass if k % 2 == 0 else solid))
        P.append(cyl(30, 900, [0, 0, height + 1050], m="metal"))
    elif crown == "helipad":
        P.append(box([top_w * 0.6, top_d * 0.6, 300], [0, 0, height], m="metal_dark"))
        P.append(cyl(min(top_w, top_d) * 0.75, 30, [0, 0, height + 300], m=M("concrete", (90, 92, 92))))
        P.append(cyl(min(top_w, top_d) * 0.45, 32, [0, 0, height + 300], m="marking"))
        P.append(cyl(min(top_w, top_d) * 0.4, 34, [0, 0, height + 300], m=M("concrete", (90, 92, 92))))
    else:  # mech: plant screen and units
        P.append(box([top_w * 0.6, top_d * 0.6, 450], [0, 0, height], m="metal_dark"))
        for _ in range(rng.randint(1, 3)):
            P.append(box([rng.choice((200, 300)), rng.choice((150, 250)), 200], [rng.uniform(-top_w / 4, top_w / 4), rng.uniform(-top_d / 4, top_d / 4), height + 450], m="metal"))
    return P


# ------------------------------------------------------------------ street and props

def lamp_post(x, y, yaw=0):
    return transform([cyl(18, 30, [0, 0, 0], m="metal_dark"), cyl(10, 600, [0, 0, 30], m="metal_dark"),
                      box([140, 10, 10], [55, 0, 615], m="metal_dark"),
                      box([50, 26, 12], [120, 0, 600], m=M("painted_iron", (40, 40, 42), emissive=[1.0, 0.82, 0.6], emissive_strength=0.0))],
                     (x, y, 0), yaw)


def tree(x, y, height=None, rng=None):
    """Street/park tree: tapered trunk, branch stubs and several overlapping, varied foliage clumps."""
    rng = rng or random
    h = height or rng.uniform(650, 1000)
    trunk_h = h * 0.45
    P = [cone(h * 0.045, trunk_h + h * 0.15, [x, y, 0], m="bark", top_ratio=0.5)]
    crown = h - trunk_h                                   # crown is ~55% of the tree, about 0.6 x h wide
    for _ in range(rng.randint(7, 11)):
        r = rng.uniform(crown * 0.2, crown * 0.32)
        a = rng.uniform(0, 2 * math.pi)
        d = rng.uniform(0, crown * 0.3)
        z = trunk_h + rng.uniform(0.1, 0.75) * crown - r
        P.append(sphere(r * 2, [x + math.cos(a) * d, y + math.sin(a) * d, z],
                        m=rng.choice(["leaves", "leaves", "leaves_light"]), scale=[1, 1, rng.uniform(0.8, 0.95)]))
    return P


def bench(x, y, yaw=0):
    P = [box([180, 12, 6], [0, s, 45], m="frame_wood") for s in (-18, -4, 10)]
    P += [box([180, 6, 12], [0, 18, 55 + k * 14], m="frame_wood") for k in range(2)]
    P += [box([8, 50, 45], [sx, 0, 0], m="metal_dark") for sx in (-75, 75)]
    return transform(P, (x, y, 0), yaw)


def bollard(x, y):
    return [cyl(16, 90, [x, y, 0], m="metal_dark"), cyl(18, 6, [x, y, 90], m="metal_dark")]


def bin_(x, y):
    return [cyl(56, 95, [x, y, 0], m="metal_dark"), cyl(60, 6, [x, y, 95], m="metal")]


def car(x, y, yaw=0, rng=None):
    """Sedan, 4.6 x 1.8 x 1.45 m: body, cabin with dark glass, four wheels, lights."""
    rng = rng or random
    paint = M("painted_iron", rng.choice([(150, 22, 24), (30, 40, 70), (190, 190, 186), (40, 40, 42), (120, 124, 128), (90, 100, 70)]), roughness=0.28)
    P = [box([460, 180, 70], [0, 0, 32], m=paint),
         box([250, 160, 50], [-20, 0, 102], m="glass_dark"),
         box([230, 150, 8], [-20, 0, 150], m=paint)]
    for wx in (-145, 145):
        for wy in (-82, 82):
            P.append({"shape": "cylinder", "size": [64, 64, 22], "location": [wx, wy, 32], "rotation": [90, 0, 0], "detail": "low", **mat("tar")})
    P += [box([4, 30, 12], [232, s, 70], m=M("plaster", (240, 240, 230), emissive=[1, 1, 0.9], emissive_strength=0.0)) for s in (-60, 60)]
    P += [box([4, 30, 10], [-232, s, 72], m=M("plaster", (150, 20, 20))) for s in (-60, 60)]
    return transform(P, (x, y, 0), yaw)


def street(length=6000, width=1050, sidewalk=320, seed=None, props=True, cars=0.3, trees=True):
    """Street along X centred on y = 0: asphalt with lane markings, 15 cm curbs, block-paved sidewalks,
    lamp posts every 25 m, trees in pits, bollards, benches, bins and parked cars."""
    rng = random.Random(seed)
    P = [box([length, width, 4], [0, 0, -4], m="asphalt")]
    for k in range(int(length / 900)):                                                                   # centre dashes
        P.append(box([300, 14, 1], [-length / 2 + 450 + k * 900, 0, 0], m="marking"))
    for s in (-1, 1):
        y_curb = s * (width / 2 + 8)
        P.append(box([length, 16, 15], [0, y_curb, -4], m="curb"))
        P.append(box([length, sidewalk, 14], [0, s * (width / 2 + 16 + sidewalk / 2), -3], m="sidewalk"))
        if not props:
            continue
        for k in range(int(length / 2500) + 1):
            x = -length / 2 + 300 + k * 2500
            P += lamp_post(x, s * (width / 2 + 60), 90 if s < 0 else 270)
            tx = x + 1250
            if trees and tx < length / 2 - 200:
                P.append(box([140, 140, 2], [tx, s * (width / 2 + 120), 10], m="soil"))
                P += tree(tx, s * (width / 2 + 120), rng=rng)
        for k in range(int(length / 1800)):
            x = -length / 2 + 900 + k * 1800
            choice = rng.random()
            yy = s * (width / 2 + 16 + sidewalk - 70)
            if choice < 0.3:
                P += bench(x, yy, 0 if s > 0 else 180)
            elif choice < 0.5:
                P += bin_(x, yy)
            elif choice < 0.65:
                P += bollard(x, s * (width / 2 + 40))
        for k in range(int(length / 700)):
            if rng.random() < cars:
                P += car(-length / 2 + 400 + k * 700 + rng.uniform(-60, 60), s * (width / 2 - 140), 0 if s > 0 else 180, rng)
    return P


def plaza(width=2000, depth=2000, seed=None):
    """Paved square with a lawn, trees, benches and lamps."""
    rng = random.Random(seed)
    P = [box([width, depth, 10], [0, 0, -2], m="cobble"),
         box([width * 0.5, depth * 0.5, 12], [0, 0, -2], m="grass"),
         box([width * 0.5 + 30, depth * 0.5 + 30, 14], [0, 0, -4], m="curb")]
    for k in range(6):
        a = k * math.pi / 3
        P += tree(math.cos(a) * width * 0.18, math.sin(a) * depth * 0.18, rng=rng)
    for s in (-1, 1):
        P += bench(0, s * depth * 0.33, 0 if s > 0 else 180)
        P += lamp_post(s * width * 0.4, s * depth * 0.4)
    return P


# ------------------------------------------------------------------ districts

def city_block(width=6000, depth=4000, seed=None, styles=("brick", "plaster", "stone", "office"), max_floors=8, towers=0.0,
               make_building=None, lots=(1200, 1500, 1800, 2400), courtyard=True, lot_depth=None):
    """Perimeter block: varied buildings around the edge, lots 12-24 m wide, heights and styles mixed.
    make_building(width, depth, rng, tall) -> parts replaces the default building (your own building types).
    Returns a list of (parts, location, name) so each building can be its own object."""
    rng = random.Random(seed)
    out = []
    lot_depth = lot_depth or min(1400, depth * 0.4)
    for side, (length, yaw, off) in enumerate([(width, 0, (0, -depth / 2 + lot_depth / 2)), (width, 180, (0, depth / 2 - lot_depth / 2))]):
        x = -length / 2
        i = 0
        while x < length / 2 - 600:
            w = min(rng.choice(lots), length / 2 - x)
            if w < 600:
                break
            tall = rng.random() < towers
            if make_building:
                parts = make_building(w - 20, lot_depth, rng, tall)
                cx = x + w / 2
                out.append((transform(parts, (0, 0, 0), yaw), (off[0] + (cx if side == 0 else -cx), off[1], 0), f"bldg_{side}_{i}"))
                x += w
                i += 1
                continue
            style = "glass" if tall else rng.choice(styles)
            floors = rng.randint(12, 30) if tall else rng.randint(3, max_floors)
            parts = building(w - 20, lot_depth, floors, style, roof=rng.choice(["flat", "flat", "flat", "gable"]) if style in ("brick", "plaster") and floors <= 4 else "flat",
                             seed=rng.randint(0, 10 ** 6))
            cx = x + w / 2
            loc = (off[0] + (cx if side == 0 else -cx), off[1], 0)
            out.append((transform(parts, (0, 0, 0), yaw), loc, f"bldg_{side}_{i}"))
            x += w
            i += 1
    if courtyard:
        out.append((plaza(width * 0.55, depth - 2 * lot_depth - 200, seed=rng.randint(0, 999)), (0, 0, 0), "courtyard"))
    return out


def city(blocks_x=3, blocks_y=2, block_w=6000, block_d=4000, street_w=1050, sidewalk=320, seed=1, towers=0.15, group="City",
         max_floors=8, styles=("brick", "plaster", "stone", "office"), make_building=None, make_street=None, extras=None, ground="soil",
         street_trees=None, lots=(1200, 1500, 1800, 2400), lot_depth=None, courtyard=True):
    """A district: a grid of perimeter blocks separated by complete streets. Builds it (place) and returns the group.
    Hooks for a specific place: make_building(width, depth, rng, tall) -> parts (your building types),
    make_street(length, width, sidewalk, rng) -> parts (your street dressing), extras(rng, pitch_x, pitch_y) ->
    [(name, parts, location)] (landmarks, parks, water...). Everything else stays the same.
    street_trees = {species: weight} plants REAL trees (unreal_kit.assets) along every street, e.g. {"maple": 2, "oak": 1};
    default: real trees when the game has them, else simple ones."""
    if street_trees is None:
        try:
            uk.assets.catalog()
            street_trees = {"maple": 2, "oak": 1, "birch": 1}
        except Exception:
            street_trees = {}
    rng = random.Random(seed)
    pitch_x = block_w + street_w + 2 * sidewalk
    pitch_y = block_d + street_w + 2 * sidewalk
    jobs = []
    for i in range(blocks_x):
        for j in range(blocks_y):
            bx = (i - (blocks_x - 1) / 2) * pitch_x
            by = (j - (blocks_y - 1) / 2) * pitch_y
            for parts, loc, name in city_block(block_w, block_d, seed=rng.randint(0, 10 ** 6), towers=towers, max_floors=max_floors, styles=styles,
                                               make_building=make_building, lots=lots, lot_depth=lot_depth, courtyard=courtyard):
                jobs.append((f"{name}_{i}_{j}", parts, (bx + loc[0], by + loc[1], 0)))
    for j in range(blocks_y + 1):
        y = (j - blocks_y / 2) * pitch_y
        sx = make_street(blocks_x * pitch_x, street_w, sidewalk, rng) if make_street else street(blocks_x * pitch_x, street_w, sidewalk, seed=rng.randint(0, 999),
                                                                                                 trees=not street_trees)
        jobs.append((f"street_x_{j}", sx, (0, y, 0)))
    for i in range(blocks_x + 1):
        x = (i - blocks_x / 2) * pitch_x
        ln = blocks_y * pitch_y - street_w - 2 * sidewalk
        sy = make_street(ln, street_w, sidewalk, rng) if make_street else street(ln, street_w, sidewalk, seed=rng.randint(0, 999), cars=0.15, trees=not street_trees)
        jobs.append((f"street_y_{i}", transform(sy, (0, 0, 0), 90), (x, 0, 0)))
    jobs.append(("ground", [box([blocks_x * pitch_x + 8000, blocks_y * pitch_y + 8000, 30], [0, 0, -34], m=ground)], (0, 0, 0)))
    if extras:
        jobs += extras(rng, pitch_x, pitch_y)
    place_many(jobs, group=group)
    if street_trees:  # real trees in the sidewalk band of every street (GPU instances)
        band = (street_w / 2 + 90, street_w / 2 + 160)
        for j in range(blocks_y + 1):
            y = (j - blocks_y / 2) * pitch_y
            uk.assets.along([[-blocks_x * pitch_x / 2, y], [blocks_x * pitch_x / 2, y]], street_trees, spacing=1100, offset=band, seed=rng.randint(0, 999),
                            name=f"trees_x_{j}")
        for i in range(blocks_x + 1):
            x = (i - blocks_x / 2) * pitch_x
            uk.assets.along([[x, -blocks_y * pitch_y / 2], [x, blocks_y * pitch_y / 2]], street_trees, spacing=1100, offset=band, seed=rng.randint(0, 999),
                            name=f"trees_y_{i}")
    return group


