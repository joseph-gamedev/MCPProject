"""EXAMPLE / WORKFLOW: rebuild a real place from OpenStreetMap data (real roads, real building footprints, coast, water,
parks), styled by a recipe, and planted with PCG only where the ground is open.

    import unreal_kit as uk, osm_town
    data = osm_town.load(r"<path>/fortkochi.json")          # Overpass JSON with `out geom` (ways carry their geometry)
    osm_town.build(data, osm_town.FORT_KOCHI, centre=(9.9655, 76.2425), half=45000)

Steps (each is reusable on its own):
  1. project lat/lon to cm around a centre (X = north, Y = east, so the map is not mirrored in Unreal)
  2. terrain (build.create_terrain): land, the sea and water bodies lowered from the coastline / water polygons
  3. roads from highway ways (width by class), joints at bends
  4. buildings on their real footprints: walls extruded from the outline, roof on the oriented bounding box
     (hip tile roof or flat with parapet), windows with shutters on rectangular buildings; churches and landmarks by tag
  5. PCG planting with surface=terrain: palms, yard trees, street trees, ground cover only where the ground is open
     (never on roofs, roads or in the water); beaches kept clear
Data: (c) OpenStreetMap contributors, ODbL.
"""

import json
import math
import random

import unreal_kit as uk
from unreal_kit.parts import M, box, cone, cyl, extrude, gable, part

# ------------------------------------------------------------------ geometry helpers


def load(path):
    with open(path, encoding="utf-8") as f:
        return json.load(f)


class Projection:
    def __init__(self, lat0, lon0):
        self.lat0, self.lon0 = lat0, lon0
        self.kx = 110540.0 * 100            # cm per degree latitude
        self.ky = 111320.0 * 100 * math.cos(math.radians(lat0))

    def __call__(self, lat, lon):
        return ((lat - self.lat0) * self.kx, (lon - self.lon0) * self.ky)


def area(poly):
    return 0.5 * abs(sum(poly[i][0] * poly[i - 1][1] - poly[i - 1][0] * poly[i][1] for i in range(len(poly))))


def centroid(poly):
    return (sum(p[0] for p in poly) / len(poly), sum(p[1] for p in poly) / len(poly))


def hull(points):
    pts = sorted(set(points))
    if len(pts) < 3:
        return pts
    def cross(o, a, b):
        return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0])
    lo, hi = [], []
    for p in pts:
        while len(lo) >= 2 and cross(lo[-2], lo[-1], p) <= 0:
            lo.pop()
        lo.append(p)
    for p in reversed(pts):
        while len(hi) >= 2 and cross(hi[-2], hi[-1], p) <= 0:
            hi.pop()
        hi.append(p)
    return lo[:-1] + hi[:-1]


def obb(poly):
    """Minimum-area oriented rectangle: (cx, cy, length along yaw, width, yaw degrees)."""
    h = hull(poly)
    best = None
    for i in range(len(h)):
        a, b = h[i], h[(i + 1) % len(h)]
        ang = math.atan2(b[1] - a[1], b[0] - a[0])
        c, s = math.cos(ang), math.sin(ang)
        us = [p[0] * c + p[1] * s for p in h]
        vs = [-p[0] * s + p[1] * c for p in h]
        ar = (max(us) - min(us)) * (max(vs) - min(vs))
        if best is None or ar < best[0]:
            cu, cv = (max(us) + min(us)) / 2, (max(vs) + min(vs)) / 2
            best = (ar, cu * c - cv * s, cu * s + cv * c, max(us) - min(us), max(vs) - min(vs), math.degrees(ang))
    _, cx, cy, L, W, yaw = best
    if W > L:
        L, W, yaw = W, L, yaw + 90
    return cx, cy, L, W, yaw


def inside(poly, p):
    x, y = p
    c = False
    for i in range(len(poly)):
        (x1, y1), (x2, y2) = poly[i], poly[i - 1]
        if (y1 > y) != (y2 > y) and x < (x2 - x1) * (y - y1) / (y2 - y1) + x1:
            c = not c
    return c


