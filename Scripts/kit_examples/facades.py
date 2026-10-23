"""EXAMPLE: build streets and districts in ANY architectural style from a style recipe (facade_styles.py).

    import unreal_kit as uk, facades, facade_styles as fs
    facades.street(fs.PARIS, length=6000, seed=1)                 # a boulevard with buildings both sides
    facades.district(fs.MEDITERRANEAN, blocks_x=3, blocks_y=2)     # a grid of blocks, corners wrap round
    facades.row(fs.PARIS, (0, 0, 20), yaw=0, length=4000)          # one row of buildings, facade facing -Y

A style recipe is a dict (see facade_styles.py) with:
  path   mesh path prefix of its piece kit (/Game/RuntimeArchitecture/<Style>/SM_XX_)
  bay    bay width cm; wall thickness; depth of a building
  house(rng)                -> one building's choices (bays, floors, colours, door bay ...) as a dict
  column(h, b, rng)         -> [(piece, z)] for bay b of building h, bottom to top
  shell(h)                  -> [(slot, x0, x1, y0, y1, z0, z1)] boxes in building space (back, sides, roof fill)
  chimneys(h)               -> [(piece, x, y, z)]
  street                    -> pavement / kerb / road widths, lamps, trees
To make a new place (Lisbon, Chicago, Kyoto ...): make its pieces with Scripts/architecture/make_style.py (or reuse a
kit), then write a recipe; the layout, instancing, corners and ground below are shared.

Pieces: one bay wide, origin = bay centre on the wall's front plane at the storey floor, facade towards -Y at yaw 0.
Custom data per building: [seed, paint A rgb (doors, shutters, shop fronts), paint B rgb (painted walls)].
"""

import math
import random

import unreal_kit as uk
from unreal_kit.assets import Instancer

GROUND = 20.0  # pavement top above the level floor (the road is 15 lower)


def _frame(origin, yaw):
    c, s = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))

    def world(lx, ly, lz=0.0):
        return (origin[0] + lx * c - ly * s, origin[1] + lx * s + ly * c, origin[2] + lz)
    return world


def _box(ins, style, slot, w, yaw, x0, x1, y0, y1, z0, z1, data=None):
    ins.add(style["path"] + "Block_" + slot.title(), w((x0 + x1) / 2, (y0 + y1) / 2, z0), yaw,
            (abs(x1 - x0) / 100.0, abs(y1 - y0) / 100.0, abs(z1 - z0) / 100.0), data)


