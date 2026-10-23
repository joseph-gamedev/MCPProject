"""Contemporary buildings made of a few big primitives: each volume is one extruded outline (any shape: rectangle,
rounded, chamfered, ellipse, curved bar, a GIS polygon) wrapped in a facade system built from whole-height or
whole-perimeter pieces (glass curtain wall with mullions, ribbon windows, fins, louvres, balconies, exoskeleton grid,
diagrid, timber screen, punched windows, ribbed cladding, open car-park decks). No per-window modules.

    from unreal_kit import modern
    P = modern.building("office_tower", seed=4)             # any type in KINDS; floors=, width=, depth=, detail=
    P = modern.building("apartment", polygon=[[x, y], ...])  # on any footprint (world cm, parts come back in world)

Every choice comes from the seed: composition of volumes, outline shapes, facade system, palette (glass tint, cladding,
accent), proportions, ground floor, roof and crown, and the type's own elements (helipad, sports field, canopies,
domes, pumps, parking), so a street of many never repeats. Storey heights and proportions follow real references
(classrooms on 10-12 m wings and 3.8 m storeys; hospital podium + ward towers with a helipad; dock doors at trailer
height; fire-engine bays; car-park decks at about 3 m).
"""

import math
import random

from .parts import M, beam, blob, box, cone, cyl, extrude, sphere, transform

LAST = {}  # the last building: kind, form, skin, storeys, volumes, props (for checks)
PROPS = []  # real meshes of the building being made: (catalog key, x, y, z, yaw, scale); planted by plant() / place()
TREES = ["maple", "oak", "birch", "japanese_maple", "aspen", "jacaranda_tree"]


def prop(key, x, y, z=0.0, yaw=0.0, scale=1.0):
    PROPS.append((key, round(x, 1), round(y, 1), round(z, 1), round(yaw, 1), round(scale, 3)))
    return []

# ------------------------------------------------------------------ materials


def _glass(rgb, rough=0.05, metal=0.3):
    return M("steel", rgb, roughness=rough, metallic=metal)


GLASS = {"blue": _glass((48, 70, 90)), "teal": _glass((62, 96, 98)), "silver": _glass((128, 142, 150), metal=0.55),
         "bronze": _glass((98, 84, 66), metal=0.45), "dark": _glass((30, 34, 40), metal=0.15), "sky": _glass((96, 128, 150), metal=0.4),
         "green": _glass((66, 94, 80), metal=0.3), "grey": _glass((76, 80, 84), metal=0.35)}
SOLID = {"white": M("plaster", (236, 236, 232), roughness=0.45), "light_grey": M("concrete", (198, 200, 200), roughness=0.6),
         "warm_grey": M("concrete", (172, 166, 158), roughness=0.7), "charcoal": M("painted_iron", (56, 58, 62), roughness=0.45),
         "black": M("painted_iron", (30, 31, 33), roughness=0.4), "sand": M("limestone", (216, 202, 176)), "stone": M("limestone", (196, 188, 172)),
         "terracotta": M("terracotta", (186, 102, 74)), "corten": M("painted_iron", (134, 68, 40), roughness=0.85),
         "timber": M("wood", (170, 122, 80)), "dark_timber": M("wood", (96, 66, 46)), "concrete": M("concrete", (186, 184, 178)),
         "brick": M("brick", (156, 88, 64)), "dark_brick": M("brick", (82, 60, 52)), "copper": M("copper", (156, 108, 72)),
         "aluminium": M("steel", (216, 218, 220), roughness=0.28, metallic=0.85), "bronze": M("bronze", (122, 94, 62), roughness=0.4),
         "sage": M("plaster", (160, 176, 150), roughness=0.6), "navy": M("painted_iron", (40, 54, 78), roughness=0.45)}
ACCENTS = [(190, 70, 50), (220, 170, 60), (50, 110, 170), (70, 140, 90), (210, 110, 50), (40, 130, 140)]
RAIL = M("tinted_glass")
CLEAR = M("glass", (150, 170, 180), opacity=0.28, roughness=0.04)
DARK = M("plaster", (40, 38, 36), roughness=0.95)


def _accent(rng):
    return M("painted_iron", rng.choice(ACCENTS), roughness=0.4)


def _solid(*names):
    return dict(SOLID[names[0]]) if len(names) == 1 else dict(SOLID[random.choice(names)])


# ------------------------------------------------------------------ outlines (counter-clockwise [(x, y)], cm)


def _area(p):
    return 0.5 * sum(p[i][0] * p[(i + 1) % len(p)][1] - p[(i + 1) % len(p)][0] * p[i][1] for i in range(len(p)))


def ccw(p):
    p = [(float(q[0]), float(q[1])) for q in p]
    if len(p) > 3 and math.hypot(p[0][0] - p[-1][0], p[0][1] - p[-1][1]) < 1:
        p.pop()
    return p if _area(p) > 0 else p[::-1]


def rect(x0, y0, x1, y1, r=0.0, cham=0.0, seg=5):
    """Rectangle, optionally with rounded (r) or chamfered (cham) corners."""
    x0, x1, y0, y1 = min(x0, x1), max(x0, x1), min(y0, y1), max(y0, y1)
    r = min(r, (x1 - x0) / 2 - 1, (y1 - y0) / 2 - 1)
    cham = min(cham, (x1 - x0) / 3, (y1 - y0) / 3)
    if r > 20:
        out = []
        for cx, cy, a0 in ((x1 - r, y0 + r, -90), (x1 - r, y1 - r, 0), (x0 + r, y1 - r, 90), (x0 + r, y0 + r, 180)):
            for k in range(seg + 1):
                a = math.radians(a0 + 90 * k / seg)
                out.append((cx + r * math.cos(a), cy + r * math.sin(a)))
        return out
    if cham > 20:
        c = cham
        return [(x0 + c, y0), (x1 - c, y0), (x1, y0 + c), (x1, y1 - c), (x1 - c, y1), (x0 + c, y1), (x0, y1 - c), (x0, y0 + c)]
    return [(x0, y0), (x1, y0), (x1, y1), (x0, y1)]


def ellipse(cx, cy, a, b, n=32):
    return [(cx + a * math.cos(2 * math.pi * k / n), cy + b * math.sin(2 * math.pi * k / n)) for k in range(n)]


def arc_bar(cx, cy, radius, depth, a0, a1, n=16):
    """A bar bent along a circle (centre cx, cy), from angle a0 to a1 (degrees)."""
    outer = [(cx + (radius + depth / 2) * math.cos(math.radians(a0 + (a1 - a0) * k / n)),
              cy + (radius + depth / 2) * math.sin(math.radians(a0 + (a1 - a0) * k / n))) for k in range(n + 1)]
    inner = [(cx + (radius - depth / 2) * math.cos(math.radians(a0 + (a1 - a0) * k / n)),
              cy + (radius - depth / 2) * math.sin(math.radians(a0 + (a1 - a0) * k / n))) for k in range(n, -1, -1)]
    return ccw(outer + inner)


def rotate(p, deg, cx=0.0, cy=0.0):
    a = math.radians(deg)
    c, s = math.cos(a), math.sin(a)
    return [(cx + (x - cx) * c - (y - cy) * s, cy + (x - cx) * s + (y - cy) * c) for x, y in p]


def scale(p, f, cx=None, cy=None):
    if cx is None:
        cx, cy = sum(q[0] for q in p) / len(p), sum(q[1] for q in p) / len(p)
    return [(cx + (x - cx) * f, cy + (y - cy) * f) for x, y in p]


def offset(p, d):
    """Outline moved outward by d cm (inward when negative), mitred corners (limited on sharp ones)."""
    if not d:
        return list(p)
    n = len(p)
    out = []
    for i in range(n):
        a, b, c = p[i - 1], p[i], p[(i + 1) % n]
        l1 = math.hypot(b[0] - a[0], b[1] - a[1]) or 1
        l2 = math.hypot(c[0] - b[0], c[1] - b[1]) or 1
        n1 = ((b[1] - a[1]) / l1, -(b[0] - a[0]) / l1)
        n2 = ((c[1] - b[1]) / l2, -(c[0] - b[0]) / l2)
        bx, by = n1[0] + n2[0], n1[1] + n2[1]
        bl = math.hypot(bx, by)
        if bl < 1e-6:
            out.append((b[0] + n2[0] * d, b[1] + n2[1] * d))
            continue
        bx, by = bx / bl, by / bl
        k = d / max(bx * n1[0] + by * n1[1], 0.4)
        out.append((b[0] + bx * k, b[1] + by * k))
    return out


def bbox(p):
    xs, ys = [q[0] for q in p], [q[1] for q in p]
    return min(xs), min(ys), max(xs), max(ys)


def _corners(p, turn=28):
    """Arclength positions of the outline's real corners (turns sharper than `turn` degrees), and the perimeter."""
    n = len(p)
    L = [math.hypot(p[(i + 1) % n][0] - p[i][0], p[(i + 1) % n][1] - p[i][1]) for i in range(n)]
    pos, acc = [], 0.0
    for i in range(n):
        a, b, c = p[i - 1], p[i], p[(i + 1) % n]
        h1 = math.atan2(b[1] - a[1], b[0] - a[0])
        h2 = math.atan2(c[1] - b[1], c[0] - b[0])
        t = abs((math.degrees(h2 - h1) + 180) % 360 - 180)
        if t > turn:
            pos.append(acc)
        acc += L[i]
    return pos, L, acc


def walk(p, step, phase=0.5, margin=0.0, normals=None):
    """Points every ~step cm along a closed outline: (x, y, yaw, nx, ny) with the outward normal; points closer than
    margin to a real corner are dropped; normals: keep only points whose normal is within 50 degrees of one of these."""
    corners, L, P = _corners(p)
    n = len(p)
    if P < 1:
        return []
    k = max(1, int(round(P / step)))
    out, i, acc = [], 0, 0.0
    for j in range(k):
        s = (j + phase) * P / k
        while i < n - 1 and s > acc + L[i]:
            acc += L[i]
            i += 1
        if margin and corners:
            dmin = min(min(abs(s - c), P - abs(s - c)) for c in corners)
            if dmin < margin:
                continue
        a, b = p[i], p[(i + 1) % n]
        li = L[i] or 1
        ux, uy = (b[0] - a[0]) / li, (b[1] - a[1]) / li
        nx, ny = uy, -ux
        if normals and not any(nx * q[0] + ny * q[1] > 0.64 for q in normals):
            continue
        t = s - acc
        out.append((a[0] + ux * t, a[1] + uy * t, math.degrees(math.atan2(uy, ux)), nx, ny))
    return out


def edges(p, min_len=0.0):
    """Straight edges: (ax, ay, bx, by, length, ux, uy, nx, ny)."""
    out = []
    for i in range(len(p)):
        a, b = p[i], p[(i + 1) % len(p)]
        L = math.hypot(b[0] - a[0], b[1] - a[1])
        if L >= min_len and L > 1:
            ux, uy = (b[0] - a[0]) / L, (b[1] - a[1]) / L
            out.append((a[0], a[1], b[0], b[1], L, ux, uy, uy, -ux))
    return out


# ------------------------------------------------------------------ primitive pieces


def slab(p, z, h, m, d=0.0):
    """The outline (moved out by d) pushed up h from z: a whole storey, a floor edge, a balcony plate, a roof."""
    q = offset(p, d) if d else p
    return extrude([[round(x, 1), round(y, 1)] for x, y in q], max(1.0, h), [0, 0, round(z, 1)], m=m, bevel=0)


def member(pt, along, out, h, z, m, off=0.0, yaw_add=0.0):
    """A piece standing on the outline at a walk() point: `along` wide (tangent), `out` deep (normal), h tall."""
    x, y, yaw, nx, ny = pt
    c = off + out / 2
    return box([along, out, h], [x + nx * c, y + ny * c, z], rot=[0, yaw + yaw_add, 0], m=m, bevel=0)


def tree(x, y, rng, h=None, z=0.0, species=None):
    """A real tree mesh (maple, oak, birch, Japanese maple, aspen, jacaranda), planted as a GPU instance."""
    return prop(species or rng.choice(TREES), x, y, z, rng.uniform(0, 360), (h / 700.0 if h else rng.uniform(0.75, 1.1)))


TILES = [M("concrete", (196, 192, 184), style="blocks", tile=60), M("granite", (150, 146, 140), style="blocks", tile=80),
         M("limestone", (214, 204, 184), style="blocks", tile=90), M("concrete", (120, 120, 118), style="blocks", tile=50),
         M("marble", (226, 222, 214), style="blocks", tile=100), M("terracotta", (176, 110, 80), style="blocks", tile=30)]
FLOWERS = [(200, 50, 60), (240, 190, 50), (150, 80, 170), (240, 240, 236), (230, 120, 40), (210, 90, 140)]


def hedge(x0, y0, x1, y1, h):
    return box([abs(x1 - x0) or 60, abs(y1 - y0) or 60, h], [(x0 + x1) / 2, (y0 + y1) / 2, 0], m=M("grass", (52, 84, 40), roughness=0.95), bevel=12)


def bench(x, y, yaw, z=0.0):
    return prop("painted_wooden_bench", x, y, z, yaw)


