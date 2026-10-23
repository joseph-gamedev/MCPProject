"""Builds a facade kit for one style in Blender (headless) from the shared families in facade_lib.

    blender -b --python Scripts/architecture/make_style.py -- paris|mediterranean [out_dir]

Pieces are one bay wide and one storey (or one feature) tall, real scale, pivot = bay centre on the wall's front
plane at the storey floor (see facade_lib). The runtime recipe (kit_examples/facade_styles.py) stacks them.
"""

import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import facade_lib as F  # noqa: E402
from facade_lib import bx, prism, prism_yz  # noqa: E402

ARGS = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
STYLE = ARGS[0] if ARGS else "paris"
OUT = os.path.abspath(ARGS[1]) if len(ARGS) > 1 else os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "Saved", "Architecture",
                                                                                      STYLE.title()))


def cut_wall(W, H, T, openings, slot="wall", joints=(), joint_depth=0.03, joint_h=0.03, radial=None, verticals=()):
    """Solid wall bay with openings [(ox, sill, head, arch)] cut through it and joints grooved into the face."""
    import bpy
    wall = bx(-W / 2, W / 2, 0, T, 0, H, slot)
    cutters = []
    for ox, sl, hd, arch in openings:
        w = ox / 2
        if arch == "round":
            pts = [(-w, sl), (w, sl), (w, hd)] + F.arc(0, hd, w, 0, math.pi, 24)[1:-1] + [(-w, hd)]
        elif arch == "segment":
            rise = ox / 6
            r = (w * w + rise * rise) / (2 * rise)
            a = math.asin(w / r)
            top = [(r * math.sin(t), hd + rise - r + r * math.cos(t)) for t in [a - 2 * a * i / 16 for i in range(17)]]
            pts = [(-w, sl), (w, sl)] + top
        else:
            pts = [(-w, sl), (w, sl), (w, hd), (-w, hd)]
        cutters.append(prism(pts, -1.0, T + 1.0, slot))
    coll = bpy.data.collections.new("open")
    bpy.context.scene.collection.children.link(coll)
    for c in cutters:
        bpy.context.scene.collection.objects.unlink(c)
        coll.objects.link(c)
    if cutters:
        mod = wall.modifiers.new("open", "BOOLEAN")
        mod.operation, mod.operand_type, mod.collection, mod.solver = "DIFFERENCE", "COLLECTION", coll, "EXACT"
        bpy.context.view_layer.objects.active = wall
        bpy.ops.object.modifier_apply(modifier=mod.name)
    for c in cutters:
        bpy.data.objects.remove(c, do_unlink=True)
    bpy.data.collections.remove(coll)
    if joints or radial or verticals:
        F.grooves([wall], joints, joint_depth, joint_h, radial, verticals)
    return wall


def lines(H, step, start=None, skip=()):
    z = start if start is not None else step
    out = []
    while z < H - 0.05:
        if not any(a <= z <= b for a, b in skip):
            out.append(z)
        z += step
    return out

# ============================================================================================== PARIS (Haussmann)


