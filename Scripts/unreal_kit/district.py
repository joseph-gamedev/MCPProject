"""A whole city district in one call: street grid, sidewalks, crossings, lamps, styled buildings on lots, street trees.

    import unreal_kit as uk
    uk.district("manhattan")                          # 3 x 2 blocks, a new random district every run (seed printed)
    uk.district("paris", blocks=(2, 2), seed=42)      # the same district again with the same seed
    uk.district(styles={"nordic": 3, "contemporary": 1}, block=(4000, 3000))   # your own mix

Presets: see PRESETS (manhattan, paris, london, downtown, mediterranean, scandinavian, singapore, soviet, industrial).
Everything is overridable: styles {style: weight}, block (w, d) cm, street / avenue widths, lots (min, max) cm,
attached (share of buildings built wall to wall), trees {species: weight}, detail (high | medium | low | auto).
Fast: buildings go as one object each, several requests in flight; detail "auto" keeps big districts light.
"""

import random
import time

from . import buildings as B
from .parts import M, box, cyl, transform

PRESETS = {
    "manhattan": {"styles": {"art_deco": 3, "glass": 3, "warehouse": 3, "victorian": 2, "brutalist": 1, "modernist": 1, "contemporary": 1},
                  "block": (7000, 2400), "street": 1100, "avenue": 1800, "attached": 0.85, "trees": {"maple": 1}, "tree_gap": 1500, "mood": "Morning"},
    "paris": {"styles": {"haussmann": 1}, "block": (5000, 3600), "street": 1200, "avenue": 2400, "attached": 1.0,
              "trees": {"maple": 1}, "tree_gap": 1000, "mood": "Morning"},
    "london": {"styles": {"victorian": 6, "contemporary": 2, "warehouse": 1, "glass": 1, "art_deco": 1}, "block": (5000, 3000), "street": 1000,
               "avenue": 1400, "attached": 0.9, "trees": {"maple": 1, "birch": 1}, "tree_gap": 1300, "mood": "Overcast"},
    "downtown": {"styles": {"glass": 4, "art_deco": 2, "brutalist": 2, "modernist": 1, "contemporary": 2}, "block": (6000, 4000), "street": 1200,
                 "avenue": 1800, "attached": 0.3, "trees": {"birch": 1}, "tree_gap": 2000, "mood": "Morning"},
    "mediterranean": {"styles": {"mediterranean": 7, "modernist": 1, "contemporary": 1}, "block": (4000, 3000), "street": 800, "avenue": 1200,
                      "attached": 0.9, "trees": {"palm_date": 1}, "tree_gap": 1400, "mood": "Noon"},
    "scandinavian": {"styles": {"nordic": 5, "contemporary": 3, "modernist": 1}, "block": (4500, 3500), "street": 1000, "avenue": 1400,
                     "attached": 0.4, "trees": {"birch": 2, "pine": 1}, "tree_gap": 1300, "mood": "Morning"},
    "singapore": {"styles": {"straits": 5, "glass": 3, "modernist": 1, "brutalist": 1}, "block": (5000, 3000), "street": 1000, "avenue": 1600,
                  "attached": 0.8, "trees": {"palm_coconut": 1}, "tree_gap": 1300, "mood": "Noon"},
    "soviet": {"styles": {"brutalist": 6, "modernist": 2}, "block": (7000, 5000), "street": 1400, "avenue": 2000, "attached": 0.1,
               "trees": {"birch": 2}, "tree_gap": 1300, "mood": "Overcast"},
    "industrial": {"styles": {"warehouse": 6, "brutalist": 1, "contemporary": 1}, "block": (6000, 3600), "street": 1200, "avenue": 1600,
                   "attached": 0.7, "trees": {"birch": 1}, "tree_gap": 2600, "mood": "Overcast"},
}
# which building types each style builds, and how wide their lots are (cm)
STYLE_KINDS = {s: [k for k, v in B.KIND_STYLES.items() if s in v] for s in B.STYLES}
LOT = {"townhouse": (560, 800), "shophouse": (500, 650), "villa": (1300, 1800), "apartment": (1300, 2400), "office": (1800, 3000), "loft": (1500, 2600)}
KIND_WEIGHT = {"apartment": 4, "office": 3, "townhouse": 3, "shophouse": 3, "loft": 2, "villa": 1}

ASPHALT, CURB, WALK = M("asphalt", (52, 54, 56)), M("concrete", (150, 148, 142)), M("paving", (170, 164, 152))
MARK, POLE, LAMP = M("plaster", (226, 222, 210), roughness=0.6), M("painted_iron", (36, 38, 40), roughness=0.5), M("light", (255, 236, 200))


