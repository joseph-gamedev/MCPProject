"""A small Sea-of-Thieves-style island, built with toolset calls only (proof of concept for composing scenes from parts).

    python pirate_island.py            # needs the game running with the FluidNinja LIVE pack

Island terrain rising out of the sea, waves breaking on its shore (coast_water reads the ground by itself), palms and jungle,
boulders, a beached shipwreck, a wooden pier with floating barrels, a campfire, a treasure chest, a third-person pirate,
warm afternoon sun and a painterly style.
"""

import math
import os
import random
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import unreal_kit as uk

SEA = 0.0                    # sea level (cm)
R = 4200.0                   # island radius at the waterline (about)
BEACH = (0.0, -3600.0)       # the landing beach, south side
rng = random.Random(7)


def ground(x, y):
    """Ground height under (x, y)."""
    hits = uk.call("physics.line_trace", start=[x, y, 5000], end=[x, y, -5000]).get("hits") or []
    return hits[0]["location"][2] if hits else -1000.0


def shore_point(angle, inland=0.0):
    """Where the ground meets the sea along a ray from the island centre (inland > 0 moves up the beach)."""
    lo, hi = 0.0, 9000.0
    for _ in range(14):
        mid = (lo + hi) / 2
        if ground(mid * math.cos(angle), mid * math.sin(angle)) > SEA:
            lo = mid
        else:
            hi = mid
    r = lo - inland
    return r * math.cos(angle), r * math.sin(angle)


COAST = [[-4200, -1500], [-3500, -3300], [-1800, -4200], [300, -3900], [2300, -4500], [3900, -3100], [4700, -900],
         [4200, 1500], [2700, 3300], [500, 4000], [-1500, 3400], [-3300, 2300], [-4700, 400]]


def stage_ground():
    uk.call("scene.clear_all", confirm=True)
    uk.call("sky.setup", hour=15.5, preset="clear")
    # an island drawn by its coastline at sea level: beach, a jungle hill inside, the sea floor shelving off; sand up to 2.5 m
    uk.call("build.create_terrain", size=[60000, 60000], location=[0, 0, -900], height=150, feature_size=2500, roughness=0.3, seed=11,
            biome="tropical", name="Island", sea_level=SEA, sand_line=SEA + 250,
            islands=[{"area": COAST, "height": 1400, "beach": 1400, "dune": 120, "depth": 900, "shelf": 2500, "wobble": 450}],
            hills=[{"at": [1500, 1200], "radius": 2200, "height": 500}])
    global BEACH
    BEACH = shore_point(-math.pi / 2, inland=700)
    print("beach", [round(v) for v in BEACH], "ground there", round(ground(*BEACH)), "peak", round(ground(0, 300)))


def stage_water_player():
    p = uk.call("character.third_person", location=[BEACH[0], BEACH[1], ground(*BEACH) + 20], yaw=-90)
    sea = uk.call("fluid.add", preset="coast_water", location=[BEACH[0], BEACH[1], SEA], follow_player=True, name="Sea")
    print("pirate", p["id"], "sea", sea["id"])


def asset(name):
    """A packaged mesh by name (level.find_assets), e.g. "SM_PalmCoconut_01"."""
    r = uk.call("level.find_assets", name=name, asset_class="StaticMesh", limit=5)
    for x in r.get("assets") or []:
        path = x.get("path", x) if isinstance(x, dict) else x
        if path.split("/")[-1].split(".")[0] == name:
            return path.split(".")[0]
    raise RuntimeError("no asset " + name)


def ring(inland, n=40, z=None):
    """Points following the coastline, `inland` cm up the beach."""
    pts = []
    for k in range(n):
        x, y = shore_point(2 * math.pi * k / n, inland)
        pts.append([round(x), round(y), round(ground(x, y)) if z is None else z])
    return pts


def stage_vegetation():
    palms = [asset(n) for n in ("SM_PalmCoconut_01", "SM_PalmCoconut_02", "SM_PalmCoconut_03", "SM_PalmDate_01")]
    jungle = [asset(n) for n in ("SM_PH_IslandTree01", "SM_PH_IslandTree02", "SM_PH_IslandTree03")]
    under = [asset(n) for n in ("SM_Bush_01", "SM_Bush_02", "SM_Fern_01", "SM_Fern_02", "SM_TallGrass_01")]
    # leaning palms along the top of the beach, a clear landing beach to the south
    beach_line = uk.call("spline.create_spline", points=ring(1100), closed=True, name="PalmLine")["spline"]["ref"]
    clear = uk.call("spline.create_spline", points=[[BEACH[0] - 1200, BEACH[1] - 2000, 0], [BEACH[0] + 1200, BEACH[1] - 2000, 0],
                                                    [BEACH[0] + 1200, BEACH[1] + 900, 0], [BEACH[0] - 1200, BEACH[1] + 900, 0]],
                    closed=True, linear=True, name="LandingClearing")["spline"]["ref"]
    uk.call("pcg.scatter", spline=beach_line, mode="Along", offset=[0, 500], spacing=650, seed=3, name="Palms",
            items=[{"asset": a, "weight": 1, "scale": [0.8, 1.25], "random_yaw": True, "collision": True} for a in palms],
            exclude=[{"spline": clear, "clearance": 0}])
    # jungle on the hill, undergrowth everywhere inland
    inner = uk.call("spline.create_spline", points=ring(2300, 30), closed=True, name="Jungle")["spline"]["ref"]
    uk.call("pcg.scatter", spline=inner, mode="Inside", spacing=700, keep=0.75, seed=5, name="JungleTrees",
            items=[{"asset": a, "weight": 1, "scale": [0.8, 1.3], "random_yaw": True, "collision": True} for a in jungle + palms[:2]])
    uk.call("pcg.scatter", spline=inner, mode="Inside", spacing=260, keep=0.6, seed=6, name="Undergrowth",
            items=[{"asset": a, "weight": 1, "scale": [0.7, 1.3], "random_yaw": True} for a in under])
    for k in (inner, beach_line, clear):
        uk.call("actors.set_visibility", target=k, visible=False)


