"""EXAMPLE: Georgian terraces (London / Dublin / Regency stucco, 1720-1830) from the packaged Georgian kit.

    import unreal_kit as uk, georgian
    georgian.street(houses=10, style="london", seed=3)                  # one street, two terraces facing each other
    georgian.terrace((0, 0, 0), yaw=0, houses=8, style="dublin")         # one terrace, facade facing -Y
    georgian.square(houses_per_side=6, style="regency")                  # four terraces round a garden square

How it works (reuse it for any modular style):
  * Pieces in /Game/RuntimeArchitecture/Georgian are one bay wide (190 cm) and one storey tall, real scale, with the
    window/door opening cut through a 35 cm wall. Origin: bay centre, y 0 on the front plane, z 0 at the storey's
    floor. Facade faces -Y at yaw 0.
  * A house = 3 bays x storeys (basement, ground, first = piano nobile, second, third) + cornice, parapet, mansard
    with dormers, party-wall chimneys; area railings and entrance steps in front.
  * Rules pick the piece per cell: door bay (pairs of houses mirror so doors sit together), arched or square ground
    windows, balconettes, dormers. Per-house custom data gives each house its own door colour and stucco tint.
  * Everything goes through uk.assets.Instancer: one instance group per piece type for the whole street.
Storeys (cm above the pavement): area floor -180, ground floor 90, first 420, second 780, third 1080, top 1330.
"""

import math
import random

import unreal_kit as uk
from unreal_kit.assets import Instancer

G = "/Game/RuntimeArchitecture/Georgian/SM_GT_"
BAY, HOUSE_BAYS, WALL = 190.0, 3, 35.0
Z = {"area": -180.0, "ground": 90.0, "first": 420.0, "second": 780.0, "third": 1080.0}
H = {"second": 300.0, "third": 250.0}
AREA, PAVEMENT, KERB = 155.0, 300.0, 30.0  # area depth (front plane to railing), pavement, kerb widths
BASE = 190.0  # street() / square() build on made ground: pavement 190 cm above the origin so basements fit above it

# Linear colours. Doors: the classic Georgian palette.
DOORS = {"black": (0.012, 0.012, 0.012), "green": (0.02, 0.07, 0.035), "navy": (0.012, 0.025, 0.08), "oxblood": (0.17, 0.02, 0.018),
         "red": (0.42, 0.03, 0.02), "yellow": (0.62, 0.42, 0.04), "teal": (0.02, 0.11, 0.11), "blue": (0.035, 0.11, 0.3),
         "grey": (0.13, 0.14, 0.14), "white": (0.75, 0.75, 0.72)}
STUCCO = {"white": (1.0, 1.0, 0.99), "cream": (1.0, 0.95, 0.83), "ivory": (0.99, 0.96, 0.9), "stone": (0.93, 0.9, 0.84), "grey": (0.9, 0.91, 0.92),
          "pink": (1.0, 0.9, 0.86)}

STYLES = {
    # London: stucco basement and ground floor (rusticated), brick above, arched ground-floor windows, balconettes
    "london": {"basement": "Stucco", "ground": "Stucco", "upper": "Brick", "arched": 0.6, "case": 0.3, "balconette": 0.85,
               "stucco": ["white", "cream", "ivory", "stone"], "doors": ["black", "black", "green", "navy", "oxblood", "red", "blue", "teal", "grey"]},
    # Dublin: red brick to the ground, painted doorcases with fanlights, bright doors
    "dublin": {"basement": "Stucco", "ground": "Brick", "upper": "Brick", "arched": 0.0, "case": 0.9, "balconette": 0.5,
               "stucco": ["stone", "grey"], "doors": ["red", "yellow", "green", "blue", "black", "teal", "oxblood", "white"]},
    # Regency (Belgravia, Brighton): stucco from top to bottom, painted one colour along the terrace
    "regency": {"basement": "Stucco", "ground": "Stucco", "upper": "Stucco", "arched": 1.0, "case": 0.6, "balconette": 1.0,
                "stucco": ["white", "ivory", "cream"], "doors": ["black", "black", "navy", "green"], "uniform": True},
}


