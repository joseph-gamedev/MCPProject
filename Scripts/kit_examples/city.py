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
from unreal_kit.parts import M, PALETTE, box, cone, cyl, gable, mat, sphere, transform  # noqa: F401

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


def facade(width, floors, floor_h=320, ground_h=420, bays=None, wall="brick_red", trim="trim_stone", frame="frame_white", door_bay=None,
           shop=False, win_w=None, win_h=None, wall_t=30, rng=None):
    """One wall with real openings: piers between bays, spandrels under windows, string courses, plinth and cornice."""
    rng = rng or random
    bays = bays or max(1, int(width / 330))
    bay = width / bays
    win_w = win_w or min(140, bay * 0.45)
    win_h = win_h or min(190, floor_h * 0.52)
    sill_z = 90
    P = []
    height = ground_h + (floors - 1) * floor_h
    P.append(box([width + 6, wall_t + 8, 60], [0, (wall_t - 8) / 2 - 4, 0], m=trim))                  # plinth
    for f in range(floors):
        z0 = 0 if f == 0 else ground_h + (f - 1) * floor_h
        fh = ground_h if f == 0 else floor_h
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
            wz = z0 + (sill_z if f > 0 else max(sill_z, (fh - win_h) * 0.55))
            pier = (bay - win_w) / 2
            P.append(box([pier, wall_t, fh], [cx - win_w / 2 - pier / 2, wall_t / 2, z0], m=wall))         # piers
            P.append(box([pier, wall_t, fh], [cx + win_w / 2 + pier / 2, wall_t / 2, z0], m=wall))
            P.append(box([win_w, wall_t, wz - z0], [cx, wall_t / 2, z0], m=wall))                            # spandrel
            top = z0 + fh - (wz + win_h)
            if top > 0:
                P.append(box([win_w, wall_t, top], [cx, wall_t / 2, wz + win_h], m=wall))                    # header wall
            P += window(cx, wz, win_w, win_h, recess=min(20, wall_t * 0.7), frame=frame, sill=trim, lintel=trim, wall_t=wall_t)
        if f > 0:
            P.append(box([width + 4, 8, 12], [0, -4, z0 - 6], m=trim))                                       # string course
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
    sides = [(width, (0, -depth / 2, 0), 0), (depth, (width / 2, 0, 0), 90), (width, (0, depth / 2, 0), 180), (depth, (-width / 2, 0, 0), 270)]
    for k, (w, off, yaw) in enumerate(sides):
        front = k == 0
        bays = max(1, int(w / bay))
        f = facade(w - (0 if k % 2 == 0 else 2 * t), floors, floor_h, ground_h, bays, wall, trim, frame,
                   door_bay=(bays // 2 if front and not shop else None), shop=shop and front, win_w=win_w, win_h=win_h, wall_t=t, rng=rng)
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
    for x in (-width / 2 + 25, width / 2 - 25):                                                               # downspouts
        P.append(cyl(10, height, [x, -depth / 2 - 10, 0], m="metal_dark"))
    return P


def glass_tower(width, depth, floors, floor_h, rng, setbacks=None, crown=None):
    """Curtain-wall tower: dark glass, aluminium mullions and floor bands, podium; setbacks [(height fraction, inset cm)]
    step the tower in (art deco / 1930s skyscrapers); crown "spire" adds a tapering spire, "flat" a plain top."""
    height = floors * floor_h
    steps = [(0.0, 0.0)] + sorted(setbacks or [])
    P = [box([width + 200, depth + 200, 500], [0, 0, -5], m="stone_light")]
    for k, (frac, inset) in enumerate(steps):
        z0 = height * frac
        z1 = height * (steps[k + 1][0] if k + 1 < len(steps) else 1.0)
        w, d = max(300, width - 2 * inset), max(300, depth - 2 * inset)
        h = z1 - z0
        P.append(box([w, d, h], [0, 0, z0], m="glass_dark"))
        for f in range(int(h // floor_h) + 1):
            P.append(box([w + 10, d + 10, 14], [0, 0, z0 + f * floor_h - 7], m="aluminium"))
        for side, (sw, yaw) in enumerate([(w, 0), (d, 90), (w, 180), (d, 270)]):
            n = max(2, int(sw / 150))
            off = [(0, -d / 2, z0), (w / 2, 0, z0), (0, d / 2, z0), (-w / 2, 0, z0)][side]
            P += transform([box([8, 14, h], [-sw / 2 + i * sw / n, -6, 0], m="aluminium") for i in range(n + 1)], off, yaw)
        P.append(box([w + 20, d + 20, 60], [0, 0, z1 - 60], m="metal_dark"))
    top_w = max(300, width - 2 * steps[-1][1])
    if crown == "spire":
        P.append(cone(top_w * 0.35, height * 0.18, [0, 0, height], m="aluminium", top_ratio=0.02))
    else:
        P.append(box([top_w * 0.6, top_w * 0.6, 300], [0, 0, height], m="metal_dark"))
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


def street(length=6000, width=1050, sidewalk=320, seed=None, props=True, cars=0.3):
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
            if tx < length / 2 - 200:
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
               make_building=None, lots=(1200, 1500, 1800, 2400), courtyard=True):
    """Perimeter block: varied buildings around the edge, lots 12-24 m wide, heights and styles mixed.
    make_building(width, depth, rng, tall) -> parts replaces the default building (your own building types).
    Returns a list of (parts, location, name) so each building can be its own object."""
    rng = random.Random(seed)
    out = []
    lot_depth = min(1400, depth * 0.4)
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
         max_floors=8, styles=("brick", "plaster", "stone", "office"), make_building=None, make_street=None, extras=None, ground="soil"):
    """A district: a grid of perimeter blocks separated by complete streets. Builds it (place) and returns the group.
    Hooks for a specific place: make_building(width, depth, rng, tall) -> parts (your building types),
    make_street(length, width, sidewalk, rng) -> parts (your street dressing), extras(rng, pitch_x, pitch_y) ->
    [(name, parts, location)] (landmarks, parks, water...). Everything else stays the same."""
    rng = random.Random(seed)
    pitch_x = block_w + street_w + 2 * sidewalk
    pitch_y = block_d + street_w + 2 * sidewalk
    jobs = []
    for i in range(blocks_x):
        for j in range(blocks_y):
            bx = (i - (blocks_x - 1) / 2) * pitch_x
            by = (j - (blocks_y - 1) / 2) * pitch_y
            for parts, loc, name in city_block(block_w, block_d, seed=rng.randint(0, 10 ** 6), towers=towers, max_floors=max_floors, styles=styles,
                                               make_building=make_building):
                jobs.append((f"{name}_{i}_{j}", parts, (bx + loc[0], by + loc[1], 0)))
    for j in range(blocks_y + 1):
        y = (j - blocks_y / 2) * pitch_y
        sx = make_street(blocks_x * pitch_x, street_w, sidewalk, rng) if make_street else street(blocks_x * pitch_x, street_w, sidewalk, seed=rng.randint(0, 999))
        jobs.append((f"street_x_{j}", sx, (0, y, 0)))
    for i in range(blocks_x + 1):
        x = (i - blocks_x / 2) * pitch_x
        ln = blocks_y * pitch_y - street_w - 2 * sidewalk
        sy = make_street(ln, street_w, sidewalk, rng) if make_street else street(ln, street_w, sidewalk, seed=rng.randint(0, 999), cars=0.15)
        jobs.append((f"street_y_{i}", transform(sy, (0, 0, 0), 90), (x, 0, 0)))
    jobs.append(("ground", [box([blocks_x * pitch_x + 8000, blocks_y * pitch_y + 8000, 30], [0, 0, -34], m=ground)], (0, 0, 0)))
    if extras:
        jobs += extras(rng, pitch_x, pitch_y)
    place_many(jobs, group=group)
    return group


