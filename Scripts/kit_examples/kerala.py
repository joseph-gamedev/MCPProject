"""EXAMPLE: a small Kerala town (inland, like Kottayam or Thrissur's outskirts) from primitives + real palms.

    import unreal_kit as uk, kerala
    kerala.town(seed=3)                         # terrain, roads, shops, houses in compounds, landmarks, palms
    uk.place("House", kerala.tharavadu(random.Random(1)))

Signature features, in order of importance: steep clay-tile roofs (hipped, with small gable vents = mukhappu) on
whitewashed or laterite walls; pastel concrete houses with window sunshades (chajjas) and black rooftop water tanks;
two-storey shop rows with roller shutters and bright signboards; laterite compound walls with gates; coconut palms
everywhere; electric poles with sagging wires; auto-rickshaws; a white church, a tiered-roof temple, a green-domed mosque.
"""

import math
import random

import unreal_kit as uk
from unreal_kit.parts import M, beam, box, cone, cyl, gable, part, transform

TILE = M("roof_tile", (168, 82, 52))                       # Mangalore clay tiles
TILE_OLD = M("roof_tile", (120, 64, 46))
LATERITE = M("sandstone", (156, 78, 52), style="blocks", tile=45)
WHITEWASH = M("plaster", (236, 234, 226))
WOOD = M("wood", (92, 58, 34))
WOOD_DARK = M("wood", (58, 36, 22))
CONCRETE = M("concrete", (170, 168, 160))
PASTELS = [M("plaster", c) for c in ((244, 196, 210), (206, 230, 168), (176, 212, 236), (250, 214, 168), (255, 238, 166), (238, 238, 232),
                                     (214, 196, 236), (168, 222, 206), (250, 190, 150))]
SIGNS = [M("painted_iron", c, roughness=0.4) for c in ((210, 30, 30), (20, 80, 190), (250, 190, 20), (20, 140, 70), (230, 90, 20), (140, 30, 120))]
BLACK_TANK = M("painted_iron", (24, 24, 26), roughness=0.5)
COPPER = M("copper", (150, 96, 60))
BRASS = M("gold", (190, 150, 70))


# ------------------------------------------------------------------ details

def chajja(x, z, w, y=-10, depth=60):
    """Concrete sunshade over a window (every Kerala window has one)."""
    return [box([w + 40, depth, 8], [x, y - depth / 2, z], rot=[-6, 0, 0], m=CONCRETE)]


def window(x, z, w=110, h=130, y=0, frame=WOOD, bars=True):
    P = [box([w, 6, h], [x, y + 8, z], m=M("tinted_glass")), box([w + 14, 8, 8], [x, y, z + h], m=frame), box([w + 14, 8, 8], [x, y, z - 8], m=frame),
         box([8, 8, h], [x - w / 2 - 4, y, z], m=frame), box([8, 8, h], [x + w / 2 + 4, y, z], m=frame), box([5, 8, h], [x, y, z], m=frame)]
    if bars:
        P += [box([3, 3, h], [x - w / 2 + i * w / 6, y - 2, z], m=M("painted_iron", (40, 60, 50))) for i in range(1, 6)]
    return P


def hip_roof(w, d, h, z, m=TILE, overhang=90, vent=True):
    """Steep hipped tile roof with the Kerala gable vent (mukhappu) near the top of the front."""
    P = [part("pyramid", [w + 2 * overhang, d + 2 * overhang, h], [0, 0, z], m=m)]
    if vent:
        gw = min(w, d) * 0.35
        P.append(gable(gw, gw * 0.9, gw * 0.55, [0, -d * 0.12, z + h * 0.52], m=m))
        P.append(box([gw * 0.8, 6, gw * 0.4], [0, -d * 0.12 - gw * 0.45, z + h * 0.53], m=WOOD_DARK))   # lattice face
    P.append(box([w + 2 * overhang + 10, 14, 18], [0, -(d / 2 + overhang), z - 10], m=WOOD_DARK))    # eave board
    return P


