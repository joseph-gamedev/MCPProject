"""
A city district where every building is different: 8 archetypes (glass towers with setbacks, twisted towers, round towers,
art deco towers, brick apartments, concrete-frame offices, townhouse rows, shop podiums) and per-building random
dimensions, floor counts, materials, window rhythm, crowns and rooftop clutter. Streets, sidewalks, lights and trees.

    python Scripts/examples/city.py [--seed 3] [--blocks 5] [--out city.jpg] [--clear]
"""

import argparse
import math
import os
import random
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from rtbuild import *  # noqa: E402,F403

BLOCK, STREET, WALK = 6000, 1800, 350
PITCH = BLOCK + STREET

BRICKS = [[0.33, 0.11, 0.07], [0.42, 0.19, 0.11], [0.26, 0.13, 0.09], [0.47, 0.3, 0.2], [0.2, 0.18, 0.17], [0.38, 0.24, 0.16]]
PLASTERS = [[0.72, 0.65, 0.52], [0.6, 0.47, 0.37], [0.5, 0.56, 0.58], [0.78, 0.74, 0.68], [0.58, 0.4, 0.28], [0.46, 0.52, 0.42], [0.7, 0.58, 0.44]]
STONES = [[0.55, 0.5, 0.42], [0.62, 0.6, 0.55], [0.44, 0.41, 0.37], [0.6, 0.53, 0.44]]
GLASSES = [[0.03, 0.05, 0.07], [0.02, 0.055, 0.055], [0.045, 0.045, 0.055], [0.06, 0.075, 0.09], [0.04, 0.05, 0.035], [0.07, 0.06, 0.05]]
METALS = [[0.55, 0.56, 0.58], [0.25, 0.26, 0.28], [0.6, 0.5, 0.35], [0.12, 0.12, 0.13]]

FACES = [((0, -1), (1, 0), 0), ((1, 0), (0, 1), 90), ((0, 1), (-1, 0), 180), ((-1, 0), (0, -1), 270)]


def faces(cx, cy, w, d):
    for (nx, ny), t, yaw in FACES:
        half, length = (d / 2, w) if ny else (w / 2, d)
        yield (cx + nx * half, cy + ny * half), (nx, ny), t, yaw, length


def on_face(c, n, t, u, off, z):
    return [c[0] + t[0] * u + n[0] * off, c[1] + t[1] * u + n[1] * off, z]


def glass(r):
    return mat("black", color=r.choice(GLASSES), roughness=0.04, metallic=0.75)


def metal(r):
    return mat("steel", color=r.choice(METALS), roughness=r.uniform(0.25, 0.5))


def punched_windows(P, r, cx, cy, w, d, z0, floors, fh, spacing, win_w, win_h, gl, sill=None, skip_ground=False):
    for c, n, t, yaw, length in faces(cx, cy, w, d):
        cols = max(1, int(length / spacing))
        step = length / cols
        for f in range(1 if skip_ground else 0, floors):
            z = z0 + f * fh + (fh - win_h) * 0.55
            for k in range(cols):
                u = -length / 2 + step * (k + 0.5)
                P += box([win_w, 10, win_h], at=on_face(c, n, t, u, 1, z), rot=[0, yaw, 0], **gl)
                if sill:
                    P += box([win_w + 24, 18, 7], at=on_face(c, n, t, u, 6, z - 7), rot=[0, yaw, 0], **sill)


def floor_bands(P, cx, cy, w, d, z0, floors, fh, band_h, out, m):
    for f in range(1, floors + 1):
        P += box([w + 2 * out, d + 2 * out, band_h], at=[cx, cy, z0 + f * fh - band_h], **m)


def mullions(P, cx, cy, w, d, z0, h, spacing, size, m):
    for c, n, t, yaw, length in faces(cx, cy, w, d):
        cols = max(1, int(length / spacing))
        for k in range(cols + 1):
            u = -length / 2 + length * k / cols
            P += box([size, size, h], at=on_face(c, n, t, u, size / 2, z0), rot=[0, yaw, 0], **m)


