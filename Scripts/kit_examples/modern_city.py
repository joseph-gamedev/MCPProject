"""EXAMPLE: a modern American downtown from the packaged modern facade kit (/Game/RuntimeArchitecture/Modern).

    import unreal_kit as uk, modern_city
    modern_city.city(blocks_x=4, blocks_y=3, seed=2)       # towers in the core, mid-rises around it
    modern_city.tower((0, 0, 20), bays=(10, 8), floors=40, skin="Office_Fin")
    modern_city.midrise((0, 0, 20), bays=(14, 6), floors=8)

Buildings are volumes: every floor of every side gets one 3 m module per bay (facades.volume), so a 40-storey
tower is ~1500 instances in a handful of instance groups. Per building custom data: [seed, glass tint (paint A),
cladding / sign colour (paint B)].
"""

import random

import unreal_kit as uk
from unreal_kit.assets import Instancer

P = "/Game/RuntimeArchitecture/Modern/SM_MO_"
BAY = 300.0
GROUND = 20.0
HS, HO, HR = 550.0, 400.0, 320.0
GLASS = [(0.35, 0.5, 0.62), (0.32, 0.52, 0.48), (0.45, 0.47, 0.5), (0.55, 0.45, 0.35), (0.22, 0.3, 0.42), (0.6, 0.62, 0.65), (0.3, 0.42, 0.55)]
CLAD = [(0.9, 0.9, 0.88), (0.15, 0.16, 0.17), (0.62, 0.32, 0.22), (0.75, 0.68, 0.55), (0.45, 0.46, 0.47), (0.5, 0.32, 0.18), (0.12, 0.16, 0.25),
        (0.85, 0.82, 0.76)]
SKINS = ["Office_Flush", "Office_Fin", "Office_Band", "Office_Unit"]


def _new(name, collision=True):
    ins = Instancer(name, collision=collision)
    ins.BLOCKS = P + "Block_"   # add_box uses the modern kit's blocks
    return ins


def volume(ins, x0, y0, nx, ny, z0, storeys, data):
    """Facade modules on all four sides of an nx x ny bay footprint whose front-left corner is (x0, y0), facade
    facing -Y on the front. storeys: [(height, pick)] bottom to top, pick(side, bay, bays) -> piece or None; side 0
    front (-Y), 1 right (+X), 2 back (+Y), 3 left (-X). Returns the top z."""
    W, D = nx * BAY, ny * BAY
    sides = [(lambda b: (x0 + (b + 0.5) * BAY, y0), 0.0, nx), (lambda b: (x0 + W, y0 + (b + 0.5) * BAY), 90.0, ny),
             (lambda b: (x0 + W - (b + 0.5) * BAY, y0 + D), 180.0, nx), (lambda b: (x0, y0 + D - (b + 0.5) * BAY), 270.0, ny)]
    z = z0
    for h, pick in storeys:
        for si, (pos, yaw, n) in enumerate(sides):
            for b in range(n):
                piece = pick(si, b, n)
                if piece:
                    x, y = pos(b)
                    ins.add(P + piece, (x, y, z), yaw, 1.0, data)
        z += h
    return z


def _street_level(rng):
    lobby_side, lobby_bay = 0, None

    def pick(side, b, n):
        nonlocal lobby_bay
        if side == lobby_side and lobby_bay is None:
            lobby_bay = rng.randrange(max(1, n - 2)) + 1 if n > 2 else 0
        if side == lobby_side and b in (lobby_bay, lobby_bay + 1):
            return "Lobby"
        return "Retail" if rng.random() < 0.85 else "Base_Blank"
    return pick


