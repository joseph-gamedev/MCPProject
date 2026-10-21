"""
Eiffel Tower and Great Pyramid of Giza at true scale, written as a generator script (rtbuild).
About 1,400 parts sent in a handful of build.create_assembly calls.

    python Scripts/examples/eiffel_and_pyramid.py [--out shot.jpg]
"""

import argparse
import math
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from rtbuild import *  # noqa: E402,F403

IRON = mat("painted_iron", color=[0.15, 0.1, 0.065], roughness=0.55, metallic=0.35)
IRON_DARK = mat("painted_iron", color=[0.1, 0.068, 0.045], roughness=0.6, metallic=0.35)
GLASS = mat("tinted_glass")
GRANITE = mat("granite", color=[0.42, 0.4, 0.37])

# Outer half-width of the tower (cm) at height z (cm), from its real outline.
OUTLINE = [(0, 6250), (2800, 4750), (5760, 3550), (8600, 2650), (11570, 1900), (16000, 1250), (20000, 880), (24000, 640), (27600, 470), (30000, 340)]
# Width of each leg; the legs meet at the second floor.
LEG = [(0, 2600), (5760, 2150), (11570, 1900)]


def interp(table, z):
    for (z0, a), (z1, b) in zip(table, table[1:]):
        if z <= z1:
            t = (z - z0) / (z1 - z0)
            return a + (b - a) * t
    return table[-1][1]


def W(z):
    return interp(OUTLINE, z)


def L(z):
    return min(interp(LEG, z), W(z))


def lattice_face(P, a0, a1, b0, b1, subdivisions, chord, brace):
    """One truss face between bottom edge a0-a1 and top edge b0-b1: X bracing in `subdivisions` panels."""
    for k in range(subdivisions):
        t0, t1 = k / subdivisions, (k + 1) / subdivisions
        p00, p01 = lerp(a0, a1, t0), lerp(a0, a1, t1)
        p10, p11 = lerp(b0, b1, t0), lerp(b0, b1, t1)
        P += beam(p00, p11, brace, **IRON)
        P += beam(p01, p10, brace, **IRON)
        if 0 < k:
            P += beam(p00, p10, brace * 0.8, **IRON)  # vertical web between panels


def leg_square(z, sx, sy):
    w, l = W(z), L(z)
    xo, yo, xi, yi = sx * w, sy * w, sx * (w - l), sy * (w - l)
    return [[xo, yo, z], [xi, yo, z], [xi, yi, z], [xo, yi, z]]