def roof_clutter(P, r, cx, cy, w, d, z):
    for _ in range(r.randint(1, 4)):
        bw, bd = r.uniform(150, min(600, w * 0.4)), r.uniform(150, min(500, d * 0.4))
        x, y = cx + r.uniform(-(w - bw) / 2, (w - bw) / 2) * 0.8, cy + r.uniform(-(d - bd) / 2, (d - bd) / 2) * 0.8
        P += box([bw, bd, r.uniform(120, 300)], at=[x, y, z], bevel=4, **mat("concrete", color=[0.5, 0.5, 0.48]))
    if r.random() < 0.35:  # water tank
        x, y = cx + w * 0.25, cy - d * 0.2
        for px, py in [(-60, -60), (60, -60), (60, 60), (-60, 60)]:
            P += box([16, 16, 220], at=[x + px, y + py, z], **mat("iron"))
        P += cyl(260, 300, at=[x, y, z + 220], **mat("wood", color=[0.3, 0.2, 0.12]))
        P += cone(290, 110, at=[x, y, z + 520], **mat("slate_roof"))
    if r.random() < 0.3:
        P += cyl(12, r.uniform(400, 1200), at=[cx - w * 0.3, cy + d * 0.3, z], **mat("steel"))


# ------------------------------------------------------------------ archetypes (each returns Parts, z0 = ground)

def glass_tower(r, cx, cy, w, d, z0):
    P = Parts()
    fh = r.choice([360, 380, 400, 420])
    gl, mm = glass(r), metal(r)
    tiers = r.randint(1, 3)
    floors_total = r.randint(18, 42)
    z, cw, cd = z0, w, d
    for i in range(tiers):
        floors = floors_total // tiers + (floors_total % tiers if i == 0 else 0)
        h = floors * fh
        P += box([cw - 30, cd - 30, h], at=[cx, cy, z], **gl)
        mullions(P, cx, cy, cw - 30, cd - 30, z, h, r.choice([150, 200, 300]), r.choice([12, 18, 25]), mm)
        if r.random() < 0.7:
            floor_bands(P, cx, cy, cw - 30, cd - 30, z, floors, fh, r.choice([25, 40, 60]), 6, mm)
        z += h
        P += box([cw - 10, cd - 10, 60], at=[cx, cy, z], **mm)
        z += 60
        s = r.uniform(0.62, 0.85)
        cw, cd = cw * s, cd * s
    crown = r.random()
    if crown < 0.35:
        P += taper(min(cw, cd) * 0.12, 4, r.uniform(1500, 4000), sides=8, at=[cx, cy, z], **mat("steel"))
    elif crown < 0.65:
        P += box([cw * 0.8, cd * 0.8, 500], at=[cx, cy, z], **mm)
        mullions(P, cx, cy, cw * 0.8, cd * 0.8, z, 500, 120, 10, mm)
    else:
        roof_clutter(P, r, cx, cy, cw, cd, z)
    return P


def twisted_tower(r, cx, cy, w, d, z0):
    P = Parts()
    s = min(w, d) * 0.72
    floors, fh, twist = r.randint(28, 50), 380, r.uniform(0.9, 2.2) * r.choice([-1, 1])
    taper_k = r.uniform(0.55, 0.95)
    gl, slab = glass(r), mat("concrete", color=r.choice([[0.8, 0.8, 0.78], [0.6, 0.6, 0.58]]))
    rings = []
    for f in range(floors + 1):
        k = 1 - (1 - taper_k) * f / floors
        rings.append([rotate_z(p, twist * f, (cx, cy)) for p in rect(s * k, s * k, z0 + f * fh, (cx, cy))])
    P += loft(rings, smooth_angle=20, **gl)
    for f in range(1, floors + 1):
        ring0 = [lerp([cx, cy, p[2]], p, 1.04) for p in rings[f]]
        ring1 = [[p[0], p[1], p[2] + 35] for p in ring0]
        P += loft([[[p[0], p[1], p[2] - 35] for p in ring0], ring1], smooth_angle=20, **slab)
    top = rings[-1][0][2]
    P += cyl(s * taper_k * 0.4, 400, at=[cx, cy, top], **slab)
    return P