def paris():
    W, T = 2.5, 0.5
    P = "SM_PA_"
    out = OUT
    HB = 6.4  # ground floor 3.9 + entresol 2.5, one rusticated base

    def base(kind):
        F.reset()
        ents = (1.15, 4.35, 5.85, None)
        if kind == "Shop":
            cut_wall(W, HB, T, [(1.95, 0, 3.55, None), ents], joints=lines(HB, 0.42, skip=[(0, 3.6), (4.3, 5.9)]), joint_depth=0.035)
            F.shopfront(1.95, 3.55, yf=0.18)
        elif kind == "Door":
            cut_wall(W, HB, T, [(2.3, 0, 3.6, "round")], joints=lines(HB, 0.42, skip=[(0, 4.8)]), radial=(0, 3.6, 1.15, 1.75, 9), joint_depth=0.035)
            F.door_leaves(2.3, 3.6, 0.32, leaves=2, rows=(0.22, 0.48, 0.3))
            F.glass_pane(2.3, 3.6, 4.75, 0.36, arch="round", head=3.6)
            for i in range(1, 8):  # iron fan grille
                a = math.pi * i / 8
                F.bar(0.15 * math.cos(a), 3.6 + 0.15 * math.sin(a), 1.12 * math.cos(a), 3.6 + 1.12 * math.sin(a), 0.03, 0.33, 0.36, "metal")
            F.ring(0, 3.6, 0.12, 0.17, 0.33, 0.36, "metal", 12)
            bx(-1.15, 1.15, 0.33, 0.36, 3.56, 3.64, "metal")
            prism([(-0.16, 4.62), (0.16, 4.62), (0.24, 5.25), (-0.24, 5.25)], -0.07, 0.02, "wall")      # keystone
        elif kind == "Window":
            cut_wall(W, HB, T, [(1.3, 0.9, 3.4, None), ents], joints=lines(HB, 0.42, skip=[(0.85, 3.45), (4.3, 5.9)]), joint_depth=0.035)
            F.casement(1.3, 0.9, 3.4, yf=0.18, leaves=2, rows=4, transom=2.8)
            F.railing(-0.65, 0.65, 0.12, 0.9, 0.7, spacing=0.1)
            F.sill(1.3, 0.9)
        else:
            cut_wall(W, HB, T, [], joints=lines(HB, 0.42), joint_depth=0.035)
        if kind in ("Shop", "Window"):
            F.casement(1.15, 4.35, 5.85, yf=0.16, leaves=2, rows=3)
        bx(-W / 2, W / 2, -0.12, 0.02, HB - 0.32, HB - 0.2, "wall")              # band under the balcony
        F.finish(f"{P}Base_{kind}", out)

    for k in ("Shop", "Door", "Window", "Blank"):
        base(k)

    def floor(name, H, ox, head, transom, hood, bal, rail_motif="scroll", keystone=False):
        F.reset()
        cut_wall(W, H, T, [(ox, 0.05, head, None)], joints=lines(H, 0.45), joint_depth=0.012, joint_h=0.012)
        F.casement(ox, 0.05, head, yf=0.16, leaves=2, rows=4 if transom else 3, transom=transom)
        F.surround(ox, 0.05, head, width=0.15, hood=hood, keystone=keystone)
        if bal == "continuous":
            F.balcony(-W / 2, W / 2, 0.75, 0.0, thick=0.18, corbels=2, rail_h=1.0, motif=rail_motif, returns=False)
        elif bal == "railing":
            F.railing(-ox / 2, ox / 2, -0.03, 0.05, 0.95, motif="rings")
        F.finish(f"{P}{name}", out)

    floor("Noble", 3.4, 1.25, 2.7, 2.2, "cornice", "continuous")
    floor("Noble_Pediment", 3.4, 1.25, 2.7, 2.2, "pediment", "continuous")
    floor("Upper", 3.1, 1.2, 2.5, 2.0, None, "railing", keystone=True)
    floor("Upper_Plain", 3.1, 1.2, 2.5, 2.0, None, "railing")
    floor("Top", 2.9, 1.15, 2.3, None, None, "continuous", rail_motif="rings")
    F.reset()
    cut_wall(W, 3.1, T, [], joints=lines(3.1, 0.45), joint_depth=0.012, joint_h=0.012)
    F.finish(f"{P}Upper_Blank", out)

    # main cornice with modillions
    F.reset()
    F.cornice(W, [(0.5, 0), (0, 0), (0, 0.12), (-0.08, 0.16), (-0.1, 0.3), (-0.45, 0.36), (-0.55, 0.44), (-0.58, 0.55), (0.5, 0.55)], modillions=4,
              mod_depth=0.42, mod_z=0.34)
    F.finish(f"{P}Cornice", out)

    def mansard(dormer):
        F.reset()
        # zinc brisis (75 deg) from behind the cornice, then the shallow terrasson; body back to 6 m
        prism_yz([(0.35, 0), (6.0, 0), (6.0, 3.7), (1.2, 3.7), (1.1, 3.0)], -W / 2, W / 2, "roof")
        bx(-W / 2, W / 2, 0.3, 1.2, 0, 0.12, "roof")
        if dormer:
            dw, z0, z1, yd = 1.0, 0.35, 2.0, 0.42
            bx(-dw / 2 - 0.12, dw / 2 + 0.12, yd, 2.0, z0 - 0.1, z1 + 0.15, "roof")               # dormer body (zinc cheeks)
            F.casement(dw, z0, z1, yf=yd - 0.14, leaves=2, rows=3)
            bx(-dw / 2 - 0.16, -dw / 2, yd - 0.18, yd, z0 - 0.1, z1 + 0.1, "trim")
            bx(dw / 2, dw / 2 + 0.16, yd - 0.18, yd, z0 - 0.1, z1 + 0.1, "trim")
            bx(-dw / 2 - 0.16, dw / 2 + 0.16, yd - 0.2, yd, z0 - 0.16, z0, "trim")
            r = dw / 2 + 0.3
            F.ring(0, z1 + 0.1, r - 0.18, r, yd - 0.2, yd + 0.4, "trim", 16)                     # rounded hood
            prism(F.arc(0, z1 + 0.1, r - 0.18, 0, math.pi, 16), yd - 0.05, yd + 0.4, "roof")
            bx(-dw / 2, dw / 2, yd - 0.16, yd - 0.02, z1, z1 + 0.1, "trim")
        F.finish(f"{P}Mansard{'_Dormer' if dormer else ''}", out)

    mansard(True)
    mansard(False)
    # party-wall chimney stack: rendered masonry, tall, with a row of terracotta pots
    F.reset()
    bx(-0.4, 0.4, 0, 3.0, 0, 2.3, "trim")
    bx(-0.46, 0.46, -0.06, 3.06, 2.3, 2.42, "trim")
    F.chimney_pots(0, 1.5, 2.42, 8, along="y", spacing=0.34)
    F.finish(f"{P}Chimney", out)
    F.blocks(P, ["wall", "trim", "roof", "paving", "road", "kerb", "soil"], out)