# ------------------------------------------------------------------ buildings (origin: front centre at ground, front faces -Y)

def tharavadu(rng, w=None, d=None):
    """Traditional house: raised laterite plinth, whitewashed (or laterite) walls, wooden pillared veranda (poomukham),
    steep hipped clay-tile roof with a mukhappu."""
    w = w or rng.choice((1100, 1300, 1500))
    d = d or rng.choice((900, 1000, 1100))
    plinth = rng.choice((60, 80, 100))
    wall = rng.choice((WHITEWASH, WHITEWASH, LATERITE))
    wh = 300
    P = [box([w + 80, d + 260, plinth], [0, d / 2 - 130, 0], m=LATERITE)]
    P += [box([w, d, wh], [0, d / 2, plinth], m=wall)]
    P += [box([w * 0.4, 20, 210], [0, -10, plinth], m=WOOD)]                                          # carved door frame area
    P += [box([110, 12, 210], [0, -14, plinth], m=WOOD_DARK)]
    for x in (-w / 3, w / 3):
        P += window(x, plinth + 90, 120, 130, y=-8)
    for i in range(5):                                                                                # veranda pillars
        x = -w / 2 + 60 + i * (w - 120) / 4
        P.append(cyl(22, wh, [x, -200, plinth], m=WOOD))
    P.append(box([w + 40, 40, 40], [0, -210, plinth + wh - 30], m=WOOD_DARK))
    P.append(box([w, 60, 45], [0, -200, plinth], m=LATERITE))                                          # sit-out ledge
    for i in range(3):                                                                                # steps
        P.append(box([240, 40, plinth * (3 - i) / 3], [0, -300 - i * 40, 0], m=LATERITE))
    P += transform(hip_roof(w, d + 260, min(w, d) * 0.55, 0, m=rng.choice((TILE, TILE_OLD))), (0, d / 2 - 130, plinth + wh), 0)
    return P


def modern_house(rng, w=None, d=None):
    """Concrete house: pastel paint, sunshades, a balcony, a car porch, sloping tile roof sections, a black water tank."""
    w = w or rng.choice((1000, 1200, 1400))
    d = d or rng.choice((900, 1100))
    paint = rng.choice(PASTELS)
    floors = rng.choice((1, 2, 2))
    fh = 310
    P = [box([w + 40, d + 40, 50], [0, d / 2, 0], m=CONCRETE)]
    for f in range(floors):
        z = 50 + f * fh
        P.append(box([w, d, fh], [0, d / 2, z], m=paint))
        for x in (-w / 3, w / 3) if f == 0 else (-w / 3, 0, w / 3):
            P += window(x, z + 90, 120, 130, y=-8, frame=M("painted_iron", (230, 230, 225)), bars=f == 0)
            P += chajja(x, z + 236, 120)
        P.append(box([w + 30, 30, 18], [0, d / 2, z + fh - 18], m=CONCRETE))
    if floors == 2:                                                                                   # balcony with a rail
        P.append(box([w * 0.45, 140, 15], [w * 0.2, -70, 50 + fh], m=CONCRETE))
        P += [box([w * 0.45, 4, 90], [w * 0.2, -138, 65 + fh], m=M("painted_iron", (40, 40, 44)))]
    P.append(box([110, 12, 220], [-w * 0.15, -8, 50], m=WOOD))                                          # door
    pw = w * 0.4                                                                                       # car porch
    P += [box([pw, 450, 18], [-w * 0.28, -225, 300], m=CONCRETE)] + [cyl(30, 250, [-w * 0.28 + sx * pw * 0.42, -420, 50], m=paint) for sx in (-1, 1)]
    top = 50 + floors * fh
    if rng.random() < 0.65:                                                                           # sloping tile roof
        P.append(part("pyramid", [w + 160, d + 160, min(w, d) * 0.38], [0, d / 2, top], m=TILE))
    else:
        P.append(box([w + 20, d + 20, 40], [0, d / 2, top], m=CONCRETE))
        P.append(gable(w * 0.5, d * 0.5, 160, [w * 0.15, d * 0.45, top + 40], m=TILE))
    P.append(cyl(130, 140, [w * 0.3, d * 0.75, top + (40 if rng.random() < 0.5 else min(w, d) * 0.2)], m=BLACK_TANK))   # Sintex tank
    return P