def round_tower(r, cx, cy, w, d, z0):
    P = Parts()
    dia = min(w, d) * r.uniform(0.7, 0.9)
    floors, fh = r.randint(16, 34), r.choice([360, 400])
    gl, band = glass(r), mat("concrete", color=r.choice([[0.75, 0.74, 0.7], [0.5, 0.5, 0.5], [0.85, 0.83, 0.8]]))
    P += cyl(dia, floors * fh, at=[cx, cy, z0], detail="high", **gl)
    for f in range(1, floors + 1):
        P += tube(dia + 80, 40, 60, at=[cx, cy, z0 + f * fh - 40], detail="high", **band)
    if r.random() < 0.5:
        for a in range(0, 360, r.choice([20, 30, 45])):  # vertical fins
            x, y = cx + (dia / 2 + 20) * math.cos(math.radians(a)), cy + (dia / 2 + 20) * math.sin(math.radians(a))
            P += box([20, 70, floors * fh], at=[x, y, z0], rot=[0, a, 0], **band)
    top = z0 + floors * fh
    P += dome(dia * 0.9, dia * r.uniform(0.15, 0.4), at=[cx, cy, top], **mat("steel", roughness=0.25)) if r.random() < 0.5 else cyl(dia * 0.5, 400, at=[cx, cy, top], **band)
    return P


def art_deco(r, cx, cy, w, d, z0):
    P = Parts()
    stone = mat("limestone", color=r.choice(STONES), style="blocks", tile=120, relief=0.8)
    dark = glass(r)
    fh = 380
    z, cw, cd = z0, w - 60, d - 60
    n_tiers = r.randint(3, 5)
    for i in range(n_tiers):
        floors = r.randint(10, 16) if i == 0 else r.randint(3, 8)
        h = floors * fh
        P += box([cw - 40, cd - 40, h], at=[cx, cy, z], **dark)
        mullions(P, cx, cy, cw - 40, cd - 40, z, h, r.choice([220, 260, 300]), r.choice([60, 80]), stone)
        floor_bands(P, cx, cy, cw - 40, cd - 40, z, floors, fh, 90, 10, stone)
        for c, n, t, yaw, length in faces(cx, cy, cw, cd):  # corner piers
            P += box([120, 120, h + 150], at=on_face(c, n, t, length / 2 - 60, -40, z), rot=[0, yaw, 0], **stone)
        z += h
        cw, cd = cw * r.uniform(0.7, 0.82), cd * r.uniform(0.7, 0.82)
    P += tiers(cw, cd, 900, count=4, taper=0.4, at=[cx, cy, z], **stone)
    P += taper(cw * 0.12, 3, r.uniform(1200, 2500), sides=8, at=[cx, cy, z + 900], **mat("steel", color=[0.75, 0.7, 0.6], roughness=0.2))
    return P


def brick_apartment(r, cx, cy, w, d, z0):
    P = Parts()
    floors, fh = r.randint(5, 9), r.choice([300, 320, 340])
    walls = mat("brick", color=r.choice(BRICKS), tile=r.choice([22, 24, 26]))
    trim = mat("limestone", color=r.choice(STONES))
    h = floors * fh
    P += box([w, d, h], at=[cx, cy, z0], **walls)
    P += box([w + 30, d + 30, fh * 0.35], at=[cx, cy, z0], **trim)  # base course
    punched_windows(P, r, cx, cy, w, d, z0, floors, fh, r.choice([280, 320, 360]), r.choice([110, 130]), fh * 0.55,
                    glass(r), sill=trim, skip_ground=r.random() < 0.5)
    P += box([w + 60, d + 60, 50], at=[cx, cy, z0 + h], bevel=8, **trim)  # cornice
    P += box([w + 20, d + 20, 90], at=[cx, cy, z0 + h + 50], **walls)  # parapet
    if r.random() < 0.6:  # balconies on the front
        rail = mat("iron")
        cols = int(w / 600)
        for f in range(2, floors, 2):
            for k in range(cols):
                x = cx - w / 2 + w * (k + 0.5) / cols
                P += box([260, 120, 18], at=[x, cy - d / 2 - 60, z0 + f * fh], **trim)
                P += box([260, 6, 100], at=[x, cy - d / 2 - 118, z0 + f * fh + 18], **rail)
    roof_clutter(P, r, cx, cy, w, d, z0 + h + 50)
    return P