def _frame(origin, yaw):
    c, s = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))

    def world(lx, ly, lz=0.0):
        return (origin[0] + lx * c - ly * s, origin[1] + lx * s + ly * c, origin[2] + lz)
    return world


def terrace(origin=(0, 0, 0), yaw=0.0, houses=8, style="london", floors=4, depth=1000.0, seed=1, ins=None, pavement=True, ground=BASE,
            lamps=False):
    """One terrace. origin = the left end of the front wall plane at pavement level; bays run along local +X and the
    facade faces local -Y (world direction yaw - 90). The area floor is 180 cm below the pavement and the pavement
    and kerb blocks reach `ground` cm down, so put origin z = ground level + 190 on flat ground (street() does).
    Returns the Instancer (flushed when ins was not given)."""
    st = STYLES[style]
    rng = random.Random(seed)
    own = ins is None
    ins = ins or Instancer(f"Terrace_{style}_{seed}")
    w = _frame(origin, yaw)
    L = houses * HOUSE_BAYS * BAY
    top = Z["third"] + H["third"] if floors >= 4 else Z["second"] + H["second"]
    roof = top + 38.0                               # parapet / mansard base
    arched = rng.random() < st["arched"]            # one choice per terrace: terraces were built as a piece
    terrace_stucco = STUCCO[rng.choice(st["stucco"])]
    for h in range(houses):
        hs = rng.random() * 100.0
        door = DOORS[rng.choice(st["doors"])]
        stucco = terrace_stucco if st.get("uniform") else STUCCO[rng.choice(st["stucco"])]
        data = [hs, *door, *stucco]
        door_bay = HOUSE_BAYS - 1 if h % 2 == 0 else 0  # pairs mirror: doors side by side
        case = rng.random() < st["case"]
        balconettes = rng.random() < st["balconette"]
        dormers = [rng.random() < 0.75 for _ in range(HOUSE_BAYS)]
        for b in range(HOUSE_BAYS):
            lx = (h * HOUSE_BAYS + b + 0.5) * BAY

            def put(piece, ly, lz, extra=None):
                ins.add(G + piece, w(lx, ly, lz), yaw, 1.0, extra or data)
            put(f"Basement_{st['basement']}", 0, Z["area"])
            if b == door_bay:
                put(f"Door_{st['ground']}{'_Case' if case else ''}", 0, Z["ground"])
                put("Steps", 0, 0)
            else:
                put(f"Ground_{st['ground']}{'_Arched' if arched else ''}", 0, Z["ground"])
                put("Area_Railing", -AREA, 0)
            put(f"First_{st['upper']}", 0, Z["first"])
            if balconettes:
                put("Balconette", 0, Z["first"] + 30)
            put(f"Second_{st['upper']}", 0, Z["second"])
            if floors >= 4:
                put(f"Third_{st['upper']}", 0, Z["third"])
            put("Cornice", 0, top)
            put("Parapet", 0, roof)
            put("Mansard_Dormer" if dormers[b] else "Mansard", 0, roof)
    # party-wall chimney stacks (also at both ends)
    for h in range(houses + 1):
        ins.add(G + "Chimney", w(h * HOUSE_BAYS * BAY, 150, roof + 265), yaw, 1.0, [h * 7.0])
    # shell: back wall, roof behind the mansard, end walls (the rear is plain brick; nobody sees it from the street)
    back_h = roof + 90 - Z["area"]
    ins.add_box("brick", w(L / 2, depth - WALL / 2, Z["area"]), (L, WALL, back_h), yaw, [seed])
    ins.add_box("slate", w(L / 2, (545 + depth) / 2, roof), (L, depth - 545, 260), yaw, [seed])
    ins.add_box("lead", w(L / 2, (145 + depth) / 2, roof + 260), (L, depth - 145, 5), yaw)
    for ex in (-WALL / 2, L + WALL / 2):
        ins.add_box("brick", w(ex, depth / 2, Z["area"]), (WALL, depth, roof + 280 - Z["area"]), yaw, [seed + ex])
    # area (sunken light well), its retaining wall, pavement and kerb
    ins.add_box("paving", w(L / 2, -AREA / 2 + 8, Z["area"] - 10), (L, AREA - 16, 10), yaw)
    for ex in (-WALL, L + WALL):  # close the ends of the area well
        ins.add_box("stone", w(ex + (WALL / 2 if ex < 0 else -WALL / 2), -AREA / 2, Z["area"] - 10), (WALL, AREA, -Z["area"] + 10), yaw)
    ins.add_box("stone", w(L / 2, -AREA, Z["area"]), (L, 30, -Z["area"]), yaw)
    if pavement:
        ins.add_box("paving", w(L / 2, -AREA - 15 - PAVEMENT / 2, -ground), (L + 2 * WALL, PAVEMENT, ground), yaw)
        ins.add_box("granite", w(L / 2, -AREA - 15 - PAVEMENT - KERB / 2, -ground), (L + 2 * WALL, KERB, ground - 15), yaw)
    if lamps:  # cast-iron lamps near the kerb
        for i in range(max(1, int(L / 1800))):
            ins.add("street_lamp", w(900 + i * 1800, -AREA - 15 - PAVEMENT + 40, 0), yaw - 90)
    if own:
        ins.flush()
    return ins


