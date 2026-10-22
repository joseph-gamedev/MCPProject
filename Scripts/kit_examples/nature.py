"""EXAMPLE: varied trees and plants from organic shapes (uk.parts.blob / limb / curve), not spheres on sticks.

    import unreal_kit as uk, nature
    uk.place("Oak", nature.tree("oak", seed=3), at=[0, 0, 0])
    uk.place_many(nature.grove(8000, 6000, count=40, species=["oak", "maple", "birch", "pine"], seed=2))
    nature.tree(species, seed, height=None, season="summer"|"autumn"|"winter")

Species: oak, maple, birch, pine, spruce, cypress, palm, willow, acacia, dead, shrub, hedge. Each has its own
structure (branching pattern, crown shape, bark, leaf colour), and every seed differs (lean, branch angles, lumps).
Make your own: grow branches with curve() + limb(), put foliage blobs at branch tips, pick real colours.
"""

import math
import random

from unreal_kit.parts import M, blob, curve, limb

LEAF = {
    "oak": [(66, 88, 42), (78, 98, 48), (58, 80, 38)],
    "maple": [(84, 110, 50), (70, 98, 44)],
    "birch": [(112, 136, 60), (98, 124, 54)],
    "pine": [(40, 62, 38), (46, 70, 42)],
    "spruce": [(34, 56, 40), (40, 62, 46)],
    "cypress": [(44, 66, 40), (50, 72, 44)],
    "palm": [(86, 116, 52), (74, 104, 46)],
    "willow": [(104, 128, 62), (92, 118, 56)],
    "acacia": [(92, 110, 52), (82, 100, 46)],
    "shrub": [(70, 96, 46), (60, 86, 42)],
}
AUTUMN = [(178, 92, 34), (196, 132, 40), (150, 56, 30), (204, 164, 58)]
BARK = {"birch": (214, 210, 198), "palm": (128, 110, 86), "pine": (96, 66, 48), "default": (82, 64, 50)}


def _leaf(species, rng, season):
    if season == "autumn" and species in ("oak", "maple", "birch", "willow"):
        return M("grass", rng.choice(AUTUMN), roughness=0.8)
    return M("grass", rng.choice(LEAF.get(species, LEAF["oak"])), roughness=0.8)


def _bark(species):
    return M("wood", BARK.get(species, BARK["default"]), roughness=0.95)


def _branches(rng, start, direction, length, radius, depth, spread, bend, out, tips, bm):
    """Recursive branching: each limb splits into 2-3 shorter, thinner limbs; tips collect foliage positions."""
    path = curve(start, direction, length, bend=bend, segments=4, wobble=0.08, seed=rng.randint(0, 99999))
    out.append(limb(path, radius, radius * 0.62, m=bm))
    if depth == 0 or radius < 2.5:
        tips.append(path[-1])
        return
    end = path[-1]
    d = [path[-1][k] - path[-2][k] for k in range(3)]
    for _ in range(rng.choice([2, 2, 3])):
        a = rng.uniform(0, 2 * math.pi)
        s = rng.uniform(spread * 0.6, spread)
        nd = [d[0] + math.cos(a) * s, d[1] + math.sin(a) * s, d[2] + rng.uniform(-0.1, 0.25)]
        _branches(rng, end, nd, length * rng.uniform(0.6, 0.78), radius * 0.62, depth - 1, spread, bend, out, tips, bm)


def broadleaf(rng, h, species, season, crown_w=0.7, depth=3, spread=0.9, clump=0.45, droop=0.0):
    bm = _bark(species)
    trunk_h = h * rng.uniform(0.32, 0.42)
    lean = [rng.uniform(-0.08, 0.08), rng.uniform(-0.08, 0.08), 1]
    P, tips = [], []
    trunk = curve([0, 0, 0], lean, trunk_h, segments=4, wobble=0.03, seed=rng.randint(0, 9999))
    r0 = h * 0.028
    P.append(limb(trunk, r0, r0 * 0.75, m=bm))
    P.append(blob([r0 * 3.2, r0 * 3.2, r0 * 1.4], [0, 0, r0 * 0.3], m=bm, seed=rng.randint(0, 999), bumpiness=0.35, flat_bottom=0.5))  # root flare
    for _ in range(rng.randint(3, 4)):
        a = rng.uniform(0, 2 * math.pi)
        _branches(rng, trunk[-1], [math.cos(a) * spread, math.sin(a) * spread, 1.0], (h - trunk_h) * 0.45, r0 * 0.7, depth - 1, spread,
                  (0, 0, -droop), P, tips, bm)
    if season != "winter" or species in ("pine", "spruce"):
        for t in tips:
            size = h * clump * rng.uniform(0.6, 1.0) * crown_w
            P.append(blob([size, size, size * rng.uniform(0.65, 0.85)], [t[0], t[1], t[2] + size * 0.1], m=_leaf(species, rng, season),
                          seed=rng.randint(0, 99999), bumpiness=0.3, detail=1))
    return P