def shop_row(rng, w=None, d=1200):
    """2-3 storey shop building: roller shutters and bright signboards below, windows with sunshades above."""
    w = w or rng.choice((600, 750, 900))
    floors = rng.choice((2, 2, 3))
    fh = 340
    paint = rng.choice(PASTELS + [CONCRETE, WHITEWASH])
    P = [box([w, d, fh * floors], [0, d / 2, 0], m=paint)]
    P.append(box([w - 40, 10, 270], [0, -4, 0], m=M("corrugated", (150, 150, 146))))                  # roller shutter (half up)
    P.append(box([w - 40, 12, 120], [0, -6, 160], m=M("painted_iron", (70, 72, 74))))
    P.append(box([w - 10, 20, 90], [0, -18, 280], m=rng.choice(SIGNS)))                                # signboard
    P.append(box([w * 0.6, 22, 40], [0, -30, 300], m=M("plaster", (250, 250, 245))))                  # its lettering panel
    P.append(box([w + 30, 120, 10], [0, -60, 270], m=CONCRETE))                                        # shop awning slab
    for f in range(1, floors):
        z = f * fh
        for x in ((-w / 4, w / 4) if w > 650 else (0,)):
            P += window(x, z + 90, 110, 140, y=-6, frame=M("painted_iron", (230, 230, 225)), bars=False)
            P += chajja(x, z + 246, 110)
        if rng.random() < 0.4:
            P.append(box([w - 60, 20, 70], [0, -14, z + 10], m=rng.choice(SIGNS)))                    # upstairs sign
    P.append(box([w + 10, 20, 70], [0, -2, fh * floors], m=paint))                                     # parapet
    if rng.random() < 0.7:
        P.append(cyl(130, 140, [w * 0.2, d * 0.6, fh * floors], m=BLACK_TANK))
    return P


def church(rng):
    """White Syrian-Christian style church: tall gable facade with pilasters, a bell tower, a cross, tiled nave roof."""
    w, d, h = 1600, 3000, 900
    P = [box([w, d, h], [0, d / 2, 0], m=WHITEWASH), gable(w + 120, d + 60, 600, [0, d / 2, h], m=TILE)]
    P += [box([w + 100, 60, 1500], [0, -30, 0], m=WHITEWASH)]                                           # facade screen
    P.append(part("pyramid", [w + 100, 60, 500], [0, -30, 1500], m=WHITEWASH))
    for x in (-w / 2 + 40, -w / 6, w / 6, w / 2 - 40):
        P.append(box([60, 30, 1500], [x, -75, 0], m=M("plaster", (250, 250, 248))))
    P.append(box([260, 20, 420], [0, -70, 0], m=WOOD_DARK))
    for x in (-w / 3, w / 3):
        P.append(box([110, 20, 260], [x, -70, 450], m=M("tinted_glass")))
    P.append(cyl(240, 30, [0, -75, 1100], m=M("tinted_glass")))
    P += [box([24, 24, 300], [0, -30, 2000], m=WHITEWASH), box([150, 24, 24], [0, -30, 2200], m=WHITEWASH)]   # cross
    P += [box([400, 400, 1900], [w / 2 + 300, 200, 0], m=WHITEWASH), part("pyramid", [460, 460, 400], [w / 2 + 300, 200, 1900], m=TILE)]
    return P