def stage_coast_props():
    boulders = [asset(n) for n in ("SM_Boulder_SmoothedEdges", "SM_Boulder_SharpEdges")]
    # boulders where the land meets the sea, some out in the surf (the water flows and foams around them)
    for k in range(14):
        a = rng.uniform(0, 2 * math.pi)
        if abs(math.atan2(math.sin(a + math.pi / 2), math.cos(a + math.pi / 2))) < 0.45:
            continue   # keep the landing beach clear
        x, y = shore_point(a, rng.uniform(-900, 300))
        s = rng.uniform(1.2, 3.2)
        uk.call("build.place_mesh", asset=rng.choice(boulders), location=[x, y, ground(x, y) - 40 * s], rotation=[rng.uniform(-15, 15), rng.uniform(0, 360), 0],
                scale=[s, s, s * rng.uniform(0.7, 1.1)], tags=["SDFSource"], name=f"Boulder{k}")
    # a ship that ran aground on the east beach
    x, y = shore_point(0.35, -600)
    uk.call("build.place_mesh", asset=asset("wreck4_Ship02"), location=[x, y, ground(x, y) - 80], rotation=[8, 115, -18], scale=[1.6, 1.6, 1.6],
            tags=["SDFSource"], name="Shipwreck")
    # a wooden pier from the landing beach out into the water
    bx, by = BEACH
    deck_z = ground(bx, by) + 90
    uk.call("build.create_primitive", shape="Box", size=[260, 2600, 18], location=[bx, by - 1500, deck_z], material="wood", name="PierDeck")
    for i in range(7):
        for side in (-110, 110):
            px, py = bx + side, by - 300 - i * 420
            gz = ground(px, py)
            uk.call("build.create_primitive", shape="Cylinder", size=[24, 24, deck_z - gz + 60], location=[px, py, gz - 60], material="wood",
                    name=f"PierPost{i}_{'L' if side < 0 else 'R'}")
    # barrels bobbing by the end of the pier
    barrels = []
    for i in range(4):
        r = uk.call("build.place_mesh", asset=asset("wreck4_Mesh01"), location=[bx + rng.uniform(-700, 700), by - 2600 - rng.uniform(0, 600), SEA + 60],
                    rotation=[rng.uniform(-30, 30), rng.uniform(0, 360), rng.uniform(-30, 30)], name=f"Barrel{i}")
        barrels.append(r["id"])
    uk.call("fluid.float", targets=barrels, water_level=SEA, buoyancy=2.2)


def stage_camp():
    # a campfire and a treasure chest at the top of the landing beach
    cx, cy = BEACH[0] + 700, BEACH[1] + 900
    gz = ground(cx, cy)
    for k, name in enumerate(("udyjbjxva_LOD1_Aset_wood_firewood_S_udyjbjxva_00_LOD1", "udyjbjxva_LOD1_Aset_wood_firewood_S_udyjbjxva_02_LOD1")):
        uk.call("build.place_mesh", asset=asset(name), location=[cx + k * 10, cy, gz], rotation=[0, k * 70, 0], name=f"Firewood{k}")
    uk.call("fluid.add", preset="fire", location=[cx, cy, gz + 10], name="Campfire")
    for k in range(6):   # stones around the fire
        a = k * math.pi / 3
        uk.call("build.create_primitive", shape="Sphere", size=[40, 40, 26], location=[cx + 75 * math.cos(a), cy + 75 * math.sin(a), gz - 5], material="stone",
                name=f"FireStone{k}")
    chest = uk.call("build.place_mesh", asset=asset("SM_PH_WoodenCrate01"), location=[cx - 500, cy + 300, ground(cx - 500, cy + 300)], rotation=[0, 25, 0],
                    scale=[0.9, 0.6, 0.6], name="TreasureChest")["id"]
    uk.call("build.create_primitive", shape="Box", size=[60, 40, 6], location=[cx - 500, cy + 300, ground(cx - 500, cy + 300) + 62], material="gold",
            name="TreasureGold")


def stage_look():
    uk.call("style.apply", preset="stylized", lighting_enabled=False)
    uk.call("sky.set_time", hour=16.2)


if __name__ == "__main__":
    uk.connect()
    stage_ground()
    stage_water_player()
    stage_vegetation()
    stage_coast_props()
    stage_camp()
    stage_look()
    uk.call("camera.reset_camera")
