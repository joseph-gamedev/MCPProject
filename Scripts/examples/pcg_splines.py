"""
Splines + PCG: a valley built from a few splines and PCG scatters.

  * a winding road and a river (spline.create with a ribbon profile)
  * a stone wall around a field (closed spline, wall profile)
  * a forest filling a closed spline, kept clear of the road and the river (pcg.scatter mode inside + exclude)
  * trees lining both sides of the road, fence posts and lamps following it (pcg.scatter along + offset/align)
  * rocks on the riverbanks

Every object type is built once as a template (build.create_assembly) and PCG places GPU instances of it.

    python Scripts/examples/pcg_splines.py [--out shot.jpg] [--seed 3] [--style realistic]
"""

import argparse
import math
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from rtbuild import *  # noqa: E402,F403

HIDE_Z = -60000  # templates live far below the ground (PCG ignores them when snapping)


def templates(rt):
    """One builder object per kind of thing PCG will place. Returns name -> node id."""
    bark = mat("wood", color=[0.3, 0.2, 0.12])
    leaf = [mat("grass", color=c, variation=0.08) for c in ([0.13, 0.32, 0.1], [0.22, 0.42, 0.12], [0.35, 0.45, 0.14])]
    ids = {}

    # Pine: trunk + stacked cones.
    pine = [cyl(40, 300, at=[0, 0, 0], **bark)]
    for k in range(4):
        pine.append(cone(420 - k * 85, 300, at=[0, 0, 180 + k * 150], detail="low", **leaf[0]))
    # Broadleaf: trunk + clustered spheres.
    oak = [cyl(55, 380, at=[0, 0, 0], **bark)]
    for dx, dy, dz, d in [(0, 0, 500, 420), (140, 60, 420, 300), (-130, -40, 430, 320), (40, -120, 600, 280), (-50, 110, 580, 260)]:
        oak.append(sphere(d, at=[dx, dy, dz], detail="low", **leaf[1]))
    birch = [cyl(30, 520, at=[0, 0, 0], **mat("plaster", color=[0.85, 0.83, 0.78]))]
    for dz, d in [(520, 260), (640, 200), (430, 220)]:
        birch.append(sphere(d, at=[0, 0, dz], detail="low", **leaf[2]))
    rock = [sphere(160, at=[0, 0, -30], detail="low", scale=[1.4, 1.0, 0.7], **mat("stone", color=[0.48, 0.47, 0.45])),
            sphere(90, at=[70, 40, -10], detail="low", **mat("stone", color=[0.42, 0.41, 0.4]))]
    post = [box([18, 18, 130], at=[0, 0, 0], **mat("wood", color=[0.36, 0.25, 0.15])),
            box([18, 400, 12], at=[0, 0, 95], **mat("wood", color=[0.4, 0.28, 0.17])),
            box([18, 400, 12], at=[0, 0, 50], **mat("wood", color=[0.4, 0.28, 0.17]))]
    lamp = [cyl(22, 420, at=[0, 0, 0], **mat("painted_iron", color=[0.08, 0.08, 0.09])),
            box([120, 14, 14], at=[50, 0, 410], **mat("painted_iron", color=[0.08, 0.08, 0.09])),
            sphere(45, at=[105, 0, 385], **mat("plaster", color=[1, 0.85, 0.6], emissive=[1, 0.75, 0.45], emissive_strength=12))]
    bush = [sphere(d, at=[x, y, d * 0.3], detail="low", **leaf[1]) for x, y, d in [(0, 0, 160), (70, 30, 120), (-60, 40, 110)]]
    # Templates are built far below the ground; only their PCG instances are seen.
    for i, (name, parts) in enumerate([("pine", pine), ("oak", oak), ("birch", birch), ("rock", rock), ("fence", post), ("lamp", lamp), ("bush", bush)]):
        ids[name] = rt.build(f"tpl_{name}", parts, location=[i * 2000, 0, HIDE_Z])[0]
    return ids


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "Saved", "pcg_splines.jpg"))
    ap.add_argument("--seed", type=int, default=3)
    ap.add_argument("--style", default="")
    args = ap.parse_args()
    rt = Runtime()

    # Ground.
    rt.build("Ground", [box([60000, 60000, 100], at=[0, 0, -100], **mat("grass", color=[0.22, 0.38, 0.12]))])
    T = templates(rt)

    # Splines.
    road_pts = [[-26000, -9000, 0], [-15000, -3000, 0], [-6000, -5500, 0], [2000, 1000, 0], [9000, 4000, 0], [17000, 1500, 0], [26000, 6000, 0]]
    road = rt.call("spline.create", name="Road", points=road_pts, follow_ground=True,
                   profile={"kind": "ribbon", "width": 700, "height": 6, "material": "asphalt", "color": [0.16, 0.16, 0.17]})["id"]
    river_pts = [[-8000, 26000, 0], [-5000, 15000, 0], [1000, 9000, 0], [3500, 0, 0], [1000, -9000, 0], [6000, -18000, 0], [4000, -26000, 0]]
    river = rt.call("spline.create", name="River", points=river_pts, follow_ground=True,
                    profile={"kind": "ribbon", "width": 1400, "height": 4, "z_offset": 3, "material": "concrete",
                             "color": [0.1, 0.3, 0.42], "roughness": 0.05, "variation": 0.0})["id"]
    field_pts = [[9000, -6000, 0], [19000, -5000, 0], [21000, -15000, 0], [11000, -17000, 0]]
    wall = rt.call("spline.create", name="FieldWall", points=field_pts, closed=True, linear=True, follow_ground=True,
                   profile={"kind": "wall", "width": 60, "height": 110, "material": "stone", "color": [0.55, 0.53, 0.5]})["id"]
    forest_pts = [[-25000, 2000, 0], [-14000, 9000, 0], [-6000, 22000, 0], [-20000, 25000, 0], [-27000, 14000, 0]]
    forest = rt.call("spline.create", name="ForestArea", points=forest_pts, closed=True)["id"]
    woods2 = rt.call("spline.create", name="EastWoods", points=[[12000, 9000, 0], [25000, 11000, 0], [24000, 24000, 0], [8000, 22000, 0]], closed=True)["id"]

    keep_clear = [{"spline": road, "clearance": 900}, {"spline": river, "clearance": 1200}]
    s = args.seed
    scatters = [
        dict(name="Forest", spline=forest, mode="inside", spacing=650, min_distance=420, keep=0.85, noise_scale=6000, exclude=keep_clear,
             items=[{"template": T["pine"], "weight": 3, "scale": [0.8, 1.5]}, {"template": T["oak"], "weight": 1, "scale": [0.8, 1.3]},
                    {"template": T["bush"], "weight": 1.5, "scale": [0.7, 1.4]}]),
        dict(name="EastWoods", spline=woods2, mode="inside", spacing=800, min_distance=500, keep=0.6, noise_scale=4000, exclude=keep_clear,
             items=[{"template": T["oak"], "weight": 2, "scale": [0.9, 1.4]}, {"template": T["birch"], "weight": 2, "scale": [0.8, 1.2]},
                    {"template": T["bush"], "weight": 1}]),
        dict(name="RoadTrees", spline=road, mode="along", spacing=900, offset=[900, 1600], min_distance=500, exclude=[{"spline": river, "clearance": 1100}],
             items=[{"template": T["birch"], "weight": 2, "scale": [0.9, 1.2]}, {"template": T["oak"], "weight": 1, "scale": [0.8, 1.1]}]),
        dict(name="Fence", spline=road, mode="along", side="right", spacing=400, offset=[520, 540], jitter=0, min_distance=100,
             exclude=[{"spline": river, "clearance": 900}], items=[{"template": T["fence"], "align": True, "random_yaw": False}]),
        dict(name="Lamps", spline=road, mode="along", side="left", spacing=2400, offset=[480, 500], jitter=0, min_distance=100,
             exclude=[{"spline": river, "clearance": 900}], items=[{"template": T["lamp"], "align": True, "random_yaw": False, "z_offset": 0}]),
        dict(name="RiverRocks", spline=river, mode="along", spacing=350, offset=[700, 1100], min_distance=200, keep=0.7, noise_scale=2500,
             exclude=[{"spline": road, "clearance": 800}], items=[{"template": T["rock"], "scale": [0.5, 1.6]}, {"template": T["bush"], "weight": 0.5}]),
    ]
    for sc in scatters:
        r = rt.call("pcg.scatter", seed=s, **sc)
        print(f"{sc['name']:>11}: {r['pcg_points']} PCG points -> {r['instances']} instances",
              {i['template']: i['instances'] for i in r['items']})

    if args.style:
        rt.call("style.apply", preset=args.style)
    rt.call("lighting.apply_mood", mood="morning")
    rt.call("camera.set_camera", location=[-21000, -24000, 9000], look_at=[-2000, 0, 0], fov=60)
    time.sleep(3)
    rt.screenshot(args.out)
    print("saved", args.out)
    # A second, closer view along the road.
    rt.call("camera.set_camera", location=[-9000, -9500, 900], look_at=[-1000, -2500, 300], fov=65)
    time.sleep(2)
    close = args.out.replace(".jpg", "_close.jpg")
    rt.screenshot(close)
    print("saved", close)


if __name__ == "__main__":
    main()
