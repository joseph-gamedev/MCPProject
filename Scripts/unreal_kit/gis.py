"""GIS: land plots (parcels) of any shape in the game, from random generation or real data, and buildings on them.

    import unreal_kit as uk
    uk.gis.create(size=(40000, 30000), layout="blocks")          # random streets, blocks and lots (any layout, zoning)
    uk.gis.load_georegions(r"C:\\data\\YasIsland_Georegions.json")  # Sector / Community / Plot files (lon/lat + metadata)
    uk.gis.load_geojson("parcels.geojson")                       # GeoJSON Polygon / MultiPolygon features
    uk.gis.build("P12", style="modernist")                       # a building fitted to plot P12 (any polygon)
    uk.gis.build_all(zoning="residential", limit=200)            # fill many plots at once (detail chosen by count)
    uk.gis.plots(zoning="commercial", empty=True); uk.gis.plot("P12"); uk.gis.export("out.geojson")

Map axes: +X east, -Y north (maps are not mirrored), cm. Geographic data is projected around an origin (gis.set_origin;
the loaders pick the data's centre unless one is set). Land use from the data (MasterplanLanduse, landuse, building, ...)
becomes the plot's zoning: residential, commercial, mixed, industrial, civic, green, water, other.
Plots are game data: scene.clear_all keeps them; uk.gis.clear() removes plots (and optionally their buildings).
"""

import json
import math
import os
import random
import re
import time

M_PER_DEG = 111320.0


def _uk():
    import unreal_kit as uk
    return uk


# ------------------------------------------------------------------ projection

class Projection:
    """Local equirectangular projection around (lat0, lon0): metres east / north -> world cm (+X east, -Y north)."""

    def __init__(self, lat0, lon0):
        self.lat0, self.lon0 = float(lat0), float(lon0)
        self.k = M_PER_DEG * math.cos(math.radians(self.lat0))

    def xy(self, lon, lat):
        return ((lon - self.lon0) * self.k * 100.0, -(lat - self.lat0) * M_PER_DEG * 100.0)

    def lonlat(self, x, y):
        return (self.lon0 + x / 100.0 / self.k, self.lat0 - y / 100.0 / M_PER_DEG)


def origin():
    """The game's geographic origin {set, latitude, longitude} (None when not set)."""
    o = _uk().call("gis.get_origin")
    return o if o.get("set") else None


def set_origin(lat, lon):
    return _uk().call("gis.set_origin", latitude=lat, longitude=lon)


def _projection(lonlat_rings, given=None):
    """The game's origin if one is set, else the given one, else the data's centre (then stored in the game)."""
    if given:
        set_origin(given[0], given[1])
        return Projection(given[0], given[1])
    o = origin()
    if o:
        return Projection(o["latitude"], o["longitude"])
    xs = [p[0] for r in lonlat_rings for p in r]
    ys = [p[1] for r in lonlat_rings for p in r]
    lat0, lon0 = (min(ys) + max(ys)) / 2, (min(xs) + max(xs)) / 2
    set_origin(lat0, lon0)
    return Projection(lat0, lon0)


# ------------------------------------------------------------------ land use

_ZONES = [
    ("mixed", ("mixed",)),
    ("residential", ("resid", "villa", "house", "apartment", "dwelling", "housing", "flat")),
    ("commercial", ("commerc", "retail", "office", "shop", "hotel", "hospitality", "business", "mall", "market")),
    ("industrial", ("industr", "warehouse", "utilit", "infrastr", "logistic", "factory", "depot", "port")),
    ("civic", ("community", "civic", "educat", "school", "health", "hospital", "relig", "mosque", "church", "government", "public",
               "institution", "police", "university", "museum", "cultur")),
    ("green", ("open space", "park", "green", "landscape", "recreat", "garden", "grass", "forest", "meadow", "golf", "sport")),
    ("water", ("water", "marina", "beach", "lake", "canal")),
]
_ZONE_KEYS = ("zoning", "zone", "MasterplanLanduse", "landuse", "land_use", "use", "category", "building", "amenity", "leisure", "natural",
              "type")