def eiffel():
    P = Parts()
    levels = [0, 1440, 2880, 4320, 5760, 7200, 8650, 10100, 11570]
    for sx, sy in [(1, 1), (-1, 1), (-1, -1), (1, -1)]:
        # stone pier under each leg
        c = leg_square(0, sx, sy)
        cx, cy = (c[0][0] + c[2][0]) / 2, (c[0][1] + c[2][1]) / 2
        P += box([L(0) + 300, L(0) + 300, 250], at=[cx, cy, -250], bevel=8, **GRANITE)
        for z0, z1 in zip(levels, levels[1:]):
            lo, hi = leg_square(z0, sx, sy), leg_square(z1, sx, sy)
            for i in range(4):
                P += beam(lo[i], hi[i], 110, **IRON_DARK)          # corner chords
                P += beam(hi[i], hi[(i + 1) % 4], 70, **IRON)      # ring girder
                lattice_face(P, lo[i], lo[(i + 1) % 4], hi[i], hi[(i + 1) % 4], 3, 110, 32)
    # Shaft above the second floor: one square truss tapering to the top.
    shaft = [11570, 13200, 14800, 16400, 18000, 19600, 21200, 22800, 24400, 26000, 27600, 29000, 30000]
    for z0, z1 in zip(shaft, shaft[1:]):
        lo, hi = rect(2 * W(z0), 2 * W(z0), z0), rect(2 * W(z1), 2 * W(z1), z1)
        for i in range(4):
            P += beam(lo[i], hi[i], 90, **IRON_DARK)
            P += beam(hi[i], hi[(i + 1) % 4], 50, **IRON)
            lattice_face(P, lo[i], lo[(i + 1) % 4], hi[i], hi[(i + 1) % 4], 2 if z0 < 20000 else 1, 90, 26)
    # Decorative arches under the first floor, one per face, following the lean of the legs.
    for face in range(4):
        path = []
        span = W(1200) - L(1200) + 300
        for i in range(25):
            a = math.pi * i / 24
            x = span * math.cos(a)
            z = 1200 + (5300 - 1200) * math.sin(a) ** 0.9
            y = -(W(z) - 120)
            path.append(rotate_z([x, y, z], face * 90))
        P += sweep([[-90, -70], [90, -70], [90, 70], [-90, 70]], path, **IRON_DARK)
        inner = [[p[0] * 0.93, p[1] * 0.93, p[2] - 250] for p in path[3:-3]]
        P += sweep([[-40, -40], [40, -40], [40, 40], [-40, 40]], inner, **IRON)
        for i in range(4, 21, 2):  # spandrel posts from the arch up to the deck
            top = [path[i][0], path[i][1], 5600]
            P += beam(path[i], top, 30, **IRON)
    P += platforms()
    # Summit: cabin, lantern, antenna.
    P += box([1650, 1650, 280], at=[0, 0, 27600], bevel=10, **IRON_DARK)
    P += box([1250, 1250, 650], at=[0, 0, 27880], **GLASS)
    for x, y in [(-625, -625), (625, -625), (625, 625), (-625, 625)]:
        P += box([60, 60, 650], at=[x, y, 27880], **IRON_DARK)
    P += box([1350, 1350, 120], at=[0, 0, 28530], **IRON_DARK)
    P += box([700, 700, 700], at=[0, 0, 28650], **IRON)
    P += cyl(420, 450, at=[0, 0, 29350], **IRON_DARK)
    P += dome(460, 260, at=[0, 0, 29800], **IRON_DARK)
    P += taper(70, 15, 3000, sides=8, at=[0, 0, 30000], **mat("steel"))
    return P


def deck(P, z, outer, inner, thickness, frieze_cols):
    """Square ring platform with an arcaded frieze girder on every side and a railing."""
    band = (outer - inner) / 2
    for face in range(4):
        yaw = face * 90
        c = rotate_z([0, -(outer / 2 - band / 2), 0], yaw)
        size = [outer, band, thickness] if face % 2 == 0 else [band, outer, thickness]
        cut_size = [outer, 60, thickness * 0.75] if face % 2 == 0 else [60, outer, thickness * 0.75]
        frieze = {"op": "subtract", "location": rotate_z([0, -outer / 2, thickness * 0.1], yaw),
                  "rotation": [0, yaw, 0],
                  "tool": {"shape": "openings", "size": [outer, 80, thickness * 0.8], "rows": 1, "cols": frieze_cols, "arched": True}}
        P += box(size, at=[c[0], c[1], z], ops=[frieze], **IRON_DARK)
        a = rotate_z([-outer / 2, -outer / 2, z + thickness + 110], yaw)
        b = rotate_z([outer / 2, -outer / 2, z + thickness + 110], yaw)
        P += beam(a, b, 14, **IRON)  # railing
        for i in range(0, 41):
            p = lerp(a, b, i / 40)
            P += beam([p[0], p[1], z + thickness], p, 8, **IRON)


def platforms():
    P = Parts()
    deck(P, 5760, 7070, 3000, 420, 30)
    deck(P, 11570, 4050, 1500, 320, 18)
    # glazed pavilions on the first floor
    for face in range(4):
        c = rotate_z([0, -2700, 6180], face * 90)
        size = [3600, 700, 600] if face % 2 == 0 else [700, 3600, 600]
        P += box(size, at=c, **GLASS)
        P += box([size[0] + 60, size[1] + 60, 50], at=[c[0], c[1], c[2] + 600], **IRON_DARK)
    return P