def flower_bed(cx, cy, w, d, rng):
    P = [box([w, d, 25], [cx, cy, 0], m=SOLID["concrete"], bevel=0), box([w - 16, d - 16, 6], [cx, cy, 25], m=M("sand", (80, 60, 44), roughness=1.0), bevel=0)]
    for _ in range(int(w * d / 12000) + 2):
        prop(rng.choice(("flower_bush", "wildflowers", "periwinkle_plant", "flower_bush")), cx + rng.uniform(-w / 2 + 30, w / 2 - 30),
             cy + rng.uniform(-d / 2 + 30, d / 2 - 30), 31, rng.uniform(0, 360), rng.uniform(0.7, 1.0))
    return P


def landscape(c):
    """The lot round the building: a tiled forecourt with a banded pattern and planted trees, and a lawn garden with
    hedges, flower beds, a stepping-stone path, benches, shrubs and trees."""
    rng, V, w, d = c["rng"], c["V"], c["w"], c["d"]
    bx0, by0 = min(v["x0"] for v in V), min(v["y0"] for v in V)
    bx1, by1 = max(v["x1"] for v in V), max(v["y1"] for v in V)
    tile = rng.choice(TILES)
    band = rng.choice([t for t in TILES if t is not tile])
    P = []
    fy0 = min(-600, by0 - 1200)
    fy1 = by0 + 20
    P.append(box([w, fy1 - fy0, 3], [0, (fy0 + fy1) / 2, 0], m=tile, bevel=0))
    k = rng.choice((300, 450, 600))
    P += [box([30, fy1 - fy0, 1], [-w / 2 + i * k, (fy0 + fy1) / 2, 3], m=band, bevel=0) for i in range(int(w / k) + 1)]
    P.append(box([w, 30, 1], [0, fy0 + 15, 3], m=band, bevel=0))
    for i in range(int(w / 1500) + 1):
        prop("street_lamp", -w / 2 + 300 + i * 1500, fy0 + 80, 3, 90)
    y = (fy0 + by0) / 2
    for i in range(4):
        x = -w / 2 + (i + 0.5) * w / 4 + rng.uniform(-150, 150)
        if abs(x - (bx0 + bx1) / 2) < 700:
            continue
        P += [box([260, 260, 50], [x, y, 0], m=SOLID["concrete"], bevel=4), box([230, 230, 6], [x, y, 50], m=M("sand", (80, 60, 44)), bevel=0)]
        tree(x, y, rng, z=50)
        P += bench(x + 230, y, 90)
    lawn = M("grass", rng.choice([(78, 112, 52), (86, 120, 58), (70, 104, 48)]), roughness=0.95)
    gy1 = max(by1 + 1500, d)
    for x0, x1, y0, y1 in ((-w / 2, bx0 - 150, by0, gy1), (bx1 + 150, w / 2, by0, gy1), (bx0 - 150, bx1 + 150, by1 + 150, gy1)):
        if x1 - x0 < 300 or y1 - y0 < 300:
            continue
        P.append(box([x1 - x0, y1 - y0, 4], [(x0 + x1) / 2, (y0 + y1) / 2, 0], m=lawn, bevel=0))
        P.append(hedge(x0 + 40, y1 - 40, x1 - 40, y1 - 40, rng.choice((90, 120))))
        cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
        if x1 - x0 > 800 and y1 - y0 > 800:
            P += flower_bed(cx, cy, min(400, (x1 - x0) * 0.4), min(250, (y1 - y0) * 0.3), rng)
        px = x0 + (x1 - x0) * rng.uniform(0.25, 0.75)
        P += [box([90, 60, 3], [px + math.sin(j * 0.35) * 60, y0 + 60 + j * 120, 4], m=tile, bevel=2) for j in range(int((y1 - y0) / 120))]
        for _ in range(max(1, int((x1 - x0) * (y1 - y0) / 2.5e6))):
            tx, ty = rng.uniform(x0 + 200, x1 - 200), rng.uniform(y0 + 200, y1 - 200)
            if abs(tx - px) > 200:
                P += tree(tx, ty, rng)
        P += bench(px + 150, cy, 90)
        for _ in range(rng.randint(3, 7)):                      # shrubs along the building
            prop(rng.choice(("boxwood", "bush", "flower_bush")), rng.uniform(x0 + 80, x1 - 80), y0 + 90, 4, rng.uniform(0, 360), rng.uniform(0.7, 1.1))
    return P


# ------------------------------------------------------------------ facade systems (skins)
# skin(p, zs, top, pal, rng, lod, party) -> parts: p the outline, zs the floor levels of this volume (zs[0] its
# bottom), top its roof level. Every system is the volume's core (glass or solid) plus whole-height or whole-perimeter
# pieces, so a 30-storey tower is a few hundred parts.


def _tone(m, f):
    q = dict(m)
    if q.get("color"):
        q["color"] = [round(min(1.0, c * f), 4) for c in q["color"]]
    return q


def glazing(p, zs, top, pal, spandrel=None, inset=0.0):
    """Glass storey by storey: each floor a pane band in a slightly different tone, with an opaque spandrel band at
    floor level (the slab edge and the sill), so the storeys read from far away."""
    r2 = random.Random(pal.get("seed", 0) + len(zs))
    q = offset(p, -inset) if inset else p
    sp = spandrel if spandrel is not None else pal.get("spandrel_h", 90)
    P = []
    for i, z in enumerate(zs):
        fh = (zs[i + 1] if i + 1 < len(zs) else top) - z
        P.append(slab(q, z + sp, fh - sp, _tone(pal["glass"], r2.uniform(0.85, 1.15))))
        if sp:
            P.append(slab(q, z, sp, pal["spandrel"]))
    return P


def _lines(p, zs, m, d=3.0, h=22.0):
    return [slab(p, z - h / 2, h, m, d) for z in zs[1:]]


def sk_curtain(p, zs, top, pal, rng, lod, party):
    P = glazing(p, zs, top, pal)
    P += _lines(p, zs, pal["frame"], 3, pal.get("line_h", 16))
    step = pal.get("mullion", 150) * (1, 1.6, 3)[2 - lod]
    for pt in walk(p, step):
        P.append(member(pt, 8, 22, top - zs[0], zs[0], pal["frame"]))
    if pal.get("bold_every"):
        P += [slab(p, z - 30, 60, pal["frame"], 6) for z in zs[1::pal["bold_every"]]]
    return P


def sk_ribbon(p, zs, top, pal, rng, lod, party):
    P = [slab(p, zs[0], top - zs[0], pal["main"])]
    sill, head = pal.get("sill", 95), pal.get("head", 45)
    for i, z in enumerate(zs):
        fh = (zs[i + 1] if i + 1 < len(zs) else top) - z
        P.append(slab(p, z + sill, fh - sill - head, pal["glass"], 3))
        if lod == 2:
            P += [member(pt, 4, 8, fh - sill - head, z + sill, pal["frame"], off=2) for pt in walk(p, 160, normals=pal.get("open"))]
    return P


def sk_fins(p, zs, top, pal, rng, lod, party):
    P = glazing(p, zs, top, pal) + _lines(p, zs, pal["frame"], 2.5, 18)
    step, depth, thick = pal.get("fin_step", 140), pal.get("fin_depth", 60), pal.get("fin_t", 14)
    ang = pal.get("fin_angle", 0)
    extra = pal.get("fin_up", 0)
    pts = walk(p, step * (1, 1.3, 2)[2 - lod], normals=pal.get("open"))
    for k, pt in enumerate(pts):
        dk = depth * (0.6 + 0.4 * math.sin(k * 0.5)) if pal.get("fin_wave") else depth
        P.append(member(pt, thick, dk, top - zs[0] + extra, zs[0], pal["fin"], yaw_add=ang))
    return P


def sk_louvres(p, zs, top, pal, rng, lod, party):
    P = glazing(p, zs, top, pal, spandrel=0)
    out, blades = pal.get("louvre_out", 55), pal.get("blades", 2)
    for i, z in enumerate(zs):
        fh = (zs[i + 1] if i + 1 < len(zs) else top) - z
        P.append(slab(p, z - 12, 24, pal["frame"], 4))
        for b in range(blades if lod else 1):
            P.append(slab(p, z + fh * (0.55 + 0.3 * b / max(1, blades)), 7, pal["fin"], out))
    return P


def sk_balconies(p, zs, top, pal, rng, lod, party):
    P = glazing(offset(p, -60), zs, top, pal, spandrel=0) if pal.get("glass_core", True) else [slab(p, zs[0], top - zs[0], pal["main"])]
    bd = pal.get("balcony", 150)
    rail = pal.get("rail", "glass")
    for i, z in enumerate(zs[1:], 1):
        d = bd * (0.45 if pal.get("alternate") and i % 2 else 1.0)
        if pal.get("wave"):
            d = bd * (0.55 + 0.45 * math.sin(i * 0.7 + pal["wave"]))
        P.append(slab(p, z - 24, 24, pal["slab"], d))
        if rail == "glass":
            P.append(slab(p, z, 105, RAIL, d - 3))
        else:
            P.append(slab(p, z, 100, pal["main"] if rail == "solid" else pal["frame"], d - 3))
    if not pal.get("glass_core", True) and lod:
        pass
    return P


def sk_grid(p, zs, top, pal, rng, lod, party):
    deep = pal.get("grid_depth", 45)
    P = glazing(p, zs, top, pal, spandrel=0, inset=deep)
    P += [slab(p, z, 36, pal["frame"]) for z in zs]
    P.append(slab(p, top - 36, 36, pal["frame"]))
    bay = pal.get("bay", 330)
    for pt in walk(p, bay):
        P.append(member(pt, pal.get("pier", 36), deep, top - zs[0], zs[0], pal["frame"], off=-deep))
    for x, y in p:                                            # corner piers
        P.append(box([deep, deep, top - zs[0]], [x, y, zs[0]], m=pal["frame"]))
    return P


def sk_diagrid(p, zs, top, pal, rng, lod, party):
    P = glazing(p, zs, top, pal) + _lines(p, zs, pal["frame"], 2.5, 14)
    per = pal.get("diag_floors", 4)
    t = pal.get("diag_t", 40)
    levels = zs[::per] + [top]
    for ax, ay, bx, by, L, ux, uy, nx, ny in edges(p, 900):
        mod = (levels[1] - levels[0]) * 0.9 if len(levels) > 1 else 900
        k = max(1, int(round(L / mod)))
        for j in range(len(levels) - 1):
            z0, z1 = levels[j], levels[j + 1]
            for i in range(k):
                s0, s1 = L * i / k, L * (i + 1) / k
                if (i + j) % 2:
                    s0, s1 = s1, s0
                a = (ax + ux * s0 + nx * 18, ay + uy * s0 + ny * 18, z0)
                b = (ax + ux * s1 + nx * 18, ay + uy * s1 + ny * 18, z1)
                P.append(beam(a, b, t, m=pal["frame"]))
    if len(edges(p, 900)) < 2:                                # curved outline: vertical frame instead
        P += [member(pt, 30, 30, top - zs[0], zs[0], pal["frame"]) for pt in walk(p, 300)]
    return P