def frame_office(r, cx, cy, w, d, z0):
    P = Parts()
    floors, fh = r.randint(6, 14), r.choice([380, 420])
    conc = mat("concrete", color=r.choice([[0.7, 0.69, 0.66], [0.55, 0.55, 0.53], [0.82, 0.8, 0.76]]))
    gl = glass(r)
    inset = 120
    P += box([w - 2 * inset, d - 2 * inset, floors * fh], at=[cx, cy, z0], **gl)
    shift = r.choice([0, 0, 300, -300])
    for f in range(1, floors + 1):
        sx = shift if f > floors * 0.6 else 0
        P += box([w + 40, d + 40, 45], at=[cx + sx, cy, z0 + f * fh - 45], **conc)
    nx, ny = max(2, int(w / 700)), max(2, int(d / 700))
    for i in range(nx + 1):
        for j in range(ny + 1):
            if 0 < i < nx and 0 < j < ny:
                continue
            x, y = cx - w / 2 + w * i / nx, cy - d / 2 + d * j / ny
            P += box([45, 45, floors * fh], at=[x, y, z0], **conc)
    if r.random() < 0.5:  # brise-soleil fins
        for c, n, t, yaw, length in faces(cx, cy, w, d):
            for k in range(int(length / 120)):
                u = -length / 2 + 60 + k * 120
                P += box([8, 60, floors * fh * 0.95], at=on_face(c, n, t, u, 40, z0 + fh), rot=[0, yaw, 0], **metal(r))
    roof_clutter(P, r, cx, cy, w, d, z0 + floors * fh)
    return P


def townhouses(r, cx, cy, w, d, z0):
    """A row of narrow houses along the lot's long side, each with its own colour, height, roof and windows."""
    P = Parts()
    along_x = w >= d
    length, depth = (w, d) if along_x else (d, w)
    depth = min(depth, 1400)
    u = -length / 2
    while u < length / 2 - 400:
        hw = min(r.uniform(500, 850), length / 2 - u)
        floors, fh = r.randint(2, 4), r.choice([290, 310, 330])
        h = floors * fh
        mc = mat(r.choice(["plaster", "brick", "plaster"]), color=r.choice(PLASTERS + BRICKS))
        trim = mat("plaster", color=[0.85, 0.83, 0.78])
        hx, hy = (cx + u + hw / 2, cy) if along_x else (cx, cy + u + hw / 2)
        bw, bd = (hw - 10, depth) if along_x else (depth, hw - 10)
        P += box([bw, bd, h], at=[hx, hy, z0], **mc)
        P += box([bw + 20, bd + 20, 30], at=[hx, hy, z0 + h], **trim)
        roof_h = r.uniform(200, 380)
        roof_m = mat(r.choice(["roof_tile", "slate_roof", "terracotta"]))
        if along_x:
            P += roof(bd + 60, bw + 10, roof_h, at=[hx, hy, z0 + h + 30], rot=[0, 90, 0], **roof_m)
        else:
            P += roof(bw + 60, bd + 10, roof_h, at=[hx, hy, z0 + h + 30], **roof_m)
        punched_windows(P, r, hx, hy, bw, bd, z0, floors, fh, r.choice([170, 200, 230]), r.choice([80, 95]), fh * 0.5, glass(r), sill=trim)
        door_face = (hx, hy - bd / 2) if along_x else (hx - bw / 2, hy)
        P += box([110, 20, 230], at=[door_face[0], door_face[1], z0], rot=[0, 0 if along_x else 90, 0], **mat("wood", color=r.choice([[0.08, 0.15, 0.1], [0.3, 0.05, 0.04], [0.05, 0.06, 0.12], [0.2, 0.12, 0.06]])))
        if r.random() < 0.7:
            P += box([60, 90, roof_h + 150], at=[hx + bw * 0.3, hy + bd * 0.2, z0 + h], **mat("brick", color=r.choice(BRICKS)))
        u += hw
    return P