def pyramid():
    P = Parts()
    stone = mat("limestone", color=[0.42, 0.34, 0.24], style="blocks", tile=140, relief=3, variation=0.3)
    P += tiers(23040, 23040, 13860, count=110, taper=0.045, bevel=0, **stone)
    # Remnant of smooth casing stones near the top and the entrance with its gabled stones on the north face.
    P += box([700, 700, 120], at=[0, 0, 13860], **stone)
    north = -23040 / 2 * (1 - 1700 / 13860)
    P += box([700, 300, 260], at=[0, north + 700, 1700], **mat("limestone", color=[0.55, 0.45, 0.31], style="blocks", tile=80))
    P += roof(900, 600, 380, at=[0, north + 850, 1960], rot=[0, 90, 0], **mat("limestone", color=[0.66, 0.55, 0.4], style="blocks", tile=120))
    P += box([180, 400, 220], at=[0, north + 400, 1500], **mat("black"))
    # Scattered blocks and rubble at the base.
    import random
    rnd = random.Random(7)
    for _ in range(60):
        side = rnd.choice([-1, 1])
        x = rnd.uniform(-12500, 12500)
        y = side * rnd.uniform(11800, 13500)
        s = rnd.uniform(60, 220)
        if rnd.random() < 0.5:
            x, y = y, x
        P += box([s * rnd.uniform(1, 1.6), s, s * rnd.uniform(0.6, 1)], at=[x, y, 0], rot=[0, rnd.uniform(0, 90), rnd.uniform(-8, 8)], bevel=s * 0.06, **stone)
    return P


def ground():
    P = Parts()
    P += box([3000000, 3000000, 20], at=[-10000, 0, -20], **mat("sand", color=[0.45, 0.37, 0.26], tile=400))
    P += box([30000, 9000, 22], at=[0, -14500, -20], **mat("grass", color=[0.06, 0.13, 0.04]))  # Champ de Mars lawn
    P += box([30000, 1200, 24], at=[0, -14500, -20], **mat("sand", color=[0.7, 0.62, 0.48], tile=150))
    leaves = mat("grass", color=[0.03, 0.07, 0.025], relief=6, tile=40, roughness=0.9)
    bark = mat("wood", color=[0.08, 0.06, 0.04])
    for i in range(-12, 13):  # chestnut rows along the lawn
        for y in (-18600, -10400):
            x = i * 1200
            P += cyl(60, 500, at=[x, y, 0], **bark)
            P += sphere(900, at=[x, y, 350], scale=[1, 1, 1.15], **leaves)
    return P


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://127.0.0.1:8018/mcp")
    ap.add_argument("--out", default=None)
    ap.add_argument("--clear", action="store_true", help="remove this session's earlier builds first")
    args = ap.parse_args()
    rt = Runtime(args.url)
    if args.clear:
        rt.call("scene.clear_all", confirm=True)
    t = time.time()
    root = rt.group("Landmarks")
    rt.build("Ground", ground(), parent=root)
    tower = rt.group("EiffelTower", parent=root)
    parts = eiffel()
    rt.build("Eiffel", parts, parent=tower)
    giza = rt.group("GreatPyramid", location=[-33000, 0, 0], parent=root)
    pparts = pyramid()
    rt.build("Pyramid", pparts, parent=giza)
    print(f"built {len(parts) + len(pparts)} parts in {time.time() - t:.1f}s")
    rt.call("ui.create_label", target=tower, text="Eiffel Tower")
    rt.call("ui.create_label", target=giza, text="Great Pyramid of Giza")
    rt.call("lighting.apply_mood", mood="sunset")
    rt.call("lighting.set_sun", rotation=[-24, 62, 0], temperature=5200)  # late sun from behind the camera, left
    rt.call("camera.set_camera", location=[-16000, -68000, 5000], look_at=[-16000, 0, 9000], fov=60)
    if args.out:
        rt.call("ops.wait", seconds=20)  # first run: let new shaders compile
        rt.screenshot(args.out, show_ui=True)
        print("saved", args.out)


if __name__ == "__main__":
    main()