# ============================================================================================== MEDITERRANEAN


def mediterranean():
    W, T = 2.2, 0.5
    P = "SM_ME_"
    out = OUT
    HG, HU = 3.2, 2.9

    def band_surround(ox, sl, hd, w=0.13):
        return F.surround(ox, sl, hd, width=w, proud=0.015, ears=False)

    # ground floor
    F.reset()
    cut_wall(W, HG, T, [(1.2, 0, 2.2, "round")])
    F.door_leaves(1.2, 2.2, 0.28, leaves=2, rows=(0.5, 0.5))
    prism([(-0.6, 2.2), (0.6, 2.2)] + F.arc(0, 2.2, 0.6, 0, math.pi, 16)[1:-1], 0.29, 0.34, "door")
    F.ring(0, 2.2, 0.6, 0.78, -0.03, 0.02, "trim", 20)
    bx(-0.78, -0.6, -0.03, 0.02, 0, 2.2, "trim")
    bx(0.6, 0.78, -0.03, 0.02, 0, 2.2, "trim")
    bx(-0.75, 0.75, -0.25, 0.3, -0.02, 0.0, "trim")
    F.lathe([(0, 0), (0.03, 0), (0.03, 0.06), (0, 0.06)], 0.18, -0.02, 1.0, "brass", 8)
    F.finish(f"{P}Ground_Door", out)

    F.reset()
    cut_wall(W, HG, T, [(0.9, 1.0, 2.2, None)])
    F.casement(0.9, 1.0, 2.2, yf=0.2, leaves=2, rows=2)
    band_surround(0.9, 1.0, 2.2)
    F.sill(0.9, 1.0)
    for i in range(6):                       # iron window grille (reja) standing proud of the wall
        x = -0.45 + 0.9 * (i + 0.5) / 6
        bx(x - 0.011, x + 0.011, -0.13, -0.108, 0.95, 2.25, "metal")
    for z in (1.0, 1.6, 2.2):
        bx(-0.5, 0.5, -0.135, -0.105, z - 0.015, z + 0.015, "metal")
    for x in (-0.5, 0.5):
        bx(x - 0.015, x + 0.015, -0.135, 0.0, 0.95, 2.25, "metal")
    F.finish(f"{P}Ground_Window", out)

    F.reset()
    cut_wall(W, HG, T, [(1.8, 0, 2.55, "segment")])
    F.shopfront(1.8, 2.55, yf=0.2, door_w=0.9, fascia=0.35)
    prism([(-0.9, 2.55), (0.9, 2.55)] + [(0.9 * math.cos(t), 2.55 + 0.3 * math.sin(t)) for t in [math.pi * i / 12 for i in range(13)]][1:-1], 0.2, 0.25, "shop")
    prism_yz([(0.0, 2.95), (-1.1, 2.45), (-1.1, 2.3), (-1.05, 2.3), (-1.05, 2.42), (0.0, 2.88)], -1.0, 1.0, "awning")   # canvas awning
    F.finish(f"{P}Ground_Shop", out)

    F.reset()
    cut_wall(W, HG, T, [])
    F.finish(f"{P}Ground_Blank", out)

    # upper floors
    def upper(name, balcony, closed=False):
        F.reset()
        if balcony:
            ox, sl, hd = 0.95, 0.0, 2.25
        else:
            ox, sl, hd = 0.9, 0.9, 2.3
        cut_wall(W, HU, T, [(ox, sl, hd, None)])
        F.casement(ox, sl, hd, yf=0.22, leaves=2, rows=4 if balcony else 3)
        band_surround(ox, sl, hd)
        F.shutters(ox, sl + 0.02, hd - 0.02, closed=closed, y=(0.03 if closed else -0.045))
        if balcony:
            F.balcony(-0.75, 0.75, 0.5, 0.0, thick=0.1, corbels=2, rail_h=1.0, motif=None, returns=True)
            for x in (-0.5, 0.48):  # terracotta pots
                F.lathe([(0, 0), (0.1, 0), (0.13, 0.18), (0.14, 0.2), (0.0, 0.2)], x, -0.3, 0.0, "accent", 12)
        else:
            F.sill(ox, sl)
        F.finish(f"{P}{name}", out)

    upper("Upper_Window", False)
    upper("Upper_Closed", False, closed=True)
    upper("Upper_Balcony", True)
    F.reset()
    cut_wall(W, HU, T, [])
    F.finish(f"{P}Upper_Blank", out)

    # tiled eave: two projecting courses, then the clay-tile pitch rising 17 deg to the ridge 4.5 m back
    F.reset()
    run, rise = 4.5, 1.4
    prism_yz([(-0.45, 0.12), (-0.45, 0.0), (0.0, 0.0), (run, rise), (run, rise + 0.12)], -W / 2, W / 2, "roof")
    bx(-W / 2, W / 2, -0.18, 0.0, -0.08, 0.0, "trim")
    bx(-W / 2, W / 2, -0.3, 0.0, -0.02, 0.04, "accent")
    bx(-W / 2, W / 2, 0.0, run, -0.3, 0.0, "wall")
    F.finish(f"{P}Eave", out)

    # flat roof: parapet with coping (Andalusian / Greek)
    F.reset()
    bx(-W / 2, W / 2, 0, 0.3, 0, 0.85, "wall")
    bx(-W / 2, W / 2, -0.04, 0.34, 0.85, 0.93, "trim")
    F.finish(f"{P}Parapet", out)

    # rendered chimney with a little tiled hood
    F.reset()
    bx(-0.35, 0.35, -0.35, 0.35, 0, 1.3, "wall")
    bx(-0.45, 0.45, -0.45, 0.45, 1.3, 1.38, "trim")
    for s in (-1, 1):
        bx(s * 0.25 - 0.06, s * 0.25 + 0.06, -0.3, 0.3, 1.38, 1.62, "wall")
    prism_yz([(-0.5, 1.62), (0.5, 1.62), (0.0, 1.85)], -0.42, 0.42, "roof")
    F.finish(f"{P}Chimney", out)
    F.blocks(P, ["wall", "trim", "roof", "paving", "road", "kerb", "soil"], out)