def shop_podium(r, cx, cy, w, d, z0):
    """Low retail building: tall glass ground floor, coloured awnings, sign bands, a roof garden or terrace."""
    P = Parts()
    h1, floors = 480, r.randint(1, 3)
    fh = 350
    walls = mat(r.choice(["plaster", "concrete", "brick"]), color=r.choice(PLASTERS))
    P += box([w - 40, d - 40, h1], at=[cx, cy, z0], **glass(r))
    P += box([w, d, floors * fh], at=[cx, cy, z0 + h1], **walls)
    punched_windows(P, r, cx, cy, w, d, z0 + h1, floors, fh, 400, 220, 180, glass(r))
    awning = mat("plaster", color=r.choice([[0.5, 0.05, 0.04], [0.05, 0.25, 0.1], [0.05, 0.1, 0.35], [0.6, 0.45, 0.05], [0.15, 0.15, 0.15]]), roughness=0.8)
    for c, n, t, yaw, length in faces(cx, cy, w, d):
        if r.random() < 0.6:
            for k in range(int(length / 500)):
                uu = -length / 2 + 250 + k * 500
                P += box([440, 160, 30], at=on_face(c, n, t, uu, 80, z0 + h1 - 100), rot=[-12, yaw, 0], **awning)
        P += box([length, 30, 90], at=on_face(c, n, t, 0, 15, z0 + h1 - 90), rot=[0, yaw, 0], **mat("black", emissive=r.choice([[1, 0.6, 0.2], [0.3, 0.6, 1], [1, 0.2, 0.3]]), emissive_strength=2))
    top = z0 + h1 + floors * fh
    if r.random() < 0.5:
        P += box([w - 200, d - 200, 40], at=[cx, cy, top], **mat("grass"))
        for _ in range(r.randint(3, 8)):
            x, y = cx + r.uniform(-w / 3, w / 3), cy + r.uniform(-d / 3, d / 3)
            P += sphere(r.uniform(200, 350), at=[x, y, top + 40], **mat("grass", color=[0.04, 0.1, 0.03], relief=4, tile=40))
    else:
        roof_clutter(P, r, cx, cy, w, d, top)
    return P


ARCHETYPES = {
    "glass_tower": (glass_tower, 3.0),
    "twisted_tower": (twisted_tower, 0.6),
    "round_tower": (round_tower, 0.8),
    "art_deco": (art_deco, 1.4),
    "brick_apartment": (brick_apartment, 2.0),
    "frame_office": (frame_office, 1.6),
    "townhouses": (townhouses, 1.5),
    "shop_podium": (shop_podium, 1.3),
}
TALL = {"glass_tower", "twisted_tower", "round_tower", "art_deco"}


def pick(r, centrality, used):
    names, weights = [], []
    for name, (_, wgt) in ARCHETYPES.items():
        tall_bias = (0.3 + 2.5 * centrality) if name in TALL else (1.6 - centrality)
        weights.append(wgt * max(0.05, tall_bias) / (1 + 2 * used.get(name, 0) / 10))
        names.append(name)
    return r.choices(names, weights)[0]


def streets(n):
    P = Parts()
    span = n * PITCH
    P += box([400000, 400000, 20], at=[0, 0, -20], **mat("asphalt", color=[0.06, 0.06, 0.065], tile=60))
    dash = mat("white", color=[0.75, 0.72, 0.6], roughness=0.6)
    for i in range(n + 1):
        c = -span / 2 + i * PITCH - STREET / 2
        for k in range(int(span / 600)):
            t = -span / 2 + k * 600
            P += box([300, 12, 2], at=[t, c, 0], **dash)
            P += box([12, 300, 2], at=[c, t, 0], **dash)
    return P