def sk_screen(p, zs, top, pal, rng, lod, party):
    P = glazing(p, zs, top, pal) + _lines(p, zs, pal["slab"], 3, 26)
    step = pal.get("slat_step", 45) * (1, 1.5, 3)[2 - lod]
    gap = pal.get("screen_gaps", 0)
    for k, pt in enumerate(walk(p, step, normals=pal.get("open"))):
        if gap and (k // 6) % gap == 0:
            continue
        P.append(member(pt, 6, 16, top - zs[0], zs[0], pal["fin"], off=30))
    return P


def _shift(pt, ds):
    x, y, yaw, nx, ny = pt
    return (x - ny * ds, y + nx * ds, yaw, nx, ny)


def window(pt, w, h, z, glass, frame, lod, t=7):
    """A window on a wall at a walk() point: glass just proud of the wall, a slim frame round it, a sill."""
    P = [member(pt, w, 5, h, z, glass)]
    if lod:
        P += [member(_shift(pt, -w / 2), t, 12, h, z, frame), member(_shift(pt, w / 2), t, 12, h, z, frame),
              member(pt, w + t, 12, t, z + h - t, frame), member(pt, w + 24, 22, 6, z - 6, frame)]
        if w > 180 and lod == 2:
            P.append(member(pt, 5, 9, h, z, frame))
    return P


def sk_punched(p, zs, top, pal, rng, lod, party):
    P = [slab(p, zs[0], top - zs[0], pal["main"])]
    pitch, ww = pal.get("pitch", 360), pal.get("win_w", 220)
    pattern = pal.get("pattern", "regular")
    frame = pal.get("win_frame") or pal["frame"]
    r2 = random.Random(pal.get("seed", 0))
    for i, z in enumerate(zs):
        fh = (zs[i + 1] if i + 1 < len(zs) else top) - z
        if pattern == "slots":
            w, h, sill = 80, fh - 60, 30
        elif pattern == "wide":
            w, h, sill = ww * 2.2, fh * 0.5, fh * 0.28
        else:
            w, h, sill = ww, fh * pal.get("win_h", 0.55), fh * 0.24
        phase = (0.5 if pattern != "staggered" else (0.25 if i % 2 else 0.75))
        for pt in walk(p, pitch * (2.2 if pattern == "wide" else 1), phase=phase, margin=w / 2 + 60, normals=pal.get("open")):
            ww2 = w
            if pattern == "random":
                ww2 = w * r2.choice((0.6, 1.0, 1.0, 1.5))
            P += window(pt, ww2, h, z + sill, pal["glass"], frame, lod)
    return P


def sk_ribbed(p, zs, top, pal, rng, lod, party):
    P = [slab(p, zs[0], top - zs[0], pal["main"])]
    step = pal.get("rib", 80) * (1, 1.8, 4)[2 - lod]
    for pt in walk(p, step):
        P.append(member(pt, 12, 6, top - zs[0] - 30, zs[0] + 15, pal["frame"]))
    if pal.get("clerestory"):
        P.append(slab(p, top - 230, 110, pal["glass"], 4))
    P.append(slab(p, top - 30, 30, pal["frame"], 8))
    return P


def sk_tiles(p, zs, top, pal, rng, lod, party):
    """Large cladding panels in two or three tones."""
    P = [slab(p, zs[0], top - zs[0], pal["main"])]
    r2 = random.Random(pal.get("seed", 0))
    tones = pal.get("tones") or [pal["main"], pal["second"]]
    step = pal.get("panel_w", 420) * (1, 1.4, 2.5)[2 - lod]
    for pt in walk(p, step):
        h = top - zs[0]
        if r2.random() < 0.35:
            h *= r2.uniform(0.4, 0.9)
        P.append(member(pt, step - 12, 5, h, zs[0], r2.choice(tones)))
    return P


def sk_monolith(p, zs, top, pal, rng, lod, party):
    """A solid volume with a few large cuts: one glazed band, vertical slots, a big window."""
    P = [slab(p, zs[0], top - zs[0], pal["main"])]
    r2 = random.Random(pal.get("seed", 0))
    if len(zs) > 1:
        z = zs[r2.randrange(len(zs))]
        P.append(slab(p, z + 60, (zs[1] - zs[0]) * 0.6, pal["glass"], 3))
    for pt in walk(p, r2.choice((900, 1300, 1800)), margin=300):
        if r2.random() < 0.5:
            P.append(member(pt, r2.choice((80, 120)), 8, (top - zs[0]) * r2.uniform(0.5, 0.9), zs[0] + 100, pal["glass"], off=-2))
    return P


def sk_open_deck(p, zs, top, pal, rng, lod, party):
    """Open car-park decks: dark interior, floor slabs, spandrel upstands, columns, optional coloured fins."""
    P = [slab(offset(p, -120), zs[0], top - zs[0], DARK)]
    up = pal.get("upstand", 105)
    for z in zs:
        P += [slab(p, z, 30, pal["slab"]), slab(p, z + 30, up, pal["main"])]
    P.append(slab(p, top, 30, pal["slab"]))
    P.append(slab(p, top + 30, up, pal["main"]))
    for pt in walk(p, 760):
        P.append(member(pt, 45, 45, top - zs[0], zs[0], pal["slab"], off=-50))
    if pal.get("deck_fins"):
        for k, pt in enumerate(walk(p, 55 * (1, 1.5, 3)[2 - lod])):
            P.append(member(pt, 8, 20, top - zs[0] + up, zs[0], pal["fin"] if k % 7 else _accent(rng), off=6))
    return P


SKINS = {"curtain": sk_curtain, "ribbon": sk_ribbon, "fins": sk_fins, "louvres": sk_louvres, "balconies": sk_balconies,
         "grid": sk_grid, "diagrid": sk_diagrid, "screen": sk_screen, "punched": sk_punched, "ribbed": sk_ribbed,
         "tiles": sk_tiles, "monolith": sk_monolith, "open_deck": sk_open_deck}


def palette(rng, skin, solids=None, glasses=None):
    """Colours and proportions of one building: glass tint, main and second cladding, frame, fins, slabs."""
    solids = solids or list(SOLID)
    g = rng.choice(glasses or list(GLASS))
    main = rng.choice([x for x in solids if not (skin == "punched" and x in ("black", "navy", "corten") and rng.random() < 0.8)] or solids)
    second = rng.choice([s for s in SOLID if s != main])
    frame = rng.choice(["aluminium", "charcoal", "black", "white", "bronze", main])
    fin = rng.choice(["timber", "white", "aluminium", "bronze", "charcoal", "terracotta", "corten", "copper", main])
    pal = {"glass": dict(GLASS[g]), "main": dict(SOLID[main]), "second": dict(SOLID[second]), "frame": dict(SOLID[frame]),
           "fin": dict(SOLID[fin]), "slab": dict(SOLID[rng.choice(["white", "light_grey", "concrete", "charcoal", main])]),
           "names": {"glass": g, "main": main, "second": second, "frame": frame, "fin": fin}, "seed": rng.randrange(1 << 30),
           "mullion": rng.choice((120, 150, 150, 180, 240)), "line_h": rng.choice((10, 16, 30)), "bold_every": rng.choice((0, 0, 0, 2, 3)),
           "sill": rng.choice((80, 95, 110)), "head": rng.choice((30, 45, 70)),
           "fin_step": rng.choice((90, 120, 150, 200)), "fin_depth": rng.choice((40, 60, 90)), "fin_t": rng.choice((8, 14, 24)),
           "fin_angle": rng.choice((0, 0, 0, 20, -25, 35)), "fin_wave": rng.random() < 0.25,
           "louvre_out": rng.choice((40, 60, 90)), "blades": rng.choice((1, 2, 3)),
           "balcony": rng.choice((120, 160, 200, 240)), "rail": rng.choice(("glass", "glass", "solid", "frame")),
           "alternate": rng.random() < 0.3, "wave": rng.uniform(0, 6) if rng.random() < 0.2 else 0, "glass_core": rng.random() < 0.7,
           "grid_depth": rng.choice((35, 50, 70)), "bay": rng.choice((270, 330, 400, 450)), "pier": rng.choice((24, 36, 50)),
           "diag_floors": rng.choice((3, 4, 5)), "diag_t": rng.choice((30, 45)),
           "slat_step": rng.choice((35, 45, 60)), "screen_gaps": rng.choice((0, 0, 2, 3)),
           "pitch": rng.choice((300, 360, 420, 540)), "win_w": rng.choice((150, 200, 260, 320)), "win_h": rng.choice((0.5, 0.6, 0.7)),
           "pattern": rng.choice(("regular", "regular", "staggered", "random", "slots", "wide")),
           "win_frame": rng.choice([SOLID["charcoal"], SOLID["charcoal"], SOLID["white"], SOLID["bronze"], SOLID["aluminium"]]),
           "spandrel_h": rng.choice((0, 80, 100, 120)),
           "rib": rng.choice((60, 80, 100)), "clerestory": rng.random() < 0.5, "panel_w": rng.choice((300, 420, 600)),
           "upstand": rng.choice((95, 110)), "deck_fins": rng.random() < 0.4, "accent": _accent(rng)}
    pal["spandrel"] = rng.choice([_tone(pal["glass"], 0.55), dict(SOLID[frame]), pal["main"], dict(SOLID["charcoal"])])
    pal["tones"] = [pal["main"], pal["second"], dict(pal["main"], color=[min(1.0, c * 1.25) for c in pal["main"].get("color", [0.5] * 3)])]
    if skin in ("curtain", "fins", "louvres", "diagrid", "grid", "screen"):
        pal["main"] = pal["glass"] if rng.random() < 0.3 else pal["main"]
    return pal


# ------------------------------------------------------------------ volumes, ground floors, roofs, crowns


def _vol(p, f0, n, role="main", skin=None, plates=None):
    x0, y0, x1, y1 = bbox(p)
    return {"p": ccw(p), "f0": int(f0), "n": max(1, int(n)), "role": role, "skin": skin, "plates": plates,
            "x0": x0, "y0": y0, "x1": x1, "y1": y1}


def ground(p, z, h, kind, pal, rng, lod):
    """Ground storey: lobby (recessed glass behind columns), shops (glass with a sign band), glass, solid."""
    P = []
    if kind == "lobby":
        rec = rng.choice((0, 120, 200, 300))
        P.append(slab(offset(p, -rec) if rec else p, z, h - 40, pal["glass"], 0 if rec else 2))
        P.append(slab(p, z + h - 40, 40, pal["slab"]))
        if rec >= 120:
            colm = rng.choice([SOLID["white"], SOLID["concrete"], SOLID["charcoal"], SOLID["aluminium"]])
            round_ = rng.random() < 0.6
            for pt in walk(p, rng.choice((600, 750, 900)), margin=100):
                c = (pt[0] - pt[3] * rec * 0.5, pt[1] - pt[4] * rec * 0.5, z)
                P.append(cyl(50, h - 40, list(c), m=colm, detail="low") if round_ else box([40, 40, h - 40], list(c), m=colm))
        if lod:
            P += [member(pt, 6, 10, h - 40, z, pal["frame"], off=-rec) for pt in walk(offset(p, -rec) if rec else p, 300)]
    elif kind == "shops":
        P += [slab(p, z, h - 90, pal["glass"], 2), slab(p, z + h - 90, 60, rng.choice([pal["frame"], pal["second"], SOLID["charcoal"], SOLID["white"]]), 6),
              slab(p, z + h - 30, 30, pal["slab"], 3)]
        if lod:
            P += [member(pt, 10, 12, h - 90, z, pal["frame"]) for pt in walk(p, rng.choice((400, 500, 600)))]
    elif kind == "glass":
        P += [slab(p, z, h, pal["glass"], 1), slab(p, z + h - 20, 20, pal["frame"], 4)]
        if lod:
            P += [member(pt, 6, 10, h, z, pal["frame"]) for pt in walk(p, 200)]
    else:
        P.append(slab(p, z, h, pal["second"] if rng.random() < 0.5 else pal["main"]))
    return P


def roof(p, z, kind, pal, rng, lod):
    """Roof: parapet cap and deck, then plant | pv | green | terrace | overhang | barrel."""
    P = [slab(p, z, 45, pal["slab"] if rng.random() < 0.5 else pal["frame"], 3), slab(offset(p, -30), z + 45, 3, M("concrete", (110, 110, 108), roughness=0.9))]
    x0, y0, x1, y1 = bbox(offset(p, -150))
    cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
    w, d = x1 - x0, y1 - y0
    if w < 200 or d < 200:
        return P
    zt = z + 48
    if kind == "green":
        P.append(slab(offset(p, -60), zt, 12, M("grass", (80, 112, 56), roughness=0.95)))
        for _ in range(rng.randint(2, 5) if lod else 1):
            x, y = rng.uniform(x0, x1), rng.uniform(y0, y1)
            P.append(blob([180, 180, 120], [x, y, zt + 60], m=M("grass", (66, 98, 48)), seed=rng.randint(0, 999), detail=1))
    elif kind == "pv":
        rows = max(1, int(d / 260))
        for r in range(rows):
            P.append(box([w * 0.9, 160, 6], [cx, y0 + (r + 0.5) * d / rows, zt + 50], rot=[0, 0, 18], m=M("steel", (30, 44, 70), roughness=0.15, metallic=0.4)))
            if lod:
                P += [box([8, 8, 50], [cx + s * w * 0.4, y0 + (r + 0.5) * d / rows, zt], m=SOLID["aluminium"]) for s in (-1, 1)]
    elif kind == "terrace":
        P.append(slab(offset(p, -40), zt, 6, SOLID["timber"]))
        P.append(slab(offset(p, -10), zt, 105, RAIL))
        pw, pd = min(w * 0.5, 900), min(d * 0.4, 600)
        px, py = cx + rng.uniform(-w / 4, w / 4), cy + rng.uniform(-d / 4, d / 4)
        P += [box([12, 12, 280], [px + sx * pw / 2, py + sy * pd / 2, zt], m=pal["frame"]) for sx in (-1, 1) for sy in (-1, 1)]
        P += [box([10, pd, 16], [px - pw / 2 + k * pw / max(1, int(pw / 50)), py, zt + 280], m=pal["fin"]) for k in range(int(pw / 50) + 1)] if lod else \
             [box([pw, pd, 12], [px, py, zt + 280], m=pal["fin"])]
    elif kind == "overhang":
        P.append(slab(p, z + 45, 30, pal["slab"], rng.choice((120, 200, 300))))
    elif kind == "barrel":
        long_x = w >= d
        span, length = (d, w) if long_x else (w, d)
        P.append(cyl(span * 1.02, length, [cx - (length / 2 if long_x else 0), cy - (0 if long_x else length / 2), zt - span * 0.02],
                     rot=[0, 0, 90] if long_x else [90, 0, 0], m=pal["frame"], size=[span * 1.02, span * 0.36, length]))
    if kind in ("plant", "pv", "terrace", "green") or rng.random() < 0.5:   # plant: units behind a screen
        for _ in range(rng.randint(1, 3)):
            bw, bd = rng.choice((150, 220, 300)), rng.choice((120, 180))
            P.append(box([bw, bd, rng.choice((110, 150))], [cx + rng.uniform(-w / 3, w / 3), cy + rng.uniform(-d / 3, d / 3), zt],
                         m=M("steel", (168, 170, 170), roughness=0.5)))
    return P


def crown(v, z, kind, pal, rng, lod):
    """Tower tops: fins rising past the roof, a glowing band, a spire, a lantern plate."""
    p = v["p"]
    if kind == "fins_up":
        h = rng.choice((300, 500, 800))
        return [member(pt, 10, 40, h, z, pal["fin"]) for pt in walk(p, 90 * (1, 1.4, 2.5)[2 - lod])] + [slab(p, z + h - 20, 20, pal["frame"], 40)]
    if kind == "glow":
        return [slab(p, z - 80, 40, M("plaster", (240, 236, 220), emissive=[1.0, 0.95, 0.85], emissive_strength=4.0), 4)]
    if kind == "spire":
        x0, y0, x1, y1 = bbox(p)
        h = rng.choice((1200, 2000, 3000))
        return [cyl(rng.choice((60, 90)), h, [(x0 + x1) / 2, (y0 + y1) / 2, z + 45], m=SOLID["aluminium"]),
                cyl(160, 300, [(x0 + x1) / 2, (y0 + y1) / 2, z + 45], m=pal["frame"])]
    if kind == "lantern":
        return [slab(offset(p, -200), z + 45, 380, pal["glass"]), slab(p, z + 425, 40, pal["slab"], 120)]
    if kind == "slope":
        x0, y0, x1, y1 = bbox(p)
        h = rng.choice((500, 800, 1200))
        return [{"shape": "extrude", "profile": [[x0, y0, z], [x0, y1, z], [x0, y1, z + h]], "direction": [x1 - x0, 0, 0], "location": [0, 0, 0],
                 **pal["glass"]}]
    return []


def plates(v, zs, top, pal, rng, lod, twist=0.0, taper=1.0):
    """Tower made of one plate per storey, each turned and scaled a little more: twisting or tapering towers."""
    p, P = v["p"], []
    cx, cy = (v["x0"] + v["x1"]) / 2, (v["y0"] + v["y1"]) / 2
    n = len(zs)
    bd = pal.get("balcony", 150) * 0.8
    for i, z in enumerate(zs):
        fh = (zs[i + 1] if i + 1 < n else top) - z
        q = scale(rotate(p, twist * i, cx, cy), 1 - (1 - taper) * i / max(1, n - 1), cx, cy)
        P.append(slab(q, z, fh - 24, pal["glass"]))
        P.append(slab(q, z + fh - 24, 24, pal["slab"], bd if pal.get("plate_balcony") else 8))
        if pal.get("plate_balcony"):
            P.append(slab(q, z + fh, 100, RAIL, bd - 3)) if i + 1 < n else None
    v["p_top"] = scale(rotate(p, twist * (n - 1), cx, cy), taper, cx, cy)
    return P


# ------------------------------------------------------------------ compositions (lot: x centred, front y = 0, back y = d)


def _r(rng, w, d, b, n, corners):
    """A rectangle outline with the building's corner treatment."""
    return lambda x0, y0, x1, y1: rect(x0, y0, x1, y1, r=corners[0] * min(abs(x1 - x0), abs(y1 - y0)), cham=corners[1] * min(abs(x1 - x0), abs(y1 - y0)))


def f_block(w, d, n, rng, b, R):
    return [_vol(R(-w / 2, 0, w / 2, d), 0, n)]


def f_bar(w, d, n, rng, b, R):
    return [_vol(R(-w / 2, 0, w / 2, min(d, b)), 0, n)]


def f_l(w, d, n, rng, b, R):
    s = rng.choice((-1, 1))
    ww = min(b, w * 0.45)
    return [_vol(R(-w / 2, 0, w / 2, b), 0, n), _vol(R(s * w / 2 - (ww if s > 0 else 0), b - 5, s * w / 2 + (0 if s > 0 else ww), d), 0, max(1, n - rng.choice((0, 1))))]


def f_u(w, d, n, rng, b, R):
    nw = max(1, n - rng.choice((0, 1)))
    return [_vol(R(-w / 2, 0, w / 2, b), 0, n), _vol(R(-w / 2, b - 5, -w / 2 + b, d), 0, nw), _vol(R(w / 2 - b, b - 5, w / 2, d), 0, nw)]


def f_h(w, d, n, rng, b, R):
    lb = b * rng.uniform(0.6, 0.85)
    return [_vol(R(-w / 2, 0, -w / 2 + b, d), 0, n), _vol(R(w / 2 - b, 0, w / 2, d), 0, n),
            _vol(R(-w / 2 + b - 5, d / 2 - lb / 2, w / 2 - b + 5, d / 2 + lb / 2), 0, max(1, n - rng.choice((0, 1, 2))), "link")]


def f_courtyard(w, d, n, rng, b, R):
    return [_vol(R(-w / 2, 0, w / 2, b), 0, n), _vol(R(-w / 2, d - b, w / 2, d), 0, max(1, n + rng.choice((0, -1)))),
            _vol(R(-w / 2, b - 5, -w / 2 + b, d - b + 5), 0, n), _vol(R(w / 2 - b, b - 5, w / 2, d - b + 5), 0, max(1, n - rng.choice((0, 1))))]


def f_stepped(w, d, n, rng, b, R):
    k = rng.choice((2, 3))
    cuts = sorted(rng.uniform(0.3, 0.7) * w - w / 2 for _ in range(k - 1))
    xs = [-w / 2] + cuts + [w / 2]
    hs = [max(1, n - i * rng.randint(1, max(1, n // 3))) for i in range(k)]
    rng.shuffle(hs)
    hs[rng.randrange(k)] = n
    return [_vol(R(xs[i] - (5 if i else 0), rng.uniform(0, d * 0.15) if i % 2 else 0, xs[i + 1], d), 0, hs[i]) for i in range(k)]


def f_cantilever(w, d, n, rng, b, R):
    """Stacked boxes shifted against each other: the upper ones cantilever out over the lower."""
    V, f = [], 0
    bw, bd = w * rng.uniform(0.6, 0.8), d * rng.uniform(0.6, 0.85)
    while f < n:
        g = min(rng.randint(1, 3), n - f)
        dx = 0 if not V else rng.choice((-1, 1)) * rng.uniform(0.1, 0.5) * (w - bw + 600)
        dy = (d - bd) / 2 + (0 if not V else rng.uniform(-0.5, 0.5) * (d - bd + 400))
        rot = rng.choice((0, 0, 0, 8, -10, 90)) if V and abs(bw - bd) > 300 else 0
        p = R(dx - bw / 2, dy, dx + bw / 2, dy + bd)
        V.append(_vol(rotate(p, rot, dx, dy + bd / 2) if rot else p, f, g, "stack"))
        f += g
    return V


def f_notched(w, d, n, rng, b, R):
    rw, rd, off = rng.uniform(300, 600), rng.uniform(250, 450), rng.uniform(-w / 6, w / 6)
    return [_vol(R(-w / 2, 0, off - rw / 2, d), 0, n), _vol(rect(off - rw / 2 - 5, rd, off + rw / 2 + 5, d), 0, n, "recess", skin="curtain"),
            _vol(R(off + rw / 2, 0, w / 2, d), 0, max(1, n + rng.choice((0, -1, -2))))]


def f_bridge(w, d, n, rng, b, R):
    """Two blocks joined high up by a bridge volume, open below."""
    gap = rng.uniform(0.25, 0.4) * w
    bb = min(b, d)
    lo = max(1, n // 2)
    return [_vol(R(-w / 2, 0, -gap / 2, bb), 0, n), _vol(R(gap / 2, rng.uniform(0, d - bb), w / 2, rng.uniform(0, d - bb) + bb), 0, max(lo + 1, n - rng.choice((0, 1, 2)))),
            _vol(rect(-gap / 2 - 5, bb * 0.15, gap / 2 + 5, bb * 0.85), lo, max(1, rng.choice((1, 2))), "bridge", skin=rng.choice(("curtain", "fins")))]


def f_curve(w, d, n, rng, b, R):
    """A bar curving along the street, convex or concave."""
    span = rng.uniform(70, 120)
    rad = (w / 2) / math.sin(math.radians(span / 2))
    if rng.random() < 0.5:                                    # convex toward the street
        cy = rad
        p = arc_bar(0, cy, rad - b / 2, b, 270 - span / 2, 270 + span / 2)
    else:
        cy = -rad + b + d * 0.2
        p = arc_bar(0, cy, rad + b / 2 - b, b, 90 - span / 2, 90 + span / 2)
    y0 = bbox(p)[1]
    return [_vol([(x, y - y0) for x, y in p], 0, n)]


def f_round(w, d, n, rng, b, R):
    a = min(w, d) / 2 * rng.uniform(0.7, 0.95)
    e = rng.choice((1.0, 1.0, 1.3, 1.6))
    return [_vol(ellipse(0, d / 2, min(a * e, w / 2), a, 36), 0, n)]


def f_podium_tower(w, d, n, rng, b, R):
    npod = rng.randint(2, 4)
    tw, td = w * rng.uniform(0.4, 0.6), d * rng.uniform(0.45, 0.65)
    ox, oy = rng.uniform(-(w - tw) / 3, (w - tw) / 3), rng.uniform(0.2, 0.8) * (d - td)
    shape = rng.choice(("rect", "rect", "round", "ellipse", "chamfer"))
    if shape == "round":
        t = ellipse(ox, oy + td / 2, min(tw, td) / 2, min(tw, td) / 2, 36)
    elif shape == "ellipse":
        t = ellipse(ox, oy + td / 2, tw / 2, td / 2, 36)
    elif shape == "chamfer":
        t = rect(ox - tw / 2, oy, ox + tw / 2, oy + td, cham=min(tw, td) * 0.2)
    else:
        t = R(ox - tw / 2, oy, ox + tw / 2, oy + td)
    return [_vol(R(-w / 2, 0, w / 2, d), 0, npod, "podium"), _vol(t, npod, max(1, n - npod), "tower")]


def f_twin(w, d, n, rng, b, R):
    npod = rng.randint(1, 3)
    gap = rng.uniform(0.15, 0.3) * w
    tw, td = (w - gap) / 2 * 0.85, d * rng.uniform(0.45, 0.7)
    V = [_vol(R(-w / 2, 0, w / 2, d), 0, npod, "podium")]
    V += [_vol(R(-gap / 2 - tw, d * 0.15, -gap / 2, d * 0.15 + td), npod, max(1, n - npod), "tower"),
          _vol(R(gap / 2, d * 0.2, gap / 2 + tw, d * 0.2 + td), npod, max(1, n - npod - rng.randint(0, max(1, n // 4))), "tower")]
    return V


def f_setback(w, d, n, rng, b, R):
    tiers = rng.randint(2, 4)
    s = rng.uniform(250, 500)
    V, f = [], 0
    counts = [max(1, n // tiers)] * tiers
    counts[0] += n - sum(counts)
    for k, c in enumerate(counts):
        if w - 2 * k * s < 800 or d - k * s < 800:
            break
        V.append(_vol(R(-w / 2 + k * s, k * s * 0.7, w / 2 - k * s, d - k * s * 0.3), f, c, "tower" if k else "main"))
        f += c
    return V


def f_tower(w, d, n, rng, b, R):
    """A free-standing tower: square, round, elliptic, chamfered or rounded, perhaps twisting or tapering."""
    s = min(w, d) * rng.uniform(0.6, 0.85)
    shape = rng.choice(("rect", "round", "chamfer", "rounded", "tri"))
    cy = d / 2
    if shape == "round":
        p = ellipse(0, cy, s / 2, s / 2, 36)
    elif shape == "chamfer":
        p = rect(-s / 2, cy - s / 2, s / 2, cy + s / 2, cham=s * 0.18)
    elif shape == "rounded":
        p = rect(-s / 2, cy - s / 2, s / 2, cy + s / 2, r=s * 0.22)
    elif shape == "tri":
        p = [(-s * 0.55, cy - s * 0.4), (s * 0.55, cy - s * 0.4), (s * 0.3, cy + s * 0.5), (-s * 0.3, cy + s * 0.5)]
    else:
        p = rect(-s / 2, cy - s / 2, s / 2, cy + s / 2)
    mode = rng.choice((None, None, "twist", "taper"))
    pl = {"twist": rng.choice((1.5, 2.0, 3.0)) * rng.choice((-1, 1))} if mode == "twist" else ({"taper": rng.uniform(0.55, 0.8)} if mode == "taper" else None)
    return [_vol(p, 0, n, "tower", plates=pl)]


def f_slab(w, d, n, rng, b, R):
    return [_vol(R(-w / 2, d * 0.1, w / 2, d * 0.1 + min(b, d * 0.8)), 0, n)]


def f_campus(w, d, n, rng, b, R):
    """Two to four pavilions of different heights, turned against each other, linked by a low glass spine."""
    k = rng.randint(2, 4)
    V = [_vol(rect(-w * 0.4, d * 0.42, w * 0.4, d * 0.58), 0, 1, "spine", skin="curtain")]
    for i in range(k):
        cx = -w / 2 + (i + 0.5) * w / k
        pw, pd = w / k * rng.uniform(0.6, 0.85), d * rng.uniform(0.35, 0.6)
        cy = d * (0.3 if i % 2 else 0.7)
        p = R(cx - pw / 2, cy - pd / 2, cx + pw / 2, cy + pd / 2)
        V.append(_vol(rotate(p, rng.choice((0, 0, 6, -8, 12)), cx, cy), 0, max(1, n - rng.randint(0, max(1, n // 2))), "pavilion"))
    V[1 + rng.randrange(k)]["n"] = n
    return V


def f_wedge(w, d, n, rng, b, R):
    k = rng.uniform(0.2, 0.45) * w
    p = [(-w / 2, 0), (w / 2, 0), (w / 2 - k, d), (-w / 2 + k * rng.uniform(0, 1.2), d)] if rng.random() < 0.5 else \
        [(-w / 2 + k, 0), (w / 2, 0), (w / 2 - k * 0.3, d), (-w / 2, d)]
    return [_vol(p, 0, n)]


FORMS = {"block": f_block, "bar": f_bar, "L": f_l, "U": f_u, "H": f_h, "courtyard": f_courtyard, "stepped": f_stepped, "cantilever": f_cantilever,
         "notched": f_notched, "bridge": f_bridge, "curve": f_curve, "round": f_round, "podium_tower": f_podium_tower, "twin": f_twin,
         "setback": f_setback, "tower": f_tower, "slab": f_slab, "campus": f_campus, "wedge": f_wedge}


def f_polygon(poly, n, rng, kind):
    """Volumes on a footprint polygon: the polygon itself, sometimes with upper floors set back or a tower on it."""
    p = ccw(poly)
    mode = rng.choice(("block", "block", "setback", "tower")) if n >= 4 else "block"
    if mode == "setback":
        lo = max(1, int(n * rng.uniform(0.4, 0.7)))
        return [_vol(p, 0, lo), _vol(offset(p, -rng.choice((250, 400))), lo, n - lo, "tower")]
    if mode == "tower" and abs(_area(p)) > 6e6 and n >= 8:
        npod = rng.randint(2, 3)
        return [_vol(p, 0, npod, "podium"), _vol(scale(p, rng.uniform(0.45, 0.6)), npod, n - npod, "tower")]
    return [_vol(p, 0, n)]


# ------------------------------------------------------------------ the type's own elements


def canopy(cx, y, w, d, z, rng, m=None, kind=None):
    """Entrance canopy against a facade at y, projecting toward -y: thin plate on posts | cantilever | glass | slats."""
    kind = kind or rng.choice(("posts", "cantilever", "glass", "slats"))
    m = m or rng.choice([SOLID["aluminium"], SOLID["white"], SOLID["timber"], SOLID["charcoal"], _accent(rng)])
    P = []
    if kind == "glass":
        P.append(box([w, d, 4], [cx, y - d / 2, z + 20], m=RAIL))
        P += [box([10, d, 20], [cx - w / 2 + k * w / max(1, int(w / 200)), y - d / 2, z], m=SOLID["aluminium"]) for k in range(int(w / 200) + 1)]
    elif kind == "slats":
        P.append(box([w, d, 10], [cx, y - d / 2, z + 26], m=SOLID["charcoal"]))
        P += [box([6, d, 22], [cx - w / 2 + 15 + k * 30, y - d / 2, z], m=m) for k in range(int((w - 30) / 30))]
    else:
        P.append(box([w, d, 28 if kind == "posts" else 45], [cx, y - d / 2, z], m=m))
    if kind in ("posts", "slats"):
        for s in (-1, 1):
            P.append(cyl(22, z, [cx + s * (w / 2 - 30), y - d + 30, 0], m=SOLID["aluminium"]))
    return P


def flags(x0, y, count, rng, h=None):
    h = h or rng.choice((900, 1100))
    P = []
    for k in range(count):
        x = x0 + k * 260
        col = rng.choice([(200, 30, 40), (0, 120, 70), (240, 240, 240), (20, 60, 150), (240, 190, 30), (30, 30, 30)])
        P += [cyl(12, h, [x, y, 0], m=SOLID["aluminium"]), box([160, 3, 100], [x + 82, y, h - 120], m=M("plaster", col, roughness=0.8))]
    return P


def sports(cx, cy, w, d, rng, kind=None):
    """Sports ground: football pitch | courts | pitch inside a running track."""
    kind = kind or rng.choice(("football", "courts", "track"))
    line = M("plaster", (240, 240, 236), roughness=0.6)
    P = []
    if kind == "courts":
        surface = M("concrete", rng.choice([(40, 90, 150), (170, 70, 50), (60, 120, 70)]))
        P.append(box([w, d, 6], [cx, cy, 0], m=M("concrete", (120, 120, 116))))
        k = max(1, int(w / 1800))
        for i in range(k):
            x = cx - w / 2 + (i + 0.5) * w / k
            cd = min(2800, d - 200)
            P += [box([1500, cd, 2], [x, cy, 6], m=surface), box([1500, 6, 1], [x, cy, 8], m=line)]
            for s in (-1, 1):
                P += [cyl(12, 300, [x, cy + s * (cd / 2 + 20), 0], m=SOLID["aluminium"]), box([180, 6, 105], [x, cy + s * (cd / 2 - 20), 290], m=SOLID["white"])]
        P += [box([w, 4, 300], [cx, cy + s * d / 2, 0], m=SOLID["charcoal"]) for s in (-1, 1)]
        return P
    if kind == "track":
        P.append(box([w, d, 4], [cx, cy, 0], m=M("terracotta", (166, 70, 50))))
        w, d = w - 900, d - 900
    P.append(box([w, d, 6], [cx, cy, 0], m=M("grass", (70, 130, 50))))
    P += [box([500, d - 10, 1], [cx - w / 2 + (k + 0.5) * 500, cy, 6], m=M("grass", (88, 150, 60))) for k in range(int(w / 500)) if k % 2]
    P += [box([w - 200, 12, 1], [cx, cy + s * (d / 2 - 100), 7], m=line) for s in (-1, 1)]
    P += [box([12, d - 200, 1], [cx + s * (w / 2 - 100), cy, 7], m=line) for s in (-1, 1)] + [box([12, d - 200, 1], [cx, cy, 7], m=line)]
    for s in (-1, 1):
        gx = cx + s * (w / 2 - 100)
        P += [box([10, 10, 240], [gx, cy - 366, 0], m=line), box([10, 10, 240], [gx, cy + 366, 0], m=line), box([10, 742, 10], [gx, cy, 240], m=line)]
    return P


def playground(cx, cy, rng, w=1400, d=1100):
    P = [box([w, d, 6], [cx, cy, 0], m=M("concrete", rng.choice([(60, 130, 170), (190, 80, 60), (90, 150, 80), (230, 170, 60)]), roughness=0.95))]
    a, b = _accent(rng), _accent(rng)
    tx = cx - w / 4
    P += [box([16, 16, 260], [tx + sx * 70, cy + sy * 70, 0], m=a) for sx in (-1, 1) for sy in (-1, 1)]
    P += [box([160, 160, 10], [tx, cy, 140], m=SOLID["timber"]), {"shape": "pyramid", "size": [190, 190, 90], "location": [tx, cy, 260], **b},
          box([60, 300, 6], [tx, cy - 240, 70], rot=[0, 0, 26], m=b)]
    sx = cx + w / 4
    P += [beam((sx + o, cy + s * 80, 0), (sx + o, cy, 230), 7, m=a) for o in (-200, 200) for s in (-1, 1)] + [box([400, 8, 8], [sx, cy, 226], m=a)]
    P.append({"shape": "dome", "size": [220, 220, 110], "location": [cx, cy + d / 3, 6], **_accent(rng)})
    return P


def parking(cx, cy, w, d, rng, lamps=True):
    P = [box([w, d, 4], [cx, cy, 0], m=M("asphalt", (54, 56, 58)))]
    line = M("plaster", (232, 232, 226), roughness=0.6)
    rows = max(1, int(d / 1700))
    for r in range(rows):
        y0 = cy - d / 2 + r * d / rows
        for k in range(int(w / 250) + 1):
            x = cx - w / 2 + k * 250
            P += [box([10, 500, 1], [x, y0 + 250, 4], m=line), box([10, 500, 1], [x, y0 + d / rows - 250, 4], m=line)]
        if lamps:
            for k in range(1, max(2, int(w / 2500))):
                x = cx - w / 2 + k * w / max(2, int(w / 2500))
                P += [cyl(14, 800, [x, y0 + d / rows / 2, 0], m=SOLID["charcoal"]), box([120, 30, 12], [x, y0 + d / rows / 2, 795], m=SOLID["charcoal"])]
    for k in range(int(w / 3000)):
        x = cx - w / 2 + (k + 0.5) * w / max(1, int(w / 3000))
        P += [box([250, 500, 15], [x, cy - d / 2 + 250, 0], m=SOLID["concrete"])] + tree(x, cy - d / 2 + 250, rng, 500)
    return P


def helipad(cx, cy, z, r=900):
    white, yellow = M("plaster", (240, 240, 236), roughness=0.6), M("painted_iron", (240, 190, 30), roughness=0.4)
    P = [cyl(2 * r, 40, [cx, cy, z], m=M("concrete", (60, 64, 66))),
         {"shape": "torus", "size": [2 * r - 120, 2 * r - 120, 24], "location": [cx, cy, z + 40], "detail": "low", **white}]
    P += [box([60, 520, 3], [cx - 150, cy, z + 40], m=white), box([60, 520, 3], [cx + 150, cy, z + 40], m=white), box([300, 60, 3], [cx, cy, z + 40], m=white)]
    P += [box([24, 24, 16], [cx + math.cos(t) * (r - 30), cy + math.sin(t) * (r - 30), z + 40], m=yellow) for t in (2 * math.pi * k / 12 for k in range(12))]
    return P


def cross_sign(x, y, z, size, color=(200, 20, 30)):
    c = M("painted_iron", color, roughness=0.4)
    return [box([size, 8, size], [x, y - 4, z], m=SOLID["white"]), box([size * 0.72, 12, size * 0.22], [x, y - 8, z + size * 0.39], m=c),
            box([size * 0.22, 12, size * 0.72], [x, y - 8, z + size * 0.14], m=c)]


def dome(cx, cy, z, dia, rng, m=None, kind=None):
    kind = kind or rng.choice(("hemisphere", "pointed", "onion"))
    m = m or rng.choice([M("gold"), SOLID["white"], M("verdigris", (90, 150, 130)), M("plaster", (40, 96, 160)), SOLID["copper"]])
    dh = dia * rng.uniform(0.12, 0.25)
    P = [cyl(dia * 1.04, dh, [cx, cy, z], m=SOLID["white"])]
    z += dh
    if kind == "onion":
        P += [sphere(dia, [cx, cy, z + dia * 0.45], m=m, scale=[1, 1, 1.25]), cone(dia * 0.3, dia * 0.45, [cx, cy, z + dia * 0.95], m=m)]
        top = z + dia * 1.35
    else:
        hh = dia * (0.5 if kind == "hemisphere" else 0.72)
        P.append({"shape": "dome", "size": [dia, dia, hh], "location": [cx, cy, z], **m})
        top = z + hh
    return P + [cyl(14, dia * 0.12, [cx, cy, top], m=M("gold")), sphere(dia * 0.05, [cx, cy, top + dia * 0.12], m=M("gold"))]


def minaret(x, y, h, dia, rng, m, kind=None):
    kind = kind or rng.choice(("round", "square", "slender"))
    P = []
    if kind == "square":
        P += [box([dia, dia, h * 0.85], [x, y, 0], m=m), box([dia + 40, dia + 40, 25], [x, y, h * 0.85], m=m),
              box([dia * 0.6, dia * 0.6, h * 0.1], [x, y, h * 0.85 + 25], m=m), {"shape": "dome", "size": [dia * 0.6, dia * 0.6, dia * 0.3], "location": [x, y, h * 0.95 + 25], **M("gold")}]
    else:
        s = 0.7 if kind == "slender" else 1.0
        P.append(cyl(dia * s, h * 0.8, [x, y, 0], m=m))
        for f in ((0.55, 0.8) if kind == "round" else (0.8,)):
            P.append(cyl(dia * s * 1.4, 20, [x, y, h * f], m=m))
        P += [cyl(dia * s * 0.85, h * 0.06, [x, y, h * 0.8], m=m), cone(dia * s, h * 0.14, [x, y, h * 0.86], m=rng.choice([M("gold"), SOLID["white"], SOLID["charcoal"]]))]
    return P + [cyl(8, h * 0.05, [x, y, h], m=M("gold"))]


def chimney(x, y, h, dia, rng):
    m = rng.choice([SOLID["brick"], SOLID["concrete"], SOLID["aluminium"]])
    return [cone(dia, h, [x, y, 0], m=m, top_ratio=rng.uniform(0.55, 0.8)),
            cyl(dia * 0.75, 60, [x, y, h * 0.9], m=M("painted_iron", (190, 40, 30), roughness=0.5))]


def silo(x, y, h, dia, rng):
    m = rng.choice([SOLID["aluminium"], SOLID["concrete"], SOLID["white"]])
    return [cyl(dia, h, [x, y, 0], m=m), cone(dia, dia * 0.3, [x, y, h], m=m)]


# ---- extras per type: c = {V, Z, w, d, rng, pal, n, front: the volume nearest the street}


def _front(c):
    return min(c["V"], key=lambda v: (v["f0"], v["y0"]))


def _tall(c):
    return max(c["V"], key=lambda v: (v["f0"] + v["n"], (v["x1"] - v["x0"]) * (v["y1"] - v["y0"])))


def _top(c, v):
    return c["Z"](v["f0"] + v["n"])


def _trees(c, k, y0, y1):
    rng = c["rng"]
    return [q for _ in range(k) for q in tree(rng.uniform(-c["w"] * 0.45, c["w"] * 0.45), rng.uniform(y0, y1), rng)]


def x_entrance(c):
    f, rng = _front(c), c["rng"]
    cx = (f["x0"] + f["x1"]) / 2
    return canopy(cx, f["y0"], rng.choice((500, 700, 900)), rng.choice((250, 350, 450)), min(c["Z"](1) - 60, 420), rng)


def x_school(c):
    rng, f = c["rng"], _front(c)
    P = x_entrance(c) + flags(f["x0"] + 200, f["y0"] - 900, rng.choice((1, 2, 3)), rng)
    back = max(v["y1"] for v in c["V"])
    rest = c["d"] - back
    if rest > 2500:
        P += sports(0, back + rest / 2, min(c["w"] * 0.85, rest * 1.6), rest * 0.8, rng)
    P += playground(f["x1"] - 900 if rng.random() < 0.5 else f["x0"] + 900, f["y0"] - 1300, rng)
    return P + _trees(c, rng.randint(3, 6), -1800, -300)


def x_kindergarten(c):
    rng = c["rng"]
    back = max(v["y1"] for v in c["V"])
    return x_entrance(c) + playground(0, back + max(700, (c["d"] - back) / 2), rng, w=min(c["w"] * 0.8, 2200), d=min(max(900, c["d"] - back - 200), 1600)) \
        + _trees(c, rng.randint(2, 4), -900, -200)


def x_campus(c):
    rng, f = c["rng"], _front(c)
    P = [box([c["w"] * 0.8, 1400, 4], [0, f["y0"] - 750, 0], m=rng.choice([M("paving", (196, 190, 178)), M("granite", (120, 118, 112))]))]
    return P + flags(-400, f["y0"] - 1300, 3, rng) + _trees(c, rng.randint(4, 8), -1500, -200)


def x_hospital(c):
    rng, t, f = c["rng"], _tall(c), _front(c)
    P = helipad((t["x0"] + t["x1"]) / 2, (t["y0"] + t["y1"]) / 2, _top(c, t) + 48, r=min(900, (t["y1"] - t["y0"]) * 0.4, (t["x1"] - t["x0"]) * 0.4))
    cx = (f["x0"] + f["x1"]) / 2
    P += canopy(cx, f["y0"], rng.choice((900, 1200)), rng.choice((500, 650)), 400, rng, kind=rng.choice(("posts", "cantilever")))
    ex = f["x0"] + 900 if rng.random() < 0.5 else f["x1"] - 900
    P += canopy(ex, f["y0"], 1300, 900, 470, rng, m=M("painted_iron", (200, 26, 30), roughness=0.4) if rng.random() < 0.5 else SOLID["white"], kind="posts")
    P += [box([1200, 900, 1], [ex, f["y0"] - 450, 1], m=M("painted_iron", (200, 40, 36), roughness=0.8))] + cross_sign(ex, f["y0"], 530, 160)
    P += cross_sign((t["x0"] + t["x1"]) / 2, t["y0"], _top(c, t) - 320, rng.choice((260, 340)), color=rng.choice([(200, 20, 30), (20, 150, 70)]))
    if f["y0"] > 2400:
        P += parking(cx, f["y0"] / 2, min(c["w"], 6000), f["y0"] * 0.7, rng)
    return P


def x_clinic(c):
    rng, f = c["rng"], _front(c)
    cx = (f["x0"] + f["x1"]) / 2
    return x_entrance(c) + cross_sign(cx + rng.choice((-1, 1)) * (f["x1"] - f["x0"]) * 0.3, f["y0"], _top(c, f) - 200, 170, color=rng.choice([(20, 150, 70), (200, 20, 30)]))


def x_plaza(c):
    rng, f = c["rng"], _front(c)
    P = x_entrance(c)
    if f["y0"] > 500:
        P.append(box([c["w"] * 0.9, f["y0"], 4], [0, f["y0"] / 2, 0], m=rng.choice([M("granite", (110, 108, 104)), M("paving", (190, 184, 170))])))
        P += _trees(c, rng.randint(2, 5), f["y0"] * 0.2, f["y0"] * 0.8)
        if rng.random() < 0.5:
            P += [box([900, 400, 40], [rng.uniform(-c["w"] / 4, c["w"] / 4), f["y0"] * 0.5, 0], m=SOLID["stone"]),
                  box([860, 360, 6], [0, 0, 0], m=M("water"))][:1]
    return P


def x_parkfront(c):
    rng = c["rng"]
    f = _front(c)
    P = x_plaza(c)
    if f["y0"] > 1200:
        P += parking(0, f["y0"] / 2, c["w"] * 0.9, f["y0"] * 0.8, rng)
    return P + flags(f["x0"] + 300, f["y0"] - 300, rng.choice((2, 3)), rng)


def x_hotel(c):
    rng, f, t = c["rng"], _front(c), _tall(c)
    cx = (f["x0"] + f["x1"]) / 2
    P = canopy(cx, f["y0"], rng.choice((1200, 1500)), rng.choice((700, 900)), 480, rng, kind=rng.choice(("posts", "cantilever", "slats")))
    P.append(box([1600, 1000, 3], [cx, f["y0"] - 500, 0], m=M("granite", (90, 88, 86))))
    low = [v for v in c["V"] if v is not t]
    if low:
        h = max(low, key=lambda v: (v["x1"] - v["x0"]) * (v["y1"] - v["y0"]))
        z = _top(c, h) + 50
        pw, pd = min(1500, (h["x1"] - h["x0"]) * 0.4), min(600, (h["y1"] - h["y0"]) * 0.3)
        px, py = h["x0"] + pw / 2 + 300, h["y0"] + pd / 2 + 300
        P += [box([pw + 300, pd + 300, 8], [px, py, z], m=SOLID["timber"]), box([pw, pd, 10], [px, py, z + 2], m=M("water"))]
    return P + _trees(c, rng.randint(2, 4), f["y0"] - 1200, f["y0"] - 300)


def x_mall(c):
    rng = c["rng"]
    P = []
    for v in c["V"]:
        if v["role"] == "entry":
            cx = (v["x0"] + v["x1"]) / 2
            P += canopy(cx, v["y0"], v["x1"] - v["x0"] + 200, 450, 480, rng, kind=rng.choice(("cantilever", "glass")))
    f = min(v["y0"] for v in c["V"])
    if f > 1500:
        P += parking(0, f / 2, c["w"] * 0.95, f * 0.85, rng)
    return P


def x_government(c):
    rng, f = c["rng"], _front(c)
    cx = (f["x0"] + f["x1"]) / 2
    w = min((f["x1"] - f["x0"]) * 0.6, 3000)
    h = c["Z"](min(2, c["n"]))
    P = [box([w, 900, 40], [cx, f["y0"] - 450, h], m=c["pal"]["slab"])]                      # a big thin roof on slender columns
    P += [cyl(40, h, [cx - w / 2 + 60 + k * (w - 120) / 7, f["y0"] - 820, 0], m=SOLID["white"]) for k in range(8)]
    P += [box([w + 400, 1400, 30], [cx, f["y0"] - 700, 0], m=SOLID["stone"]), box([w * 0.8, 900, 8], [cx, f["y0"] - 2600, 0], m=M("water"))]
    return P + flags(cx - 260, f["y0"] - 1700, 3, rng, h=1300)


def x_police(c):
    rng, f = c["rng"], _front(c)
    cx = (f["x0"] + f["x1"]) / 2
    blue = M("painted_iron", rng.choice([(20, 50, 130), (30, 70, 160)]), roughness=0.4)
    return canopy(cx, f["y0"], 700, 350, 340, rng, m=blue) + [box([f["x1"] - f["x0"], 30, 60], [cx, f["y0"] - 15, c["Z"](1) - 20], m=blue)] \
        + flags(f["x0"] + 300, f["y0"] - 600, 1, rng)


def x_fire(c):
    rng, f = c["rng"], _front(c)
    door = rng.choice([M("painted_iron", (180, 26, 24), roughness=0.4), SOLID["white"], SOLID["aluminium"]])
    bays = max(2, int((f["x1"] - f["x0"]) * 0.7 / 480))
    x0 = f["x0"] + rng.choice((200, (f["x1"] - f["x0"]) * 0.3))
    P = []
    for k in range(bays):
        x = x0 + 240 + k * 480
        if x > f["x1"] - 240:
            break
        P += [box([380, 20, 440], [x, f["y0"] - 8, 0], m=door), box([380, 22, 60], [x, f["y0"] - 9, 250], m=GLASS["dark"])]
    P.append(box([f["x1"] - f["x0"], 700, 4], [(f["x0"] + f["x1"]) / 2, f["y0"] - 350, 0], m=SOLID["concrete"]))
    tx = f["x1"] + 300
    th = _top(c, f) + rng.choice((500, 900))
    P += [box([300, 300, th], [tx, f["y0"] + 400, 0], m=c["pal"]["main"]), box([340, 340, 20], [tx, f["y0"] + 400, th], m=c["pal"]["frame"])]
    return P


def x_mosque(c):
    rng, t = c["rng"], _tall(c)
    cx, cy = (t["x0"] + t["x1"]) / 2, (t["y0"] + t["y1"]) / 2
    z = _top(c, t) + 45
    span = min(t["x1"] - t["x0"], t["y1"] - t["y0"])
    kind = rng.choice(("hemisphere", "pointed", "onion"))
    dm = rng.choice([M("gold"), SOLID["white"], M("verdigris", (90, 150, 130)), M("plaster", (40, 96, 160)), SOLID["copper"]])
    P = dome(cx, cy, z, span * rng.uniform(0.45, 0.62), rng, m=dm, kind=kind)
    if rng.random() < 0.5 and span > 1600:
        P += [q for sx in (-1, 1) for sy in (-1, 1) for q in dome(cx + sx * span * 0.32, cy + sy * span * 0.32, z, span * 0.16, rng, m=dm, kind=kind)]
    count = rng.choice((1, 2, 2, 4))
    mk = rng.choice(("round", "square", "slender"))
    mh, md = max(z * 2.2, 2200) * rng.uniform(0.9, 1.4), rng.choice((260, 320, 380))
    cs = [(t["x0"] - md, t["y0"] - md), (t["x1"] + md, t["y0"] - md), (t["x0"] - md, t["y1"] + md), (t["x1"] + md, t["y1"] + md)]
    for x, y in (cs[:2] if count == 2 else cs if count == 4 else [rng.choice(cs)]):
        P += minaret(x, y, mh, md, rng, c["pal"]["main"], kind=mk)
    P.append(box([max(1200, (t["x1"] - t["x0"]) * 0.6), 1600, 5], [cx, t["y0"] - 820, 0], m=rng.choice([M("marble", (230, 228, 222)), M("paving", (200, 190, 170))])))
    return P


def x_church(c):
    rng, f = c["rng"], _front(c)
    th = _top(c, f) + rng.choice((1200, 1800, 2400))
    tw = rng.choice((300, 400))
    tx = f["x1"] + tw if rng.random() < 0.5 else f["x0"] - tw
    return [box([tw, tw * 1.6, th], [tx, f["y0"] + 300, 0], m=c["pal"]["main"]), box([tw * 0.12, 30, 500], [tx, f["y0"] + 300 - tw * 0.8 - 15, th - 700], m=c["pal"]["frame"]),
            box([300, 30, tw * 0.12], [tx, f["y0"] + 300 - tw * 0.8 - 15, th - 400], m=c["pal"]["frame"])]


def x_annex(c, floors=2):
    rng, f = c["rng"], _front(c)
    aw, ad = rng.uniform(1200, 1800), rng.uniform(900, 1200)
    saved = dict(LAST, props=LAST.get("props"))
    parts = building("office", seed=rng.randrange(1 << 30), floors=floors, width=aw, depth=ad, form="block", detail=c["detail"], site=False)
    LAST.clear()
    LAST.update(saved)
    x = f["x0"] - aw / 2 - 5 if rng.random() < 0.5 else f["x1"] + aw / 2 + 5
    return transform(parts, (x, f["y0"], 0), 0)


def x_warehouse(c):
    rng, f = c["rng"], _front(c)
    P = x_annex(c)
    doors = max(2, int((f["x1"] - f["x0"]) / 400) - 2)
    for k in range(doors):
        x = f["x0"] + 400 + k * 400
        if x > f["x1"] - 300:
            break
        P += [box([250, 16, 270], [x, f["y0"] - 6, 120], m=SOLID["aluminium"]), box([320, 40, 20], [x, f["y0"] - 20, 430], m=SOLID["charcoal"])]
    P += [box([f["x1"] - f["x0"], 250, 120], [(f["x0"] + f["x1"]) / 2, f["y0"] - 125, 0], m=SOLID["concrete"]),
          box([c["w"] * 1.1, 2400, 4], [0, f["y0"] - 1450, 0], m=M("asphalt", (60, 62, 64)))]
    return P


def x_factory(c):
    rng, t = c["rng"], _tall(c)
    P = x_annex(c)
    for k in range(rng.choice((1, 2, 3))):
        P += chimney(t["x1"] - 500 - k * 700, t["y1"] - 600, _top(c, t) + rng.choice((1500, 2500, 3500)), rng.choice((200, 300)), rng)
    for k in range(rng.choice((0, 2, 3, 4))):
        P += silo(t["x0"] - 600, t["y0"] + 600 + k * 550, rng.choice((1200, 1600, 2000)), 450, rng)
    return P


def x_parking(c):
    rng, t = c["rng"], _tall(c)
    z = _top(c, t)
    blue = M("painted_iron", (20, 70, 170), roughness=0.4)
    sx = t["x1"] - 300
    P = [box([300, 40, 300], [sx, t["y0"] - 20, z - 400], m=blue), box([60, 44, 220], [sx - 60, t["y0"] - 22, z - 360], m=SOLID["white"]),
         box([140, 44, 50], [sx, t["y0"] - 22, z - 190], m=SOLID["white"]), box([140, 44, 50], [sx, t["y0"] - 22, z - 290], m=SOLID["white"]),
         box([50, 44, 150], [sx + 60, t["y0"] - 22, z - 290], m=SOLID["white"])]
    P += [box([400, 400, z + 300], [t["x0"] + 200, t["y1"] - 200, 0], m=c["pal"]["second"])]
    return P


def x_petrol(c):
    rng = c["rng"]
    bm = M("painted_iron", rng.choice([(200, 30, 30), (20, 100, 60), (240, 180, 20), (20, 60, 150), (230, 110, 20)]), roughness=0.35)
    f = _front(c)
    off = f["y0"]
    cw, cd = min(c["w"] * 0.85, rng.choice((1800, 2400, 3000))), max(600, min(off * 0.8, 1100))
    cy, z = off * 0.45, rng.choice((520, 550))
    P = [box([c["w"], off, 4], [0, off / 2, 0], m=SOLID["concrete"]), box([cw, cd, 70], [0, cy, z], m=SOLID["white"]), box([cw + 10, cd + 10, 40], [0, cy, z + 70], m=bm)]
    islands = max(2, int(cw / 700))
    for k in range(islands):
        x = -cw / 2 + (k + 0.5) * cw / islands
        P += [box([90, 420, 20], [x, cy, 0], m=SOLID["concrete"]), cyl(30, z, [x, cy + 260, 0], m=SOLID["aluminium"])]
        P += [box([60, 40, 180], [x, cy + o, 20], m=bm) for o in (-110, 110)]
    return P + [box([180, 40, 700], [c["w"] / 2 - 200, 150, 0], m=bm), box([160, 44, 300], [c["w"] / 2 - 200, 150, 380], m=SOLID["white"])]


def x_villa(c):
    rng, f = c["rng"], _front(c)
    P = []
    back = max(v["y1"] for v in c["V"])
    pw, pd = rng.choice((800, 1000, 1200)), rng.choice((350, 400))
    py = back + 300 + pd / 2 if c["d"] - back > pd + 500 else f["y0"] - pd / 2 - 300
    px = rng.uniform(-c["w"] / 4, c["w"] / 4)
    P += [box([pw + 300, pd + 300, 10], [px, py, 0], m=rng.choice([SOLID["stone"], SOLID["timber"], SOLID["white"]])),
          box([pw, pd, 12], [px, py, 2], m=M("water"))]
    P += _trees(c, rng.randint(2, 5), f["y0"] - 600, max(v["y1"] for v in c["V"]) + 400)
    return P + canopy((f["x0"] + f["x1"]) / 2, f["y0"], 400, 250, 300, rng)


# ------------------------------------------------------------------ types

_ALL_GLASS = list(GLASS)
_W = ["white", "light_grey", "warm_grey", "charcoal", "sand", "stone", "concrete", "terracotta", "timber", "brick", "dark_brick", "black", "sage", "corten", "navy"]

KINDS = {
    # name: floors, lot (w, d), ground / typical storey, forms, skins, ground floors, roofs, crowns, bar depth, extras, building setback
    "apartment": dict(floors=(5, 14), lot=((2400, 4200), (1600, 2800)), g=420, t=310, b=1300,
                      forms=["block", "bar", "L", "U", "stepped", "cantilever", "notched", "curve", "courtyard", "podium_tower", "setback", "round", "wedge"],
                      skins={"balconies": 4, "punched": 4, "screen": 1, "ribbon": 2, "curtain": 1, "grid": 1, "louvres": 1, "tiles": 1},
                      grounds=["lobby", "shops", "glass", "solid"], roofs=["plant", "green", "terrace", "overhang", "pv"], extras=x_entrance),
    "villa": dict(floors=(1, 3), lot=((1800, 2600), (1600, 2400)), g=330, t=320, b=900, site=(400, 0.55),
                  forms=["cantilever", "L", "block", "stepped", "wedge"], skins={"punched": 2, "ribbon": 2, "screen": 2, "monolith": 2, "curtain": 1},
                  grounds=["glass", "solid", "glass"], roofs=["overhang", "green", "terrace", "plant"], solids=["white", "white", "concrete", "timber", "dark_timber", "charcoal", "stone", "sand", "black"],
                  extras=x_villa),
    "townhouse": dict(floors=(3, 4), lot=((600, 900), (1200, 1600)), g=320, t=300, b=1200, party=True,
                      forms=["block"], skins={"punched": 4, "screen": 2, "monolith": 1}, grounds=["glass", "solid"], roofs=["terrace", "green", "plant"]),
    "shophouse": dict(floors=(3, 5), lot=((600, 1000), (1200, 1800)), g=420, t=310, b=1200, party=True,
                      forms=["block"], skins={"punched": 3, "screen": 2, "louvres": 1, "tiles": 1}, grounds=["shops"], roofs=["terrace", "plant"]),
    "office": dict(floors=(5, 12), lot=((2800, 4800), (2200, 3600)), g=500, t=380, b=1700,
                   forms=["block", "notched", "stepped", "H", "L", "cantilever", "bridge", "curve", "wedge", "setback", "round"],
                   skins={"curtain": 3, "fins": 3, "grid": 2, "louvres": 2, "ribbon": 2, "diagrid": 1, "screen": 1, "tiles": 1},
                   grounds=["lobby", "lobby", "glass", "shops"], roofs=["plant", "pv", "green", "terrace"], crowns=["none", "none", "glow", "fins_up", "lantern"], extras=x_plaza),
    "loft": dict(floors=(3, 6), lot=((2400, 4000), (1800, 3000)), g=450, t=400, b=1400,
                 forms=["block", "L", "courtyard", "stepped", "notched"], skins={"punched": 3, "ribbed": 1, "grid": 2, "tiles": 1},
                 grounds=["glass", "shops", "solid"], roofs=["plant", "pv", "terrace"], solids=["brick", "dark_brick", "charcoal", "black", "concrete", "corten"]),
    "office_tower": dict(floors=(14, 40), lot=((3000, 4600), (3000, 4600)), g=550, t=390, b=1800, site=(900, 1.0),
                         forms=["podium_tower", "twin", "setback", "tower", "tower"], skins={"curtain": 4, "fins": 3, "diagrid": 2, "grid": 2, "louvres": 2},
                         grounds=["lobby"], roofs=["plant"], crowns=["fins_up", "glow", "spire", "lantern", "slope", "none"], extras=x_plaza),
    "school": dict(floors=(2, 3), lot=((6000, 9000), (7000, 10000)), g=420, t=380, b=1150, site=(1800, 0.5),
                   forms=["L", "U", "courtyard", "H", "campus", "curve", "bar"], skins={"ribbon": 3, "punched": 3, "fins": 2, "screen": 2, "tiles": 2},
                   grounds=["glass", "solid"], roofs=["pv", "green", "plant", "overhang"], extras=x_school,
                   solids=["white", "terracotta", "timber", "brick", "sage", "light_grey", "sand", "concrete"]),
    "kindergarten": dict(floors=(1, 2), lot=((3000, 4500), (3500, 5000)), g=360, t=360, b=1000, site=(800, 0.55),
                         forms=["L", "block", "campus", "curve", "cantilever"], skins={"punched": 3, "screen": 3, "tiles": 2, "ribbon": 1},
                         grounds=["glass", "solid"], roofs=["green", "overhang", "pv"], extras=x_kindergarten,
                         solids=["white", "timber", "terracotta", "sage", "sand", "navy"]),
    "university": dict(floors=(4, 7), lot=((6000, 9000), (5000, 8000)), g=480, t=400, b=1500, site=(1600, 1.0),
                       forms=["courtyard", "H", "U", "campus", "bridge", "curve", "cantilever", "podium_tower"],
                       skins={"grid": 2, "fins": 2, "curtain": 2, "ribbon": 2, "screen": 1, "monolith": 1}, grounds=["lobby", "glass"],
                       roofs=["green", "pv", "plant", "terrace"], extras=x_campus),
    "hospital": dict(floors=(6, 12), lot=((7000, 10000), (6000, 9000)), g=480, t=420, b=1800, site=(2800, 1.0),
                     forms=["podium_tower", "twin", "H", "campus"], skins={"ribbon": 3, "grid": 2, "curtain": 2, "punched": 2, "fins": 1, "louvres": 1},
                     grounds=["lobby", "glass"], roofs=["plant"], extras=x_hospital, solids=["white", "white", "light_grey", "sand", "concrete", "stone"]),
    "clinic": dict(floors=(2, 3), lot=((2500, 4000), (2500, 3500)), g=420, t=380, b=1400, site=(500, 1.0),
                   forms=["block", "L", "notched", "curve", "cantilever"], skins={"ribbon": 2, "punched": 2, "fins": 2, "screen": 1, "curtain": 1},
                   grounds=["glass", "lobby"], roofs=["plant", "green", "pv"], extras=x_clinic, solids=["white", "light_grey", "sand", "timber", "sage"]),
    "business_park": dict(floors=(3, 5), lot=((5000, 8000), (6000, 9000)), g=450, t=380, b=1500, site=(2200, 1.0),
                          forms=["H", "L", "U", "campus", "bridge", "curve", "cantilever", "stepped"], skins={"curtain": 3, "fins": 2, "ribbon": 2, "louvres": 2, "grid": 1},
                          grounds=["lobby", "glass"], roofs=["pv", "green", "plant"], extras=x_parkfront),
    "bank": dict(floors=(2, 6), lot=((2200, 3200), (2000, 3000)), g=520, t=400, b=1600, site=(500, 1.0),
                 forms=["block", "notched", "round", "wedge"], skins={"monolith": 2, "grid": 2, "curtain": 2, "fins": 2}, grounds=["lobby", "glass"],
                 roofs=["plant"], extras=x_plaza, solids=["stone", "sand", "black", "charcoal", "bronze", "white"]),
    "hotel": dict(floors=(8, 20), lot=((3500, 5000), (3500, 5000)), g=540, t=320, b=1600, site=(1100, 1.0),
                  forms=["podium_tower", "bar", "curve", "setback", "tower", "L"], skins={"balconies": 4, "fins": 2, "curtain": 2, "louvres": 1, "screen": 1, "punched": 1},
                  grounds=["lobby"], roofs=["terrace", "plant"], crowns=["none", "glow", "lantern", "fins_up"], extras=x_hotel),
    "mall": dict(floors=(2, 3), lot=((9000, 14000), (8000, 12000)), g=600, t=550, b=6000, site=(3500, 1.0),
                 forms=["mall"], skins={"tiles": 3, "monolith": 2, "ribbed": 1}, grounds=["solid", "shops"], roofs=["plant", "pv"], extras=x_mall),
    "supermarket": dict(floors=(1, 1), lot=((4000, 6000), (5000, 7000)), g=650, t=600, b=4000, site=(2600, 1.0),
                        forms=["block"], skins={"tiles": 2, "ribbed": 1, "monolith": 1}, grounds=["shops"], roofs=["pv", "plant"], extras=x_mall),
    "government": dict(floors=(3, 5), lot=((6000, 9000), (5000, 7000)), g=520, t=420, b=1500, site=(3000, 1.0),
                       forms=["block", "U", "H", "courtyard", "round"], skins={"grid": 3, "fins": 2, "monolith": 2, "curtain": 1}, grounds=["lobby"],
                       roofs=["plant", "green"], extras=x_government, solids=["white", "stone", "sand", "concrete", "light_grey"]),
    "museum": dict(floors=(2, 4), lot=((5000, 8000), (4000, 6000)), g=600, t=520, b=1600, site=(2400, 1.0),
                   forms=["cantilever", "wedge", "round", "stepped", "curve", "notched"], skins={"monolith": 4, "tiles": 1, "screen": 1, "curtain": 1},
                   grounds=["glass", "solid"], roofs=["plant", "green", "overhang"], extras=x_plaza, solids=["white", "corten", "stone", "black", "copper", "concrete", "aluminium"]),
    "police_station": dict(floors=(2, 4), lot=((3000, 5000), (3000, 4500)), g=420, t=380, b=1500, site=(900, 1.0),
                           forms=["block", "L", "notched", "cantilever"], skins={"punched": 2, "ribbon": 2, "grid": 1, "fins": 1}, grounds=["lobby", "glass"],
                           roofs=["plant", "pv"], extras=x_police, solids=["white", "light_grey", "concrete", "navy", "charcoal"]),
    "fire_station": dict(floors=(2, 3), lot=((3200, 4500), (2600, 4000)), g=560, t=360, b=1800, site=(900, 1.0),
                         forms=["block", "L"], skins={"punched": 2, "ribbed": 1, "tiles": 1, "ribbon": 1}, grounds=["solid"], roofs=["plant", "pv"], extras=x_fire,
                         solids=["brick", "dark_brick", "charcoal", "concrete", "white", "corten"]),
    "mosque": dict(floors=(1, 1), lot=((2600, 4200), (2600, 4200)), g=900, t=900, b=3000, site=(1200, 1.0),
                   forms=["block"], skins={"monolith": 1, "punched": 1}, grounds=["solid"], roofs=["none"], extras=x_mosque,
                   solids=["white", "sand", "stone"]),
    "church": dict(floors=(1, 1), lot=((2000, 2800), (3500, 5000)), g=1000, t=1000, b=2000, site=(600, 1.0),
                   forms=["block", "wedge"], skins={"monolith": 2, "screen": 1}, grounds=["solid"], roofs=["none", "overhang"], extras=x_church,
                   solids=["white", "concrete", "brick", "stone"]),
    "warehouse": dict(floors=(1, 1), lot=((6000, 12000), (5000, 9000)), g=1100, t=1100, b=9000, site=(2600, 1.0),
                      forms=["block"], skins={"ribbed": 3, "tiles": 1}, grounds=["solid"], roofs=["pv", "plant"], extras=x_warehouse,
                      solids=["aluminium", "light_grey", "charcoal", "navy", "white", "sage"]),
    "factory": dict(floors=(1, 1), lot=((6000, 10000), (5000, 8000)), g=1000, t=1000, b=8000, site=(1500, 1.0),
                    forms=["block", "L"], skins={"ribbed": 3, "tiles": 1}, grounds=["solid"], roofs=["barrel", "pv", "plant"], extras=x_factory,
                    solids=["aluminium", "light_grey", "charcoal", "corten", "white", "navy"]),
    "parking_garage": dict(floors=(3, 6), lot=((3500, 5000), (3000, 4500)), g=330, t=310, b=4000, site=(500, 1.0),
                           forms=["block"], skins={"open_deck": 1}, grounds=["none"], roofs=["none"], extras=x_parking,
                           solids=["concrete", "light_grey", "white", "warm_grey"]),
    "petrol_station": dict(floors=(1, 1), lot=((3500, 5000), (3500, 5000)), g=400, t=400, b=1200, site=(2200, 0.35),
                           forms=["block"], skins={"tiles": 1, "ribbon": 1, "curtain": 1}, grounds=["glass"], roofs=["plant"], extras=x_petrol),
}


def f_mall(w, d, n, rng, b, R):
    V = [_vol(R(-w / 2, 600, w / 2, d), 0, n, "main")]
    for k in range(rng.choice((1, 2, 3))):
        px = (k - 1) * w / 3 if rng.random() < 0.7 else rng.uniform(-w / 3, w / 3)
        pw = rng.uniform(800, 1300)
        sh = rng.choice(("rect", "round"))
        p = ellipse(px, 700, pw / 2, 650, 24) if sh == "round" else rect(px - pw / 2, 0, px + pw / 2, 700)
        V.append(_vol(p, 0, n, "entry", skin="curtain"))
    return V


FORMS["mall"] = f_mall
TYPES = list(KINDS)


def _pick(rng, weights):
    items = list(weights.items()) if isinstance(weights, dict) else [(x, 1) for x in weights]
    t = rng.uniform(0, sum(w for _, w in items))
    for k, w in items:
        t -= w
        if t <= 0:
            return k
    return items[-1][0]


def _fit_floors(V, n):
    """Exact storeys: scale the composition so its tallest point is n storeys."""
    top = max(v["f0"] + v["n"] for v in V)
    if top == n:
        return V
    for v in V:
        f0 = int(round(v["f0"] * n / top))
        t = max(f0 + 1, int(round((v["f0"] + v["n"]) * n / top)))
        v["f0"], v["n"] = min(f0, n - 1), min(t, n) - min(f0, n - 1)
    tallest = max(V, key=lambda v: v["f0"] + v["n"])
    tallest["n"] = n - tallest["f0"]
    return V


def building(kind="office", seed=None, width=None, depth=None, floors=None, polygon=None, form=None, skin=None, detail="medium",
             site=True, street=None):
    """A contemporary building of a type in KINDS (apartment, villa, townhouse, shophouse, office, loft, office_tower, school,
    kindergarten, university, hospital, clinic, business_park, bank, hotel, mall, supermarket, government, museum,
    police_station, fire_station, mosque, church, warehouse, factory, parking_garage, petrol_station).
    Lot: width x depth (cm), front edge on y = 0 facing -y, centred on x = 0; the type's site elements (sports field,
    parking, canopies, helipad, domes, pumps) fill the rest of the lot (site=False: the building alone).
    polygon: build on this footprint instead (world cm, counter-clockwise or not); parts come back in world coordinates.
    form: a composition in FORMS; skin: a facade system in SKINS (both picked from the type by the seed otherwise).
    floors: exact storeys. detail: high | medium | low. seed: same seed, same building."""
    if kind not in KINDS:
        raise ValueError(f"unknown kind {kind!r}; one of {', '.join(KINDS)}")
    global PROPS
    outer, PROPS = PROPS, []
    try:
        return _building(kind, seed, width, depth, floors, polygon, form, skin, detail, site, street)
    finally:
        LAST["props"] = PROPS
        PROPS = outer


def _building(kind, seed, width, depth, floors, polygon, form, skin, detail, site, street):
    K = KINDS[kind]
    rng = random.Random(seed)
    lod = {"high": 2, "medium": 1, "low": 0, "mass": 0}.get(detail, 1) if isinstance(detail, str) else int(detail)
    n = int(floors) if floors else rng.randint(*K["floors"])
    sk = skin or _pick(rng, K["skins"])
    pal = palette(rng, sk, K.get("solids"))
    party = K.get("party")
    if party:
        pal["open"] = [(0, -1), (0, 1)]                       # row houses: windows front and back only
    g, t = K["g"], K["t"]
    Z = lambda f: 0 if f <= 0 else g + (f - 1) * t
    frame = None
    if polygon is not None:
        P0 = ccw(polygon)
        V = f_polygon(P0, n, rng, kind)
        x0, y0, x1, y1 = bbox(P0)
        w, d = x1 - x0, y1 - y0
        site = False
    else:
        w = width or rng.uniform(*K["lot"][0])
        d = depth or rng.uniform(*K["lot"][1])
        sb, frac = K.get("site", (0, 1.0)) if site else (0, 1.0)
        sb = min(sb, d * 0.45)
        bd = max(600, (d - sb) * frac) if site else d
        corners = rng.choice([(0, 0), (0, 0), (0, 0), (0.18, 0), (0.3, 0), (0, 0.15)]) if kind not in ("warehouse", "factory", "parking_garage", "townhouse", "shophouse") else (0, 0)
        R = lambda x0, y0, x1, y1: rect(x0, y0, x1, y1, r=corners[0] * min(abs(x1 - x0), abs(y1 - y0)), cham=corners[1] * min(abs(x1 - x0), abs(y1 - y0)))
        fm = form or rng.choice(K["forms"])
        V = FORMS[fm](w, bd, n, rng, min(K["b"], bd), R)
        for v in V:
            v["p"] = [(x, y + sb) for x, y in v["p"]]
            v["y0"] += sb
            v["y1"] += sb
    V = _fit_floors(V, n)
    gk = rng.choice(K["grounds"])
    rk = rng.choice(K["roofs"])
    ck = rng.choice(K.get("crowns", ["none"]))
    P = []
    second = rng.random() < 0.6
    for i, v in enumerate(V):
        zs = [Z(f) for f in range(v["f0"], v["f0"] + v["n"])]
        top = Z(v["f0"] + v["n"])
        vs = v["skin"] or sk
        vp = pal
        if v["role"] == "podium" and second:                   # podiums in the second material and system
            vs = rng.choice(["ribbon", "tiles", "monolith", "grid", "punched"])
            vp = dict(pal, main=pal["second"])
        if v["f0"] == 0 and gk not in ("none", "solid") and v["role"] not in ("entry",) and len(zs) > 1:
            P += ground(v["p"], 0, zs[1], gk, vp, rng, lod)
            zs = zs[1:]
        elif v["f0"] > 0 and not any(u is not v and u["f0"] + u["n"] >= v["f0"] and u["f0"] < v["f0"] for u in V):
            P.append(slab(v["p"], zs[0] - 30, 30, pal["slab"]))   # soffit of a cantilever or bridge
        if v.get("plates"):
            P += plates(v, zs, top, dict(pal, plate_balcony=kind in ("apartment", "hotel") or rng.random() < 0.3), rng, lod,
                        twist=v["plates"].get("twist", 0), taper=v["plates"].get("taper", 1))
            p_top = v["p_top"]
        else:
            P += SKINS[vs](v["p"], zs, top, vp, rng, lod, party)
            p_top = v["p"]
        if rk != "none":
            covered = any(u is not v and u["f0"] == v["f0"] + v["n"] for u in V)
            P += roof(p_top, top, "plant" if covered else rk, pal, rng, lod)
        if v["role"] == "tower" or (len(V) == 1 and n >= 10):
            P += crown(dict(v, p=p_top), top + 45, ck, pal, rng, lod)
    main = min((v for v in V if v["f0"] == 0), key=lambda v: v["y0"], default=None)
    if main is not None and kind not in ("warehouse", "factory", "parking_garage", "mosque", "petrol_station"):
        pts = walk(main["p"], 200, normals=[(0, -1)])
        if pts:
            e = pts[len(pts) // 2]
            dw = rng.choice((240, 300, 360))
            hh = min(Z(1) - 40, 330)
            P += [member(e, dw + 40, 30, hh + 20, 0, pal["frame"], off=0), member(e, dw, 36, hh, 0, _tone(GLASS["dark"], 1.6), off=0),
                  member(e, 6, 40, hh, 0, pal["frame"], off=0),
                  member(e, dw + rng.choice((200, 400, 700)), rng.choice((180, 260, 360)), 22, hh + 30, rng.choice([pal["slab"], pal["frame"], SOLID["aluminium"]]))]
    if site and kind not in ("warehouse", "factory", "parking_garage", "petrol_station", "mall", "supermarket", "townhouse", "shophouse"):
        P += landscape({"V": V, "w": w, "d": d, "rng": rng})
    if site and K.get("extras"):
        P += K["extras"]({"V": V, "Z": Z, "w": w, "d": d, "rng": rng, "pal": pal, "n": n, "detail": detail})
    props = PROPS
    LAST.clear()
    LAST.update(props=props, kind=kind, form=(form or (fm if polygon is None else "polygon")), skin=sk, ground=gk, roof=rk, crown=ck, storeys=max(v["f0"] + v["n"] for v in V),
                volumes=[{k: v[k] for k in ("f0", "n", "role", "x0", "y0", "x1", "y1")} for v in V], palette=pal["names"])
    return P


def plant(props=None, at=(0, 0, 0), yaw=0.0, name="Plants"):
    """Places a building's real meshes (LAST["props"] by default), moved like its parts (at, yaw), as GPU instances."""
    from . import assets
    props = LAST.get("props", []) if props is None else props
    if not props:
        return []
    a = math.radians(yaw)
    ca, sa = math.cos(a), math.sin(a)
    ins = assets.Instancer(name, collision=False)
    for key, x, y, z, r, sc in props:
        var = assets.variants(key)
        k = int(abs(x * 7 + y * 13)) % len(var)
        ins.add(var[k], (at[0] + x * ca - y * sa, at[1] + x * sa + y * ca, at[2] + z), yaw=r + yaw, scale=sc)
    return ins.flush()


def place(kind="office", at=(0, 0, 0), yaw=0.0, name=None, **kw):
    """Builds a building (see building()), places it at `at` turned by yaw, and plants its trees, shrubs and benches.
    Returns {"node", "plants", "parts", "info"}."""
    import unreal_kit as uk
    from .parts import transform
    P = building(kind, **kw)
    info = dict(LAST)
    node = uk.place_many([(name or kind.title(), transform(P, at, yaw) if yaw else P, at if not yaw else (0, 0, 0))])[0]
    return {"node": node, "plants": plant(info.get("props"), at, yaw, (name or kind.title()) + "_Plants"), "parts": len(P), "info": info}