def conifer(rng, h, species):
    """Pine: bare lower trunk, irregular layered crown; spruce: dense cone of drooping tiers to the ground."""
    bm = _bark(species)
    P = []
    lean = [rng.uniform(-0.05, 0.05), rng.uniform(-0.05, 0.05), 1]
    trunk = curve([0, 0, 0], lean, h, segments=5, wobble=0.015, seed=rng.randint(0, 9999))
    P.append(limb(trunk, h * 0.022, h * 0.004, m=bm))
    start = 0.5 if species == "pine" else 0.08
    tiers = rng.randint(6, 9) if species == "spruce" else rng.randint(4, 6)
    for i in range(tiers):
        f = start + (1 - start) * i / tiers
        idx = min(len(trunk) - 1, int(f * (len(trunk) - 1)))
        c = trunk[idx]
        w = h * (0.42 if species == "spruce" else 0.3) * (1 - f) ** (1.1 if species == "spruce" else 0.6) + h * 0.05
        for k in range(1 if species == "spruce" else rng.randint(2, 3)):
            off = 0 if species == "spruce" else w * 0.25
            a = rng.uniform(0, 2 * math.pi)
            P.append(blob([w, w * rng.uniform(0.85, 1.1), w * (0.38 if species == "spruce" else 0.5)],
                          [c[0] + math.cos(a) * off, c[1] + math.sin(a) * off, f * h], m=_leaf(species, rng, "summer"),
                          seed=rng.randint(0, 99999), bumpiness=0.35, detail=1, flat_bottom=0.3 if species == "spruce" else 0))
    P.append(blob([h * 0.06, h * 0.06, h * 0.12], [trunk[-1][0], trunk[-1][1], h * 0.97], m=_leaf(species, rng, "summer"), seed=rng.randint(0, 999), detail=1))
    return P


def palm(rng, h):

    bm = _bark("palm")
    P = []
    trunk = curve([0, 0, 0], [rng.uniform(-0.3, 0.3), rng.uniform(-0.3, 0.3), 1], h, bend=(rng.uniform(-0.4, 0.4), rng.uniform(-0.4, 0.4), 0),
                  segments=7, seed=rng.randint(0, 9999))
    P.append(limb(trunk, h * 0.03, h * 0.022, m=bm, sides=8))
    top = trunk[-1]
    for i in range(rng.randint(9, 13)):
        a = 2 * math.pi * i / 11 + rng.uniform(-0.2, 0.2)
        frond = curve(top, [math.cos(a), math.sin(a), rng.uniform(0.2, 0.6)], h * rng.uniform(0.32, 0.42), bend=(0, 0, -1.6), segments=6,
                      seed=rng.randint(0, 9999))
        P.append(limb(frond, h * 0.035, h * 0.004, m=_leaf("palm", rng, "summer"), sides=4, flat=0.12))
    P.append(blob([h * 0.07] * 3, top, m=M("wood", (110, 86, 54)), seed=rng.randint(0, 99), detail=1))  # coconuts / crown
    return P


def willow(rng, h, season):
    P = broadleaf(rng, h, "willow", season, crown_w=0.55, depth=2, spread=1.1, clump=0.3)
    tips = [p["sections"][-1][0] for p in P if p.get("shape") == "loft"][-12:]
    for t in tips:  # hanging curtains
        for _ in range(3):
            hang = curve([t[0] + rng.uniform(-60, 60), t[1] + rng.uniform(-60, 60), t[2]], [0, 0, -1], h * rng.uniform(0.35, 0.55), segments=3,
                         seed=rng.randint(0, 9999), wobble=0.05)
            P.append(limb(hang, h * 0.02, h * 0.008, m=_leaf("willow", rng, season), sides=4, flat=0.35))
    return P