def _pick(rng, weights):
    total = sum(weights.values())
    r = rng.uniform(0, total)
    for k, w in weights.items():
        r -= w
        if r <= 0:
            return k
    return k


def _street(x0, y0, x1, y1, walk, vertical):
    """A road rectangle with curbs and sidewalks on its two long sides and a dashed centre line."""
    P = []
    cx, cy, L, W = (x0 + x1) / 2, (y0 + y1) / 2, (x1 - x0) if not vertical else (y1 - y0), (y1 - y0) if not vertical else (x1 - x0)

    def bx(along, across, size_a, size_c, z, h, m):
        if vertical:
            return box([size_c, size_a, h], [cx + across, cy + along, z], m=m)
        return box([size_a, size_c, h], [cx + along, cy + across, z], m=m)
    P.append(bx(0, 0, L, W, -4, 4, ASPHALT))
    for k in range(int(L / 900)):
        P.append(bx(-L / 2 + 450 + k * 900, 0, 300, 14, 0, 1, MARK))
    for s in (-1, 1):
        P.append(bx(0, s * (W / 2 + 8), L, 16, -4, 15, CURB))
        P.append(bx(0, s * (W / 2 + 16 + walk / 2), L, walk, -3, 14, WALK))
    return P


def _lamp(x, y, yaw):
    arm = transform([box([120, 8, 8], [60, 0, 600], m=POLE), box([46, 24, 12], [118, 0, 588], m=LAMP)], (x, y, 0), yaw)
    return [cyl(14, 610, [x, y, 10], m=POLE), cyl(30, 30, [x, y, 10], m=POLE)] + arm