def block_dressing(r, bx, by):
    """Sidewalk slab, street lights and street trees around one block."""
    P = Parts()
    P += box([BLOCK, BLOCK, 18], at=[bx, by, 0], bevel=4, **mat("concrete", color=[0.5, 0.49, 0.47], style="concrete", tile=150))
    pole, head = mat("painted_iron", color=[0.05, 0.05, 0.05]), mat("light", emissive=[1, 0.85, 0.6], emissive_strength=8)
    leaves = mat("grass", color=[0.035, 0.08, 0.03], relief=5, tile=40)
    for c, n, t, yaw, length in faces(bx, by, BLOCK, BLOCK):
        for k in range(1, int(length / 1500)):
            u = -length / 2 + k * 1500
            p = on_face(c, n, t, u, -80, 18)
            if k % 2:
                P += cyl(22, 750, at=p, **pole)
                P += beam([p[0], p[1], p[2] + 740], [p[0] + n[0] * 200, p[1] + n[1] * 200, p[2] + 740], 12, **pole)
                P += box([60, 30, 15], at=[p[0] + n[0] * 200, p[1] + n[1] * 200, p[2] + 720], rot=[0, yaw, 0], **head)
            elif r.random() < 0.8:
                P += cyl(35, 300, at=p, **mat("wood", color=[0.1, 0.07, 0.05]))
                P += sphere(r.uniform(380, 520), at=[p[0], p[1], p[2] + 230], scale=[1, 1, 1.2], **leaves)
    return P


def lots(r, bx, by):
    """Split the buildable part of a block into 1-4 lots: (cx, cy, w, d)."""
    inner = BLOCK - 2 * WALK
    mode = r.choice(["one", "two", "two", "four", "three"])
    if mode == "one":
        return [(bx, by, inner, inner)]
    if mode == "two":
        s = r.uniform(0.35, 0.65)
        a = inner * s
        if r.random() < 0.5:
            return [(bx - inner / 2 + a / 2, by, a, inner), (bx + a / 2, by, inner - a, inner)]
        return [(bx, by - inner / 2 + a / 2, inner, a), (bx, by + a / 2, inner, inner - a)]
    if mode == "three":
        a = inner * r.uniform(0.3, 0.45)
        return [(bx - inner / 2 + a / 2, by, a, inner), (bx + a / 2, by - inner / 4, inner - a, inner / 2), (bx + a / 2, by + inner / 4, inner - a, inner / 2)]
    h = inner / 2
    return [(bx - h / 2, by - h / 2, h, h), (bx + h / 2, by - h / 2, h, h), (bx - h / 2, by + h / 2, h, h), (bx + h / 2, by + h / 2, h, h)]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://127.0.0.1:8018/mcp")
    ap.add_argument("--seed", type=int, default=3)
    ap.add_argument("--blocks", type=int, default=5)
    ap.add_argument("--out", default=None)
    ap.add_argument("--clear", action="store_true")
    args = ap.parse_args()
    r = random.Random(args.seed)
    rt = Runtime(args.url)
    if args.clear:
        rt.call("scene.clear_all", confirm=True)
    t0 = time.time()
    city = rt.group("City")
    rt.build("Streets", streets(args.blocks), parent=city)
    n, used, total = args.blocks, {}, 0
    for i in range(n):
        for j in range(n):
            bx, by = (i - (n - 1) / 2) * PITCH, (j - (n - 1) / 2) * PITCH
            centrality = 1 - math.hypot(i - (n - 1) / 2, j - (n - 1) / 2) / (n * 0.71)
            parts = block_dressing(r, bx, by)
            for (cx, cy, w, d) in lots(r, bx, by):
                kind = pick(r, centrality, used)
                used[kind] = used.get(kind, 0) + 1
                margin = r.uniform(80, 250)
                parts += ARCHETYPES[kind][0](r, cx, cy, w - 2 * margin, d - 2 * margin, 18)
            total += len(parts)
            rt.build(f"Block_{i}_{j}", parts, parent=city)
    print(f"{total} parts in {time.time() - t0:.1f}s; buildings by type: {used}")
    rt.call("lighting.apply_mood", mood="morning")
    rt.call("lighting.set_sun", rotation=[-38, 35, 0], temperature=5600)
    rt.call("camera.set_camera", location=[-n * PITCH * 0.75, -n * PITCH * 0.95, n * PITCH * 0.42], look_at=[0, 0, 2500], fov=55)
    if args.out:
        rt.call("ops.wait", seconds=3)
        rt.screenshot(args.out)
        print("saved", args.out)


if __name__ == "__main__":
    main()