def acacia(rng, h, season):
    """Flat umbrella crown on a forked trunk (savanna)."""
    bm = _bark("acacia")
    P, tips = [], []
    r0 = h * 0.025
    for k in range(2):
        a = rng.uniform(0, 2 * math.pi)
        stem = curve([0, 0, 0], [math.cos(a) * 0.5, math.sin(a) * 0.5, 1], h * 0.7, segments=4, wobble=0.06, seed=rng.randint(0, 9999))
        P.append(limb(stem, r0, r0 * 0.4, m=bm))
        tips.append(stem[-1])
    for t in tips:
        w = h * rng.uniform(0.7, 0.9)
        P.append(blob([w, w * 0.9, h * 0.16], [t[0], t[1], t[2] + h * 0.05], m=_leaf("acacia", rng, season), seed=rng.randint(0, 9999),
                      bumpiness=0.25, detail=2, flat_bottom=0.6))
    return P


def shrub(rng, size):
    return [blob([size * rng.uniform(0.6, 1.0), size * rng.uniform(0.6, 1.0), size * rng.uniform(0.5, 0.8)],
                 [rng.uniform(-size * 0.3, size * 0.3), rng.uniform(-size * 0.3, size * 0.3), size * 0.3], m=_leaf("shrub", rng, "summer"),
                 seed=rng.randint(0, 99999), bumpiness=0.35, detail=1, flat_bottom=0.5) for _ in range(rng.randint(2, 4))]


def tree(species="oak", seed=0, height=None, season="summer"):
    """One tree (parts, base at 0,0,0). Real heights: oak 15-20 m, maple 12-18, birch 12-16, pine 18-25, spruce 15-25,
    cypress 10-15, palm 8-15, willow 10-14, acacia 6-9."""
    rng = random.Random(f"{species}{seed}")
    ranges = {"oak": (1400, 2000), "maple": (1100, 1700), "birch": (1100, 1500), "pine": (1600, 2400), "spruce": (1400, 2200),
              "cypress": (900, 1400), "palm": (800, 1400), "willow": (900, 1300), "acacia": (600, 900), "dead": (800, 1400)}
    h = height or rng.uniform(*ranges.get(species, (900, 1400)))
    if species in ("pine", "spruce"):
        return conifer(rng, h, species)
    if species == "palm":
        return palm(rng, h)
    if species == "willow":
        return willow(rng, h, season)
    if species == "acacia":
        return acacia(rng, h, season)
    if species == "cypress":
        return [limb(curve([0, 0, 0], [0, 0, 1], h * 0.2, seed=seed), h * 0.02, h * 0.015, m=_bark("cypress")),
                blob([h * 0.22, h * 0.22, h * 0.9], [0, 0, h * 0.52], m=_leaf("cypress", rng, season), seed=rng.randint(0, 9999), bumpiness=0.18)]
    if species == "dead":
        return broadleaf(rng, h, "oak", "winter", depth=3, spread=0.8)
    if species == "shrub":
        return shrub(rng, height or rng.uniform(80, 180))
    if species == "hedge":
        return [blob([height or 300, 90, 120], [0, 0, 60], m=_leaf("shrub", rng, season), seed=rng.randint(0, 999), bumpiness=0.15, flat_bottom=0.7)]
    if species == "birch":
        return broadleaf(rng, h, "birch", season, crown_w=0.45, depth=3, spread=0.55, clump=0.28)
    if species == "maple":
        return broadleaf(rng, h, "maple", season, crown_w=0.75, depth=3, spread=0.75, clump=0.4)
    return broadleaf(rng, h, "oak", season, crown_w=0.8, depth=3, spread=1.0, clump=0.42)


def grove(width, depth, count=30, species=("oak", "maple", "birch", "pine"), seed=0, season="summer", shrubs=0.5, at=(0, 0, 0)):
    """Jobs for uk.place_many: a natural mix of trees (one object each) and shrubs, spaced so crowns do not collide."""
    rng = random.Random(seed)
    jobs, placed = [], []
    for i in range(count * 4):
        if len(placed) >= count:
            break
        x, y = rng.uniform(-width / 2, width / 2), rng.uniform(-depth / 2, depth / 2)
        if any((x - px) ** 2 + (y - py) ** 2 < 500 ** 2 for px, py in placed):
            continue
        placed.append((x, y))
        sp = rng.choice(species)
        jobs.append((f"{sp}_{i}", tree(sp, seed=rng.randint(0, 10 ** 6), season=season), (at[0] + x, at[1] + y, at[2])))
        if rng.random() < shrubs:
            jobs.append((f"shrub_{i}", tree("shrub", seed=rng.randint(0, 10 ** 6)), (at[0] + x + rng.uniform(-300, 300), at[1] + y + rng.uniform(-300, 300), at[2])))
    return jobs
