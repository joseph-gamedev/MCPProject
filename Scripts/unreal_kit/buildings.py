"""Procedural buildings with real detail and wide variation: a reusable pipeline.

    from unreal_kit import buildings as B
    uk.place("Block", B.building("apartment", seed=4), at=(0, 0, 0))     # one building (parts), front faces -Y
    B.TYPES                                                             # apartment, townhouse, shophouse, villa, office, loft

Layers (each usable on its own):
  1. components  window(), door(), shopfront(), railing(), balcony(), cornice(), pilaster() - detailed parts in facade space
  2. facade      facade(width, floors, plan) - floors x bays with base / middle / top, openings cut into the wall
  3. types       building(kind, ...) - a recipe that picks sizes, window kinds, railings, materials and details from the seed
Facade space: x along the facade (centred), z up from the ground, y = 0 on the wall face, outside is -y, the wall body
runs from y = 0 to y = +wall. Sizes in cm, real proportions (storey 300-380, door 220, sill 90, rail 100-110).
"""

import math
import random

from .parts import M, box, cone, cyl, extrude, transform

# ------------------------------------------------------------------ materials (named so recipes read clearly)

MAT = {
    "frame_white": M("plaster", (232, 232, 226), roughness=0.4), "frame_dark": M("painted_iron", (38, 40, 42), roughness=0.45),
    "frame_wood": M("wood", (104, 70, 44)), "frame_alu": M("steel", (176, 178, 180), roughness=0.35), "frame_green": M("painted_iron", (36, 74, 58), roughness=0.45),
    "glass": M("glass", (150, 170, 180), opacity=0.28, roughness=0.04), "room": M("plaster", (70, 60, 50), roughness=0.9),
    "stone": M("limestone", (206, 198, 180)), "stone_dark": M("granite", (120, 118, 112)), "concrete": M("concrete", (178, 176, 168)),
    "iron": M("painted_iron", (30, 30, 32), roughness=0.5), "steel": M("steel", (190, 192, 194), roughness=0.3), "railglass": M("tinted_glass"),
    "wood": M("wood", (120, 82, 52)), "wood_dark": M("wood", (72, 48, 30)),
    "sign": M("painted_iron", (160, 30, 30), roughness=0.4), "awning": M("plaster", (150, 40, 40), roughness=0.9),
}
CURTAINS = [M("plaster", c, roughness=0.95) for c in ((232, 224, 204), (210, 190, 160), (190, 60, 50), (70, 90, 120), (240, 240, 236), (120, 140, 100))]
SHUTTERS = [M("painted_iron", c, roughness=0.5) for c in ((40, 90, 70), (30, 60, 110), (110, 40, 30), (60, 60, 60), (200, 196, 180), (90, 60, 40))]


def m(x):
    return MAT.get(x, x) if isinstance(x, str) else x


# ------------------------------------------------------------------ 1. components

def _frame(x, y, z, w, h, t, d, mat):
    """Rectangular frame of members t wide, d deep, outer size w x h, bottom-left at (x - w/2, z)."""
    return [box([w, d, t], [x, y, z], m=mat), box([w, d, t], [x, y, z + h - t], m=mat),
            box([t, d, h - 2 * t], [x - w / 2 + t / 2, y, z + t], m=mat), box([t, d, h - 2 * t], [x + w / 2 - t / 2, y, z + t], m=mat)]


def _bars(x, y, z, w, h, cols, rows, mat, t=2.4):
    P = []
    for i in range(1, cols):
        P.append(box([t, 2.5, h], [x - w / 2 + i * w / cols, y, z], m=mat))
    for j in range(1, rows):
        P.append(box([w, 2.5, t], [x, y, z + j * h / rows - t / 2], m=mat))
    return P


