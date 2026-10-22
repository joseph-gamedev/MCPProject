"""Real assets packaged with the game: trees, palms, bushes, ferns and grass made in Blender (Sapling Tree Gen and our
own generators), placed as single objects or scattered by the thousand as GPU instances.

    from unreal_kit import assets as a
    a.catalog()                                        # {"oak": [paths...], "palm_coconut": [...], "grass": [...], ...}
    a.place("oak", at=[0, 0, 0], seed=2)               # one tree (a random variant), scene node
    a.forest([[-5000, -5000], [5000, -5000], [5000, 5000], [-5000, 5000]], {"oak": 3, "birch": 2, "fir": 1})
    a.along([[0, 0], [10000, 0]], {"palm_coconut": 1}, spacing=900, offset=(600, 800))   # palms beside a road
    a.ground_cover(area, {"grass": 6, "wildflowers": 1, "fern": 1}, spacing=120)          # dense grass

Use these for nature in realistic scenes instead of primitive blobs. Sizes are real (oak 18 m, palm 9-14 m, bush 1-2 m,
grass 0.3-1 m); scale ranges add variety. All use scanned bark and leaf textures.
Generated species: aspen, birch, maple, japanese_maple, oak, pine, fir, willow, bottlebrush, palm_coconut, palm_date,
bush, boxwood, flower_bush, fern, grass, tall_grass, wildflowers.
Scanned models (Poly Haven, CC0): island_tree, jacaranda_tree, nettle_plant, pachira_aquatica, grass_medium,
grass_bermuda, fern, dandelion, celandine, periwinkle_plant, boulder, rock.
Street / building props (scanned): street_lamp (2), fire_hydrant, metal_trash_can, painted_wooden_bench,
modular_street_seating, concrete_road_barrier, planter_box (2), modular_chainlink_fence, large_castle_door,
rollershutter_door, rollershutter_window, wooden_picnic_table, outdoor_table_chair_set, wooden_barrels, wooden_crate.
Architecture modules (real size, cm): door_panel, door_painted, door_double, window_sash, window_sash_small,
window_shutters, window_tall, window_wide, balcony, cornice_2m, string_course_2m, pediment (wall pieces: pivot on the
wall plane at the bottom centre, they stick out towards -Y, so put them at the wall's front face with yaw = the wall's
facing - 0 for a wall facing -Y); column_tuscan (4 m), column_doric (6 m), balustrade_2m, railing_iron_2m,
steps_entrance, chimney, dormer (free-standing, pivot at the bottom).
catalog() shows what the build has.
"""

import random
import re

from . import call

ROOT = "/Game/RuntimeFoliage"
_CATALOG = None


def _key(asset_name):
    """SM_PalmCoconut_02 -> palm_coconut; SM_PH_IslandTree01 -> island_tree (scanned Poly Haven models)."""
    base = re.sub(r"^SM_(PH_)?", "", asset_name)
    base = re.sub(r"_?\d+$", "", base)
    return re.sub(r"_+", "_", re.sub(r"(?<!^)(?=[A-Z])", "_", base)).lower()


def catalog(refresh=False):
    """{species: [static mesh paths]} from the packaged foliage (cached)."""
    global _CATALOG
    if _CATALOG is None or refresh:
        cat = {}
        found = []
        for root in (ROOT, "/Game/RuntimeArchitecture/Props", "/Game/RuntimeArchitecture/Modules"):
            try:
                found += call("level.find_assets", asset_class="StaticMesh", folder_path=root, recursive=True, limit=1000).get("assets", [])
            except Exception:
                pass  # a build without that folder
        for a in found:
            path = a["path"].split(".")[0]
            cat.setdefault(_key(path.rsplit("/", 1)[-1]), []).append(path)
        _CATALOG = {k: sorted(v) for k, v in cat.items()}
    return _CATALOG


def variants(species):
    cat = catalog()
    if species not in cat:
        raise KeyError(f"no asset '{species}' (have: {', '.join(sorted(cat))})")
    return cat[species]


def place(species, at, yaw=None, scale=1.0, seed=None, name=None, collision=True):
    """One plant (a random variant of the species) as a scene node; returns its id."""
    rng = random.Random(seed)
    path = rng.choice(variants(species))
    args = {"asset": path, "location": list(at) + [0] * (3 - len(at)), "rotation": [0, rng.uniform(0, 360) if yaw is None else yaw, 0],
            "scale": [scale] * 3, "collision": collision}
    if name:
        args["name"] = name
    return call("build.place_mesh", **args)["id"]


def _items(mix, scale, collision):
    out = []
    for species, weight in mix.items():
        paths = variants(species)
        for p in paths:
            out.append({"asset": p, "weight": weight / len(paths), "scale": list(scale), "collision": collision, "random_yaw": True})
    return out


def _spline(points, closed, name):
    pts = [list(p) + [0] * (3 - len(p)) for p in points]
    res = call("spline.create_spline", points=pts, closed=closed, name=name)
    sp = res.get("spline")
    return sp.get("ref") or sp.get("id") if isinstance(sp, dict) else res.get("id")


def forest(area, mix, spacing=700, scale=(0.85, 1.15), keep=1.0, seed=1, collision=True, name="Forest", exclude=None):
    """Fill a closed area [[x, y], ...] with a weighted species mix {species: weight}. GPU instances: thousands are cheap.
    keep < 1 thins by noise (clearings); exclude = [{"spline": id, "clearance": cm}] keeps roads clear."""
    args = {"spline": _spline(area, True, name + "Area"), "items": _items(mix, scale, collision), "mode": "Inside", "spacing": spacing,
            "keep": keep, "seed": seed, "ground": True, "name": name}
    if exclude:
        args["exclude"] = exclude
    return call("pcg.scatter", **args)


def along(path, mix, spacing=900, offset=(0, 0), side="Both", scale=(0.9, 1.1), seed=1, collision=True, name="Row"):
    """Plants along a path [[x, y], ...]: street trees, palm avenues, hedges. offset = [min, max] cm beside the line."""
    return call("pcg.scatter", spline=_spline(path, False, name + "Line"), items=_items(mix, scale, collision), mode="Along", spacing=spacing,
                offset=list(offset), side=side, seed=seed, ground=True, name=name)


def ground_cover(area, mix=None, spacing=120, scale=(0.8, 1.3), keep=0.85, seed=1, name="GroundCover"):
    """Dense grass / flowers / ferns inside an area (no collision)."""
    return forest(area, mix or {"grass": 6, "tall_grass": 1, "wildflowers": 1}, spacing=spacing, scale=scale, keep=keep, seed=seed,
                  collision=False, name=name)


WIND = ROOT + "/Materials/MPC_Wind.MPC_Wind"


def wind(strength=0.4, speed=0.6, direction=(1.0, 0.35), gustiness=0.5, sway=10.0, flutter=1.0):
    """Wind for all foliage (live): strength 0 calm, 0.4 light breeze (default), 1 windy, 2-3 storm; speed how fast it
    moves; direction (x, y) where it blows to; gustiness 0 steady - 1 strong gusts; sway cm the tree tops move per
    unit of strength; flutter cm of leaf / frond / grass flutter. Calm scene: wind(0.15); storm: wind(2.5, 1.2, gustiness=0.9)."""
    for name, value in (("WindStrength", strength), ("WindSpeed", speed), ("Gustiness", gustiness), ("SwayAmount", sway), ("LeafFlutter", flutter)):
        call("materials.set_collection_param", collection=WIND, name=name, value=value)
    call("materials.set_collection_param", collection=WIND, name="WindDirection", value=[direction[0], direction[1], 0, 0])