def street(houses=10, style="london", width=900.0, seed=3, floors=4, lamps=True, origin=(0, 0, 0)):
    """A straight street: road along +X with a terrace on each side facing it, on made ground (pavement 190 cm
    above origin z). Returns the node ids."""
    ins = Instancer(f"Street_{style}_{seed}")
    L = houses * HOUSE_BAYS * BAY
    front = width / 2 + KERB + PAVEMENT + 15 + AREA        # road centre to the front wall planes
    ox, oy, oz = origin[0], origin[1], origin[2] + BASE
    terrace((ox, oy + front, oz), 0.0, houses, style, floors, seed=seed, ins=ins)                      # north side, faces -Y
    terrace((ox + L, oy - front, oz), 180.0, houses, style, floors, seed=seed + 1, ins=ins)           # south side, faces +Y
    ins.add_box("asphalt", (ox + L / 2, oy, oz - BASE), (L + 4000, width, BASE - 15))
    if lamps:
        for side in (-1, 1):
            for i in range(int(L / 1800) + 1):
                x = ox + 600 + i * 1800
                ins.add("street_lamp", (x, oy + side * (width / 2 + KERB + 40), oz), 90 if side < 0 else -90)
    return ins.flush()


def square(houses_per_side=6, style="regency", seed=5, floors=4, garden=True):
    """Four terraces round a garden square (Bedford Square, Merrion Square), streets on all sides."""
    ins = Instancer(f"Square_{style}_{seed}")
    z0 = BASE
    L = houses_per_side * HOUSE_BAYS * BAY
    road = 900.0
    front = road + KERB + PAVEMENT + 15 + AREA
    half = L / 2 + front
    # each side: (origin, yaw) so the facade faces the centre
    sides = [((-L / 2, half, z0), 0.0), ((L / 2, -half, z0), 180.0), ((half, L / 2, z0), 270.0), ((-half, -L / 2, z0), 90.0)]
    for i, (o, yw) in enumerate(sides):
        terrace(o, yw, houses_per_side, style, floors, seed=seed + i, ins=ins)
    ins.add_box("asphalt", (0, 0, 0), (2 * half + 2000, 2 * half + 2000, z0 - 15))
    if garden:  # railed private garden: lawn, plane-like trees round the edge, gravel walk
        g = half - front - road / 2 - 100          # garden half-size, inside the road ring
        ins.add_box("soil", (0, 0, 0), (2 * g, 2 * g, z0 + 5))
        ins.add_box("granite", (0, 0, 0), (2 * g + 40, 2 * g + 40, z0))
        for k in range(4):                           # railings round it (2 m bays)
            n = int(2 * g / BAY)
            for i in range(n):
                a = -g + (i + 0.5) * 2 * g / n
                x, y, yw = [(a, -g, 0.0), (a, g, 180.0), (g, a, 90.0), (-g, a, 270.0)][k]
                ins.add(G + "Area_Railing", (x, y, z0), yw, (2 * g / n / BAY, 1.0, 1.0))
    ids = ins.flush()
    if garden:
        rng = random.Random(seed)
        trees = Instancer("SquareTrees")
        for k in range(4):
            for t in range(int((2 * g - 1000) / 1100) + 1):
                a = -g + 500 + t * 1100
                x, y = [(a, -g + 450), (a, g - 450), (g - 450, a), (-g + 450, a)][k]
                trees.add(rng.choice(uk.assets.variants("maple") + uk.assets.variants("oak")), (x, y, z0 + 5), rng.uniform(0, 360), rng.uniform(0.85, 1.05))
        ids += trees.flush()
        uk.assets.ground_cover([(-g + 30, -g + 30, z0 + 10), (g - 30, -g + 30, z0 + 10), (g - 30, g - 30, z0 + 10), (-g + 30, g - 30, z0 + 10)],
                               {"grass": 8, "grass_medium": 2}, spacing=45, keep=0.95, seed=seed, name="SquareLawn")
    return ids