def tower(origin, bays=(10, 8), floors=40, skin=None, podium=3, setback=1, seed=None, ins=None):
    """Office tower: a podium (street-level retail + lobby, then `podium` floors) on the whole footprint, the shaft
    set back `setback` bays on every side, a glass crown and a roof. origin = front-left corner at pavement level."""
    rng = random.Random(seed)
    own = ins is None
    ins = ins or _new(f"Tower_{seed}")
    skin = skin or rng.choice(SKINS)
    data = [rng.random() * 100, *rng.choice(GLASS), *rng.choice(CLAD)]
    nx, ny = bays
    x0, y0, z0 = origin
    pod_skin = "Office_Band" if skin != "Office_Band" else "Office_Flush"
    z = volume(ins, x0, y0, nx, ny, z0, [(HS, _street_level(rng))] + [(HO, lambda s, b, n: pod_skin)] * podium, data)
    ins.add_box("roof", (x0 + nx * BAY / 2, y0 + ny * BAY / 2, z - 30), (nx * BAY - 20, ny * BAY - 20, 30))
    sx, sy = nx - 2 * setback, ny - 2 * setback
    tx, ty = x0 + setback * BAY, y0 + setback * BAY
    top = volume(ins, tx, ty, sx, sy, z, [(HO, lambda s, b, n: skin)] * floors + [(220.0, lambda s, b, n: "Crown")], data)
    ins.add_box("roof", (tx + sx * BAY / 2, ty + sy * BAY / 2, top - 260), (sx * BAY - 20, sy * BAY - 20, 30))
    # rooftop plant behind the crown
    if sx > 4 and sy > 4:
        volume(ins, tx + 2 * BAY, ty + 2 * BAY, sx - 4, sy - 4, top - 230, [(380.0, lambda s, b, n: "Screen")], data)
        ins.add_box("roof", (tx + sx * BAY / 2, ty + sy * BAY / 2, top + 140), ((sx - 4) * BAY, (sy - 4) * BAY, 10))
    if own:
        ins.flush()
    return ins


def midrise(origin, bays=(14, 6), floors=8, seed=None, ins=None):
    """Residential mid-rise on a retail base: balconies or loggias on the long sides, panels on the ends."""
    rng = random.Random(seed)
    own = ins is None
    ins = ins or _new(f"Midrise_{seed}")
    data = [rng.random() * 100, *rng.choice(GLASS), *rng.choice(CLAD)]
    front = rng.choice(("Res_Balcony", "Res_Loggia", "Res_Panel"))
    rhythm = rng.choice((1, 2, 3))

    def res(side, b, n):
        if side in (1, 3):
            return "Res_Panel"
        return front if (b % rhythm == 0 or front == "Res_Panel") else "Res_Panel"
    nx, ny = bays
    x0, y0, z0 = origin
    top = volume(ins, x0, y0, nx, ny, z0, [(HS, _street_level(rng))] + [(HR, res)] * floors + [(116.0, lambda s, b, n: "Parapet")], data)
    ins.add_box("roof", (x0 + nx * BAY / 2, y0 + ny * BAY / 2, top - 146), (nx * BAY - 20, ny * BAY - 20, 30))
    if own:
        ins.flush()
    return ins


def _markings(ins, ax, a0, a1, c, width, rng):
    """Lane markings on a road along axis ax ('x' or 'y') from a0 to a1 centred at c: double yellow centre line,
    dashed white lane lines."""
    def box(slot, p0, p1, off, w):
        if ax == "x":
            ins.add_box(slot, ((p0 + p1) / 2, c + off, GROUND - 15), (p1 - p0, w, 1))
        else:
            ins.add_box(slot, (c + off, (p0 + p1) / 2, GROUND - 15), (w, p1 - p0, 1))
    for off in (-15, 15):
        box("marking_y", a0, a1, off, 12)
    for off in (-width / 4, width / 4):
        p = a0
        while p + 300 < a1:
            box("marking", p, p + 300, off, 12)
            p += 900


def _crosswalk(ins, ax, at, c, width):
    for k in range(int(width / 100)):
        o = -width / 2 + 50 + k * 100
        if ax == "x":   # crossing a road that runs along y, at y = at
            ins.add_box("marking", (c + o, at, GROUND - 15), (50, 300, 1))
        else:
            ins.add_box("marking", (at, c + o, GROUND - 15), (300, 50, 1))