def temple(rng):
    """Kerala temple: laterite compound wall, square sanctum (sreekovil) with two tiers of copper/tile roof and a
    finial, a pillared hall, and the brass lamp pillar (deepastambham) in front."""
    P = []
    s = 3200
    for (sx, sy, lx, ly) in ((0, -s / 2, s, 40), (0, s / 2, s, 40), (-s / 2, 0, 40, s), (s / 2, 0, 40, s)):
        if sy == -s / 2:   # front wall with a gate gap
            P += [box([s / 2 - 300, 40, 220], [-s / 4 - 150, sy, 0], m=LATERITE), box([s / 2 - 300, 40, 220], [s / 4 + 150, sy, 0], m=LATERITE)]
            P += [box([700, 400, 60], [0, sy, 0], m=LATERITE), box([640, 400, 380], [0, sy, 60], m=WHITEWASH)]
            P += transform(hip_roof(640, 400, 260, 0, vent=False), (0, sy, 440), 0)
        else:
            P.append(box([lx, ly, 220], [sx, sy, 0], m=LATERITE))
    P += [box([900, 900, 120], [0, 300, 0], m=LATERITE), box([700, 700, 350], [0, 300, 120], m=LATERITE)]
    P.append(part("pyramid", [1100, 1100, 380], [0, 300, 470], m=COPPER))
    P.append(box([500, 500, 120], [0, 300, 760], m=WOOD_DARK))
    P.append(part("pyramid", [700, 700, 320], [0, 300, 880], m=COPPER))
    P += [cyl(60, 120, [0, 300, 1190], m=BRASS), cone(70, 120, [0, 300, 1310], m=BRASS)]
    for x in (-350, -120, 120, 350):                                                                  # namaskara mandapam
        P.append(cyl(28, 280, [x, -500, 60], m=WOOD))
    P += [box([900, 500, 60], [0, -500, 0], m=LATERITE), part("pyramid", [1000, 640, 260], [0, -500, 340], m=TILE)]
    P += [box([120, 120, 60], [0, -1250, 0], m=LATERITE), cyl(50, 700, [0, -1250, 60], m=BRASS)]    # deepastambham
    for k in range(6):
        P.append(cyl(150 - k * 15, 10, [0, -1250, 150 + k * 100], m=BRASS))
    return P


def mosque(rng):
    """Kerala mosque: white hall, green dome, two minarets with green caps, arched-look entrance."""
    w, d = 1600, 1800
    P = [box([w, d, 650], [0, d / 2, 0], m=WHITEWASH), box([w + 40, d + 40, 40], [0, d / 2, 650], m=M("plaster", (250, 250, 245)))]
    green = M("painted_iron", (30, 140, 80), roughness=0.4)
    P += [part("dome", [800, 800, 450], [0, d / 2, 690], m=green), cyl(20, 140, [0, d / 2, 1140], m=BRASS)]
    for x in (-w / 2 - 100, w / 2 + 100):
        P += [cyl(180, 1500, [x, 0, 0], m=WHITEWASH), cyl(230, 40, [x, 0, 1100], m=M("plaster", (250, 250, 245))),
              cone(200, 320, [x, 0, 1500], m=green)]
    P += [box([360, 20, 380], [0, -10, 0], m=green), box([300, 22, 330], [0, -14, 0], m=WOOD_DARK)]
    return P


def auto_rickshaw(rng):
    """Three-wheeler: yellow lower body, black canvas top, one front wheel."""
    body = M("painted_iron", (240, 190, 20), roughness=0.4)
    black = M("painted_iron", (20, 20, 22), roughness=0.6)
    P = [box([240, 130, 70], [0, 0, 30], m=body), box([90, 120, 120], [80, 0, 70], m=body), box([250, 140, 12], [-10, 0, 175], m=black),
         box([10, 130, 90], [-120, 0, 90], m=black), box([60, 120, 60], [125, 0, 100], m=M("tinted_glass"))]
    P += [cyl(50, 14, [-80, y, 25], rotation=[90, 0, 0], m=black) for y in (-60, 60)] + [cyl(50, 14, [140, 0, 25], rotation=[90, 0, 0], m=black)]
    return P


def electric_pole():
    P = [box([22, 22, 900], [0, 0, 0], m=CONCRETE), box([160, 12, 12], [0, 0, 840], m=M("painted_iron", (60, 60, 60)))]
    P += [cyl(8, 14, [x, 0, 852], m=M("plaster", (230, 230, 225))) for x in (-70, -25, 25, 70)]
    return P