def clip_bounds(pts, half):
    return [p for p in pts if abs(p[0]) <= half and abs(p[1]) <= half]


# ------------------------------------------------------------------ style recipe (Fort Kochi heritage town)

FORT_KOCHI = {
    "name": "FortKochi",
    "biome": "Tropical",
    "walls": [M("plaster", c) for c in ((238, 236, 228), (236, 230, 210), (232, 200, 120), (170, 205, 225), (236, 190, 190), (190, 220, 190),
                                      (226, 214, 180), (210, 170, 140))],
    "shutters": [M("painted_iron", c, roughness=0.5) for c in ((40, 90, 70), (30, 60, 110), (110, 40, 30), (70, 50, 30), (30, 30, 30))],
    "roof_tile": [M("roof_tile", (168, 82, 52)), M("roof_tile", (130, 66, 46)), M("roof_tile", (150, 90, 70))],
    "flat_roof": M("concrete", (150, 148, 140)),
    "tiled_share": 0.75,          # heritage core: most roofs are clay tile
    "floor_h": 330,
    "road": {"secondary": (800, "asphalt"), "tertiary": (650, "asphalt"), "unclassified": (500, "asphalt"), "residential": (500, "asphalt"),
             "living_street": (450, "paving"), "service": (360, "asphalt"), "pedestrian": (450, "paving"), "footway": (200, "laterite"),
             "path": (180, "laterite")},
    "materials": {"asphalt": M("asphalt", (58, 58, 60)), "paving": M("paving", (170, 160, 150)), "laterite": M("sandstone", (160, 96, 64))},
    "plants": {"yards": {"palm_coconut": 5, "island_tree": 2, "bush": 3, "palm_date": 1}, "street": {"island_tree": 2, "oak": 1},
               "ground": {"grass": 4, "grass_medium": 2, "fern": 1}},
}


# ------------------------------------------------------------------ builders

def _church(cx, cy, L, W, yaw, rng):
    """Dutch-colonial church (St Francis style): white walls, steep gable tile roof along the long axis, a stepped
    gable screen on the entrance end, a small cross."""
    white = M("plaster", (242, 240, 234))
    h = 950
    P = [box([L, W, h], [0, 0, 0], m=white), gable(L + 80, W + 120, W * 0.55, [0, 0, h], m=M("roof_tile", (150, 74, 50)), rot=[0, 90, 0])]
    front = -L / 2 - 20
    steps = [(W + 60, 300), (W * 0.75, 260), (W * 0.5, 220), (W * 0.25, 200)]
    z = h
    for w, sh in steps:
        P.append(box([60, w, sh], [front, 0, z], m=white))
        z += sh
    P += [box([40, 24, 260], [front - 10, 0, z], m=white), box([40, 140, 24], [front - 10, 0, z + 170], m=white)]
    P += [box([20, 220, 380], [front - 30, 0, 0], m=M("wood", (70, 45, 28)))]
    for k in range(int(L / 400)):
        x = -L / 2 + 300 + k * 400
        for s in (-1, 1):
            P.append(box([140, 20, 280], [x, s * (W / 2 + 10), 300], m=M("wood", (60, 90, 70))))
    return [dict(p, location=_rot(p["location"], yaw, cx, cy), rotation=[(p.get("rotation") or [0, 0, 0])[0], (p.get("rotation") or [0, 0, 0])[1] + yaw,
                                                                          (p.get("rotation") or [0, 0, 0])[2]]) for p in P]


def _rot(loc, yaw, cx, cy):
    a = math.radians(yaw)
    x, y, z = loc
    return [cx + x * math.cos(a) - y * math.sin(a), cy + x * math.sin(a) + y * math.cos(a), z]


