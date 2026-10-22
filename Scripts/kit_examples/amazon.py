"""EXAMPLE: an Amazon river village in layered rainforest, vegetation scattered with PCG (unreal_kit.assets).

    import unreal_kit as uk, amazon
    amazon.scene(seed=4)                 # river, forest, village, lighting (about a minute)
    amazon.stilt_house(rng)              # parts for one house (place with uk.place)

Layers (real assets, GPU instances through pcg.scatter): emergent giants over the canopy, the canopy itself, an
understory of palms and shrubs, ferns and ground plants, and lush river banks. The river and the village clearing
are exclusion zones, so the forest stops at the water and around the houses.
Sizes in cm; the scene spans `size` cm square centred on the origin; the river runs along X.
"""

import math
import random

import unreal_kit as uk
from unreal_kit.parts import M, box, cone, cyl, extrude, gable, part, transform

LAND = M("sand", (150, 122, 86), roughness=0.95)         # leaf-litter forest floor
MUD = M("sand", (146, 118, 84), roughness=0.9)
LATERITE = M("sand", (150, 88, 58), roughness=0.95)       # red earth of the clearing
WATER = M("water", (92, 74, 44))                          # sediment-brown river
WOOD = M("wood", (110, 82, 56))
WOOD_DARK = M("wood", (70, 50, 34))
WOOD_GREY = M("wood", (128, 118, 104))                    # weathered planks
THATCH = M("wood", (164, 132, 82), roughness=0.95)
TIN = M("corrugated", (150, 146, 136))
TIN_RUST = M("corrugated", (140, 92, 64))
PAINT = [M("painted_iron", (52, 112, 128), roughness=0.6), M("painted_iron", (180, 70, 60), roughness=0.6),
         M("painted_iron", (212, 196, 140), roughness=0.6), M("painted_iron", (86, 128, 80), roughness=0.6), WOOD_GREY, WOOD_GREY]


def river_y(x, size):
    """River centre line (meanders gently along X)."""
    return -size * 0.08 + size * 0.07 * math.sin(x / (size * 0.28)) + size * 0.025 * math.sin(x / (size * 0.11) + 1.3)


# ------------------------------------------------------------------ buildings

def stilt_house(rng, w=None, d=None, stilts=None):
    """Riverside house on stilts (palafita): plank walls in paint or bare wood, thatch or tin roof, veranda with a rail,
    a ladder. Origin at ground level under the front edge; front faces -Y."""
    w = w or rng.choice((520, 600, 680))
    d = d or rng.choice((440, 500, 560))
    h_st = stilts or rng.choice((140, 180, 220))
    wall = rng.choice(PAINT)
    P = []
    for sx in (-1, 0, 1):
        for sy in (-1, 1):
            P.append(cyl(16, h_st + 10, [sx * (w / 2 - 20), sy * (d / 2 - 20), 0], m=WOOD_DARK))
    for sx in (-1, 1):                                                       # veranda posts
        P.append(cyl(14, h_st + 10, [sx * (w / 2 - 20), -d / 2 - 160, 0], m=WOOD_DARK))
    floor = h_st
    P.append(box([w + 20, d + 180, 14], [0, -90, floor], m=WOOD_GREY))      # floor + veranda deck
    wh = 260
    # walls with a door and windows (gaps), planks suggested by thin battens
    P += [box([w, 8, wh], [0, d / 2, floor + 14], m=wall),
          box([8, d, wh], [-w / 2, 0, floor + 14], m=wall), box([8, d, wh], [w / 2, 0, floor + 14], m=wall)]
    dx = rng.uniform(-w / 4, w / 4)
    P += [box([dx + w / 2 - 50, 8, wh], [(-w / 2 + dx - 50) / 2, -d / 2, floor + 14], m=wall),
          box([w / 2 - dx - 50, 8, wh], [(w / 2 + dx + 50) / 2, -d / 2, floor + 14], m=wall),
          box([100, 8, wh - 210], [dx, -d / 2, floor + 14 + 210], m=wall)]
    for side in (-1, 1):                                                     # shuttered windows on the sides
        P.append(box([10, 90, 70], [side * (w / 2 + 2), 0, floor + 120], m=WOOD_DARK))
    for i in range(int(w / 60)):
        P.append(box([4, 4, wh], [-w / 2 + 30 + i * 60, -d / 2 - 5, floor + 14], m=WOOD_DARK))
    rail_z = floor + 14
    P += [box([w + 20, 6, 6], [0, -d / 2 - 175, rail_z + 90], m=WOOD_DARK)]
    P += [box([5, 5, 90], [-w / 2 + i * (w / 8), -d / 2 - 175, rail_z], m=WOOD_DARK) for i in range(9)]
    for k in range(int(h_st / 30) + 1):                                      # ladder down from the veranda
        P.append(box([70, 6, 5], [w / 2 - 70, -d / 2 - 200 - k * 4, k * 30], m=WOOD_DARK))
    P.append(box([5, 40, h_st + 20], [w / 2 - 105, -d / 2 - 210, 0], rot=[0, 0, 0], m=WOOD_DARK))
    P.append(box([5, 40, h_st + 20], [w / 2 - 35, -d / 2 - 210, 0], m=WOOD_DARK))
    roof_z = floor + 14 + wh
    if rng.random() < 0.55:
        P.append(gable(w + 120, d + 260, rng.uniform(170, 220), [0, -90, roof_z], m=THATCH))
    else:
        P.append(gable(w + 80, d + 230, rng.uniform(110, 150), [0, -90, roof_z], m=rng.choice((TIN, TIN_RUST))))
    if rng.random() < 0.5:                                                   # water barrel / hammock bar on the veranda
        P.append(cyl(60, 80, [-w / 2 + 60, -d / 2 - 90, floor + 14], m=M("painted_iron", (40, 80, 140))))
    return P