def window(x, z, w, h, kind="casement", frame="frame_white", reveal=14, panes=(2, 3), sill="stone", head="lintel", surround=False,
           shutters=None, curtain=None, rng=None):
    """A window opening at (x, sill height z), w x h. kind: casement | sash | french | modern | arched | fixed.
    head: lintel | keystone | hood | arch | flat | none. Adds the room behind the glass (and a curtain) so it never
    reads as a flat panel."""
    rng = rng or random
    fr = m(frame)
    P = []
    yf = reveal                                              # frame plane, set back in the reveal
    # room behind: dark back wall, side jambs of the reveal, a curtain
    P.append(box([w + 20, 4, h + 20], [x, yf + 70, z - 10], m=MAT["room"]))
    if curtain is not False:
        cur = curtain or rng.choice(CURTAINS)
        open_l, open_r = rng.uniform(0.15, 0.45), rng.uniform(0.15, 0.45)
        P.append(box([w * open_l, 3, h - 10], [x - w / 2 + w * open_l / 2, yf + 25, z + 5], m=cur))
        P.append(box([w * open_r, 3, h - 10], [x + w / 2 - w * open_r / 2, yf + 25, z + 5], m=cur))
    P.append(box([w, 1.5, h], [x, yf + 3, z], m=MAT["glass"]))
    t = 7 if kind != "modern" else 5
    P += _frame(x, yf, z, w, h, t, 8, fr)
    iw, ih = w - 2 * t, h - 2 * t
    if kind == "casement":                                   # two side-hung leaves with glazing bars, a transom light
        tr = z + t + ih * 0.78
        P.append(box([iw, 7, 6], [x, yf - 1, tr - 3], m=fr))
        lw = iw / 2
        for s in (-1, 1):
            cx = x + s * lw / 2
            P += _frame(cx, yf - 2, z + t, lw, tr - 3 - (z + t), 5, 5, fr)
            P += _bars(cx, yf - 2, z + t + 5, lw - 10, tr - 3 - (z + t) - 10, max(1, panes[0] // 2), panes[1], fr)
        P += _bars(x, yf - 2, tr + 3, iw, z + h - t - tr - 3, panes[0], 1, fr)
    elif kind == "sash":                                     # double hung: upper sash proud of the lower, meeting rail, horns
        mid = z + t + ih / 2
        P += _frame(x, yf - 4, mid - 2, iw, ih / 2 + 2, 5, 4, fr)
        P += _frame(x, yf, z + t, iw, ih / 2 + 2, 5, 4, fr)
        P += [box([iw, 6, 5], [x, yf - 3, mid - 3], m=fr)]
        for s in (-1, 1):
            P.append(box([5, 4, 8], [x + s * (iw / 2 - 2.5), yf - 4, mid - 10], m=fr))   # horns
        P += _bars(x, yf - 4, mid + 3, iw - 10, ih / 2 - 8, panes[0], max(1, panes[1] // 2), fr)
        P += _bars(x, yf, z + t + 5, iw - 10, ih / 2 - 8, panes[0], max(1, panes[1] // 2), fr)
    elif kind == "french":                                   # two glazed doors to the floor with bottom panels
        lw = iw / 2
        for s in (-1, 1):
            cx = x + s * lw / 2
            P += _frame(cx, yf - 2, z + t, lw, ih, 6, 5, fr)
            P.append(box([lw - 12, 4, ih * 0.28], [cx, yf - 1, z + t + 6], m=fr))         # bottom panel
            P.append(box([lw - 24, 2, ih * 0.28 - 14], [cx, yf - 2.5, z + t + 13], m=fr))  # fielded
            P += _bars(cx, yf - 2, z + t + ih * 0.28 + 6, lw - 12, ih * 0.72 - 12, 1, 3, fr)
    elif kind == "modern":                                   # big lights, slim frame, one mullion, an opening vent
        P.append(box([4, 7, ih], [x - iw * 0.12, yf - 1, z + t], m=fr))
        P += _frame(x + iw * 0.32, yf - 2, z + t + ih * 0.55, iw * 0.36, ih * 0.45, 4, 4, fr)
    elif kind == "arched":                                   # tall window with a round top and fan bars
        P += _bars(x, yf - 2, z + t, iw, ih, 2, 4, fr)
    elif kind == "fixed":
        P += _bars(x, yf - 2, z + t, iw, ih, panes[0], panes[1], fr)
    # reveal jambs (the wall's thickness shows round the window)
    P += [box([4, yf, h], [x - w / 2 - 2, yf / 2, z], m=MAT["concrete"]), box([4, yf, h], [x + w / 2 + 2, yf / 2, z], m=MAT["concrete"])]
    # sill: projecting stone with a drip, or a threshold for doors
    if sill and kind != "french":
        P.append(box([w + 18, yf + 8, 6], [x, yf / 2 - 4, z - 6], m=m(sill)))
        P.append(box([w + 18, 3, 3], [x, -6, z - 9], m=m(sill)))
    elif kind == "french":
        P.append(box([w + 10, yf + 4, 4], [x, yf / 2 - 2, z - 4], m=m("stone")))
    # head
    hm = m(sill or "stone")
    if head == "lintel":
        P.append(box([w + 30, 4, 18], [x, -2, z + h + 2], m=hm))
    elif head == "keystone":
        P += [box([w + 30, 4, 18], [x, -2, z + h + 2], m=hm), box([22, 7, 28], [x, -5, z + h - 2], m=hm)]
    elif head == "hood":                                     # little cornice on two brackets
        P += [box([w + 46, 18, 8], [x, -16, z + h + 22], m=hm), box([w + 40, 10, 12], [x, -8, z + h + 10], m=hm)]
        P += [box([10, 16, 26], [x + s * (w / 2 + 14), -8, z + h - 4], m=hm) for s in (-1, 1)]
    elif head == "arch":                                     # segmental arch of voussoirs
        r = w / 2 + 10
        n = 9
        for i in range(n):
            a = math.pi * (i + 0.5) / n
            # voussoirs radiate in the wall plane (pitch turns about Y, the axis out of the wall)
            P.append(box([14, 6, 26], [x + math.cos(a) * r, -3, z + h - 6 + math.sin(a) * r * 0.45], rot=[90 - math.degrees(a), 0, 0], m=hm,
                         pivot="center"))
    if surround:                                             # architrave round the opening
        P += _frame(x, -3, z - 12, w + 28, h + 24, 12, 5, hm)
    if shutters:                                             # louvred shutters folded back against the wall
        sh = m(shutters)
        sw = w / 2
        for s in (-1, 1):
            cx = x + s * (w / 2 + sw / 2 + 4)
            P += _frame(cx, -4, z, sw, h, 5, 4, sh)
            for k in range(int((h - 10) / 9)):
                P.append(box([sw - 10, 3, 5], [cx, -5, z + 5 + k * 9], rot=[-25, 0, 0], m=sh))
    return P


def door(x, w=110, h=230, kind="panel", mat="wood_dark", frame="stone", reveal=20, fanlight=False, rng=None):
    """Entrance door at ground level: panelled, glazed or double; stone surround, step, handle, optional fanlight."""
    P = [box([w + 20, 4, h + 20], [x, reveal + 60, 0], m=MAT["room"])]
    dm = m(mat)
    if kind == "double":
        for s in (-1, 1):
            cx = x + s * w / 4
            P += [box([w / 2 - 2, 5, h], [cx, reveal, 0], m=dm)] + _frame(cx, reveal - 2, 15, w / 2 - 20, h - 40, 6, 3, dm)
    elif kind == "glazed":
        P += _frame(x, reveal, 0, w, h, 9, 6, dm) + [box([w - 18, 2, h - 18], [x, reveal + 2, 9], m=MAT["glass"])]
        P += _bars(x, reveal, 9, w - 18, h - 18, 1, 4, dm)
    else:                                                    # six-panel door
        P.append(box([w, 5, h], [x, reveal, 0], m=dm))
        for r, (z0, hh) in enumerate(((20, 70), (100, 80), (190, 30))):
            for s in (-1, 1):
                P += _frame(x + s * w / 4, reveal - 2, z0, w / 2 - 22, hh, 4, 2, dm)
    P.append(box([4, 6, 12], [x + w * 0.35, reveal - 4, h * 0.45], m=MAT["steel"]))
    fh = 0
    if fanlight:
        fh = 50
        P += [box([w, 2, fh], [x, reveal + 2, h + 6], m=MAT["glass"])] + _frame(x, reveal, h, w, fh + 12, 6, 6, m("frame_white"))
    P += _frame(x, -4, 0, w + 40, h + fh + 30, 16, 8, m(frame))                              # surround
    P += [box([w + 60, 40, 16], [x, -20, -1], m=m(frame))]                                  # step
    P += [box([4, reveal, h + fh], [x - w / 2 - 2, reveal / 2, 0], m=MAT["concrete"]), box([4, reveal, h + fh], [x + w / 2 + 2, reveal / 2, 0], m=MAT["concrete"])]
    return P


def shopfront(x, w, h=380, frame="frame_dark", sign=None, awning=None, rng=None):
    """Shop: stall riser, big glazing with mullions and transom, recessed door, sign fascia, cornice, optional awning."""
    rng = rng or random
    fr = m(frame)
    P = [box([w, 4, h - 80], [x, 60, 0], m=MAT["room"])]
    P += [box([w - 20, 20, 50], [x, 10, 0], m=fr)]                                         # stall riser
    P += [box([w - 20, 2, h - 190], [x, 14, 50], m=MAT["glass"])]
    for i in range(int(w / 120) + 1):
        P.append(box([6, 10, h - 190], [x - w / 2 + 10 + i * (w - 20) / max(1, int(w / 120)), 8, 50], m=fr))
    P += [box([w - 20, 10, 8], [x, 8, h - 140], m=fr), box([w - 20, 2, 60], [x, 14, h - 132], m=MAT["glass"])]   # transom
    dx = x + w / 2 - 70
    P += [box([90, 6, 230], [dx, 40, 0], m=MAT["glass"])] + _frame(dx, 38, 0, 96, 236, 8, 6, fr)
    P += [box([w + 10, 14, 70], [x, -8, h - 70], m=m(sign) if sign else rng.choice([MAT["sign"], MAT["frame_dark"], MAT["frame_green"], m("frame_white")]))]
    P += [box([w * 0.6, 4, 34], [x, -16, h - 52], m=m("frame_white"))]                       # lettering panel
    P += [box([w + 30, 30, 12], [x, -16, h], m=fr)]                                         # cornice
    for s in (-1, 1):
        P.append(box([24, 20, h], [x + s * (w / 2 - 12), -6, 0], m=fr))                     # pilasters
    if awning:
        P.append(box([w - 20, 130, 4], [x, -65, h - 95], rot=[-14, 0, 0], m=m(awning)))
        P.append(box([w - 20, 3, 20], [x, -128, h - 125], m=m(awning)))                     # valance
    return P


def railing_run(ax, ay, bx, by, z, height=105, style="bars", mat="iron"):
    """One straight run of railing between two plan points (any direction) standing on z."""
    mt = m(mat)
    out = []
    ln = math.hypot(bx - ax, by - ay)
    yaw = math.degrees(math.atan2(by - ay, bx - ax))
    mx, my = (ax + bx) / 2, (ay + by) / 2
    def along(t):
        return ax + (bx - ax) * t, ay + (by - ay) * t
    if style == "glass":
        out.append(box([ln, 2, height - 12], [mx, my, z + 6], rot=[0, yaw, 0], m=MAT["railglass"]))
        out.append(box([ln + 4, 6, 5], [mx, my, z + height - 5], rot=[0, yaw, 0], m=MAT["steel"]))
        for k in range(int(ln / 120) + 1):
            px, py = along(k / max(1, int(ln / 120)))
            out.append(box([4, 4, height - 5], [px, py, z], rot=[0, yaw, 0], m=MAT["steel"]))
    elif style == "balustrade":
        out += [box([ln, 26, 16], [mx, my, z], rot=[0, yaw, 0], m=mt), box([ln + 6, 30, 14], [mx, my, z + height - 14], rot=[0, yaw, 0], m=mt)]
        n = max(2, int(ln / 26))
        prof = [[0, 0], [7, 0], [7, 6], [5, 10], [5, 16], [9, 34], [6, 48], [4, 54], [6, 58], [7, 64], [7, height - 30], [0, height - 30]]
        for k in range(n):
            px, py = along((k + 0.5) / n)
            out.append({"shape": "revolve", "profile": prof, "location": [px, py, z + 16], "segments": 10, **mt})
    elif style == "wood":
        n = max(1, int(ln / 150))
        for k in range(n + 1):
            px, py = along(k / n)
            out.append(box([9, 9, height], [px, py, z], rot=[0, yaw, 0], m=mt))
        out += [box([ln, 10, 6], [mx, my, z + height - 6], rot=[0, yaw, 0], m=mt), box([ln, 6, 6], [mx, my, z + 12], rot=[0, yaw, 0], m=mt)]
        for k in range(int(ln / 13)):
            px, py = along((k + 0.5) / int(ln / 13))
            out.append(box([5, 2.5, height - 24], [px, py, z + 18], rot=[0, yaw, 0], m=mt))
    elif style == "cable":
        for k in range(max(1, int(ln / 140)) + 1):
            px, py = along(k / max(1, int(ln / 140)))
            out.append(box([5, 2, height], [px, py, z], rot=[0, yaw, 0], m=MAT["steel"]))
        out.append(box([ln, 6, 4], [mx, my, z + height - 4], rot=[0, yaw, 0], m=MAT["wood"]))
        out += [box([ln, 0.8, 0.8], [mx, my, z + 12 + i * (height - 26) / 6], rot=[0, yaw, 0], m=MAT["steel"]) for i in range(6)]
    else:                                                # bars (and ornate)
        out += [box([ln, 5, 5], [mx, my, z + height - 5], rot=[0, yaw, 0], m=mt), box([ln, 4, 4], [mx, my, z + 8], rot=[0, yaw, 0], m=mt)]
        n = max(2, int(ln / 11))
        for k in range(n + 1):
            px, py = along(k / n)
            out.append(box([1.8, 1.8, height - 13], [px, py, z + 8], rot=[0, yaw, 0], m=mt))
        if style == "ornate":                            # a band of rings and a lower band
            out.append(box([ln, 2, 2], [mx, my, z + height * 0.62], rot=[0, yaw, 0], m=mt))
            out.append(box([ln, 2, 2], [mx, my, z + height * 0.38], rot=[0, yaw, 0], m=mt))
            for k in range(n):
                if k % 2 == 0:
                    px, py = along((k + 0.5) / n)
                    out.append({"shape": "torus", "size": [10, 10, 1.6], "location": [px, py, z + height * 0.5 - 5], "rotation": [0, yaw, 90], **mt})
    return out



def railing(x0, x1, y, z, height=105, style="bars", mat="iron", return_depth=0):
    """Railing from x0 to x1 at depth y, standing on z. style: bars | ornate | glass | balustrade | wood | cable.
    return_depth > 0 also runs it back to the wall at both ends (balconies)."""
    P = railing_run(x0, y, x1, y, z, height, style, mat)
    if return_depth > 0:
        P += railing_run(x0, y, x0, y + return_depth, z, height, style, mat) + railing_run(x1, y, x1, y + return_depth, z, height, style, mat)
    return P


def balcony(x, z, w, depth=120, kind="slab", rail="bars", rail_mat="iron", slab_mat="concrete"):
    """Balcony on the wall face at floor height z: slab with a moulded edge; brackets (classic) or a clean cantilever
    (modern); railing on three sides."""
    sm = m(slab_mat)
    P = [box([w, depth, 14], [x, -depth / 2, z - 14], m=sm), box([w + 6, 8, 18], [x, -depth - 2, z - 18], m=sm)]
    if kind == "brackets":
        for s in (-1, 1) if w < 300 else (-1, 0, 1):
            bx = x + s * (w / 2 - 25)
            P += [box([14, depth * 0.7, 10], [bx, -depth * 0.35, z - 24], m=sm), box([14, depth * 0.45, 12], [bx, -depth * 0.22, z - 36], m=sm),
                  box([14, depth * 0.2, 14], [bx, -depth * 0.1, z - 50], m=sm)]
    if rail == "balustrade":
        rail_mat = slab_mat                                  # turned balusters are stone, like the slab
    P += railing(x - w / 2 + 3, x + w / 2 - 3, -depth + 6, z, 105 if rail != "balustrade" else 95, rail, rail_mat, return_depth=depth - 8)
    return P


def cornice(width, z, depth=40, mat="stone", kind="moulded"):
    """Cornice band along the facade at height z (moulded = stepped profile, dentils = with a row of dentils)."""
    mm = m(mat)
    P = [box([width + 2 * depth, depth + 8, 12], [0, -depth / 2 + 4, z + 26], m=mm), box([width + depth, depth * 0.7, 10], [0, -depth * 0.35 + 4, z + 16], m=mm),
         box([width + depth * 0.4, depth * 0.4, 16], [0, -depth * 0.2 + 4, z], m=mm)]
    if kind == "dentils":
        P += [box([8, 10, 10], [-width / 2 + 10 + i * 18, -depth * 0.45, z + 6], m=mm) for i in range(int(width / 18))]
    return P


def pilaster(x, z0, z1, w=40, mat="stone"):
    mm = m(mat)
    return [box([w, 10, z1 - z0], [x, -5, z0], m=mm), box([w + 12, 14, 18], [x, -7, z1 - 18], m=mm), box([w + 12, 14, 24], [x, -7, z0], m=mm)]


def quoins(x, z0, z1, mat="stone", size=40):
    """Alternating corner stones up a corner at facade x."""
    mm = m(mat)
    P = []
    z = z0
    k = 0
    while z < z1:
        w = size * (1.4 if k % 2 == 0 else 0.9)
        P.append(box([w, 6, size * 0.75], [x - math.copysign(w / 2, x), -3, z], m=mm))
        z += size * 0.75 + 3
        k += 1
    return P


# ------------------------------------------------------------------ 2. facade grammar

def _wall_bay(cx, bay, z0, fh, opening, wall_m, t):
    """Wall around one opening (x offset, w, sill z rel, h) in a bay of one storey."""
    P = []
    if opening is None:
        return [box([bay, t, fh], [cx, t / 2, z0], m=wall_m)]
    dx, w, sz, h = opening
    l, r = cx + dx - w / 2, cx + dx + w / 2
    if l - (cx - bay / 2) > 0.5:
        P.append(box([l - (cx - bay / 2), t, fh], [(cx - bay / 2 + l) / 2, t / 2, z0], m=wall_m))
    if (cx + bay / 2) - r > 0.5:
        P.append(box([cx + bay / 2 - r, t, fh], [(r + cx + bay / 2) / 2, t / 2, z0], m=wall_m))
    if sz > 0.5:
        P.append(box([w, t, sz], [cx + dx, t / 2, z0], m=wall_m))
    top = fh - (sz + h)
    if top > 0.5:
        P.append(box([w, t, top], [cx + dx, t / 2, z0 + sz + h], m=wall_m))
    return P


def facade(width, plan, rng):
    """Builds one facade from a plan:
    plan = {"wall": mat, "t": thickness, "bays": n, "floors": [floor, ...] bottom to top, "trim": mat,
            "string_courses": bool, "cornice": kind | None, "pilasters": bool, "quoins": bool, "plinth": mat | None}
    floor = {"h": storey height, "kind": "window" | "shop" | "entrance" | "blank", "window": {...window kwargs...},
             "w": opening width, "wh": opening height, "sill": sill height, "balcony": None | "all" | "centre" | "alternate" | "ends",
             "balcony_kind", "rail", "rail_mat", "door_bay": index}"""
    wall_m = m(plan["wall"])
    t = plan.get("t", 30)
    bays = plan["bays"]
    bay = width / bays
    trim = plan.get("trim", "stone")
    P = []
    z = 0
    for fi, fl in enumerate(plan["floors"]):
        fh = fl["h"]
        for b in range(bays):
            cx = -width / 2 + (b + 0.5) * bay
            kind = fl["kind"]
            if kind == "shop":
                P += [box([bay, t, 0.01], [cx, t / 2, z], m=wall_m)]
                P += shopfront(cx, bay - 10, fh, frame=fl.get("frame", "frame_dark"), awning=fl.get("awning"), rng=rng)
                continue
            if kind == "entrance" and b == fl.get("door_bay", bays // 2):
                dw = fl.get("door_w", 120)
                P += _wall_bay(cx, bay, z, fh, (0, dw, 0, 230 + (50 if fl.get("fanlight") else 0)), wall_m, t)
                P += door(cx, dw, 230, fl.get("door_kind", "panel"), fl.get("door_mat", "wood_dark"), trim, reveal=t * 0.6, fanlight=fl.get("fanlight"), rng=rng)
                continue
            if kind == "blank":
                P += _wall_bay(cx, bay, z, fh, None, wall_m, t)
                continue
            w, wh, sz = fl.get("w", 120), fl.get("wh", 180), fl.get("sill", 80)
            pairs = fl.get("pair", False)
            win = dict(fl.get("window", plan.get("ground_window", {})))
            bal = fl.get("balcony")
            has_bal = (bal == "all" or (bal == "centre" and b == bays // 2) or (bal == "alternate" and b % 2 == 1) or (bal == "ends" and b in (0, bays - 1))
                       or (isinstance(bal, (set, list, tuple)) and b in bal))
            if has_bal:
                win = dict(win, kind="french")
                sz, wh = 0, min(fh - 50, 240)
            if pairs and not has_bal:
                ww = w / 2 - 10
                P += _wall_bay(cx, bay, z, fh, (0, w, sz, wh), wall_m, t)
                P.append(box([20, t, wh], [cx, t / 2, z + sz], m=wall_m))                  # central pier
                for s in (-1, 1):
                    P += window(cx + s * (ww / 2 + 10), z + sz, ww, wh, reveal=t * 0.5, rng=rng, **win)
            else:
                P += _wall_bay(cx, bay, z, fh, (0, w, sz, wh), wall_m, t)
                P += window(cx, z + sz, w, wh, reveal=t * 0.5, rng=rng, **win)
            if has_bal:
                bw = min(bay - 20, w + 120) if fl.get("balcony_kind") != "continuous" else bay
                P += balcony(cx, z, bw, fl.get("balcony_depth", 120), fl.get("balcony_kind", "slab"), fl.get("rail", "bars"), fl.get("rail_mat", "iron"),
                             fl.get("slab_mat", "concrete"))
        if fi > 0 and plan.get("string_courses"):
            P.append(box([width + 10, 8, 14], [0, -4, z - 7], m=m(trim)))
        z += fh
    if plan.get("plinth"):
        P.append(box([width + 6, t + 10, 60], [0, t / 2 - 5, 0], m=m(plan["plinth"])))
    if plan.get("pilasters"):
        for b in range(bays + 1):
            P += pilaster(-width / 2 + b * width / bays, plan["floors"][0]["h"], z, 36, trim)
    if plan.get("quoins"):
        for s in (-1, 1):
            P += quoins(s * width / 2, 0, z, trim)
    if plan.get("cornice"):
        P += cornice(width, z, 40, trim, plan["cornice"])
    return P, z


# ------------------------------------------------------------------ 3. building types

WALLS = {
    "render": [M("plaster", c) for c in ((236, 232, 222), (232, 214, 180), (214, 196, 170), (196, 210, 214), (234, 206, 196), (206, 214, 190),
                                       (240, 224, 196))],
    "brick": [M("brick", c) for c in ((150, 84, 64), (124, 82, 66), (176, 134, 104), (110, 64, 50))],
    "stone": [M("limestone", c) for c in ((214, 204, 184), (196, 186, 166))] + [M("sandstone", (196, 168, 130))],
    "concrete": [M("concrete", c) for c in ((196, 194, 186), (160, 158, 152))],
    "panel": [M("painted_iron", c, roughness=0.5) for c in ((230, 230, 226), (60, 62, 66), (170, 90, 60), (140, 150, 136))],
}


def _roof(P, w, d, z, kind, rng, eave=60, mat=None):
    tile = mat or rng.choice([M("roof_tile", (160, 80, 56)), M("slate_roof", (72, 74, 80)), M("roof_tile", (120, 70, 52))])
    if kind == "hip":
        P.append({"shape": "pyramid", "size": [w + 2 * eave, d + 2 * eave, min(w, d) * 0.42], "location": [0, d / 2, z], **tile})
        P.append(box([w + 2 * eave + 4, 8, 22], [0, -eave - 2, z - 16], m=MAT["frame_white"]))                       # fascia
        P += [box([6, eave, 8], [-w / 2 - eave + 30 + i * 60, -eave / 2, z - 10], m=MAT["wood_dark"]) for i in range(int((w + 2 * eave) / 60))]  # rafter tails
    elif kind == "gable":
        P.append({"shape": "roof", "size": [w + 2 * eave, d + 2 * eave, min(w, d) * 0.4], "location": [0, d / 2, z], **tile})
    else:                                                    # flat with parapet and coping, plant on top
        P.append(box([w, d, 20], [0, d / 2, z], m=MAT["concrete"]))
        for _ in range(rng.randint(1, 3)):
            P.append(box([rng.choice((120, 180)), rng.choice((90, 120)), rng.choice((90, 140))], [rng.uniform(-w / 3, w / 3), d / 2 + rng.uniform(-d / 4, d / 4), z + 20],
                         m=M("steel", (170, 172, 172), roughness=0.5)))


def _sides(P, w, d, floors_h, wall_m, t, plan, rng):
    """Side and back walls: the back gets plain windows, the sides are mostly blank (party walls)."""
    zt = sum(floors_h)
    for s in (-1, 1):
        P.append(box([t, d, zt], [s * (w / 2 - t / 2), d / 2, 0], m=wall_m))
    # the back: the same storeys with plain windows (no shops, doors or balconies)
    back = []
    for f in plan["floors"]:
        b = dict(f, kind="window", balcony=None)
        if f["kind"] in ("shop", "entrance"):
            b.update(w=110, wh=150, sill=90, window={"kind": "casement", "curtain": False, "sill": "concrete", "head": "lintel"})
        back.append(b)
    back_plan = dict(plan, floors=back, pilasters=False, quoins=False)
    BP, _ = facade(w, back_plan, rng)
    P += transform(BP, (0, d, 0), 180)


TYPES = ["apartment", "townhouse", "shophouse", "villa", "office", "loft"]


def _recipe(k, rng, depth=None, floors=None):
    """Style choices for one building of type k: (facade plan, width, depth, roof kind, bay width)."""
    if k == "apartment":
        n = floors or rng.randint(4, 8)
        bays = rng.choice((3, 4, 5, 6))
        bay = rng.choice((320, 360, 400))
        w, d = bays * bay, depth or rng.choice((1200, 1400))
        wall = rng.choice(WALLS["render"] + WALLS["concrete"] + WALLS["panel"])
        win = {"kind": rng.choice(("modern", "casement", "modern")), "frame": rng.choice(("frame_alu", "frame_dark", "frame_white")), "sill": rng.choice(("concrete", "stone")),
               "head": rng.choice(("flat", "none")), "panes": (2, 2)}
        rail = rng.choice(("glass", "bars", "cable", "glass"))
        bal = rng.choice(("all", "alternate", "centre", "all"))
        ground = {"h": 360, "kind": rng.choice(("entrance", "shop")), "door_kind": "glazed", "door_mat": "frame_alu", "door_w": 180,
                  "w": bay * 0.6, "wh": 200, "sill": 90, "window": win}
        if ground["kind"] == "shop":
            ground["awning"] = rng.choice((None, "awning"))
        upper = {"h": 300, "kind": "window", "w": bay * rng.uniform(0.45, 0.6), "wh": 150, "sill": 85, "window": win, "balcony": bal, "rail": rail,
                 "rail_mat": "steel" if rail != "bars" else "iron", "balcony_kind": rng.choice(("slab", "continuous")), "balcony_depth": rng.choice((120, 150))}
        plan = {"wall": wall, "t": 25, "bays": bays, "floors": [ground] + [dict(upper) for _ in range(n - 1)], "trim": "concrete", "string_courses": False,
                "cornice": None, "plinth": "stone_dark"}
        roof = "flat"
    elif k == "townhouse":
        n = floors or rng.randint(3, 5)
        bays = rng.choice((3, 3, 4))
        bay = rng.choice((280, 300, 330))
        w, d = bays * bay, depth or 1100
        wall = rng.choice(WALLS["render"] + WALLS["brick"] + WALLS["stone"])
        win = {"kind": rng.choice(("sash", "casement")), "frame": rng.choice(("frame_white", "frame_dark", "frame_green")), "sill": "stone",
               "head": rng.choice(("keystone", "hood", "lintel", "arch")), "surround": rng.random() < 0.5, "panes": rng.choice(((2, 4), (3, 4), (2, 2))),
               "shutters": rng.choice(SHUTTERS) if rng.random() < 0.4 else None}
        rail = rng.choice(("ornate", "bars", "balustrade"))
        noble = {"h": 360, "kind": "window", "w": 120, "wh": 230, "sill": 40, "window": win, "balcony": rng.choice(("all", "centre", "alternate")),
                 "rail": rail, "balcony_kind": "brackets", "balcony_depth": 70, "slab_mat": "stone"}
        upper = {"h": 320, "kind": "window", "w": 115, "wh": 180, "sill": 80, "window": win, "balcony": None}
        top = {"h": 280, "kind": "window", "w": 100, "wh": 130, "sill": 70, "window": dict(win, head="lintel", shutters=None), "balcony": None}
        ground = {"h": 380, "kind": "entrance", "door_kind": rng.choice(("panel", "double")), "door_mat": rng.choice(("wood_dark", "frame_green", "frame_dark")),
                  "fanlight": rng.random() < 0.6, "door_bay": rng.choice((0, bays // 2)), "w": 115, "wh": 200, "sill": 100,
                  "window": dict(win, shutters=None)}
        floors_l = [ground, noble] + [dict(upper) for _ in range(n - 3)] + [top]
        plan = {"wall": wall, "t": 35, "bays": bays, "floors": floors_l, "trim": "stone", "string_courses": rng.random() < 0.7,
                "cornice": rng.choice(("moulded", "dentils")), "quoins": rng.random() < 0.4, "plinth": "stone"}
        roof = rng.choice(("flat", "hip"))
    elif k == "shophouse":
        n = floors or rng.randint(2, 4)
        bays = rng.choice((2, 3))
        bay = rng.choice((300, 340))
        w, d = bays * bay, depth or 1200
        wall = rng.choice(WALLS["render"])
        win = {"kind": rng.choice(("casement", "french", "casement")), "frame": rng.choice(("frame_wood", "frame_white", "frame_green")), "sill": "concrete",
               "head": rng.choice(("hood", "lintel")), "shutters": rng.choice(SHUTTERS) if rng.random() < 0.6 else None, "panes": (2, 3)}
        upper = {"h": 310, "kind": "window", "w": 120, "wh": 170, "sill": 80, "window": win, "balcony": rng.choice((None, "all", "centre")), "rail": rng.choice(("ornate", "bars", "wood")),
                 "balcony_kind": "brackets", "balcony_depth": 80}
        ground = {"h": 400, "kind": "shop", "awning": rng.choice(("awning", None)), "frame": rng.choice(("frame_dark", "frame_green", "frame_wood"))}
        plan = {"wall": wall, "t": 25, "bays": bays, "floors": [ground] + [dict(upper) for _ in range(n - 1)], "trim": "concrete", "string_courses": True,
                "cornice": "moulded", "plinth": None}
        roof = rng.choice(("flat", "flat", "gable"))
    elif k == "villa":
        n = floors or rng.choice((1, 2, 2))
        bays = rng.choice((3, 4, 5))
        bay = rng.choice((320, 360))
        w, d = bays * bay, depth or rng.choice((900, 1100))
        wall = rng.choice(WALLS["render"] + WALLS["stone"])
        win = {"kind": rng.choice(("casement", "french", "arched")), "frame": rng.choice(("frame_white", "frame_wood")), "sill": "stone",
               "head": rng.choice(("arch", "lintel", "keystone")), "shutters": rng.choice(SHUTTERS) if rng.random() < 0.7 else None, "panes": (2, 3)}
        ground = {"h": 340, "kind": "entrance", "door_kind": rng.choice(("double", "glazed")), "door_mat": rng.choice(("wood", "frame_white")), "door_bay": bays // 2,
                  "fanlight": rng.random() < 0.5, "w": 130, "wh": 220, "sill": 50, "window": win}
        upper = {"h": 320, "kind": "window", "w": 120, "wh": 180, "sill": 70, "window": win, "balcony": rng.choice(("centre", "ends", None)),
                 "rail": rng.choice(("balustrade", "wood", "ornate")), "balcony_kind": "brackets", "balcony_depth": 100, "slab_mat": "stone"}
        plan = {"wall": wall, "t": 35, "bays": bays, "floors": [ground] + [dict(upper) for _ in range(n - 1)], "trim": "stone", "string_courses": n > 1,
                "cornice": None, "plinth": "stone", "quoins": rng.random() < 0.3}
        roof = "hip"
    elif k == "office":
        n = floors or rng.randint(5, 10)
        bays = rng.choice((5, 6, 8))
        bay = rng.choice((300, 360))
        w, d = bays * bay, depth or 1600
        wall = rng.choice(WALLS["stone"] + WALLS["concrete"])
        win = {"kind": "modern", "frame": rng.choice(("frame_alu", "frame_dark")), "sill": None, "head": "none", "curtain": False}
        upper = {"h": 380, "kind": "window", "w": bay - rng.choice((60, 90)), "wh": rng.choice((220, 250)), "sill": 70, "window": win, "balcony": None}
        ground = {"h": 480, "kind": "shop", "frame": "frame_alu"}
        plan = {"wall": wall, "t": 40, "bays": bays, "floors": [ground] + [dict(upper) for _ in range(n - 1)], "trim": "stone_dark", "string_courses": rng.random() < 0.5,
                "cornice": "moulded" if rng.random() < 0.4 else None, "pilasters": rng.random() < 0.6, "plinth": "stone_dark"}
        roof = "flat"
    else:  # loft
        n = floors or rng.randint(3, 6)
        bays = rng.choice((4, 5, 6))
        bay = rng.choice((380, 420))
        w, d = bays * bay, depth or 1600
        wall = rng.choice(WALLS["brick"])
        win = {"kind": "fixed", "frame": rng.choice(("frame_dark", "frame_green")), "sill": "concrete", "head": "arch" if rng.random() < 0.5 else "lintel",
               "panes": (4, 4), "curtain": False}
        upper = {"h": 380, "kind": "window", "w": bay * 0.7, "wh": 240, "sill": 80, "window": win, "balcony": rng.choice((None, None, "alternate")), "rail": "bars",
                 "balcony_kind": "slab", "balcony_depth": 90}
        ground = {"h": 450, "kind": rng.choice(("shop", "entrance")), "frame": "frame_dark", "door_kind": "double", "door_mat": "frame_dark",
                  "w": bay * 0.7, "wh": 280, "sill": 80, "window": win}
        plan = {"wall": wall, "t": 40, "bays": bays, "floors": [ground] + [dict(upper) for _ in range(n - 1)], "trim": "concrete", "string_courses": True,
                "cornice": "dentils", "pilasters": rng.random() < 0.5, "plinth": "stone_dark"}
        roof = "flat"
    return plan, w, d, roof, bay


def _with_width(plan, width, bay):
    """The same plan re-laid for another facade width (bay count follows the type's bay size)."""
    q = dict(plan, bays=max(1, round(width / bay)))
    q["floors"] = [dict(f) for f in plan["floors"]]
    for f in q["floors"]:
        if f.get("door_bay") is not None and f["door_bay"] >= q["bays"]:
            f["door_bay"] = q["bays"] // 2
    return q


def _back(plan):
    """The plan for a back or courtyard facade: plain windows on every storey, no shops, doors or balconies."""
    floors = []
    for f in plan["floors"]:
        b = dict(f, kind="window", balcony=None)
        if f["kind"] in ("shop", "entrance"):
            b.update(w=110, wh=150, sill=90, window={"kind": "casement", "curtain": False, "sill": "concrete", "head": "lintel"})
        floors.append(b)
    return dict(plan, floors=floors, pilasters=False, quoins=False, cornice=None)


def _place(P, parts, cx, cy, yaw):
    P += transform(parts, (cx, cy, 0), yaw)


def _parapet(P, outline, z, plan, h=90):
    """Parapet with coping along every edge of a closed outline."""
    t = plan.get("t", 30)
    wall, trim = m(plan["wall"]), m(plan.get("trim", "concrete"))
    for i in range(len(outline)):
        (ax, ay), (bx, by) = outline[i], outline[(i + 1) % len(outline)]
        ln = math.hypot(bx - ax, by - ay)
        if ln < 1:
            continue
        yaw = math.degrees(math.atan2(by - ay, bx - ax))
        # inset half the wall so the parapet sits on the wall line, inside the outline
        nx, ny = -(by - ay) / ln, (bx - ax) / ln
        cx, cy = (ax + bx) / 2 + nx * t / 2, (ay + by) / 2 + ny * t / 2
        P.append(box([ln + t, t, h], [cx, cy, z], rot=[0, yaw, 0], m=wall))
        P.append(box([ln + t + 10, t + 10, 8], [cx, cy, z + h], rot=[0, yaw, 0], m=trim))


def _flat_roof(P, outline, z, plan, rng):
    cx = sum(p[0] for p in outline) / len(outline)
    cy = sum(p[1] for p in outline) / len(outline)
    P.append(extrude([[p[0] - cx, p[1] - cy] for p in outline], 20, [cx, cy, z], m=MAT["concrete"]))
    P.append(extrude([[p[0] - cx, p[1] - cy] for p in outline], 4, [cx, cy, 0.5], m=MAT["concrete"]))      # ground floor slab
    xs, ys = [p[0] for p in outline], [p[1] for p in outline]
    for _ in range(rng.randint(1, 3)):
        P.append(box([rng.choice((120, 180)), rng.choice((90, 120)), rng.choice((90, 140))], [cx + rng.uniform(-50, 50), cy + rng.uniform(-50, 50), z + 20],
                     m=M("steel", (170, 172, 172), roughness=0.5)))
    _parapet(P, outline, z, plan)


def _rect(kind, rng, width, depth, floors):
    plan, w, d, roof, bay = _recipe(kind, rng, depth, floors)
    if width:
        w = width
        plan = _with_width(plan, w, bay)
    P = []
    front, top_z = facade(w, plan, rng)
    P += front
    _sides(P, w, d, [f["h"] for f in plan["floors"]], m(plan["wall"]), plan.get("t", 30), plan, rng)
    if roof == "flat":
        _flat_roof(P, [(-w / 2, 0), (w / 2, 0), (w / 2, d), (-w / 2, d)], top_z, plan, rng)
    else:
        _roof(P, w, d, top_z, roof, rng)
        P.append(box([w - 2, d - 2, 4], [0, d / 2, 0.5], m=MAT["concrete"]))
    return P


def _l_shape(kind, rng, wa, wb, depth, floors, corner):
    """Two wings meeting at the street corner (the origin): wing A runs along +X with its street facade facing -Y,
    wing B runs along +Y with its street facade facing -X. corner: square | chamfer | tower | balconies."""
    plan, w0, d, roof, bay = _recipe(kind, rng, depth, floors)
    D = depth or d
    wa = wa or max(w0, 3 * bay + D)
    wb = wb or max(w0 * rng.uniform(0.7, 1.0), 2 * bay + D)
    t = plan.get("t", 30)
    P = []
    c = {"chamfer": rng.choice((300, 380)), "tower": 0, "square": 0, "balconies": 0}.get(corner, 0)
    heights = [f["h"] for f in plan["floors"]]
    top_z = sum(heights)
    # street facades
    pa = _with_width(plan, wa - c, bay)
    pb = _with_width(plan, wb - c, bay)
    if corner == "balconies":                                # balconies on the corner bays, wrapped round below
        for q, idx in ((pa, 0), (pb, None)):
            for f in q["floors"][1:]:
                if f["kind"] == "window":
                    f["balcony"] = {idx if idx is not None else q["bays"] - 1}
                    f["balcony_kind"] = "slab"
    if corner == "square":
        pa["quoins"] = pb["quoins"] = True
    fa, _ = facade(wa - c, pa, rng)
    _place(P, fa, c + (wa - c) / 2, 0, 0)
    fb, _ = facade(wb - c, pb, rng)
    _place(P, fb, 0, c + (wb - c) / 2, -90)
    if c:                                                    # the cut corner: one bay with an entrance or shop below
        diag = c * math.sqrt(2)
        pc = dict(plan, bays=1, floors=[dict(f) for f in plan["floors"]], pilasters=False, quoins=False)
        g = pc["floors"][0]
        if g["kind"] == "window":
            g.update(kind="entrance", door_bay=0)
        if g["kind"] == "entrance":
            g["door_bay"] = 0
        for f in pc["floors"][1:]:
            if f["kind"] == "window":
                f["w"] = min(f.get("w", 120), diag - 60)
                f["balcony"] = "all" if rng.random() < 0.5 else None
        fc, _ = facade(diag, pc, rng)
        _place(P, fc, c / 2, c / 2, -45)
    # courtyard facades and wing ends
    pba = _with_width(_back(plan), wa - D, bay)
    bfa, _ = facade(wa - D, pba, rng)
    _place(P, bfa, D + (wa - D) / 2, D, 180)
    pbb = _with_width(_back(plan), wb - D, bay)
    bfb, _ = facade(wb - D, pbb, rng)
    _place(P, bfb, D, D + (wb - D) / 2, 90)
    wall = m(plan["wall"])
    P.append(box([t, D, top_z], [wa - t / 2, D / 2, 0], m=wall))         # wing ends (party walls)
    P.append(box([D, t, top_z], [D / 2, wb - t / 2, 0], m=wall))
    if corner == "balconies":                                # one wrap-around balcony per upper floor
        z = heights[0]
        rail = plan["floors"][-1].get("rail", "bars")
        rmat = plan["floors"][-1].get("rail_mat", "iron")
        dep = 130
        reach = bay
        for f in plan["floors"][1:]:
            P.append(box([reach + dep, dep, 14], [(reach - dep) / 2, -dep / 2, z - 14], m=MAT["concrete"]))
            P.append(box([dep, reach, 14], [-dep / 2, reach / 2, z - 14], m=MAT["concrete"]))
            P += railing_run(-dep + 6, -dep + 6, reach, -dep + 6, z, 105, rail, rmat)
            P += railing_run(-dep + 6, -dep + 6, -dep + 6, reach, z, 105, rail, rmat)
            P += railing_run(reach, -dep + 6, reach, 0, z, 105, rail, rmat) + railing_run(-dep + 6, reach, 0, reach, z, 105, rail, rmat)
            z += f["h"]
    if corner == "tower":                                    # round corner tower rising a storey above the roof
        r = rng.choice((220, 260))
        th = top_z + rng.choice((300, 420))
        tw = m(plan.get("trim", "stone")) if rng.random() < 0.4 else wall
        P.append(cyl(2 * r, th, [r * 0.35, r * 0.35, 0], m=tw))
        z = heights[0]
        for f in plan["floors"][1:] + [{"h": th - top_z}]:
            for a in (200, 225, 250):                        # windows facing the corner
                ang = math.radians(a)
                px, py = r * 0.35 + math.cos(ang) * (r + 1), r * 0.35 + math.sin(ang) * (r + 1)
                P.append(box([70, 6, min(160, f["h"] - 90)], [px, py, z + 70], rot=[0, a + 90, 0], m=MAT["glass"]))
                P.append(box([86, 8, 10], [px, py, z + 60], rot=[0, a + 90, 0], m=m(plan.get("trim", "stone"))))
            z += f["h"]
        P.append(cyl(2 * r + 30, 24, [r * 0.35, r * 0.35, th], m=m(plan.get("trim", "stone"))))
        cap = rng.choice(("cone", "dome"))
        capm = rng.choice([M("slate_roof", (72, 74, 80)), M("verdigris", (90, 140, 120)), M("copper", (160, 100, 64))])
        if cap == "cone":
            P.append(cone(2 * r + 40, r * 2.2, [r * 0.35, r * 0.35, th + 24], m=capm))
        else:
            P.append({"shape": "dome", "size": [2 * r + 20, 2 * r + 20, r], "location": [r * 0.35, r * 0.35, th + 24], **capm})
        P.append(cyl(10, 120, [r * 0.35, r * 0.35, th + 24 + (r * 2.2 if cap == "cone" else r)], m=MAT["iron"]))
    # roofs
    outline = [(c, 0), (wa, 0), (wa, D), (D, D), (D, wb), (0, wb), (0, c)] if c else [(0, 0), (wa, 0), (wa, D), (D, D), (D, wb), (0, wb)]
    if roof == "flat":
        _flat_roof(P, outline, top_z, plan, rng)
    else:                                                    # two pitched wings meeting at the corner
        tile = rng.choice([M("roof_tile", (160, 80, 56)), M("slate_roof", (72, 74, 80))])
        e = 50
        P.append({"shape": "roof", "size": [D + 2 * e, wa + 2 * e, D * 0.45], "location": [wa / 2, D / 2, top_z], "rotation": [0, 90, 0], **tile})
        P.append({"shape": "roof", "size": [D + 2 * e, wb - D + e, D * 0.45], "location": [D / 2, D + (wb - D) / 2 + e / 2, top_z], **tile})
        P.append(extrude([[p[0] - wa / 2, p[1] - wb / 2] for p in outline], 4, [wa / 2, wb / 2, 0.5], m=MAT["concrete"]))
    return P


def building(kind="apartment", seed=None, width=None, depth=None, floors=None, shape="rect", corner=None, wing=None,
             massing=None, party=None, street=("-y",)):
    """A complete building of a type, varied by seed.
    shape rect: front facade on the street (-Y), back, sides, roof; origin = front centre at ground.
    shape L (corner building): two street facades meeting at the origin, wing A along +X (facing -Y), wing B along +Y
      (facing -X); width = wing A length, wing = wing B length; corner = square | chamfer | tower | balconies (default:
      chosen by the seed).
    massing: a massing model (see MASSINGS, or "auto" to let the type and seed pick): the building is composed of
      volumes on a lot (width x depth, front edge at y = 0, centred on x = 0) and every exposed face gets a facade.
      party: lot sides that touch neighbours and stay blank ("-x", "+x"); street: faces that get the shop / entrance
      ground floor and the main facade.
    Returns parts."""
    rng = random.Random(seed)
    if massing:
        return massed(kind, massing, rng, width, depth, floors, party, street)
    if shape in ("L", "corner"):
        corner = corner or rng.choice(("square", "chamfer", "tower", "balconies"))
        return _l_shape(kind, rng, width, wing, depth, floors, corner)
    return _rect(kind, rng, width, depth, floors)


# ------------------------------------------------------------------ 4. massing models
# A massing model is the building's overall volume before any detail: boxes on a lot (podium + tower, setbacks,
# courtyard, bars, stacked or notched blocks). Volumes share one storey grid so floors line up across them; every
# face that is not hidden by a neighbour gets a facade for exactly the storeys it shows, roofs get parapets, and a
# roof that another volume steps back from becomes a terrace with railings and planters.
# Depth rules (daylight): housing bars 10-14 m, office bars 14-20 m, tower plates 18-32 m.

def _vol(x0, y0, x1, y1, f0, n, tag="main"):
    return {"x0": min(x0, x1), "x1": max(x0, x1), "y0": min(y0, y1), "y1": max(y0, y1), "f0": int(f0), "n": max(1, int(n)), "tag": tag}


def _m_block(w, d, n, rng, b):
    return [_vol(-w / 2, 0, w / 2, d, 0, n)]


def _m_slab(w, d, n, rng, b):
    """Long thin bar (Corbusian slab): depth b, often a recessed glazed ground floor."""
    return [_vol(-w / 2, 0, w / 2, b, 0, n)]


def _m_podium_tower(w, d, n, rng, b):
    """Point tower on a 2-4 storey podium that fills the lot; tower set back from the street, optional slimmer crown."""
    npod = rng.randint(2, 4)
    nt = max(n, rng.randint(10, 22))
    tw = max(1400, min(w - 400, w * rng.uniform(0.45, 0.65)))
    td = max(1400, min(d - 600, d * rng.uniform(0.5, 0.7)))
    sb = rng.uniform(400, max(450, d - td - 200))
    ox = rng.uniform(-(w - tw) / 2 + 200, (w - tw) / 2 - 200) if w - tw > 500 else 0
    V = [_vol(-w / 2, 0, w / 2, d, 0, npod, "podium")]
    crown = rng.random() < 0.5
    top = nt - (2 if crown else 0)
    V.append(_vol(ox - tw / 2, sb, ox + tw / 2, sb + td, npod, top - npod, "tower"))
    if crown:
        i = rng.choice((200, 300))
        V.append(_vol(ox - tw / 2 + i, sb + i, ox + tw / 2 - i, sb + td - i, top, 2, "tower"))
    return V


def _m_twin(w, d, n, rng, b):
    """Two towers on a shared podium."""
    npod = rng.randint(2, 3)
    gap = rng.uniform(800, 1400)
    tw = max(1200, (w - gap - 400) / 2)
    td = max(1200, min(d - 600, d * rng.uniform(0.5, 0.7)))
    sb = rng.uniform(300, max(350, d - td - 200))
    V = [_vol(-w / 2, 0, w / 2, d, 0, npod, "podium")]
    na, nb = max(n, rng.randint(9, 18)), max(n, rng.randint(9, 18))
    V.append(_vol(-gap / 2 - tw, sb, -gap / 2, sb + td, npod, na - npod, "tower"))
    V.append(_vol(gap / 2, sb, gap / 2 + tw, sb + td, npod, nb - npod, "tower"))
    return V


def _m_setback(w, d, n, rng, b):
    """Wedding cake (New York 1916 zoning): the block steps back from the street and sides as it rises."""
    n = max(n, rng.randint(9, 16))
    tiers = rng.randint(2, 4)
    s = rng.uniform(300, 500)
    base = max(2, int(n * rng.uniform(0.35, 0.5)))
    rest = n - base
    V, f, inset = [], 0, 0
    counts = [base] + [max(1, rest // (tiers - 1))] * (tiers - 1)
    counts[-1] += n - sum(counts)
    for k, c in enumerate(counts):
        if c <= 0:
            continue
        x0, x1 = -w / 2 + inset, w / 2 - inset
        if x1 - x0 < 900:
            break
        V.append(_vol(x0, inset, x1, d, f, c, "tower" if k else "main"))
        f += c
        inset += s
    return V


def _m_terraced(w, d, n, rng, b):
    """Terraced: every 1-2 storeys the front steps back, each roof a terrace for the floor above."""
    n = min(max(n, 4), 8)
    step = rng.uniform(250, 380)
    per = rng.choice((1, 1, 2))
    V, f, k = [], 0, 0
    while f < n:
        c = min(per if f else max(1, per), n - f)
        y0 = k * step
        if d - y0 < 600:
            break
        V.append(_vol(-w / 2, y0, w / 2, d, f, c))
        f += c
        k += 1
    return V


def _m_courtyard(w, d, n, rng, b):
    """Perimeter block around a courtyard (European city block)."""
    V = [_vol(-w / 2, 0, w / 2, b, 0, n), _vol(-w / 2, d - b, w / 2, d, 0, n + rng.choice((0, 0, -1, 1)))]
    V += [_vol(-w / 2, b, -w / 2 + b, d - b, 0, n), _vol(w / 2 - b, b, w / 2, d - b, 0, n + rng.choice((0, -1)))]
    return V


def _m_u(w, d, n, rng, b):
    """U plan: a street bar with two wings back, open court behind."""
    nw = n - rng.choice((0, 0, 1))
    return [_vol(-w / 2, 0, w / 2, b, 0, n), _vol(-w / 2, b, -w / 2 + b, d, 0, nw), _vol(w / 2 - b, b, w / 2, d, 0, nw)]


def _m_l(w, d, n, rng, b):
    """L plan on one street: street bar plus a wing back along one side."""
    s = rng.choice((-1, 1))
    x0 = s * w / 2
    ww = min(b, w * 0.5)
    return [_vol(-w / 2, 0, w / 2, b, 0, n), _vol(x0, b, x0 - s * ww, d, 0, max(1, n - rng.choice((0, 1))))]


def _m_t(w, d, n, rng, b):
    """T plan: street bar with a narrower stem behind (rear extension)."""
    sw = min(w * 0.5, max(b * 0.8, w * 0.35))
    return [_vol(-w / 2, 0, w / 2, b, 0, n), _vol(-sw / 2, b, sw / 2, d, 0, max(1, n - rng.choice((0, 1, 2))))]


def _m_h(w, d, n, rng, b):
    """H plan: two wings running back from the street joined by a lower link."""
    lb = b * rng.uniform(0.7, 0.9)
    return [_vol(-w / 2, 0, -w / 2 + b, d, 0, n), _vol(w / 2 - b, 0, w / 2, d, 0, n),
            _vol(-w / 2 + b, d / 2 - lb / 2, w / 2 - b, d / 2 + lb / 2, 0, max(1, n - rng.choice((0, 1, 2))))]


def _m_stacked(w, d, n, rng, b):
    """Stacked boxes: groups of storeys shifted against each other (cantilevers), contemporary housing."""
    n = max(n, rng.randint(5, 9))
    bw, bd = w * rng.uniform(0.75, 0.85), d * rng.uniform(0.8, 0.9)
    V, f, k = [], 0, 0
    while f < n:
        g = min(rng.randint(2, 3), n - f)
        dx = 0 if k == 0 else rng.choice((-1, 1)) * rng.uniform(150, (w - bw) / 2)
        dy = (d - bd) / 2 + (0 if k == 0 else rng.uniform(-(d - bd) / 2, (d - bd) / 2))
        V.append(_vol(dx - bw / 2, dy, dx + bw / 2, dy + bd, f, g, "stack"))
        f += g
        k += 1
    return V


def _m_notched(w, d, n, rng, b):
    """A block split by a recessed slot (subtractive): two masses and a set-back glazed middle."""
    rw = rng.uniform(300, 500)
    rd = rng.uniform(220, 380)
    off = rng.uniform(-w / 6, w / 6)
    return [_vol(-w / 2, 0, off - rw / 2, d, 0, n), _vol(off - rw / 2, rd, off + rw / 2, d, 0, n + rng.choice((0, 0, 1)), "recess"),
            _vol(off + rw / 2, 0, w / 2, d, 0, n + rng.choice((0, -1, 1)))]


def _m_stepped(w, d, n, rng, b):
    """Height steps across the street front: 2-3 parts of falling height (skyline variety in one building)."""
    k = rng.choice((2, 3))
    cuts = sorted(rng.uniform(0.3, 0.7) * w - w / 2 for _ in range(k - 1))
    xs = [-w / 2] + cuts + [w / 2]
    heights = sorted({max(2, n - i * rng.randint(1, 3)) for i in range(k)}, reverse=True)
    while len(heights) < k:
        heights.append(max(1, heights[-1] - 1))
    if rng.random() < 0.5:
        heights.reverse()
    return [_vol(xs[i], 0, xs[i + 1], d, 0, heights[i]) for i in range(k)]


MASSINGS = {"block": _m_block, "slab": _m_slab, "podium_tower": _m_podium_tower, "twin_towers": _m_twin, "setback": _m_setback,
            "terraced": _m_terraced, "courtyard": _m_courtyard, "U": _m_u, "L": _m_l, "T": _m_t, "H": _m_h, "stacked": _m_stacked,
            "notched": _m_notched, "stepped": _m_stepped}
KIND_MASSINGS = {
    "apartment": ["block", "slab", "podium_tower", "courtyard", "U", "T", "terraced", "stacked", "notched", "stepped", "setback"],
    "office": ["block", "podium_tower", "twin_towers", "setback", "H", "notched", "stacked", "stepped", "slab"],
    "loft": ["block", "courtyard", "U", "L", "stepped", "setback", "notched"],
    "townhouse": ["block", "T", "L", "terraced"],
    "shophouse": ["block", "T", "terraced"],
    "villa": ["block", "L", "T", "H", "U"],
}
# lot size each massing needs (min width, min depth) and how deep a bar is for each type (cm)
_LOT = {"podium_tower": (2600, 2400), "twin_towers": (4200, 2400), "setback": (2400, 2000), "courtyard": (3200, 3200), "U": (2800, 2600),
        "H": (2800, 2600), "L": (2200, 2400), "T": (1800, 2600), "slab": (4500, 0), "stacked": (1800, 1400), "notched": (2000, 1200),
        "stepped": (2400, 1200), "terraced": (1200, 1400)}
_BAR = {"apartment": (1100, 1400), "office": (1500, 1900), "loft": (1300, 1600), "townhouse": (900, 1100), "shophouse": (900, 1200), "villa": (900, 1100)}


def _faces(v):
    return [("-y", v["y0"], v["x0"], v["x1"]), ("+y", v["y1"], v["x0"], v["x1"]), ("-x", v["x0"], v["y0"], v["y1"]), ("+x", v["x1"], v["y0"], v["y1"])]


def _covers(V, v, dirn, plane, zl, zh, Z):
    """Rectangles (u0, u1, z0, z1) of the face plane hidden by other volumes that touch or pass through it."""
    out = []
    for o in V:
        if o is v:
            continue
        if dirn in ("-y", "+y"):
            lo, hi, u0, u1 = o["y0"], o["y1"], o["x0"], o["x1"]
            touch = (abs(hi - plane) < 2 and dirn == "-y") or (abs(lo - plane) < 2 and dirn == "+y") or (lo < plane - 1 and hi > plane + 1)
        else:
            lo, hi, u0, u1 = o["x0"], o["x1"], o["y0"], o["y1"]
            touch = (abs(hi - plane) < 2 and dirn == "-x") or (abs(lo - plane) < 2 and dirn == "+x") or (lo < plane - 1 and hi > plane + 1)
        if not touch:
            continue
        za, zb = max(Z(o["f0"]), zl), min(Z(o["f0"] + o["n"]), zh)
        if zb - za > 0.5:
            out.append((u0, u1, za, zb))
    return out


def _free(lo, hi, ivs):
    out, cur = [], lo
    for a, b in sorted(ivs):
        if a > cur + 0.5:
            out.append((cur, min(a, hi)))
        cur = max(cur, b)
        if cur >= hi:
            break
    if hi > cur + 0.5:
        out.append((cur, hi))
    return out


def _exposed(ua, ub, covers, zl, zh):
    """Splits a face along u into runs with the same visible height ranges."""
    cuts = sorted({ua, ub} | {min(max(c[0], ua), ub) for c in covers} | {min(max(c[1], ua), ub) for c in covers})
    runs = []
    for a, b in zip(cuts, cuts[1:]):
        if b - a < 1:
            continue
        mid = (a + b) / 2
        fr = _free(zl, zh, [(c[2], c[3]) for c in covers if c[0] < mid < c[1]])
        key = [(round(x), round(y)) for x, y in fr]
        if runs and runs[-1][2] == key and abs(runs[-1][1] - a) < 1:
            runs[-1][1] = b
        else:
            runs.append([a, b, key])
    return runs


def _put(P, parts, dirn, plane, u, z):
    pos = {"-y": ((u, plane, z), 0), "+y": ((u, plane, z), 180), "-x": ((plane, u, z), -90), "+x": ((plane, u, z), 90)}[dirn]
    P += transform(parts, *pos)


def massed(kind, massing, rng, width=None, depth=None, floors=None, party=None, street=("-y",)):
    """Builds a building of type `kind` from a massing model (name in MASSINGS or "auto")."""
    if massing == "auto":
        massing = rng.choice(KIND_MASSINGS.get(kind, ["block"]))
    gen = MASSINGS[massing]
    plan, w0, d0, roof, bay = _recipe(kind, rng, depth, floors)
    n = len(plan["floors"])
    bar = rng.uniform(*_BAR.get(kind, (1100, 1400)))
    mw, md = _LOT.get(massing, (0, 0))
    # open space between wings: at least 12 m and ~0.7 x the building height (daylight in the court)
    court = max(1200, 0.7 * sum(f["h"] for f in plan["floors"]))
    need = {"courtyard": (2 * bar + court * 1.4, 2 * bar + court * 1.4), "U": (2 * bar + court, bar + court), "H": (2 * bar + court, 2.2 * bar),
            "L": (bar + court, bar + court), "T": (bar * 2.2, bar + court)}.get(massing)
    if need:
        mw, md = max(mw, need[0]), max(md, need[1])
    row = kind in ("townhouse", "shophouse")               # row houses keep their narrow plot; wings grow back, not sideways
    w = width or (w0 if row else max(w0, mw))
    d = depth or max(d0 if massing in ("block", "stepped", "notched", "stacked", "terraced", "setback") else 0, md, bar)
    if party is None:
        party = ("-x", "+x") if kind in ("townhouse", "shophouse") and massing in ("block", "T", "terraced") else ()
    V = gen(w, d, n, rng, min(bar, d))
    # one storey grid for the whole building: ground, the recipe's storeys, then its typical storey repeated
    pf = plan["floors"]
    typical = pf[-2] if len(pf) > 2 else (pf[1] if len(pf) > 1 else dict(pf[0], kind="window", balcony=None))
    top_f = pf[-1] if len(pf) > 1 else typical
    if typical["kind"] != "window":
        typical = dict(typical, kind="window")
    top_n = max(v["f0"] + v["n"] for v in V)
    H = [pf[i]["h"] if i < len(pf) - 1 else typical["h"] for i in range(top_n + 1)]
    H[0] = pf[0]["h"]
    zs = [0]
    for h in H:
        zs.append(zs[-1] + h)

    def Z(i):
        return zs[min(i, len(zs) - 1)]

    # tall volumes of modern types may get a curtain-wall skin
    curtain = kind in ("apartment", "office") and massing in ("podium_tower", "twin_towers", "setback", "stacked") and rng.random() < 0.6
    cw_win = {"kind": "modern", "frame": rng.choice(("frame_alu", "frame_dark")), "sill": None, "head": "none", "curtain": False}

    def storey(i, v):
        last = i == v["f0"] + v["n"] - 1
        if i == 0:
            f = dict(pf[0])
        elif last and top_n > 1 and i == top_n - 1 and len(pf) > 2:
            f = dict(top_f)
        elif i < len(pf) - 1:
            f = dict(pf[i])
        else:
            f = dict(typical)
        if f["kind"] not in ("window", "shop", "entrance", "blank"):
            f["kind"] = "window"
        f["h"] = H[i]
        if i > 0 and curtain and v["tag"] in ("tower", "stack"):
            f.update(kind="window", w=bay - 30, wh=H[i] - 45, sill=22, window=cw_win, balcony=f.get("balcony") if kind == "apartment" else None,
                     balcony_kind="continuous", rail="glass", rail_mat="steel")
        if v["tag"] == "recess" and i > 0:                  # the slot is glazed
            f.update(kind="window", w=bay - 40, wh=H[i] - 60, sill=30, window=cw_win, balcony=None)
        return f

    t = plan.get("t", 30)
    wall = m(plan["wall"])
    trim = m(plan.get("trim", "concrete"))
    rail = next((f.get("rail") for f in pf if f.get("rail")), "glass" if kind in ("apartment", "office") else "bars")
    rail_mat = "steel" if rail in ("glass", "cable") else ("stone" if rail == "balustrade" else "iron")
    P = []
    bounds = (-w / 2, w / 2)
    for v in V:
        z0, z1 = Z(v["f0"]), Z(v["f0"] + v["n"])
        for dirn, plane, ua, ub in _faces(v):
            on_lot_side = (dirn == "-x" and v["x0"] <= bounds[0] + 1) or (dirn == "+x" and v["x1"] >= bounds[1] - 1)
            role = "party" if (dirn in party and on_lot_side) else ("front" if dirn in street else "back")
            for a, b, free in _exposed(ua, ub, _covers(V, v, dirn, plane, z0, z1, Z), z0, z1):
                W = b - a
                for za, zb in free:
                    # snap the visible range to whole storeys; leftovers are plain wall
                    i = next((k for k in range(v["f0"], v["f0"] + v["n"] + 1) if Z(k) >= za - 1), None)
                    j = next((k for k in range(v["f0"] + v["n"], v["f0"] - 1, -1) if Z(k) <= zb + 1), None)
                    segs = []
                    if role == "party" or W < bay * 0.55 or i is None or j is None or j <= i:
                        segs.append(("wall", za, zb))
                    else:
                        if Z(i) - za > 1:
                            segs.append(("wall", za, Z(i)))
                        segs.append(("facade", i, j))
                        if zb - Z(j) > 1:
                            segs.append(("wall", Z(j), zb))
                    for sg in segs:
                        if sg[0] == "wall":
                            _put(P, [box([W, t, sg[2] - sg[1]], [0, t / 2, 0], m=wall)], dirn, plane, (a + b) / 2, sg[1])
                            continue
                        fi, fj = sg[1], sg[2]
                        q = dict(plan, floors=[storey(k, v) for k in range(fi, fj)])
                        if fi > 0:
                            q["plinth"] = None
                        if fj < v["f0"] + v["n"]:
                            q["cornice"] = None
                        if role == "back":
                            q = _back(q)
                        q = _with_width(q, W, bay)
                        fp, _ = facade(W, q, rng)
                        _put(P, fp, dirn, plane, (a + b) / 2, Z(fi))
        # floor plate under every volume (soffit of a cantilever, ground slab at street level)
        cx, cy, vw, vd = (v["x0"] + v["x1"]) / 2, (v["y0"] + v["y1"]) / 2, v["x1"] - v["x0"], v["y1"] - v["y0"]
        P.append(box([vw - 1, vd - 1, 4 if z0 == 0 else 22], [cx, cy, z0 + (0.5 if z0 == 0 else -2)], m=MAT["concrete"]))
        # roof: terrace (something stands on it), pitched (low top volumes of pitched types) or flat with parapet
        carries = any(o is not v and abs(Z(o["f0"]) - z1) < 2 and o["x0"] < v["x1"] - 1 and o["x1"] > v["x0"] + 1 and o["y0"] < v["y1"] - 1
                      and o["y1"] > v["y0"] + 1 for o in V)
        if roof in ("hip", "gable") and not carries and massing not in ("stacked", "setback", "podium_tower", "twin_towers"):
            e = 50
            tile = rng.choice([M("roof_tile", (160, 80, 56)), M("slate_roof", (72, 74, 80)), M("roof_tile", (120, 70, 52))])
            if roof == "hip":
                P.append({"shape": "pyramid", "size": [vw + 2 * e, vd + 2 * e, min(vw, vd) * 0.42], "location": [cx, cy, z1], **tile})
            elif vw >= vd:
                P.append({"shape": "roof", "size": [vd + 2 * e, vw + 2 * e, vd * 0.42], "location": [cx, cy, z1], "rotation": [0, 90, 0], **tile})
            else:
                P.append({"shape": "roof", "size": [vw + 2 * e, vd + 2 * e, vw * 0.42], "location": [cx, cy, z1], **tile})
            P.append(box([vw + 2 * e, vd + 2 * e, 14], [cx, cy, z1 - 14], m=MAT["frame_white"]))         # eaves board
            continue
        P.append(box([vw, vd, 20], [cx, cy, z1], m=MAT["concrete"]))
        for dirn, plane, ua, ub in _faces(v):
            inward = 1 if dirn in ("-y", "-x") else -1
            for a, b, free in _exposed(ua, ub, _covers(V, v, dirn, plane, z1 - 2, z1 - 1, Z), z1 - 2, z1 - 1):
                if not free:
                    continue
                L, u = b - a, (a + b) / 2
                if carries:                                  # terrace edge: low upstand, railing, planters
                    if dirn in ("-y", "+y"):
                        yy = plane + inward * 12
                        P.append(box([L, 24, 30], [u, plane + inward * 12, z1], m=trim))
                        P += railing_run(a + 10, yy, b - 10, yy, z1 + 30, 100, rail, rail_mat)
                    else:
                        xx = plane + inward * 12
                        P.append(box([24, L, 30], [plane + inward * 12, u, z1], m=trim))
                        P += railing_run(xx, a + 10, xx, b - 10, z1 + 30, 100, rail, rail_mat)
                    for k in range(int(L / 400)):
                        if rng.random() < 0.45:
                            pu = a + 200 + k * 400
                            pos = [pu, plane + inward * 50, z1 + 20] if dirn in ("-y", "+y") else [plane + inward * 50, pu, z1 + 20]
                            size = [140, 45, 50] if dirn in ("-y", "+y") else [45, 140, 50]
                            P.append(box(size, pos, m=M("concrete", (120, 116, 110))))
                            P.append(box([size[0] - 10, size[1] - 10, 25], [pos[0], pos[1], z1 + 70], m=M("grass", (70, 110, 50))))
                else:                                        # parapet with coping
                    if dirn in ("-y", "+y"):
                        P += [box([L, t, 90], [u, plane + inward * t / 2, z1], m=wall), box([L + 8, t + 10, 8], [u, plane + inward * t / 2, z1 + 90], m=trim)]
                    else:
                        P += [box([t, L, 90], [plane + inward * t / 2, u, z1], m=wall), box([t + 10, L + 8, 8], [plane + inward * t / 2, u, z1 + 90], m=trim)]
        if not carries:                                      # rooftop plant on the highest roofs
            plant = M("steel", (170, 172, 172), roughness=0.5)
            for _ in range(rng.randint(1, 3)):
                P.append(box([rng.choice((120, 180)), rng.choice((90, 120)), rng.choice((90, 140))],
                             [cx + rng.uniform(-vw / 4, vw / 4), cy + rng.uniform(-vd / 4, vd / 4), z1 + 20], m=plant))
            if v["n"] + v["f0"] >= 6 and vw > 800 and vd > 800:
                P.append(box([420, 320, 300], [cx + rng.uniform(-vw / 6, vw / 6), cy, z1 + 20], m=wall))     # lift / stair overrun
                if kind == "apartment" and rng.random() < 0.5:
                    P.append(cyl(260, 240, [cx - vw / 4, cy + vd / 5, z1 + 120], m=M("wood", (110, 80, 56))))  # water tank
                    P += [box([12, 12, 100], [cx - vw / 4 + sx * 90, cy + vd / 5 + sy * 90, z1 + 20], m=MAT["iron"]) for sx in (-1, 1) for sy in (-1, 1)]
    return P