def building_parts(poly, tags, style, rng):
    """Parts for one footprint (world cm). Walls follow the real outline; roof and openings use its oriented box."""
    ar = area(poly)
    if ar < 6 * 10000:                           # sheds under 6 m2: skip
        return []
    cx, cy, L, W, yaw = obb(poly)
    rect = ar / max(L * W, 1) > 0.82
    kind = tags.get("building", "yes")
    if kind == "church" or tags.get("amenity") == "place_of_worship" and "church" in tags.get("name", "").lower():
        return _church(cx, cy, L, W, yaw, rng)
    levels = tags.get("building:levels")
    try:
        floors = max(1, min(6, int(float(levels))))
    except (TypeError, ValueError):
        floors = 1 if ar < 90 * 10000 else rng.choice((1, 2, 2)) if ar < 250 * 10000 else rng.choice((2, 2, 3))
        if kind == "school":
            floors = rng.choice((2, 3))
    fh = style["floor_h"]
    h = floors * fh
    wall = rng.choice(style["walls"])
    local = [[p[0] - cx, p[1] - cy] for p in poly]
    P = [extrude(local, h, [cx, cy, 0], m=wall)]
    P.append(extrude(local, 40, [cx, cy, 0], m=M("concrete", (120, 116, 108))))      # plinth band
    tiled = rng.random() < style["tiled_share"] and ar < 900 * 10000
    if tiled:
        rh = min(W, 900) * 0.42
        P.append(part("pyramid", [L + 110, W + 110, rh], [cx, cy, h], m=rng.choice(style["roof_tile"]), rot=[0, yaw, 0]))
        P.append(box([L + 120, W + 120, 14], [cx, cy, h - 14], rot=[0, yaw, 0], m=M("wood", (70, 46, 30))))
    else:
        P.append(extrude(local, 25, [cx, cy, h], m=style["flat_roof"]))
        P.append(box([L, W, 90], [cx, cy, h], rot=[0, yaw, 0], m=wall) if False else extrude(local, 90, [cx, cy, h], m=wall))
        if rng.random() < 0.5:
            P.append(cyl(120, 130, [cx + rng.uniform(-L / 4, L / 4), cy, h + 25], m=M("painted_iron", (24, 24, 26))))
    if rect and L > 400:
        a = math.radians(yaw)
        ux, uy = math.cos(a), math.sin(a)          # along the long side
        vx, vy = -uy, ux                           # across
        shutter = rng.choice(style["shutters"])
        n = max(1, int(L / 300))
        for f in range(floors):
            z = f * fh + 100
            for side in (-1, 1):
                for i in range(n):
                    t = -L / 2 + (i + 0.5) * L / n
                    px, py = cx + ux * t + vx * side * (W / 2 + 3), cy + uy * t + vy * side * (W / 2 + 3)
                    P.append(box([110, 6, 150], [px, py, z], rot=[0, yaw, 0], m=M("tinted_glass")))
                    for s2 in (-1, 1):
                        sx, sy = px + ux * s2 * 85, py + uy * s2 * 85
                        P.append(box([55, 8, 160], [sx, sy, z - 5], rot=[0, yaw, 0], m=shutter))
                    if not tiled:
                        P.append(box([150, 50, 8], [px + vx * side * 25, py + vy * side * 25, z + 165], rot=[0, yaw, 0], m=M("concrete", (170, 168, 160))))
        dx, dy = cx + vx * -(W / 2 + 4), cy + vy * -(W / 2 + 4)
        P.append(box([120, 8, 230], [dx, dy, 0], rot=[0, yaw, 0], m=M("wood", (80, 52, 32))))
    return P


def road_parts(pts, width, mat):
    P = []
    for a, b in zip(pts, pts[1:]):
        L = math.hypot(b[0] - a[0], b[1] - a[1])
        if L < 1:
            continue
        yaw = math.degrees(math.atan2(b[1] - a[1], b[0] - a[0]))
        P.append(box([L, width, 10], [(a[0] + b[0]) / 2, (a[1] + b[1]) / 2, -2], rot=[0, yaw, 0], m=mat, bevel=0))
    for p in pts[1:-1]:
        P.append(cyl(width, 10, [p[0], p[1], -2], m=mat))
    return P