def compound(rng, w, d, house, gate_side=-1):
    """A house lot: laterite wall with a gate gap facing the road (front, -Y), the house set back."""
    P = []
    h = 150
    gw = 320
    P += [box([w / 2 - gw / 2, 30, h], [-w / 4 - gw / 4, -d / 2, 0], m=LATERITE), box([w / 2 - gw / 2, 30, h], [w / 4 + gw / 4, -d / 2, 0], m=LATERITE)]
    P += [box([30, d, h], [-w / 2, 0, 0], m=LATERITE), box([30, d, h], [w / 2, 0, 0], m=LATERITE), box([w, 30, h], [0, d / 2, 0], m=LATERITE)]
    P += [box([40, 40, h + 40], [s * gw / 2, -d / 2, 0], m=WHITEWASH) for s in (-1, 1)]
    P += [box([gw, 8, 130], [0, -d / 2 - 10, 0], m=M("painted_iron", (40, 60, 90)))]                   # gate
    P += [box([gw - 20, d * 0.35, 4], [0, -d / 2 + d * 0.18, 0], m=M("sand", (200, 180, 150)))]          # gravel drive
    P += transform(house, (rng.uniform(-w * 0.08, w * 0.08), -d * 0.1, 0), 0)
    return P


# ------------------------------------------------------------------ the town