def district(blocks_x=3, blocks_y=2, houses_x=9, houses_y=6, street_width=1100.0, styles=None, square_at=None, seed=7, floors=4,
             origin=(0, 0, 0), trees=True):
    """A Georgian district: a grid of city blocks, each ringed by four terraces facing the streets, with private
    gardens behind and optionally one block left open as a railed garden square. On made ground (pavement 190 cm
    above origin z). styles: {style: weight}; square_at: (i, j) block index for the garden square.
    Returns the node ids. Example: georgian.district(3, 3, styles={"london": 3, "regency": 1}, square_at=(1, 1))"""
    styles = styles or {"london": 3, "dublin": 1, "regency": 1}
    rng = random.Random(seed)
    names, weights = list(styles), list(styles.values())
    ins = Instancer(f"District_{seed}")
    ox, oy, z0 = origin[0], origin[1], origin[2] + BASE
    depth = 1000.0
    edge = AREA + 15 + PAVEMENT + KERB                      # front plane to the road edge
    bw = houses_x * HOUSE_BAYS * BAY + 2 * WALL              # block size between front planes
    bd = houses_y * HOUSE_BAYS * BAY + 2 * depth + 2 * WALL
    pitch_x, pitch_y = bw + 2 * edge + street_width, bd + 2 * edge + street_width
    total_x, total_y = blocks_x * pitch_x, blocks_y * pitch_y
    # roads only between the blocks (a slab under the blocks would fill the sunken areas); overlaps at crossings are
    # invisible because the material is world-aligned
    road_w = street_width + 2 * KERB
    for j in range(blocks_y + 1):
        ins.add_box("asphalt", (ox + total_x / 2 - pitch_x / 2, oy + (j - 0.5) * pitch_y, origin[2]), (total_x + road_w, road_w, BASE - 15))
    for i in range(blocks_x + 1):
        ins.add_box("asphalt", (ox + (i - 0.5) * pitch_x, oy + total_y / 2 - pitch_y / 2, origin[2]), (road_w, total_y + road_w, BASE - 15))
    tree_ins = Instancer(f"District_{seed}_Trees")
    for i in range(blocks_x):
        for j in range(blocks_y):
            cx, cy = ox + i * pitch_x, oy + j * pitch_y
            x0, x1, y0, y1 = cx - bw / 2, cx + bw / 2, cy - bd / 2, cy + bd / 2
            if square_at == (i, j):
                _garden(ins, tree_ins, cx, cy, bw / 2 + edge - KERB, bd / 2 + edge - KERB, z0, rng)
                continue
            st = lambda: rng.choices(names, weights)[0]  # noqa: E731
            s_ns = st()                                  # facing terraces of a street usually match; vary per block side
            sides = [((x1 - WALL, y1, z0), 180.0, houses_x, s_ns), ((x0 + WALL, y0, z0), 0.0, houses_x, st()),
                     ((x1, y0 + depth + WALL, z0), 90.0, houses_y, st()), ((x0, y1 - depth - WALL, z0), 270.0, houses_y, s_ns)]
            for k, (o, yw, n, style) in enumerate(sides):
                terrace(o, yw, n, style, floors, depth, seed=rng.randint(0, 99999), ins=ins, lamps=True)
            # pavement round the corners (just below the terrace pavements so they never z-fight)
            for sx in (-1, 1):
                for sy in (-1, 1):
                    px = cx + sx * (bw / 2 + edge / 2 - KERB / 2)
                    py = cy + sy * (bd / 2 - depth / 2 + edge / 2)
                    ins.add_box("paving", (px, py, z0 - BASE), (edge - KERB, depth + edge + 2 * WALL, BASE - 0.5))
            # back gardens inside the block, with a few trees showing over the roofs
            gx, gy = bw / 2 - depth - WALL, bd / 2 - depth - WALL
            ins.add_box("soil", (cx, cy, z0 - BASE), (2 * gx + 20, 2 * gy + 20, BASE - 120))
            if trees:
                for _ in range(int(gx * gy / 2.5e6) + 1):
                    sp = rng.choice(("maple", "birch", "oak"))
                    tree_ins.add(rng.choice(uk.assets.variants(sp)), (cx + rng.uniform(-gx + 300, gx - 300), cy + rng.uniform(-gy + 300, gy - 300), z0 - 120),
                                 rng.uniform(0, 360), rng.uniform(0.6, 0.8))
    ids = ins.flush()
    ids += tree_ins.flush()
    return ids


