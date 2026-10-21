"""
One village scene rendered in every art style preset (style.apply), a screenshot per style.

    python Scripts/examples/style_showcase.py [--out-dir Saved/StyleShots] [--styles toon,lowpoly]
"""

import argparse
import math
import os
import random
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from rtbuild import *  # noqa: E402,F403

GRASS = mat("grass", color=[0.2, 0.42, 0.12])
PATH = mat("sand", color=[0.62, 0.52, 0.36])
WATER = mat("concrete", color=[0.1, 0.35, 0.55], roughness=0.08, variation=0.0)
WALLS = [mat("plaster", color=c) for c in ([0.92, 0.86, 0.72], [0.85, 0.62, 0.45], [0.75, 0.82, 0.9], [0.95, 0.92, 0.88])]
ROOFS = [mat("roof_tile", color=c) for c in ([0.62, 0.2, 0.12], [0.25, 0.32, 0.5], [0.45, 0.25, 0.15])]
WOOD = mat("wood", color=[0.38, 0.24, 0.13])
LEAF = [mat("grass", color=c, variation=0.05) for c in ([0.15, 0.45, 0.12], [0.3, 0.55, 0.15], [0.55, 0.6, 0.15])]
ROCK = mat("stone", color=[0.5, 0.5, 0.52])
STONE = mat("cobblestone", color=[0.55, 0.53, 0.5])


def house(x, y, yaw, w, d, h, rnd):
    wall, roofing = rnd.choice(WALLS), rnd.choice(ROOFS)
    P = Parts()
    P += box([w, d, h], at=[x, y, 0], rot=[0, yaw, 0], **wall)
    P += roof(w + 60, d + 60, h * 0.6, at=[x, y, h], rot=[0, yaw, 0], **roofing)
    c, s = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))
    # door and two windows on the front face
    fx, fy = x + c * 0 - s * (-d / 2 - 2), y + s * 0 + c * (-d / 2 - 2)
    P += box([90, 8, 200], at=[fx, fy, 0], rot=[0, yaw, 0], **WOOD)
    for o in (-w * 0.3, w * 0.3):
        P += box([70, 8, 70], at=[fx + c * o, fy + s * o, h * 0.55], rot=[0, yaw, 0], **mat("concrete", color=[0.15, 0.2, 0.3], roughness=0.1))
    P += box([50, 50, h * 0.5], at=[x + c * w * 0.25, y + s * w * 0.25, h * 1.15], rot=[0, yaw, 0], **STONE)  # chimney
    return P


def tree(x, y, rnd):
    P = Parts()
    h = rnd.uniform(500, 900)
    P += cyl(50, h * 0.45, at=[x, y, 0], detail="low", **WOOD)
    leaf = rnd.choice(LEAF)
    if rnd.random() < 0.5:
        for k in range(3):  # pine: stacked cones
            P += cone(h * (0.75 - k * 0.18), h * 0.4, at=[x, y, h * (0.3 + k * 0.22)], detail="low", **leaf)
    else:
        P += sphere(h * 0.6, at=[x, y, h * 0.45], detail="low", **leaf)
        P += sphere(h * 0.4, at=[x + h * 0.2, y, h * 0.65], detail="low", **leaf)
    return P


def scene(seed=7):
    rnd = random.Random(seed)
    P = Parts()
    P += box([30000, 30000, 40], at=[0, 0, -40], **GRASS)
    P += box([400, 9000, 6], at=[0, 0, 0], **PATH)
    P += box([9000, 400, 6], at=[0, 1500, 0], **PATH)
    P += cyl(2600, 10, at=[3200, -2200, -4], **WATER)  # pond
    for i, (x, y) in enumerate([(-900, -2500), (900, -1600), (-1000, -600), (1100, 300), (-900, 2600), (1800, 2600), (-2600, 2400), (2700, 600)]):
        yaw = 90 if x < 0 else -90
        P += house(x, y, yaw, rnd.uniform(500, 750), rnd.uniform(450, 600), rnd.uniform(320, 480), rnd)
    # well in the square
    P += tube(260, 100, 30, at=[500, 1500, 0], **STONE)
    P += box([20, 20, 220], at=[400, 1500, 0], **WOOD)
    P += box([20, 20, 220], at=[600, 1500, 0], **WOOD)
    P += roof(300, 200, 90, at=[500, 1500, 220], **ROOFS[0])
    for _ in range(70):
        x, y = rnd.uniform(-7000, 7000), rnd.uniform(-6000, 7000)
        if abs(x) < 2200 and abs(y) < 3600 or math.hypot(x - 3200, y + 2200) < 1600:
            continue
        P += tree(x, y, rnd)
    for _ in range(25):
        x, y = rnd.uniform(-6000, 6000), rnd.uniform(-5000, 6000)
        if abs(x) < 400:
            continue
        s = rnd.uniform(80, 260)
        P += sphere(s, at=[x, y, -s * 0.25], detail="low", scale=[1.3, 1, 0.7], **ROCK)
    return P


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out-dir", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "Saved", "StyleShots"))
    ap.add_argument("--styles", default="")
    args = ap.parse_args()
    os.makedirs(args.out_dir, exist_ok=True)
    rt = Runtime()
    root = rt.group("Village")
    rt.build("Village", scene(), parent=root)
    rt.call("camera.set_camera", location=[-5200, -6800, 2600], look_at=[300, 400, 200], fov=55)
    styles = [s for s in args.styles.split(",") if s] or [p["name"] for p in rt.call("style.list_presets")["presets"]]
    for s in styles:
        rt.call("style.apply", preset=s)
        time.sleep(2.0)  # let exposure and lighting settle
        path = os.path.join(args.out_dir, f"style_{s}.jpg")
        rt.screenshot(path)
        print("saved", path)


if __name__ == "__main__":
    main()