def town(seed=3, radius=17000, palms=True):
    """Terrain with the town on a level pad, a main road with shops, a cross road, lanes of house compounds, a church,
    a temple and a mosque, electric poles with wires, auto-rickshaws, coconut palms and greenery all round."""
    rng = random.Random(seed)
    uk.look.mood("Morning")
    roads_x = [(0, 800), (-6500, 450), (6500, 450)]     # (y, width) roads running along X
    roads_y = [(0, 700), (-8500, 450), (8500, 450)]     # (x, width) roads running along Y
    R = radius
    terr = uk.tools.build.create_terrain(size=[2 * R + 16000, 2 * R + 16000], location=[0, 0, 0], height=900, feature_size=14000, roughness=0.5,
                                         seed=seed, biome="Tropical", flatten=[{"at": [0, 0], "radius": R, "blend": 5000, "height": 0, "bare": False}])
    try:   # fewer bare-earth patches: a green, monsoon-fed town
        uk.tools.materials.set_scalar(target=terr["id"], name="PatchStart", value=0.8)
    except Exception as e:  # noqa: BLE001
        print("ground tweak skipped:", e)
    jobs = []
    asphalt = M("asphalt", (60, 60, 62))
    for y, wd in roads_x:
        jobs.append((f"road_x_{y}", [box([2 * R, wd, 6], [0, y, -1], m=asphalt), box([2 * R, 12, 7], [0, y, -1], m="marking")] if wd > 600 else
                     [box([2 * R, wd, 6], [0, y, -1], m=asphalt)], (0, 0, 0)))
    for x, wd in roads_y:
        jobs.append((f"road_y_{x}", [box([wd, 2 * R, 6], [x, 0, -2], m=asphalt)], (0, 0, 0)))
    # shops along the main road near the junction, both sides
    for side in (-1, 1):
        x = -9500
        while x < 9500:
            sp = shop_row(rng)
            w = max(p["size"][0] for p in sp[:1])
            if abs(x + w / 2) < 700:                    # keep the junction open
                x += 900
                continue
            jobs.append((f"shop_{side}_{int(x)}", transform(sp, (0, 0, 0), 0 if side < 0 else 180), (x + w / 2, side * (400 + 180), 0)))
            x += w + rng.choice((0, 0, 60))
    # house compounds between the lanes
    blocks = [(xc, yc) for xc in (-12500, -4250, 4250, 12500) for yc in (-10500, -3250, 3250, 10500)]
    landmarks = {(-12500, -10500): "temple", (12500, 10500): "church", (12500, -10500): "mosque"}
    for (xc, yc) in blocks:
        kind = landmarks.get((xc, yc))
        if kind:
            fn = {"temple": temple, "church": church, "mosque": mosque}[kind]
            jobs.append((kind, transform(fn(rng), (0, 0, 0), 0 if yc > 0 else 180), (xc, yc + (-1200 if yc > 0 else 1200), 0)))
            continue
        near_main = abs(yc) < 4000
        for i in (-1, 1):
            for j in (-1, 1):
                if near_main and j == (-1 if yc > 0 else 1):
                    continue                            # the shop rows own the strip along the main road
                cx, cy = xc + i * 2000 + rng.uniform(-250, 250), yc + j * 1700 + rng.uniform(-150, 150)
                if rng.random() < 0.12:
                    continue                            # an empty plot: left to the trees
                face = (0 if j < 0 else 180) + rng.uniform(-4, 4)
                house = tharavadu(rng) if rng.random() < 0.4 else modern_house(rng)
                cw, cd = rng.choice((2600, 2900, 3200)), rng.choice((2300, 2600))
                jobs.append((f"house_{int(cx)}_{int(cy)}", transform(compound(rng, cw, cd, house), (0, 0, 0), face), (cx, cy, 0)))
    # electric poles with wires along the roads
    wire = M("painted_iron", (20, 20, 20))
    for y, wd in roads_x:
        xs = list(range(-R + 1000, R, 3500))
        P = []
        for x in xs:
            P += transform(electric_pole(), (x, y + wd / 2 + 80, 0), 0)
        for a, b in zip(xs, xs[1:]):
            for off in (-70, 70):
                mid = (a + b) / 2
                P += [beam([a + 0, y + wd / 2 + 80 + off * 0, 852], [mid, y + wd / 2 + 80, 800], 3, m=wire),
                      beam([mid, y + wd / 2 + 80, 800], [b, y + wd / 2 + 80, 852], 3, m=wire)]
        jobs.append((f"poles_{y}", P, (0, 0, 0)))
    # auto-rickshaws (a stand at the junction) and a few on the roads
    autos = []
    for k in range(5):
        autos += transform(auto_rickshaw(rng), (-1500 - k * 300, 520, 0), 90)
    for k in range(6):
        y, wd = rng.choice(roads_x)
        autos += transform(auto_rickshaw(rng), (rng.uniform(-R * 0.7, R * 0.7), y + rng.choice((-1, 1)) * wd * 0.25, 0), rng.choice((0, 180)))
    jobs.append(("autos", autos, (0, 0, 0)))
    if palms:   # ground cover first: scattered after the buildings it would land on their roofs
        _ground_cover(R, roads_x, roads_y)
    uk.place_many(jobs, group="KeralaTown")
    if palms:
        _greenery(rng, R, roads_x, roads_y, blocks, landmarks)
    uk.look.mood("Morning")
    return terr