# ============================================================================================== MODERN (US downtown)


def modern():
    """Contemporary American downtown: glass office towers (flush curtain wall, fins, full-height glazing, precast
    ribbon bands), residential mid-rises (glass balconies, metal-panel cladding), retail podiums and lobbies.
    Modules are 3 m wide; office floors 4 m, residential 3.2 m, street level 5.5 m."""
    W = 3.0
    P = "SM_MO_"
    out = OUT
    HO, HR, HS = 4.0, 3.2, 5.5

    def slab_edge(H, depth=0.35):
        bx(-W / 2, W / 2, 0.05, depth, 0, 0.35, "spandrel")       # floor slab seen through the glass
        bx(-W / 2, W / 2, 0.3, 0.32, 0.35, H, "spandrel")          # dark interior backing

    # --- office: flush curtain wall with spandrel band
    F.reset()
    bx(-W / 2, W / 2, 0.0, 0.02, 0, HO, "cglass")
    slab_edge(HO)
    bx(-W / 2, W / 2, -0.01, 0.03, 0, 1.0, "spandrel")
    bx(-W / 2, -W / 2 + 0.06, -0.1, 0.04, 0, HO, "alu")
    bx(-0.02, 0.02, -0.05, 0.04, 1.0, HO, "alu")
    bx(-W / 2, W / 2, -0.05, 0.04, 0.97, 1.03, "alu")
    F.finish(f"{P}Office_Flush", out)

    # --- office: full-height glass with deep vertical fins
    F.reset()
    bx(-W / 2, W / 2, 0.0, 0.02, 0, HO, "cglass")
    slab_edge(HO)
    bx(-W / 2, -W / 2 + 0.1, -0.55, 0.04, 0, HO, "alu")
    bx(-W / 2, W / 2, -0.06, 0.04, 0, 0.12, "alu")
    F.finish(f"{P}Office_Fin", out)

    # --- office: horizontal ribbon windows between white precast bands
    F.reset()
    bx(-W / 2, W / 2, -0.15, 0.3, 0, 1.15, "concrete")
    bx(-W / 2, W / 2, -0.15, 0.3, HO - 0.45, HO, "concrete")
    bx(-W / 2, W / 2, 0.08, 0.1, 1.15, HO - 0.45, "cglass")
    bx(-W / 2, W / 2, 0.12, 0.35, 0, HO, "spandrel")
    bx(-W / 2, -W / 2 + 0.05, 0.0, 0.1, 1.15, HO - 0.45, "alu")
    F.finish(f"{P}Office_Band", out)

    # --- office: unitised glass, blue-grey, thin mullions every 1.5 m and a slab line
    F.reset()
    bx(-W / 2, W / 2, 0.0, 0.02, 0, HO, "cglass")
    slab_edge(HO)
    for x in (-W / 2, 0.0):
        bx(x, x + 0.05, -0.06, 0.03, 0, HO, "alu")
    bx(-W / 2, W / 2, -0.03, 0.03, 0.0, 0.25, "alu")
    F.finish(f"{P}Office_Unit", out)

    # --- residential: floor-to-ceiling glass, sliding door, projecting slab and glass balustrade
    F.reset()
    bx(-W / 2, W / 2, 0.0, 0.02, 0.0, HR, "cglass")
    slab_edge(HR)
    bx(-W / 2, -W / 2 + 0.08, -0.05, 0.05, 0, HR, "alu")
    bx(-0.04, 0.04, -0.05, 0.05, 0, HR, "alu")
    bx(-W / 2, W / 2, -0.05, 0.05, HR - 0.25, HR, "concrete")
    bx(-W / 2, W / 2, -1.5, 0.0, -0.05, 0.2, "concrete")                     # balcony slab
    bx(-W / 2 + 0.03, W / 2 - 0.03, -1.47, -1.45, 0.2, 1.25, "rglass")        # glass balustrade
    bx(-W / 2, W / 2, -1.5, -1.43, 1.2, 1.25, "alu")
    bx(-W / 2 + 0.02, -W / 2 + 0.05, -1.5, 0.0, 0.2, 1.25, "rglass")
    F.finish(f"{P}Res_Balcony", out)

    # --- residential: metal-panel cladding (paint B) with punched windows
    F.reset()
    cut_wall(W, HR, 0.3, [(1.5, 0.6, 2.75, None)], slot="panel", joints=[1.0, 2.0], joint_depth=0.012, joint_h=0.015,
             verticals=[(-1.0, 0, 0.6), (1.0, 0, HR)])
    bx(-0.75, 0.75, 0.12, 0.14, 0.6, 2.75, "cglass")
    bx(-0.75, 0.75, 0.16, 0.3, 0.6, 2.75, "spandrel")
    for x in (-0.75, 0.71):
        bx(x, x + 0.04, 0.08, 0.14, 0.6, 2.75, "alu")
    bx(-0.75, 0.75, 0.08, 0.14, 0.6, 0.64, "alu")
    bx(-0.75, 0.75, 0.08, 0.14, 2.71, 2.75, "alu")
    bx(-0.83, 0.83, -0.1, 0.12, 2.75, 2.82, "alu")                           # thin metal hood
    F.finish(f"{P}Res_Panel", out)

    # --- residential: inset loggia with full glass and panel frame
    F.reset()
    cut_wall(W, HR, 1.2, [(2.6, 0.0, 2.9, None)], slot="panel")
    bx(-1.3, 1.3, 1.0, 1.02, 0, 2.9, "cglass")
    bx(-1.3, 1.3, 1.05, 1.2, 0, 2.9, "spandrel")
    bx(-1.3, 1.3, 0.0, 1.0, -0.02, 0.0, "concrete")
    bx(-1.27, 1.27, 0.03, 0.05, 0.0, 1.1, "rglass")
    bx(-1.3, 1.3, 0.0, 0.06, 1.07, 1.12, "alu")
    F.finish(f"{P}Res_Loggia", out)

    # --- street level: retail glass with sign band and canopy (paint A), lobby double height
    F.reset()
    bx(-W / 2, W / 2, 0.25, 0.27, 0.0, 4.2, "cglass")
    bx(-W / 2, W / 2, 0.3, 0.6, 0.0, HS, "spandrel")
    bx(-W / 2, -W / 2 + 0.12, 0.0, 0.3, 0, HS, "concrete")
    bx(-W / 2, W / 2, 0.0, 0.3, 4.2, HS, "concrete")
    bx(-W / 2, W / 2, -0.08, 0.0, 4.35, 4.95, "shop")                         # sign band
    bx(-W / 2, W / 2, -1.6, 0.0, 3.85, 3.95, "alu")                           # canopy
    bx(-0.02, 0.02, 0.2, 0.27, 0.0, 4.2, "alu")
    bx(-W / 2, W / 2, 0.2, 0.27, 2.7, 2.75, "alu")
    F.finish(f"{P}Retail", out)
    F.reset()
    bx(-W / 2, W / 2, 0.4, 0.42, 0, HS, "cglass")
    bx(-W / 2, W / 2, 0.45, 0.8, 0, HS, "spandrel")
    for x in (-W / 2, -0.02):
        bx(x, x + 0.06, 0.3, 0.45, 0, HS, "alu")
    bx(-W / 2, W / 2, 0.3, 0.45, 3.0, 3.06, "alu")
    bx(-W / 2, W / 2, -3.0, 0.4, 4.6, 4.85, "alu")                            # deep entrance canopy
    bx(-W / 2, W / 2, 0.0, 0.4, HS - 0.4, HS, "concrete")
    F.finish(f"{P}Lobby", out)
    F.reset()
    bx(-W / 2, W / 2, 0.0, 0.3, 0, HS, "concrete")
    F.finish(f"{P}Base_Blank", out)

    # --- roof: glass crown band / parapet, louvred mechanical screen
    F.reset()
    bx(-W / 2, W / 2, -0.05, 0.25, 0, 2.2, "cglass")
    bx(-W / 2, W / 2, -0.1, 0.25, 2.1, 2.25, "alu")
    bx(-W / 2, -W / 2 + 0.06, -0.12, 0.25, 0, 2.2, "alu")
    F.finish(f"{P}Crown", out)
    F.reset()
    bx(-W / 2, W / 2, 0, 0.25, 0, 1.1, "concrete")
    bx(-W / 2, W / 2, -0.03, 0.28, 1.1, 1.16, "alu")
    F.finish(f"{P}Parapet", out)
    F.reset()
    for k in range(int(3.6 / 0.15)):
        bx(-W / 2, W / 2, -0.05 + 0.0, 0.05, 0.1 + k * 0.15, 0.17 + k * 0.15, "alu")
    for x in (-W / 2, 0):
        bx(x, x + 0.08, -0.05, 0.1, 0, 3.8, "alu")
    F.finish(f"{P}Screen", out)
    F.blocks(P, ["concrete", "spandrel", "alu", "cglass", "panel", "roof", "paving", "road", "kerb", "soil", "marking", "marking_y"], out)


if __name__ == "__main__":
    {"paris": paris, "mediterranean": mediterranean, "modern": modern}[STYLE]()
    print("STYLE DONE", STYLE)