def city(blocks_x=4, blocks_y=3, block_bays=(28, 20), road=1500.0, sidewalk=450.0, seed=2, origin=(0, 0, 0), props=True):
    """A modern downtown grid: towers where the block is near the centre, residential mid-rises further out, some
    blocks split into two buildings, small plazas with trees. Returns node ids."""
    rng = random.Random(seed)
    ins = _new(f"Downtown_{seed}")
    ox, oy = origin[0], origin[1]
    bw, bd = block_bays[0] * BAY, block_bays[1] * BAY
    lot_w, lot_d = bw + 2 * sidewalk, bd + 2 * sidewalk
    px, py = lot_w + road, lot_d + road
    cxm, cym = (blocks_x - 1) / 2, (blocks_y - 1) / 2
    maxd = max(cxm, cym, 1)
    total_x, total_y = blocks_x * px + road, blocks_y * py + road
    # roads (one slab under the grid; lots sit on top), markings and crosswalks
    ins.add_box("road", (ox + (blocks_x - 1) * px / 2, oy + (blocks_y - 1) * py / 2, origin[2]), (total_x + 2000, total_y + 2000, GROUND - 15))
    for j in range(blocks_y + 1):
        yc = oy + (j - 0.5) * py
        for i in range(blocks_x):
            xc = ox + i * px
            _markings(ins, "x", xc - lot_w / 2, xc + lot_w / 2, yc, road, rng)
    for i in range(blocks_x + 1):
        xc = ox + (i - 0.5) * px
        for j in range(blocks_y):
            yc = oy + j * py
            _markings(ins, "y", yc - lot_d / 2, yc + lot_d / 2, xc, road, rng)
    for i in range(blocks_x + 1):          # crosswalks on every approach of every crossing
        for j in range(blocks_y + 1):
            xc, yc = ox + (i - 0.5) * px, oy + (j - 0.5) * py
            for s in (-1, 1):
                _crosswalk(ins, "x", yc + s * (road / 2 + 180), xc, road)
                _crosswalk(ins, "y", xc + s * (road / 2 + 180), yc, road)
    trees = Instancer(f"Downtown_{seed}_Props", collision=True)
    for i in range(blocks_x):
        for j in range(blocks_y):
            cx, cy = ox + i * px, oy + j * py
            z0 = origin[2] + GROUND
            ins.add_box("paving", (cx, cy, origin[2]), (lot_w, lot_d, GROUND))                       # sidewalk + lot
            ins.add_box("kerb", (cx, cy, origin[2]), (lot_w + 30, lot_d + 30, GROUND - 2))
            x0, y0 = cx - bw / 2, cy - bd / 2
            d = max(abs(i - cxm) / max(cxm, 1), abs(j - cym) / max(cym, 1)) if maxd else 0
            core = d < 0.55
            split = rng.random() < (0.35 if core else 0.6)
            parcels = [(0, block_bays[0])] if not split else [(0, block_bays[0] // 2 - 1), (block_bays[0] // 2 + 1, block_bays[0])]
            for a, b in parcels:
                n = b - a
                s = rng.randint(0, 99999)
                if core and rng.random() < 0.85:
                    margin = 2 if n > 12 else 1                                                       # plaza round the tower
                    floors = int(rng.uniform(22, 55) * (1.0 - 0.4 * d))
                    tower((x0 + (a + margin) * BAY, y0 + margin * BAY, z0), (n - 2 * margin, block_bays[1] - 2 * margin), floors,
                          podium=rng.choice((2, 3, 4)), setback=rng.choice((1, 2)) if n - 2 * margin > 8 else 1, seed=s, ins=ins)
                    if props:
                        for k in range(3):
                            sp = rng.choice(("birch", "maple"))
                            trees.add(rng.choice(uk.assets.variants(sp)), (x0 + (a + 0.6 + k * (n - 1.2) / 2) * BAY, y0 + 0.6 * BAY, z0),
                                      rng.uniform(0, 360), rng.uniform(0.4, 0.5))
                else:
                    midrise((x0 + a * BAY, y0, z0), (n, block_bays[1]), rng.randint(5, 12) if core else rng.randint(4, 9), seed=s, ins=ins)
            if props:  # sidewalk dressing: street trees, lamps, a hydrant and bins per side
                for side, (sx, sy, along) in enumerate(((0, -1, "x"), (0, 1, "x"), (-1, 0, "y"), (1, 0, "y"))):
                    length = bw if along == "x" else bd
                    for k in range(int(length / 1200)):
                        t = -length / 2 + 600 + k * 1200
                        px_, py_ = (cx + t, cy + sy * (bd / 2 + sidewalk - 90)) if along == "x" else (cx + sx * (bw / 2 + sidewalk - 90), cy + t)
                        if k % 3 == 1:
                            trees.add("street_lamp", (px_, py_, z0), (0 if along == "y" else 90) + (180 if (sx > 0 or sy > 0) else 0))
                        else:
                            sp = rng.choice(("birch", "maple"))
                            trees.add(rng.choice(uk.assets.variants(sp)), (px_, py_, z0), rng.uniform(0, 360), rng.uniform(0.38, 0.48))
                    hx, hy = (cx - length / 2 + 300, cy + sy * (bd / 2 + sidewalk - 60)) if along == "x" else (cx + sx * (bw / 2 + sidewalk - 60), cy - length / 2 + 300)
                    trees.add("fire_hydrant", (hx, hy, z0), rng.uniform(0, 360))
                    trees.add("metal_trash_can", (hx + (150 if along == "x" else 0), hy + (0 if along == "x" else 150), z0), rng.uniform(0, 360))
    ids = ins.flush()
    ids += trees.flush()
    return ids