def row(style, origin, yaw=0.0, length=4000.0, seed=None, ins=None, returns=(False, False), pavement=True, lamps=True):
    """A row of buildings along local +X from origin (front plane, pavement level), facade facing local -Y.
    Building widths come from the recipe; the row is filled up to `length`. returns = (left, right): wrap the end
    building's facade round the corner onto the side street. Returns the Instancer (flushed if ins was None)."""
    rng = random.Random(seed)
    own = ins is None
    ins = ins or Instancer(f"Row_{style['name']}_{seed}")
    w = _frame(origin, yaw)
    bay = style["bay"]
    houses, x = [], 0.0
    while True:
        h = style["house"](rng)
        if x + h["bays"] * bay > length + 1:
            n = int((length - x) // bay)
            if n <= 0:
                break
            h = style["house"](rng, bays=n)
        h["x"] = x
        houses.append(h)
        x += h["bays"] * bay
    L = x
    for i, h in enumerate(houses):
        h["first"], h["last"] = i == 0, i == len(houses) - 1           # shells only close the row's ends
        data = [rng.random() * 100.0, *h["paint_a"], *h["paint_b"]]
        for b in range(h["bays"]):
            lx = h["x"] + (b + 0.5) * bay
            for piece, z in style["column"](h, b, rng):
                ins.add(style["path"] + piece, w(lx, 0, z), yaw, 1.0, data)
        for slot, x0, x1, y0, y1, z0, z1 in style["shell"](h):
            _box(ins, style, slot, w, yaw, h["x"] + x0, h["x"] + x1, y0, y1, z0, z1, data)
        for piece, cx, cy, cz in style["chimneys"](h, rng):
            ins.add(style["path"] + piece, w(h["x"] + cx, cy, cz), yaw, 1.0, data)
        # corner returns: the end building's facade continues along its side wall
        for side, on in ((0, returns[0] and i == 0), (1, returns[1] and i == len(houses) - 1)):
            if not on:
                continue
            nb = int((style["depth"] - style["wall"]) // bay)
            ex = h["x"] if side == 0 else h["x"] + h["bays"] * bay
            ryaw = yaw - 90 if side == 0 else yaw + 90
            for k in range(nb):
                ly = (k + 0.5) * bay + style["wall"] * 0.2
                cb = 0 if k == 0 else 1  # the first return bay never gets the door
                for piece, z in style["column"](dict(h, door=-1), cb, rng):
                    ins.add(style["path"] + piece, w(ex, ly, z), ryaw, 1.0, data)
    st = style["street"]
    if pavement:
        pw, kw = st["pavement"], st["kerb"]
        ext = st["pavement"] if any(returns) else 0.0
        _box(ins, style, "paving", w, yaw, -ext * returns[0], L + ext * returns[1], -pw, 0, -GROUND, 0)
        _box(ins, style, "kerb", w, yaw, -ext * returns[0], L + ext * returns[1], -pw - kw, -pw, -GROUND, -2)
        for side, on in ((0, returns[0]), (1, returns[1])):   # pavement round the corner, along the return
            if on:
                ex = 0.0 if side == 0 else L
                sx0, sx1 = (ex - pw, ex) if side == 0 else (ex, ex + pw)
                _box(ins, style, "paving", w, yaw, sx0, sx1, 0, style["depth"] + pw, -GROUND, -0.5)
        if lamps and st.get("lamp"):
            for i in range(max(1, int(L / st["lamp_spacing"]))):
                ins.add(st["lamp"], w(st["lamp_spacing"] / 2 + i * st["lamp_spacing"], -pw + 45, 0), yaw - 90)
        if st.get("trees"):
            for i in range(int(L / st["tree_spacing"])):
                sp = rng.choice(st["trees"])
                lo, hi = st.get("tree_scale", (0.55, 0.7))
                ins.add(rng.choice(uk.assets.variants(sp)), w(st["tree_spacing"] * (i + 0.5), -pw + 90, 0), rng.uniform(0, 360), rng.uniform(lo, hi))
    if own:
        ins.flush()
    return ins


def street(style, length=6000.0, width=None, seed=None, origin=(0, 0, 0)):
    """A straight street along +X: a row of buildings on each side facing it, road between. Returns node ids."""
    ins = Instancer(f"Street_{style['name']}_{seed}")
    st = style["street"]
    width = width or st["road"]
    front = width / 2 + st["kerb"] + st["pavement"]
    ox, oy, oz = origin[0], origin[1], origin[2] + GROUND
    row(style, (ox, oy + front, oz), 0.0, length, seed, ins)
    row(style, (ox + length, oy - front, oz), 180.0, length, seed + 1, ins)
    _box(ins, style, "road", lambda x, y, z=0.0: (ox + x, oy + y, oz + z), 0.0, -1000, length + 1000, -width / 2, width / 2, -GROUND, -GROUND + 5)
    return ins.flush()


def district(style, blocks_x=3, blocks_y=2, block_w=6000.0, block_d=4500.0, road=None, seed=3, origin=(0, 0, 0), courtyards=True):
    """A grid of city blocks; each block is four rows of buildings round a courtyard, corners wrapped. block_w /
    block_d are measured between front walls. Returns node ids."""
    st = style["street"]
    road = road or st["road"]
    rng = random.Random(seed)
    ins = Instancer(f"District_{style['name']}_{seed}")
    ox, oy, z0 = origin[0], origin[1], origin[2] + GROUND
    edge = st["pavement"] + st["kerb"]
    px, py = block_w + 2 * edge + road, block_d + 2 * edge + road
    depth = style["depth"]
    world = lambda x, y, z=0.0: (ox + x, oy + y, z0 + z)  # noqa: E731
    for j in range(blocks_y + 1):
        _box(ins, style, "road", world, 0.0, -px / 2 - road, blocks_x * px - px / 2 + road, (j - 0.5) * py - road / 2, (j - 0.5) * py + road / 2,
             -GROUND, -GROUND + 5)
    for i in range(blocks_x + 1):
        _box(ins, style, "road", world, 0.0, (i - 0.5) * px - road / 2, (i - 0.5) * px + road / 2, -py / 2 - road, blocks_y * py - py / 2 + road,
             -GROUND, -GROUND + 5)
    for i in range(blocks_x):
        for j in range(blocks_y):
            cx, cy = ox + i * px, oy + j * py
            x0, x1, y0, y1 = cx - block_w / 2, cx + block_w / 2, cy - block_d / 2, cy + block_d / 2
            # north and south rows span the block and wrap their corners; east and west rows fit between them
            side_len = block_d - 2 * depth
            rows = [((x1, y1, z0), 180.0, block_w, (True, True)), ((x0, y0, z0), 0.0, block_w, (True, True)),
                    ((x1, y0 + depth, z0), 90.0, side_len, (False, False)), ((x0, y1 - depth, z0), 270.0, side_len, (False, False))]
            for o, yw, ln, ret in rows:
                if ln >= style["bay"]:
                    row(style, o, yw, ln, rng.randint(0, 99999), ins, returns=ret)
            if courtyards:
                _box(ins, style, "soil", world, 0.0, x0 + depth - ox, x1 - depth - ox, y0 + depth - oy, y1 - depth - oy, -GROUND, -5)
    return ins.flush()