def maloca(rng, dia=1800):
    """Large round communal house: ring of posts, low palm-thatch walls, tall conical thatch roof down to the ground."""
    P = [cyl(dia + 60, 10, [0, 0, 0], m=LATERITE)]
    n = 16
    for i in range(n):
        a = 2 * math.pi * i / n
        P.append(cyl(22, 260, [math.cos(a) * dia / 2, math.sin(a) * dia / 2, 0], m=WOOD_DARK))
    P.append(cone(dia + 260, dia * 0.72, [0, 0, 180], m=THATCH))
    P.append(cone(dia * 0.24, dia * 0.12, [0, 0, 180 + dia * 0.69], m=WOOD_DARK))
    P.append(box([200, 60, 220], [0, -dia / 2 - 20, 0], m=WOOD_DARK))                # doorway frame
    P.append(box([150, 64, 200], [0, -dia / 2 - 22, 0], m=M("black")))
    return P


def canoe(length=600, rng=None):
    """Dugout canoe: hull, inner cavity and a paddle."""
    rng = rng or random
    return [part("capsule", [70, 70, length], [0, 0, 0], rot=[90, 0, 0], m=WOOD_DARK, pivot="center"),
            part("capsule", [54, 54, length - 60], [0, 0, 14], rot=[90, 0, 0], m=WOOD, pivot="center"),
            box([8, 160, 3], [rng.uniform(-10, 10), 0, 30], rot=[0, rng.uniform(-15, 15), 0], m=WOOD)]


def dock(length=1400, width=220):
    P = [box([width, length, 12], [0, length / 2, 120], m=WOOD_GREY)]
    for k in range(int(length / 200) + 1):
        for sx in (-1, 1):
            P.append(cyl(16, 140, [sx * (width / 2 - 10), k * 200, 0], m=WOOD_DARK))
    return P


def fire_pit():
    P = [cyl(160, 6, [0, 0, 0], m=M("black", roughness=1.0))]
    for i in range(10):
        a = 2 * math.pi * i / 10
        P.append(box([30, 26, 22], [math.cos(a) * 80, math.sin(a) * 80, 0], rot=[0, math.degrees(a), 0], m=M("stone", (110, 104, 96))))
    for a in (0, 60, 120):
        P.append(box([120, 12, 12], [0, 0, 8], rot=[0, a, 0], m=WOOD_DARK, pivot="center"))
    return P


def drying_rack():
    P = [box([8, 8, 170], [x, y, 0], m=WOOD_DARK) for x in (-120, 120) for y in (-40, 40)]
    P += [box([260, 6, 6], [0, y, 168], m=WOOD_DARK) for y in (-40, 0, 40)]
    P += [box([30, 70, 4], [-90 + i * 45, 0, 172], m=M("sand", (180, 150, 110))) for i in range(5)]
    return P