def district(preset="manhattan", blocks=(3, 2), seed=None, at=(0, 0), styles=None, block=None, street=None, avenue=None, lots=None,
             attached=None, trees=None, detail="auto", clear=True, mood=True, camera=True, name="District"):
    """Builds a district and returns {seed, buildings, parts, styles, seconds, center, size}. seed None = new every run."""
    import unreal_kit as uk
    t0 = time.time()
    cfg = dict(PRESETS.get(preset, PRESETS["downtown"]) if isinstance(preset, str) else preset or {})
    for k, v in (("styles", styles), ("block", block), ("street", street), ("avenue", avenue), ("attached", attached), ("trees", trees)):
        if v is not None:
            cfg[k] = v
    seed = random.randrange(1 << 30) if seed is None else seed
    rng = random.Random(seed)
    bw, bd = cfg["block"]
    sw, aw, walk = cfg["street"], cfg["avenue"], 380
    nx, ny = blocks
    pitch_x, pitch_y = bw + aw + 2 * walk + 32, bd + sw + 2 * walk + 32
    W, D = nx * pitch_x + aw + 2 * walk, ny * pitch_y + sw + 2 * walk
    ox, oy = at[0] - (nx * pitch_x) / 2, at[1] - (ny * pitch_y) / 2
    n_lots_est = nx * ny * 2 * bw / 1600
    lod = detail if detail != "auto" else ("medium" if n_lots_est <= 24 else "low")
    if clear:
        uk.call("scene.clear_all", confirm=True)
    # streets (x direction) between block rows, avenues (y direction) between block columns, intersections
    P = [box([W + 2000, D + 2000, 10], [at[0], at[1], -24], m=M("concrete", (120, 118, 112)))]
    for j in range(ny + 1):
        y = oy + j * pitch_y
        P += _street(ox - aw / 2, y - sw / 2, ox + nx * pitch_x + aw / 2, y + sw / 2, walk, False)
    for i in range(nx + 1):
        x = ox + i * pitch_x
        for j in range(ny):
            y0, y1 = oy + j * pitch_y + sw / 2 + walk + 16, oy + (j + 1) * pitch_y - sw / 2 - walk - 16
            P += _street(x - aw / 2, y0, x + aw / 2, y1, walk, True)
        for j in range(ny + 1):                              # crossing stripes at each intersection
            y = oy + j * pitch_y
            for k in range(6):
                P.append(box([50, 300, 1], [x - aw / 2 - 160, y - sw / 2 + 60 + k * (sw - 120) / 5, 0], m=MARK))
    lamps = []
    for j in range(ny + 1):
        y = oy + j * pitch_y
        for s in (-1, 1):
            for k in range(int(nx * pitch_x / 2500)):
                lamps += _lamp(ox + 600 + k * 2500, y + s * (sw / 2 + 60), 90 if s < 0 else -90)
    uk.place(f"{name}_Streets", P + lamps)
    # lots along the two street sides of every block; corner lots get facades on the side street too
    jobs, used = [], {}
    for i in range(nx):
        for j in range(ny):
            bx0 = ox + i * pitch_x + aw / 2 + walk + 16
            bx1 = bx0 + bw
            by0 = oy + j * pitch_y + sw / 2 + walk + 16
            by1 = by0 + bd
            for side in ("south", "north"):
                x = bx0
                lots_here = []
                while x < bx1 - 400:
                    style = _pick(rng, cfg["styles"])
                    kinds = STYLE_KINDS.get(style) or ["apartment"]
                    kind = _pick(rng, {k: KIND_WEIGHT.get(k, 1) for k in kinds})
                    lo, hi = lots or LOT.get(kind, (1200, 2200))
                    w = rng.uniform(lo, hi)
                    if bx1 - (x + w) < lo * 0.6:                    # the last lot takes what is left
                        w = bx1 - x
                    joined = rng.random() < cfg.get("attached", 0.8)
                    gap = 0 if joined else rng.uniform(250, 700)
                    lots_here.append([x, w, style, kind, joined])
                    x += w + gap
                for k, (x, w, style, kind, joined) in enumerate(lots_here):
                    first, last = k == 0, k == len(lots_here) - 1
                    nxt_joined = not last and lots_here[k + 1][4]
                    party = set()
                    if side == "south":
                        if not first and joined:
                            party.add("-x")
                        if nxt_joined:
                            party.add("+x")
                        street_dirs = ("-y",) + (("-x",) if first else ()) + (("+x",) if last else ())
                    else:                                            # turned round: local +x points west
                        if not first and joined:
                            party.add("+x")
                        if nxt_joined:
                            party.add("-x")
                        street_dirs = ("-y",) + (("+x",) if first else ()) + (("-x",) if last else ())
                    try:
                        parts = B.building(kind, seed=rng.randrange(1 << 30), style=style, width=w, depth=bd / 2 - 40, party=tuple(party),
                                           street=street_dirs, detail=lod)
                    except Exception:                                # an odd lot for this massing: plain block instead
                        parts = B.building(kind, seed=rng.randrange(1 << 30), style=style, width=w, depth=bd / 2 - 40, massing="block",
                                           party=tuple(party), street=street_dirs, detail=lod)
                    cx = x + w / 2
                    if side == "south":
                        jobs.append((f"{name}_{i}{j}s{k}", parts, (cx, by0, 0)))
                    else:
                        jobs.append((f"{name}_{i}{j}n{k}", transform(parts, (0, 0, 0), 180), (cx, by1, 0)))
                    used[style] = used.get(style, 0) + 1
    t_gen = time.time() - t0
    uk.place_many(jobs, group=name)
    # street trees: GPU instances in the sidewalks, clear of the corners
    try:
        ins = uk.assets.Instancer(f"{name}_Trees", collision=False)
        for j in range(ny + 1):
            y = oy + j * pitch_y
            for s in (-1, 1):
                for i in range(nx):
                    xa = ox + i * pitch_x + aw / 2 + walk + 300
                    for k in range(int((bw - 600) / cfg.get("tree_gap", 1400)) + 1):
                        species = rng.choice(uk.assets.variants(_pick(rng, cfg["trees"])))
                        ins.add(species, (xa + k * cfg.get("tree_gap", 1400), y + s * (sw / 2 + 150), 0), yaw=rng.uniform(0, 360),
                                scale=rng.uniform(0.75, 1.0))
        for i in range(nx + 1):                                   # avenues are tree-lined too (boulevards)
            x = ox + i * pitch_x
            for s in (-1, 1):
                for j in range(ny):
                    ya = oy + j * pitch_y + sw / 2 + walk + 300
                    for k in range(int((bd - 600) / cfg.get("tree_gap", 1400)) + 1):
                        species = rng.choice(uk.assets.variants(_pick(rng, cfg["trees"])))
                        ins.add(species, (x + s * (aw / 2 + 150), ya + k * cfg.get("tree_gap", 1400), 0), yaw=rng.uniform(0, 360),
                                scale=rng.uniform(0.75, 1.0))
        ins.flush()
    except Exception as e:                                       # trees are optional (older builds without the foliage)
        print("district: trees skipped:", e)
    if mood and cfg.get("mood"):
        try:
            uk.look.mood(cfg["mood"])
        except Exception:
            pass
    if camera:
        try:
            uk.call("camera.set_camera", location=[ox - aw, oy - 300, 900], look_at=[ox + pitch_x, oy + pitch_y * 0.5, 600])
        except Exception:
            pass
    out = {"seed": seed, "preset": preset if isinstance(preset, str) else "custom", "buildings": len(jobs), "parts": sum(len(j[1]) for j in jobs),
           "styles": used, "detail": lod, "seconds": round(time.time() - t0, 1), "generate_seconds": round(t_gen, 1),
           "center": list(at), "size": [round(W), round(D)]}
    print("district:", out)
    return out