def fishing_net(rng):
    """Chinese fishing net (cheena vala): a teak cantilever leaning out over the water from a small platform, a square
    net hung from its tip, rope stays, stone counterweights. Origin on the shore, net towards +X (the sea)."""
    teak = M("wood", (96, 70, 46))
    rope = M("wood", (60, 50, 40))
    P = [box([500, 400, 60], [0, 0, -40], m=teak), box([480, 380, 12], [0, 0, 20], m=M("wood", (120, 96, 70)))]
    for s in (-1, 1):
        P.append(part("box", [1500, 26, 26], [700, s * 160, 600], rot=[-28, 0, 0], m=teak, pivot="center"))
    P.append(part("box", [26, 400, 26], [1350, 0, 940], m=teak, pivot="center"))
    P.append(part("lattice", [900, 900, 10], [1650, 0, 200], rot=[0, 0, 0], m=M("painted_iron", (40, 36, 30))))
    for s in (-1, 1):
        for t in (0.0, 1.0):
            P.append(part("box", [8, 8, 760], [1350 + (t - 0.5) * 820, s * 400, 560], m=rope, pivot="center"))
    for k in range(5):
        P.append(part("sphere", [50, 50, 50], [-150 + k * 25, rng.uniform(-60, 60), 300 + k * 60], m=M("stone", (120, 116, 110))))
    return P


# ------------------------------------------------------------------ the whole place