# ------------------------------------------------------------------ the scene

def scene(size=36000, seed=4, river_w=3200, village=(0, 0), lighting=True, density=1.0):
    """River, land, layered rainforest (PCG), village in a clearing on the north bank. Returns a summary dict."""
    rng = random.Random(seed)
    half = size / 2
    xs = [-half + i * size / 48 for i in range(49)]
    bank_n = [(x, river_y(x, size) + river_w / 2) for x in xs]
    bank_s = [(x, river_y(x, size) - river_w / 2) for x in xs]
    jobs = []
    # land on both sides of the river (extruded outlines), mud banks a little lower, water under everything
    jobs.append(("land_north", [extrude([list(p) for p in bank_n] + [[half, half], [-half, half]], 120, [0, 0, 0], m=LAND)], (0, 0, 0)))
    jobs.append(("land_south", [extrude([[-half, -half], [half, -half]] + [list(p) for p in reversed(bank_s)], 120, [0, 0, 0], m=LAND)], (0, 0, 0)))
    # the land's own 50 cm edge above the water reads as the steep mud bank of a tropical river
    jobs.append(("river", [box([size + 4000, size + 4000, 20], [0, 0, 50], m=WATER)], (0, 0, 0)))
    # the water is see-through: a silty bed under it (shallow near the banks) gives it its brown body
    jobs.append(("riverbed", [box([size + 4000, size + 4000, 30], [0, 0, 0], m=M("sand", (104, 84, 58), roughness=1.0))], (0, 0, 0)))
    # village clearing on the north bank
    vx = village[0]
    vy = river_y(vx, size) + river_w / 2
    cw, cd = 8000, 6000
    clearing = [[vx - cw / 2, vy + 200], [vx + cw / 2, vy + 200], [vx + cw / 2 + 600, vy + cd * 0.6], [vx + cw / 3, vy + cd],
                [vx - cw / 3, vy + cd], [vx - cw / 2 - 600, vy + cd * 0.6]]
    jobs.append(("clearing", [extrude(clearing, 124, [0, 0, 0], m=LATERITE)], (0, 0, 0)))
    z = 124
    houses = []
    for i in range(9):                                                        # stilt houses facing the river
        a = -cw / 2 + 900 + i * (cw - 1800) / 8
        row = i % 2
        hx = vx + a + rng.uniform(-150, 150)
        hy = vy + 700 + row * 1700 + rng.uniform(-200, 200)
        houses.append((f"house_{i}", transform(stilt_house(rng), (0, 0, 0), rng.uniform(-8, 8)), (hx, hy, z)))
    for i in range(2):                                                        # two houses standing in the river on long stilts
        hx = vx + (-1 if i == 0 else 1) * (cw / 2 + 400)
        hy = river_y(hx, size) + river_w / 2 - 500
        houses.append((f"water_house_{i}", stilt_house(rng, stilts=260), (hx, hy, 0)))
    jobs += houses
    jobs.append(("maloca", maloca(rng), (vx - 1200, vy + cd * 0.72, z)))
    jobs.append(("fire_pit", fire_pit(), (vx + 1300, vy + cd * 0.55, z)))
    for i in range(2):
        jobs.append((f"drying_rack_{i}", drying_rack(), (vx + 2400 + i * 400, vy + cd * 0.7, z)))
    jobs.append(("dock", dock(1500), (vx + 300, vy - 1400, 0)))
    for i in range(4):
        jobs.append((f"canoe_{i}", transform(canoe(rng.choice((520, 600, 700)), rng), (0, 0, 0), rng.uniform(-20, 20)),
                     (vx + 600 + i * 170 * (1 if i % 2 else -1) + rng.uniform(-60, 60), vy - 700 - i * 260, 66)))
    for i in range(3):                                                        # canoes pulled up on the bank
        jobs.append((f"beached_canoe_{i}", transform(canoe(520, rng), (0, 0, 0), rng.uniform(70, 110)), (vx - 2600 + i * 250, vy + 20, 110)))
    uk.place_many(jobs, group="Amazon")

    # ---- vegetation (PCG): exclusion zones keep the river and the clearing open
    def spline(points, closed, name):
        r = uk.call("spline.create_spline", points=[list(p) + [0] * (3 - len(p)) for p in points], closed=closed, name=name, linear=closed)
        sp = r.get("spline")
        return (sp.get("ref") or sp.get("id")) if isinstance(sp, dict) else r.get("id")

    river_line = spline([(x, river_y(x, size), 100) for x in xs[::2]], False, "RiverLine")
    clear_zone = spline([(x, y, 130) for x, y in clearing], True, "ClearingZone")
    keep_out = [{"spline": river_line, "clearance": river_w / 2 + 250}, {"spline": clear_zone, "clearance": 350}]
    area = [(-half + 200, -half + 200), (half - 200, -half + 200), (half - 200, half - 200), (-half + 200, half - 200)]
    d = 1.0 / max(0.25, density)
    layers = [
        ("Emergent", {"jacaranda_tree": 1, "island_tree": 2}, 2600 * d, (1.6, 2.2), 0.8),
        ("Canopy", {"island_tree": 5, "oak": 1, "maple": 1}, 800 * d, (1.0, 1.5), 0.95),
        ("Palms", {"palm_date": 3, "palm_coconut": 1}, 1500 * d, (0.9, 1.3), 0.6),
        ("Understory", {"bush": 3, "boxwood": 1}, 420 * d, (0.9, 1.6), 0.85),
        ("Ferns", {"fern": 5, "nettle_plant": 1, "tall_grass": 1}, 190 * d, (1.0, 1.8), 0.8),
        ("Floor", {"periwinkle_plant": 2, "celandine": 1, "grass_medium": 1}, 150 * d, (1.0, 1.6), 0.7),
    ]
    out = {}
    for name, mix, spacing, scale, keep in layers:
        r = uk.assets.forest(area, mix, spacing=spacing, scale=scale, keep=keep, seed=rng.randint(0, 9999), name=name, exclude=keep_out,
                             collision=name in ("Emergent", "Canopy"))
        out[name] = r.get("instances")
    for side, bank in (("N", bank_n), ("S", bank_s)):                         # lush banks: palms leaning over the water, shrubs
        # Unreal is left-handed: walking +X, "Right" is +Y. The north bank's land is +Y of its line, the south bank's -Y.
        r = uk.assets.along([(x, y) for x, y in bank[1:-1]], {"palm_date": 2, "bush": 3, "fern": 3}, spacing=260,
                            offset=(150, 700), side="Right" if side == "N" else "Left",
                            scale=(0.8, 1.3), seed=rng.randint(0, 9999), name=f"Bank{side}", collision=False)
        out[f"bank_{side}"] = r.get("instances")
    # village plants: cassava garden rows, a few fruit palms and grass edges
    gx, gy = vx + 1300, vy + cd * 0.87                                       # open ground behind the houses
    garden = [(gx - 900, gy - 600), (gx + 900, gy - 600), (gx + 900, gy + 600), (gx - 900, gy + 600)]
    out["garden"] = uk.assets.forest(garden, {"bush": 1}, spacing=120, scale=(0.35, 0.5), keep=1.0, seed=7, name="CassavaGarden",
                                     collision=False).get("instances")
    palms = uk.assets.Instancer("VillagePalms")
    for i in range(10):
        a = rng.uniform(0, 2 * math.pi)
        palms.add(rng.choice(uk.assets.variants("palm_coconut")), (vx + math.cos(a) * rng.uniform(2500, 3600), vy + cd * 0.5 + math.sin(a) * 2400, z),
                  rng.uniform(0, 360), rng.uniform(0.8, 1.1))
    palms.flush()
    # grass only in a band along the clearing's edge (scattering over the whole clearing would land tufts on roofs and decks)
    out["edge_grass"] = uk.assets.along([(x, y) for x, y in clearing + [clearing[0]]], {"tall_grass": 1, "grass": 4}, spacing=90,
                                        offset=(0, 500), side="Right", scale=(0.8, 1.3), seed=3, collision=False, name="ClearingGrass").get("instances")
    if lighting:   # humid morning: warm low sun, haze, a little breeze
        uk.look.mood("Morning")
        try:
            uk.tools.lighting.set_fog(density=0.03)
        except Exception:  # noqa: BLE001
            pass
        uk.assets.wind(strength=0.25, speed=0.5)
    return out