def zoning_of(props, default="other"):
    """Land use from typical GIS attributes -> residential | commercial | mixed | industrial | civic | green | water | other."""
    lower = {str(k).lower(): v for k, v in props.items()}     # attribute names in any case (USE, LandUse, landuse)
    for k in _ZONE_KEYS:
        v = props.get(k, lower.get(k.lower()))
        if v in (None, "", "yes") or not isinstance(v, str):
            continue
        low = v.lower()
        for zone, words in _ZONES:
            if any(w in low for w in words):
                return zone
    pid = " ".join(str(props.get(k, "")) for k in ("AldarPlotID", "PlotID", "name", "id")).upper()
    if re.search(r"(^|[_\-\s])(V|TH|VILLA|TOWNHOUSE)([_\-\s\d]|$)", pid):
        return "residential"
    return default


# ------------------------------------------------------------------ street frontage for imported plots

def street_edges(polys, tolerance=150.0):
    """For each polygon, the edges NOT shared with a neighbour (they face a street or open land)."""
    xs = [p[0] for P in polys for p in P] or [0.0]
    ys = [p[1] for P in polys for p in P] or [0.0]
    cell = max(2000.0, max(max(xs) - min(xs), max(ys) - min(ys)) / 512.0)    # grid sized to the data (never millions of cells)
    grid = {}
    segs = []
    for pi, P in enumerate(polys):
        n = len(P)
        for e in range(n):
            a, b = P[e], P[(e + 1) % n]
            segs.append((pi, e, a, b))
            L = math.hypot(b[0] - a[0], b[1] - a[1])
            steps = max(1, min(4096, int(L / (cell * 0.5)) + 1))     # the cells along the edge, not its bounding box
            cells = {(int((a[0] + (b[0] - a[0]) * k / steps) // cell), int((a[1] + (b[1] - a[1]) * k / steps) // cell)) for k in range(steps + 1)}
            for c in cells:
                for dx in (-1, 0, 1):
                    for dy in (-1, 0, 1):
                        grid.setdefault((c[0] + dx, c[1] + dy), []).append(len(segs) - 1)

    def dist(p, a, b):
        dx, dy = b[0] - a[0], b[1] - a[1]
        L2 = dx * dx + dy * dy
        t = 0 if L2 < 1e-9 else max(0.0, min(1.0, ((p[0] - a[0]) * dx + (p[1] - a[1]) * dy) / L2))
        return math.hypot(a[0] + dx * t - p[0], a[1] + dy * t - p[1])

    out = [[] for _ in polys]
    for k, (pi, e, a, b) in enumerate(segs):
        shared = False
        L = math.hypot(b[0] - a[0], b[1] - a[1])
        probes = [(a[0] + (b[0] - a[0]) * f, a[1] + (b[1] - a[1]) * f) for f in ((0.5,) if L < 600 else (0.25, 0.5, 0.75))]
        hits = 0
        for p in probes:
            for j in grid.get((int(p[0] // cell), int(p[1] // cell)), ()):
                pj = segs[j][0]
                if pj != pi and dist(p, segs[j][2], segs[j][3]) < tolerance:
                    hits += 1
                    break
        shared = hits * 2 > len(probes)
        if not shared:
            out[pi].append(e)
    return out


# ------------------------------------------------------------------ sending plots to the game

def _clean_ring(ring):
    pts = [(float(p[0]), float(p[1])) for p in ring]
    if len(pts) > 3 and abs(pts[0][0] - pts[-1][0]) < 1e-9 and abs(pts[0][1] - pts[-1][1]) < 1e-9:
        pts.pop()
    out = []
    for p in pts:
        if not out or abs(p[0] - out[-1][0]) > 1e-9 or abs(p[1] - out[-1][1]) > 1e-9:
            out.append(p)
    return out


def add(features, geographic=True, origin_latlon=None, replace=True, frontage="auto", chunk=2500, z=0.0):
    """Sends features [{ring: [(lon, lat) | (x_cm, y_cm)], kind: plot|area|block, id?, zoning?, props?}] to the game.
    geographic: rings are lon/lat (projected around the origin); else world cm. frontage auto = street edges are those not
    shared with a neighbouring plot. Returns {plots, areas, ids, origin, seconds}."""
    uk = _uk()
    t0 = time.time()
    feats = [dict(f, ring=_clean_ring(f["ring"])) for f in features if len(f.get("ring", [])) >= 3]
    proj = None
    if geographic:
        proj = _projection([f["ring"] for f in feats], origin_latlon)
        for f in feats:
            f["xy"] = [proj.xy(p[0], p[1]) for p in f["ring"]]
    else:
        for f in feats:
            f["xy"] = list(f["ring"])
    plots = [f for f in feats if f.get("kind", "plot") == "plot"]
    if frontage == "auto":
        fronts = street_edges([f["xy"] for f in plots])
        for f, e in zip(plots, fronts):
            f.setdefault("street_edges", e)
    payload = []
    for f in feats:
        d = {"polygon": [[round(x, 1), round(y, 1), z] for x, y in f["xy"]], "kind": f.get("kind", "plot")}
        if d["kind"] == "plot":
            props = dict(f.get("props") or {})
            d["zoning"] = f.get("zoning") or zoning_of(props)
            d["props"] = props
            if f.get("id"):
                d["id"] = str(f["id"])
            if f.get("street_edges") is not None:
                d["street_edges"] = list(f["street_edges"])
        payload.append(d)
    # areas first (drawn under the plots), then plots in chunks; the map is redrawn once at the end
    payload.sort(key=lambda d: d["kind"] != "area")
    ids = []
    for i in range(0, len(payload), chunk):
        part = payload[i:i + chunk]
        res = uk.call("gis.add_plots", plots=part, replace=replace and i == 0, redraw=i + chunk >= len(payload))
        ids += res.get("ids", [])
    out = {"plots": len(plots), "areas": len(feats) - len(plots), "ids": ids[:20] + (["..."] if len(ids) > 20 else []),
           "origin": {"latitude": proj.lat0, "longitude": proj.lon0} if proj else None, "seconds": round(time.time() - t0, 1)}
    print("gis.add:", {k: v for k, v in out.items() if k != "ids"})
    return out


# ------------------------------------------------------------------ readers

def load_georegions(path, types=("Plot",), sectors=None, communities=None, landuse=None, coords="geo", areas=True, limit=None, replace=True,
                    origin_latlon=None):
    """GeoRegions JSON ({mapName, features: [{type: Sector|Community|Plot, name, id, metadata, polygon: [[lon, lat, h]...],
    unrealPolygon: [[x, y, z]...]}]}). types: which features become plots; sectors / communities / landuse filter by name
    (case-insensitive substrings); areas: sectors become the land under the plots; coords "geo" (projected here) or
    "unreal" (the file's unrealPolygon cm as they are)."""
    with open(path, encoding="utf-8-sig") as fh:
        data = json.load(fh)

    def match(v, wanted):
        if not wanted:
            return True
        wanted = [wanted] if isinstance(wanted, str) else wanted
        return any(w.lower() in str(v or "").lower() for w in wanted)

    feats = []
    for f in data.get("features", []):
        md = f.get("metadata") or {}
        t = f.get("type")
        if not match(md.get("SectorName"), sectors) or (t != "Sector" and not match(md.get("CommunityName"), communities)):
            continue
        ring = f.get("unrealPolygon") if coords == "unreal" else f.get("polygon")
        if not ring:
            continue
        if t == "Sector" and areas:
            feats.append({"ring": ring, "kind": "area"})
        elif t in types:
            props = dict(md, name=f.get("name"), source_type=t)
            z = zoning_of(props)
            if landuse and not match(md.get("MasterplanLanduse") or z, landuse):
                continue
            pid = md.get("AldarPlotID") or md.get("PlotID") or f.get("id")
            feats.append({"ring": ring, "kind": "plot", "id": pid, "props": props, "zoning": z})
    plots = [f for f in feats if f["kind"] == "plot"]
    if limit and len(plots) > limit:
        keep = set(id(f) for f in plots[:limit])
        feats = [f for f in feats if f["kind"] != "plot" or id(f) in keep]
    print(f"georegions {os.path.basename(path)}: {sum(1 for f in feats if f['kind'] == 'plot')} plots, {sum(1 for f in feats if f['kind'] == 'area')} areas")
    return add(feats, geographic=coords != "unreal", origin_latlon=origin_latlon, replace=replace)


def _geojson_features(gj):
    if isinstance(gj, str):
        gj = json.loads(gj) if gj.lstrip().startswith("{") else json.load(open(gj, encoding="utf-8-sig"))
    t = gj.get("type")
    if t == "FeatureCollection":
        items = gj.get("features", [])
    elif t == "Feature":
        items = [gj]
    else:
        items = [{"type": "Feature", "geometry": gj, "properties": {}}]
    out = []
    for it in items:
        g = it.get("geometry") or {}
        props = dict(it.get("properties") or {})
        fid = it.get("id") or props.get("id") or props.get("ID")
        polys = [g["coordinates"]] if g.get("type") == "Polygon" else g.get("coordinates", []) if g.get("type") == "MultiPolygon" else []
        for k, poly in enumerate(polys):
            if poly:
                out.append({"ring": [p[:2] for p in poly[0]], "kind": "plot", "props": props,
                            "id": (f"{fid}_{k}" if len(polys) > 1 else fid) if fid is not None else None})
    return out


def _is_geographic(feats):
    """Longitude / latitude when every corner is in range and the data spans under 3 degrees (a town, a region);
    small projected metre values (0..1000) would otherwise pass as degrees."""
    pts = [p for f in feats for p in f["ring"]]
    if not pts or not all(-180 <= p[0] <= 180 and -90 <= p[1] <= 90 for p in pts):
        return False
    return max(p[0] for p in pts) - min(p[0] for p in pts) < 3 and max(p[1] for p in pts) - min(p[1] for p in pts) < 3


def _local_metres(feats):
    """Projected data in metres: recentred on its middle, metres -> cm, north up (-Y)."""
    xs = [p[0] for f in feats for p in f["ring"]]
    ys = [p[1] for f in feats for p in f["ring"]]
    x0, y0 = (min(xs) + max(xs)) / 2, (min(ys) + max(ys)) / 2
    for f in feats:
        f["ring"] = [((p[0] - x0) * 100.0, -(p[1] - y0) * 100.0) for p in f["ring"]]
    return feats


def load_geojson(src, replace=True, origin_latlon=None, kind="plot"):
    """GeoJSON (path, text or dict): Polygon / MultiPolygon features become plots with their properties (WGS84 lon/lat, or
    metres for projected data)."""
    feats = _geojson_features(src)
    for f in feats:
        f["kind"] = kind
    geo = _is_geographic(feats)
    return add(feats if geo else _local_metres(feats), geographic=geo, origin_latlon=origin_latlon, replace=replace)


# ------------------------------------------------------------------ plots in the game

def create(size=(40000, 30000), layout="blocks", center=(0, 0, 0), area=None, street_width=1200, block_size=(8000, 5000), lot_width=(1200, 2400),
           lot_depth=0, zoning=None, center_commercial=True, seed=None, replace=True):
    """Random land: streets, blocks and lots (layout blocks | grid | organic | radial). Returns the game's summary."""
    args = dict(size=list(size), layout=layout.capitalize(), center=list(center), street_width=street_width, block_size=list(block_size),
                lot_width=list(lot_width), lot_depth=lot_depth, center_commercial=center_commercial, seed=-1 if seed is None else seed, replace=replace)
    if area:
        args["area"] = [list(p) + [0] * (3 - len(p)) for p in area]
    if zoning:
        args["zoning"] = zoning
    return _uk().call("gis.create_plots", **args)


def plots(zoning="", empty=False, min_area_m2=0, limit=100000):
    """Plot summaries (id, area_m2, centroid, zoning, building, ...), paged in requests of 500."""
    uk, out = _uk(), []
    while len(out) < limit:
        page = uk.call("gis.list_plots", zoning=zoning, only_empty=empty, min_area_m2=min_area_m2, limit=min(500, limit - len(out)), offset=len(out))
        out += page.get("plots", [])
        if not page.get("plots") or len(out) >= page.get("total", 0):
            break
    return out


def plot(pid):
    return _uk().call("gis.get_plot", id=pid)


def show(plots=True, labels=True, streets=True, max_labels=2000):
    return _uk().call("gis.show_plots", plots=plots, labels=labels, streets=streets, max_labels=max_labels)


def export(path=None, lonlat=True):
    """The plots as GeoJSON (dict); written to path when given."""
    uk, feats, total, batch = _uk(), [], None, 200
    while total is None or len(feats) < total:
        try:
            page = uk.call("gis.export_geo_json", lon_lat=lonlat, limit=batch, offset=len(feats))
        except uk.ToolError as e:                              # answers are size-capped: smaller pages
            if "too large" in str(e) and batch > 1:
                batch = max(1, batch // 2)
                continue
            raise
        total = page.get("total", 0)
        if not page.get("features"):
            break
        feats += page["features"]
    fc = {"type": "FeatureCollection", "features": feats}
    if path:
        with open(path, "w", encoding="utf-8") as fh:
            json.dump(fc, fh)
    return fc


def clear(buildings=True):
    """Removes all plots (and the buildings standing on them)."""
    uk = _uk()
    if buildings:
        ids = [p["building"] for p in plots(limit=100000) if p.get("building")]
        for i in range(0, len(ids), 100):
            uk.call("ops.run_batch", calls=[{"tool": "scene.delete_node", "arguments": {"target": b}} for b in ids[i:i + 100]])
    return uk.call("gis.delete_plots", all=True)


# ------------------------------------------------------------------ buildings on plots

KIND_SETBACK = {"villa": (450, 250), "townhouse": (150, 0), "apartment": (300, 150), "office": (400, 250), "shophouse": (0, 0), "loft": (300, 150)}


def kind_for(zoning, area_m2):
    """A building type that suits the land use and plot size."""
    if zoning == "residential":
        return "townhouse" if area_m2 < 260 else ("villa" if area_m2 < 1600 else "apartment")
    if zoning == "commercial":
        return "shophouse" if area_m2 < 400 else "office"
    if zoning == "mixed":
        return "apartment"
    if zoning == "industrial":
        return "loft"
    if zoning == "civic":
        return "office"
    return "apartment" if area_m2 > 600 else "villa"


def _floors_from(props):
    for k in ("building:levels", "levels", "Levels", "floors", "Floors", "NumFloors", "FLOORS", "storeys", "stories"):
        v = props.get(k)
        try:
            if v not in (None, ""):
                return max(1, int(float(v)))
        except (TypeError, ValueError):
            pass
    for k in ("height", "Height", "building:height", "HEIGHT", "BuildingHeight"):
        v = props.get(k)
        try:
            if v not in (None, ""):
                return max(1, round(float(str(v).replace("m", "").strip()) / 3.2))
        except (TypeError, ValueError):
            pass
    return None


def parts_for(info, kind=None, style="auto", floors=None, setback=None, detail="medium", seed=None, massing=None):
    """Parts (world cm) of a building fitted to a plot: rectangular plots get the full building pipeline (massing, style)
    facing the main street edge; any other shape a footprint building along every edge. Green / water plots: none."""
    from . import buildings as B
    zoning = info.get("zoning", "other")
    if zoning in ("green", "water"):
        return [], None
    props = info.get("props") or {}
    area = info.get("area_m2", 0)
    kind = kind or kind_for(zoning, area)
    floors = floors or _floors_from(props)
    rng = random.Random(seed)
    poly = [(p[0], p[1]) for p in info["polygon"]]
    z = info["polygon"][0][2] if info["polygon"] else 0
    street = [i for i, e in enumerate(info["edges"]) if e["street"]]
    if style in (None, "auto", "modern") and detail != "mass":    # contemporary building on the plot outline itself
        from . import modern
        fs = setback if setback is not None else KIND_SETBACK.get(kind, (300, 150))[0]
        q = poly if B._signed_area(poly) > 0 else poly[::-1]
        inner = B.inset_polygon(q, max(fs * 0.6, 100))
        if abs(B._signed_area(inner)) < 2e5:
            inner = q
        parts = modern.building(kind if kind in modern.KINDS else "office", seed=rng.randrange(1 << 30), floors=floors, polygon=inner, detail=detail)
        return [dict(p_, location=[p_["location"][0], p_["location"][1], p_["location"][2] + z]) if "location" in p_ else p_ for p_ in parts], kind
    front, side = setback if isinstance(setback, (tuple, list)) else ((setback, setback * 0.5) if setback is not None else KIND_SETBACK.get(kind, (300, 150)))
    if detail != "mass" and info.get("rectangularity", 0) >= 0.86 and len(poly) <= 6:
        e = info["edges"][info["front"]]
        ax, ay, bx, by = e["a"][0], e["a"][1], e["b"][0], e["b"][1]
        L = math.hypot(bx - ax, by - ay) or 1
        ux, uy = (bx - ax) / L, (by - ay) / L
        ix, iy = -uy, ux                                       # inward (plots are counter-clockwise, positive area)
        s = [(p[0] - ax) * ux + (p[1] - ay) * uy for p in poly]
        t = [(p[0] - ax) * ix + (p[1] - ay) * iy for p in poly]
        width = (max(s) - min(s)) - 2 * side
        depth = max(t) - front - max(side, 150)
        if width > 450 and depth > 450:
            row = kind in ("townhouse", "shophouse") and side == 0
            bseed = rng.randrange(1 << 30)
            parts = B.building(kind, seed=bseed, style=style, width=width, depth=depth, floors=floors, detail=detail,
                               massing=massing or "auto", party=("-x", "+x") if row else ())
            # some massings have minimum sizes: if it overhangs the plot, the same building as a plain block that fits
            xs = [q["location"][0] for q in parts if q.get("shape") == "box"]
            ys = [q["location"][1] for q in parts if q.get("shape") == "box"]
            if xs and (max(abs(min(xs)), abs(max(xs))) > width / 2 + side + 120 or max(ys) > depth + front + 250):
                parts = B.building(kind, seed=bseed, style=style, width=width, depth=depth, floors=floors, detail=detail, massing="block",
                                   party=("-x", "+x") if row else ())
            yaw = math.degrees(math.atan2(-ix, iy))            # local -y (front) -> outward = -inward
            cs = (min(s) + max(s)) / 2
            ox, oy = ax + ux * cs + ix * front, ay + uy * cs + iy * front
            from .parts import transform
            return transform(parts, (ox, oy, z), yaw), kind
    inner = B.inset_polygon(poly if B._signed_area(poly) > 0 else poly[::-1], max(front * 0.6, side))
    if abs(B._signed_area(inner)) < 2e5:                       # too small after setbacks: use the plot itself
        inner = poly
    return B.footprint([(x, y, z) for x, y in inner], kind, style=style, floors=floors, seed=rng.randrange(1 << 30),
                       street_edges=street or None, detail=detail), kind


def fetch(ids, batch=40):
    """Full plot data for many ids in few requests (answers are capped at 256 KB: batches halve when a batch is too big)."""
    uk = _uk()
    out, i = [], 0
    while i < len(ids):
        part = ids[i:i + batch]
        try:
            res = uk.call("ops.run_batch", calls=[{"tool": "gis.get_plot", "arguments": {"id": x}} for x in part])
        except uk.ToolError as e:
            if "too large" in str(e) and batch > 1:
                batch = max(1, batch // 2)
                continue
            raise
        out += [r for r in res.get("results", []) if r.get("ok", True)]
        i += len(part)
    return out


def build(pid, kind=None, style="auto", floors=None, setback=None, detail="medium", seed=None, massing=None):
    """A building on one plot (any shape), recorded on the plot. Returns {plot, node, kind, parts}."""
    uk = _uk()
    info = plot(pid)
    if info.get("building"):
        try:
            uk.call("scene.delete_node", target=info["building"])
        except Exception:
            pass
    P, k = parts_for(info, kind, style, floors, setback, detail, seed, massing)
    if not P:
        trees([info])
        return {"plot": pid, "node": None, "kind": "trees", "parts": 0}
    node = uk.place_many([(f"Bldg_{info['id']}", P, (0, 0, 0))])[0]
    uk.call("gis.set_plot", id=info["id"], building=node)
    return {"plot": info["id"], "node": node, "kind": k, "parts": len(P)}


def trees(infos, density=1.0, mix=None, seed=None):
    """Trees and palms scattered inside green plots (GPU instances, one request)."""
    uk = _uk()
    rng = random.Random(seed)
    mix = mix or {"palm_date": 2, "palm_coconut": 1, "maple": 1}
    ins = uk.assets.Instancer("GIS_Trees", collision=False)
    count = 0
    for info in infos:
        poly = [(p[0], p[1]) for p in info["polygon"]]
        xs, ys = [p[0] for p in poly], [p[1] for p in poly]
        area = info.get("area_m2", 0)
        n = int(min(400, area / 90 * density))
        for _ in range(n * 3):
            if n <= 0:
                break
            x, y = rng.uniform(min(xs), max(xs)), rng.uniform(min(ys), max(ys))
            if _inside(poly, x, y):
                species = rng.choice([k for k, w in mix.items() for _ in range(int(w))])
                ins.add(rng.choice(uk.assets.variants(species)), (x, y, info["polygon"][0][2]), yaw=rng.uniform(0, 360), scale=rng.uniform(0.7, 1.05))
                n -= 1
                count += 1
    if count:
        ins.flush()
    return count


def _inside(poly, x, y):
    inside, j = False, len(poly) - 1
    for i in range(len(poly)):
        if (poly[i][1] > y) != (poly[j][1] > y) and x < (poly[j][0] - poly[i][0]) * (y - poly[i][1]) / ((poly[j][1] - poly[i][1]) or 1e-9) + poly[i][0]:
            inside = not inside
        j = i
    return inside


def build_all(ids=None, zoning="", limit=300, detail="auto", style="auto", styles=None, seed=None, hide_plots=False, greenery=True):
    """Buildings on many plots in one go (empty plots only unless ids are given). detail auto: medium up to 40 plots, low
    up to 400, a massing model above (thousands). styles = {style: weight} to mix (e.g. {"modernist": 2, "glass": 1}).
    Returns {built, parts, trees, seconds}."""
    uk = _uk()
    t0 = time.time()
    rng = random.Random(seed)
    summaries = [p for p in plots(zoning=zoning, empty=ids is None, limit=100000) if ids is None or p["id"] in set(ids)]
    if limit and len(summaries) > limit:
        rng.shuffle(summaries)
        summaries = summaries[:limit]
    lod = detail if detail != "auto" else ("medium" if len(summaries) <= 40 else "low" if len(summaries) <= 400 else "mass")
    infos = fetch([p["id"] for p in summaries])
    jobs, owners, green = [], [], []
    for info in infos:
        st = style
        if styles:
            st = rng.choices(list(styles), weights=list(styles.values()))[0]
        try:
            P, k = parts_for(info, style=st, detail=lod, seed=rng.randrange(1 << 30))
        except Exception as e:                                 # an odd plot: a plain block on it
            P, k = parts_for(info, style=None, detail="mass", seed=rng.randrange(1 << 30))
        if not P:
            green.append(info)
            continue
        jobs.append((f"Bldg_{info['id']}", P, (0, 0, 0)))
        owners.append(info["id"])
    nodes = uk.place_many(jobs, group="GIS_Buildings") if jobs else []
    for i in range(0, len(owners), 100):
        uk.call("ops.run_batch", calls=[{"tool": "gis.set_plot", "arguments": {"id": pid, "building": nid}}
                                        for pid, nid in zip(owners[i:i + 100], nodes[i:i + 100]) if nid])
    planted = trees(green) if greenery and green else 0
    if hide_plots:
        show(plots=False, labels=False, streets=True)
    out = {"built": len(jobs), "parts": sum(len(j[1]) for j in jobs), "trees": planted, "detail": lod, "seconds": round(time.time() - t0, 1)}
    print("gis.build_all:", out)
    return out