def build(data, style, centre, half=45000, seed=3, plants=True):
    """Builds the place inside +-half cm of centre. Returns a summary dict."""
    rng = random.Random(seed)
    proj = Projection(*centre)
    els = data["elements"]
    def geom(e):
        return [proj(g["lat"], g["lon"]) for g in e.get("geometry", []) if g]

    # ---- coast and water -> terrain
    coast_ways = [geom(e) for e in els if e.get("tags", {}).get("natural") == "coastline"]
    chain = _chain(coast_ways)
    flatten = []
    sea = None
    if chain:
        # land is on the left of OSM coastlines; close the sea polygon round the far side of the chain
        start, end = chain[0], chain[-1]
        mid = chain[len(chain) // 2]
        dx, dy = end[0] - start[0], end[1] - start[1]
        # water is on the right of travel; with X = north, Y = east the frame is mirrored, so "right" is (-dy, dx)
        right = (-dy, dx)
        far = 4 * half
        n = math.hypot(*right) or 1
        rx, ry = right[0] / n * far, right[1] / n * far
        sea = chain + [(end[0] + rx, end[1] + ry), (start[0] + rx, start[1] + ry)]
        flatten.append({"area": [list(p) for p in sea], "height": -450, "blend": 1800, "bare": False})
    waters = []
    for e in els:
        t = e.get("tags", {})
        if t.get("natural") == "water" and e["type"] == "way":
            g = geom(e)
            if len(g) > 3 and any(abs(p[0]) < half * 1.4 and abs(p[1]) < half * 1.4 for p in g):
                waters.append(g)
                flatten.append({"area": [list(p) for p in g], "height": -300, "blend": 600, "bare": False})
    terr = uk.tools.build.create_terrain(size=[2 * half + 20000, 2 * half + 20000], location=[0, 0, 0], height=120, feature_size=20000, roughness=0.3,
                                         seed=seed, biome=style["biome"], flatten=flatten, name=f"{style['name']}Ground")
    tid = terr["id"]
    jobs = [("Sea", [box([4 * half, 4 * half, 20], [0, 0, -100], m=M("water", (60, 90, 96)))], (0, 0, 0))]
    # ---- beaches
    beaches = []
    for e in els:
        if e.get("tags", {}).get("natural") == "beach":
            g = geom(e)
            if len(g) > 3:
                beaches.append(g)
                c = centroid(g)
                jobs.append((f"beach_{e['id']}", [extrude([[p[0] - c[0], p[1] - c[1]] for p in g], 6, [c[0], c[1], -4], m=M("sand", (176, 160, 128)))], (0, 0, 0)))
    # ---- roads
    road_lines = []
    batch, count = [], 0
    for e in els:
        t = e.get("tags", {})
        hw = t.get("highway")
        if hw not in style["road"]:
            continue
        g = clip_bounds(geom(e), half)
        if len(g) < 2:
            continue
        width, mk = style["road"][hw]
        batch += road_parts(g, width, style["materials"][mk])
        road_lines.append((g, width))
        if len(batch) > 300:
            jobs.append((f"roads_{count}", batch, (0, 0, 0)))
            batch, count = [], count + 1
    if batch:
        jobs.append((f"roads_{count}", batch, (0, 0, 0)))
    # ---- buildings on their footprints
    nb = 0
    for e in els:
        t = e.get("tags", {})
        if "building" not in t or e["type"] != "way":
            continue
        g = geom(e)
        if len(g) < 4 or not all(abs(p[0]) < half and abs(p[1]) < half for p in g):
            continue
        poly = g[:-1] if g[0] == g[-1] else g
        P = building_parts(poly, t, style, rng)
        if P:
            jobs.append((f"b_{e['id']}", P, (0, 0, 0)))
            nb += 1
    # ---- landmarks: Chinese fishing nets on the shore near their map point
    for e in els:
        t = e.get("tags", {})
        if "fishing net" in t.get("name", "").lower() and e["type"] == "node" and chain:
            p = proj(e["lat"], e["lon"])
            near = sorted(chain, key=lambda q: (q[0] - p[0]) ** 2 + (q[1] - p[1]) ** 2)[:1]
            k0 = chain.index(near[0])
            for k in range(max(0, k0 - 6), min(len(chain) - 1, k0 + 6), 2):
                a, b = chain[k], chain[k + 1]
                yaw = math.degrees(math.atan2(b[1] - a[1], b[0] - a[0])) - 90   # face the water (right of the coastline)
                jobs.append((f"fishing_net_{k}", [dict(q, location=_rot(q["location"], yaw, a[0], a[1]),
                                                        rotation=[(q.get("rotation") or [0, 0, 0])[0], (q.get("rotation") or [0, 0, 0])[1] + yaw,
                                                                  (q.get("rotation") or [0, 0, 0])[2]]) for q in fishing_net(rng)], (0, 0, 0)))
    uk.place_many(jobs, group=style["name"])
    out = {"terrain": tid, "buildings": nb, "roads": len(road_lines)}
    if plants:
        out.update(_plant(style, half, tid, road_lines, beaches, rng))
    uk.look.mood("Morning")
    return out


def _chain(ways):
    """Joins coastline pieces end to start into one polyline."""
    ways = [w for w in ways if len(w) > 1]
    if not ways:
        return []
    chain = list(ways.pop(0))
    changed = True
    while ways and changed:
        changed = False
        for w in list(ways):
            if _close(w[0], chain[-1]):
                chain += w[1:]
            elif _close(w[-1], chain[0]):
                chain = w[:-1] + chain
            else:
                continue
            ways.remove(w)
            changed = True
    return chain


def _close(a, b):
    return abs(a[0] - b[0]) < 50 and abs(a[1] - b[1]) < 50


def _plant(style, half, tid, road_lines, beaches, rng):
    """PCG planting on the open ground only (surface = the terrain)."""
    pl = style["plants"]
    out = {}
    area_poly = [(-half, -half), (half, -half), (half, half), (-half, half)]
    ex = []
    for g in beaches:
        sp = uk.call("spline.create_spline", points=[[p[0], p[1], 0] for p in g], closed=True, linear=True, name="BeachZone")["spline"]
        ex.append({"spline": sp.get("ref") or sp.get("id"), "clearance": 200})
    out["yards"] = uk.assets.forest(area_poly, pl["yards"], spacing=850, scale=(0.8, 1.15), keep=0.85, seed=11, name="Yards", surface=tid,
                                    exclude=ex, collision=False).get("instances")
    out["ground"] = uk.assets.ground_cover(area_poly, pl["ground"], spacing=260, keep=0.9, seed=12, name="Ground", surface=tid, exclude=ex).get("instances")
    n = 0
    for g, width in road_lines:                    # rain trees along the bigger streets
        if width >= 600 and len(g) >= 2:
            r = uk.assets.along([(p[0], p[1]) for p in g], pl["street"], spacing=1400, offset=(width / 2 + 150, width / 2 + 450), scale=(1.0, 1.35),
                                seed=rng.randint(0, 999), name="StreetTrees", surface=tid, collision=False)
            n += r.get("instances", 0)
    out["street_trees"] = n
    return out