def _garden(ins, tree_ins, cx, cy, hx, hy, z0, rng):
    """Railed garden square filling a block: pavement ring, kerb, railings, lawn, trees round the edge."""
    g_x, g_y = hx - 300, hy - 300
    ins.add_box("paving", (cx, cy, z0 - BASE), (2 * hx, 2 * hy, BASE))
    ins.add_box("soil", (cx, cy, z0 - BASE), (2 * g_x, 2 * g_y, BASE + 5))
    for k in range(4):
        length = 2 * (g_x if k < 2 else g_y)
        n = max(1, int(length / BAY))
        for i in range(n):
            a = -length / 2 + (i + 0.5) * length / n
            x, y, yw = [(cx + a, cy - g_y, 0.0), (cx + a, cy + g_y, 180.0), (cx + g_x, cy + a, 90.0), (cx - g_x, cy + a, 270.0)][k]
            ins.add(G + "Area_Railing", (x, y, z0), yw, (length / n / BAY, 1.0, 1.0))
    for k in range(4):
        length = 2 * (g_x if k < 2 else g_y) - 900
        for t in range(int(length / 1000) + 1):
            a = -length / 2 + t * 1000
            x, y = [(cx + a, cy - g_y + 450), (cx + a, cy + g_y - 450), (cx + g_x - 450, cy + a), (cx - g_x + 450, cy + a)][k]
            sp = rng.choice(("maple", "oak"))
            tree_ins.add(rng.choice(uk.assets.variants(sp)), (x, y, z0 + 5), rng.uniform(0, 360), rng.uniform(0.85, 1.05))
    try:
        uk.assets.ground_cover([(cx - g_x + 30, cy - g_y + 30, z0 + 10), (cx + g_x - 30, cy - g_y + 30, z0 + 10), (cx + g_x - 30, cy + g_y - 30, z0 + 10),
                                (cx - g_x + 30, cy + g_y - 30, z0 + 10)], {"grass": 8, "grass_medium": 2}, spacing=50, keep=0.95, name="SquareLawn")
    except Exception as e:  # noqa: BLE001  (a build without foliage still gets the square)
        print("lawn skipped:", e)