def _greenery(rng, R, roads_x, roads_y, blocks, landmarks):
    """Coconut palms in every compound and along the roads, banana-like palms and shrubs, dense green beyond the town."""
    ins = uk.assets.Instancer("KeralaPalms", collision=False)
    coco = uk.assets.variants("palm_coconut")
    for (xc, yc) in blocks:
        if (xc, yc) in landmarks:
            continue
        for i in (-1, 1):
            for j in (-1, 1):
                cx, cy = xc + i * 2000, yc + j * 1700
                for _ in range(rng.randint(2, 4)):     # a mango / jackfruit tree or two
                    a = rng.uniform(0, 2 * math.pi)
                    ins.add(rng.choice(uk.assets.variants(rng.choice(("island_tree", "oak", "maple")))),
                            (cx + math.cos(a) * rng.uniform(900, 1400), cy + math.sin(a) * rng.uniform(800, 1100), 0), rng.uniform(0, 360), rng.uniform(0.5, 0.75))
                for _ in range(rng.randint(5, 9)):     # bananas, hibiscus and crotons along the walls
                    a = rng.uniform(0, 2 * math.pi)
                    sp = rng.choice(("palm_date", "flower_bush", "bush", "bush", "boxwood", "fern"))
                    ins.add(rng.choice(uk.assets.variants(sp)), (cx + math.cos(a) * rng.uniform(1000, 1450), cy + math.sin(a) * rng.uniform(900, 1200), 0),
                            rng.uniform(0, 360), rng.uniform(0.35, 0.6) if sp == "palm_date" else rng.uniform(0.7, 1.2))
                for _ in range(rng.randint(3, 6)):     # palms round the house, inside the wall
                    a = rng.uniform(0, 2 * math.pi)
                    ins.add(rng.choice(coco), (cx + math.cos(a) * rng.uniform(900, 1600), cy + math.sin(a) * rng.uniform(800, 1300), 0), rng.uniform(0, 360),
                            rng.uniform(0.8, 1.15))
                for _ in range(rng.randint(2, 4)):
                    a = rng.uniform(0, 2 * math.pi)
                    ins.add(rng.choice(uk.assets.variants(rng.choice(("bush", "palm_date", "fern")))),
                            (cx + math.cos(a) * rng.uniform(800, 1500), cy + math.sin(a) * rng.uniform(700, 1200), 0), rng.uniform(0, 360), rng.uniform(0.6, 1.0))
    for y, wd in roads_x:                              # palms leaning over the roads
        for x in range(-R + 600, R, 1300):
            if rng.random() < 0.55 and abs(x) > 10500:
                ins.add(rng.choice(coco), (x + rng.uniform(-200, 200), y + rng.choice((-1, 1)) * (wd / 2 + rng.uniform(200, 450)), 0), rng.uniform(0, 360),
                        rng.uniform(0.85, 1.2))
    ins.flush()


def _ground_cover(R, roads_x, roads_y):
    """Grass over the town (not on the roads) and groves beyond it."""
    road_lines = []
    for y, wd in roads_x:
        sp = uk.call("spline.create_spline", points=[[-R, y, 0], [R, y, 0]], closed=False, linear=True, name=f"RoadX{y}")["spline"]
        road_lines.append({"spline": sp.get("ref") or sp.get("id"), "clearance": wd / 2 + 60})
    for x, wd in roads_y:
        sp = uk.call("spline.create_spline", points=[[x, -R, 0], [x, R, 0]], closed=False, linear=True, name=f"RoadY{x}")["spline"]
        road_lines.append({"spline": sp.get("ref") or sp.get("id"), "clearance": wd / 2 + 60})
    town_area = [(-R, -R), (R, -R), (R, R), (-R, R)]
    uk.assets.forest(town_area, {"grass": 5, "grass_medium": 2, "tall_grass": 1}, spacing=120, scale=(0.9, 1.5), keep=0.95, seed=8, name="TownGrass",
                     exclude=road_lines, collision=False)
    # beyond the town: coconut groves and mixed green on the hills
    out = 2 * R + 14000
    area = [(-out / 2, -out / 2), (out / 2, -out / 2), (out / 2, out / 2), (-out / 2, out / 2)]
    town_zone = uk.call("spline.create_spline", points=[[R * math.cos(a / 16 * 2 * math.pi), R * math.sin(a / 16 * 2 * math.pi), 0] for a in range(16)],
                        closed=True, linear=True, name="TownZone")["spline"]
    zone = town_zone.get("ref") or town_zone.get("id")
    ex = [{"spline": zone, "clearance": 300}]
    uk.assets.forest(area, {"palm_coconut": 6, "island_tree": 1, "jacaranda_tree": 1}, spacing=900, scale=(0.85, 1.2), keep=0.9, seed=5, name="Groves",
                     exclude=ex, collision=False)
    uk.assets.forest(area, {"bush": 3, "fern": 3, "tall_grass": 2}, spacing=350, scale=(0.8, 1.4), keep=0.8, seed=6, name="Undergrowth", exclude=ex,
                     collision=False)
