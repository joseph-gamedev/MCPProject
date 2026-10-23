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

from .parts import M, blob, box, cone, cyl, extrude, transform

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


_LOD = 2  # detail level while a building is generated: 2 high, 1 medium, 0 low (set by building(detail=...))
DETAIL = {"high": 2, "medium": 1, "low": 0}


# ------------------------------------------------------------------ 1. components

def _frame(x, y, z, w, h, t, d, mat):
    """Rectangular frame of members t wide, d deep, outer size w x h, bottom-left at (x - w/2, z)."""
    return [box([w, d, t], [x, y, z], m=mat), box([w, d, t], [x, y, z + h - t], m=mat),
            box([t, d, h - 2 * t], [x - w / 2 + t / 2, y, z + t], m=mat), box([t, d, h - 2 * t], [x + w / 2 - t / 2, y, z + t], m=mat)]


def _bars(x, y, z, w, h, cols, rows, mat, t=2.4):
    P = []
    if _LOD == 0:                                            # low detail: no glazing bars
        return P
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
    if curtain is not False and _LOD > 0:
        cur = curtain if isinstance(curtain, dict) else rng.choice(CURTAINS)
        open_l, open_r = rng.uniform(0.15, 0.45), rng.uniform(0.15, 0.45)
        P.append(box([w * open_l, 3, h - 10], [x - w / 2 + w * open_l / 2, yf + 25, z + 5], m=cur))
        if _LOD > 1:
            P.append(box([w * open_r, 3, h - 10], [x + w / 2 - w * open_r / 2, yf + 25, z + 5], m=cur))
    P.append(box([w, 1.5, h], [x, yf + 3, z], m=MAT["glass"]))
    t = 7 if kind != "modern" else 5
    P += _frame(x, yf, z, w, h, t, 8, fr)
    iw, ih = w - 2 * t, h - 2 * t
    if _LOD == 0:                                            # low detail: frame, one mullion, sill, lintel, shutter panels
        if kind in ("casement", "french", "sash", "fixed") and w > 70:
            P.append(box([5, 7, ih], [x, yf - 1, z + t], m=fr) if kind != "sash" else box([iw, 7, 5], [x, yf - 1, z + h / 2], m=fr))
        if sill and kind != "french":
            P.append(box([w + 18, yf + 8, 6], [x, yf / 2 - 4, z - 6], m=m(sill)))
        if head not in ("none", "flat", None):
            P.append(box([w + 30, 4, 18], [x, -2, z + h + 2], m=m(sill or "stone")))
        if shutters:
            P += [box([w / 2, 4, h], [x + sg * (w * 0.75 + 4), -4, z], m=m(shutters)) for sg in (-1, 1)]
        return P
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
    # reveal jambs (the wall's thickness shows round the window): close-ups only
    if _LOD > 1:
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
            if _LOD == 0:                                    # low detail: a plain panel
                P.append(box([sw, 4, h], [cx, -4, z], m=sh))
                continue
            P += _frame(cx, -4, z, sw, h, 5, 4, sh)
            step = 9 if _LOD > 1 else 16
            for k in range(int((h - 10) / step)):
                P.append(box([sw - 10, 3, step * 0.55], [cx, -5, z + 5 + k * step], rot=[-25, 0, 0], m=sh))
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
    if style == "solid":                                     # solid upstand (brutalist, loggias) with a coping
        out.append(box([ln, 14, height - 6], [mx, my, z], rot=[0, yaw, 0], m=mt))
        out.append(box([ln + 2, 20, 6], [mx, my, z + height - 6], rot=[0, yaw, 0], m=mt))
        return out
    if style == "tube":                                      # modernist ship rail: posts and three round tubes
        for k in range(max(1, int(ln / 150)) + 1):
            px, py = along(k / max(1, int(ln / 150)))
            out.append(box([4, 4, height], [px, py, z], rot=[0, yaw, 0], m=mt))
        for hz in (0.33, 0.66, 1.0):
            out.append(box([ln, 4, 4], [mx, my, z + height * hz - 4], rot=[0, yaw, 0], m=mt))
        return out
    if style == "glass":
        out.append(box([ln, 2, height - 12], [mx, my, z + 6], rot=[0, yaw, 0], m=MAT["railglass"]))
        out.append(box([ln + 4, 6, 5], [mx, my, z + height - 5], rot=[0, yaw, 0], m=MAT["steel"]))
        for k in range(int(ln / 120) + 1):
            px, py = along(k / max(1, int(ln / 120)))
            out.append(box([4, 4, height - 5], [px, py, z], rot=[0, yaw, 0], m=MAT["steel"]))
    elif style == "balustrade":
        out += [box([ln, 26, 16], [mx, my, z], rot=[0, yaw, 0], m=mt), box([ln + 6, 30, 14], [mx, my, z + height - 14], rot=[0, yaw, 0], m=mt)]
        n = max(2, int(ln / (26, 40, 60)[2 - _LOD]))
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
        sp = (13, 20, 32)[2 - _LOD]
        for k in range(int(ln / sp)):
            px, py = along((k + 0.5) / int(ln / sp))
            out.append(box([5, 2.5, height - 24], [px, py, z + 18], rot=[0, yaw, 0], m=mt))
    elif style == "cable":
        for k in range(max(1, int(ln / 140)) + 1):
            px, py = along(k / max(1, int(ln / 140)))
            out.append(box([5, 2, height], [px, py, z], rot=[0, yaw, 0], m=MAT["steel"]))
        out.append(box([ln, 6, 4], [mx, my, z + height - 4], rot=[0, yaw, 0], m=MAT["wood"]))
        out += [box([ln, 0.8, 0.8], [mx, my, z + 12 + i * (height - 26) / 6], rot=[0, yaw, 0], m=MAT["steel"]) for i in range(6)]
    else:                                                # bars (and ornate)
        out += [box([ln, 5, 5], [mx, my, z + height - 5], rot=[0, yaw, 0], m=mt), box([ln, 4, 4], [mx, my, z + 8], rot=[0, yaw, 0], m=mt)]
        n = max(2, int(ln / (11, 16, 30)[2 - _LOD]))
        for k in range(n + 1):
            px, py = along(k / n)
            out.append(box([1.8, 1.8, height - 13], [px, py, z + 8], rot=[0, yaw, 0], m=mt))
        if style == "ornate":                            # a band of rings and a lower band
            out.append(box([ln, 2, 2], [mx, my, z + height * 0.62], rot=[0, yaw, 0], m=mt))
            out.append(box([ln, 2, 2], [mx, my, z + height * 0.38], rot=[0, yaw, 0], m=mt))
            for k in range(n):
                if _LOD and k % (2 if _LOD > 1 else 4) == 0:
                    px, py = along((k + 0.5) / n)
                    out.append({"shape": "torus", "size": [10, 10, 1.6], "location": [px, py, z + height * 0.5 - 5], "rotation": [0, yaw, 90], "detail": "low", **mt})
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
    if kind == "dentils" and _LOD:
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


def _shade(mt, f):
    """The same material, lighter (f > 1) or darker (f < 1)."""
    d = dict(m(mt))
    c = d.get("color") or [0.42, 0.39, 0.35]
    d["color"] = [round(min(1.0, x * f), 4) for x in c]
    return d


def _skin(pieces, plan, rng):
    """Cladding laid over the plain wall pieces of a facade: battens, boards, panels, rustication on the base."""
    out = []
    for p, ground in pieces:
        sx, _, sz = p["size"]
        x, _, z = p["location"]
        if sx < 3 or sz < 3:
            continue
        skin = plan.get("base_skin") if ground and plan.get("base_skin") else plan.get("skin")
        if not skin:
            continue
        sm = m(plan.get("skin_m", plan["wall"]))
        if skin == "battens":                                # vertical timber slats with shadow gaps
            n = max(1, int(sx / (plan.get("batten_gap", 11) * (1, 1.5, 2.5)[2 - _LOD])))
            for k in range(n):
                out.append(box([4.5, 3, sz], [x - sx / 2 + (k + 0.5) * sx / n, -1.5, z], m=sm))
        elif skin == "boards":                               # horizontal weatherboarding: shadow lines every board
            dark = _shade(sm, 0.55)
            for k in range(1, int(sz / 19)):
                out.append(box([sx, 1.2, 1.6], [x, -0.6, z + k * 19], m=dark))
        elif skin == "panels":                               # rainscreen panels in a few tones
            tones = plan.get("skin_tones") or [sm]
            pw, ph = plan.get("panel", (120, 60))
            if _LOD == 0:
                pw, ph = pw * 2, ph * 2
            nx, nz = max(1, round(sx / pw)), max(1, round(sz / ph))
            for i in range(nx):
                for j in range(nz):
                    out.append(box([sx / nx - 1.6, 2, sz / nz - 1.6], [x - sx / 2 + (i + 0.5) * sx / nx, -1, z + j * sz / nz + 0.8], m=rng.choice(tones)))
        elif skin == "rustication":                          # banded stone base: deep joints every course
            dark = _shade(m(plan.get("base_wall", plan["wall"])), 0.55)
            for k in range(1, int(sz / 42) + 1):
                out.append(box([sx, 1, 2.5], [x, -0.4, z + k * 42 - 1.25], m=dark))
    return out


def _life(x, z_sill, w, plan, wall_m, rng):
    """Signs of use round a window: flower boxes, an AC unit, rain streaks under the sill."""
    L = plan.get("life") or {}
    P = []
    if _LOD == 0:
        return P
    r = rng.random()
    if r < L.get("flowers", 0):
        P.append(box([w + 6, 22, 18], [x, -14, z_sill - 2], m=M("terracotta", (150, 80, 56))))
        P.append(blob([w, 26, 30], [x, -14, z_sill + 12], m=M("leaves", (58, 104, 40)), seed=rng.randint(0, 999), bumpiness=0.4))
        for _ in range(rng.randint(2, 4)):
            P.append(blob([18, 14, 12], [x + rng.uniform(-w / 2.4, w / 2.4), -22, z_sill + 26], seed=rng.randint(0, 999),
                          m=M("leaves", rng.choice([(200, 40, 60), (230, 120, 160), (240, 200, 60), (250, 250, 250), (190, 60, 150)]))))
    elif r < L.get("flowers", 0) + L.get("ac", 0):
        ax = x + rng.choice((-1, 1)) * w * 0.22
        P += [box([82, 38, 56], [ax, -22, z_sill - 80], m=M("plaster", (226, 226, 220), roughness=0.5)),
              box([60, 2, 44], [ax - 6, -41.5, z_sill - 74], m=MAT["frame_dark"]),
              box([4, 40, 4], [ax - 34, -20, z_sill - 84], m=MAT["iron"]), box([4, 40, 4], [ax + 34, -20, z_sill - 84], m=MAT["iron"])]
    if rng.random() < L.get("streaks", 0):
        st = _shade(wall_m, 0.8)
        for sx in (-1, 1):
            ln = rng.uniform(40, 140)
            P.append(box([rng.uniform(4, 9), 0.6, ln], [x + sx * (w / 2 - 4), -0.3, z_sill - 8 - ln], m=st))
    return P


def _sys_ribbon(width, z, fh, fl, plan, rng, wall_m, t, W):
    """Modernist ribbon window: a continuous glazed band between solid spandrels, thin mullions, blinds, optional sunshade."""
    s, head = fl.get("sill", 95), fl.get("head_h", 35)
    bh = fh - s - head
    cm = min(fl.get("corner", 60), width / 4)
    fr = m(fl.get("window", {}).get("frame", "frame_dark"))
    P = W([box([width, t, s], [0, t / 2, z], m=wall_m), box([width, t, head], [0, t / 2, z + s + bh], m=wall_m)])
    P += W([box([cm, t, bh], [sg * (width / 2 - cm / 2), t / 2, z + s], m=wall_m) for sg in (-1, 1)])
    gw, rv = width - 2 * cm, 10
    P += [box([gw, 4, bh + 10], [0, rv + 70, z + s - 5], m=MAT["room"]), box([gw, 1.5, bh], [0, rv + 3, z + s], m=MAT["glass"])]
    n = max(1, round(gw / fl.get("mullion", 130)))
    P += [box([5, 8, bh], [-gw / 2 + k * gw / n, rv, z + s], m=fr) for k in range(n + 1)]
    P += [box([gw, 8, 5], [0, rv, z + s], m=fr), box([gw, 8, 5], [0, rv, z + s + bh - 5], m=fr)]
    blind = M("plaster", (232, 230, 222), roughness=0.7)
    for k in range(n):
        x, r = -gw / 2 + (k + 0.5) * gw / n, rng.random()
        if r < 0.35:
            drop = bh * rng.uniform(0.15, 0.75)
            P.append(box([gw / n - 8, 2, drop], [x, rv + 9, z + s + bh - drop - 4], m=blind))
        elif r < 0.55:
            P.append(box([gw / n * 0.4, 3, bh - 8], [x - gw / n * 0.28, rv + 25, z + s + 4], m=rng.choice(CURTAINS)))
        if k % 3 == 1:                                       # an opening light
            P += _frame(x, rv - 2, z + s + bh * 0.55, gw / n - 12, bh * 0.4, 4, 4, fr)
    P.append(box([gw + 6, rv + 8, 3], [0, rv / 2 - 4, z + s - 3], m=MAT["steel"]))
    P += [box([4, rv, bh], [sg * (gw / 2 + 2), rv / 2, z + s], m=wall_m) for sg in (-1, 1)]
    if fl.get("shade"):                                      # brise-soleil: a thin cantilevered shelf over the band
        P.append(box([width + 10, 70, 8], [0, -35, z + s + bh + 2], m=m(fl.get("shade_m", "frame_white"))))
    return P


def _sys_curtain(width, z, fh, plan, rng, sysp, lobby=False):
    """Curtain wall: glass on a mullion grid, opaque spandrel at each slab, optional deep fins, blinds behind."""
    ms, md = sysp.get("mullion", 150), sysp.get("mullion_d", 12)
    sp = 0 if lobby else sysp.get("spandrel_h", 90)
    gm = sysp.get("glass_m") or MAT["glass"]
    fm = sysp.get("frame_m") or MAT["steel"]
    P = [box([width, 4, fh], [0, 90, z], m=MAT["room"]), box([width, 1.5, fh - sp], [0, 6, z + sp], m=gm)]
    if sp:
        P.append(box([width, 3, sp], [0, 6, z], m=sysp.get("spandrel_m") or MAT["frame_dark"]))
    n = max(1, round(width / ms))
    for k in range(n + 1):
        x = -width / 2 + k * width / n
        P.append(box([6, md, fh], [x, 6 - md / 2, z], m=fm))
        if sysp.get("fin_d") and k % sysp.get("fin_every", 1) == 0:
            P.append(box([4, sysp["fin_d"], fh], [x, 6 - md - sysp["fin_d"] / 2, z], m=sysp.get("fin_m") or fm))
    P += [box([width, md, 6], [0, 6 - md / 2, z], m=fm), box([width, md, 5], [0, 6 - md / 2, z + sp], m=fm)]
    if lobby:
        P.append(box([width, md, 5], [0, 6 - md / 2, z + fh * 0.62], m=fm))
    else:
        blind = M("plaster", (226, 224, 216), roughness=0.7)
        for k in range(n):
            if rng.random() < 0.3:
                drop = (fh - sp) * rng.uniform(0.2, 0.7)
                P.append(box([width / n - 8, 2, drop], [-width / 2 + (k + 0.5) * width / n, 12, z + fh - drop - 4], m=blind))
    return P


def _sys_frame(width, bays, z, fh, fl, plan, rng, t, W):
    """Structural frame facade: piers and spandrels with recessed infill windows. Proportions make the style:
    deep piers + recessed spandrels = art deco verticals; equal deep grid = brutalist; brick piers + arches = warehouse."""
    sp = plan.get("sys", {})
    pw, pd = sp.get("pier_w", 40), sp.get("pier_d", 30)
    sh, sd, hh = sp.get("spandrel_h", 90), sp.get("spandrel_d", 0), sp.get("head_h", 25)
    pm, smat = m(sp.get("pier_m", plan["wall"])), m(sp.get("spandrel_m", plan["wall"]))
    bay = width / bays
    P = []
    for i in range(bays + 1):
        x = min(max(-width / 2 + i * bay, -width / 2 + pw / 2), width / 2 - pw / 2)
        P.append(box([pw, t + pd, fh], [x, (t - pd) / 2, z], m=pm))
    win = dict(fl.get("window", {}))
    for b in range(bays):
        cx, iw = -width / 2 + (b + 0.5) * bay, bay - pw
        P += W([box([iw, t + sd, sh], [cx, (t - sd) / 2, z], m=smat)]) if sd >= 0 else W([box([iw, t + sd, sh], [cx, (t - sd) / 2, z], m=smat)])
        P.append(box([iw, t, hh], [cx, t / 2, z + fh - hh], m=pm))
        orn = sp.get("ornament")
        face = -sd - 1.5
        if orn == "chevron":                                 # zigzag band on the spandrel (art deco)
            k = max(2, int(iw / 26))
            for j in range(k):
                xx = cx - iw / 2 + (j + 0.5) * iw / k
                P.append(box([4, 3, sh * 0.5], [xx, face, z + sh * 0.25], rot=[35 if j % 2 else -35, 0, 0], m=_shade(smat, 1.35)))
        elif orn == "fluted":
            k = max(2, int(iw / 14))
            P += [box([5, 3, sh - 20], [cx - iw / 2 + (j + 0.5) * iw / k, face, z + 10], m=_shade(smat, 1.25)) for j in range(k)]
        wz, wh = z + sh, fh - sh - hh
        w2 = dict(win, sill=sp.get("win_sill"), head=sp.get("win_head", "none"))
        if w2["head"] == "arch":
            w2["sill"] = sp.get("win_sill") or pm
            wh -= 22
        P += window(cx, wz, iw - 8, wh, reveal=max(8, t * 0.45), rng=rng, **w2)
        if w2["head"] == "arch":                             # brick in the arch's haunches
            P.append(box([iw, t, 22], [cx, t / 2, wz + wh], m=pm))
        if sp.get("box"):                                    # deep projecting window surround (brutalist)
            P += _frame(cx, -sp["box"] / 2, wz - 6, iw + 10, wh + 12, 14, sp["box"], pm)
    return P


def _loggia(cx, bay, z, fh, fl, plan, rng, wall_m, t, W):
    """Inset balcony: an opening cut into the wall, a room-deep recess with lined sides and ceiling, glazed back wall,
    railing or solid parapet at the face, a plant or a chair."""
    w = min(bay - 50, fl.get("loggia_w", bay * 0.78))
    L, oh = fl.get("loggia_d", 150), fh - 45
    lm = m(fl.get("loggia_m", plan["wall"]))
    P = W(_wall_bay(cx, bay, z, fh, (0, w, 0, oh), wall_m, t))
    P += [box([8, L, oh], [cx - w / 2 + 4, L / 2, z], m=lm), box([8, L, oh], [cx + w / 2 - 4, L / 2, z], m=lm),
          box([w, L, 8], [cx, L / 2, z + oh - 8], m=lm), box([w, L, 5], [cx, L / 2, z], m=MAT["concrete"])]
    dh = min(240, oh - 12)
    back = _wall_bay(cx, w, z, oh, (0, w - 40, 0, dh), wall_m, t) + window(cx, z, w - 40, dh, kind="modern",
                                                                            frame=fl.get("window", {}).get("frame", "frame_dark"), sill=None, head="none",
                                                                            reveal=8, rng=rng)
    P += transform(back, (0, L, 0))
    rail = fl.get("rail", "glass")
    if rail == "solid":
        P += railing_run(cx - w / 2 + 2, 7, cx + w / 2 - 2, 7, z, 105, "solid", wall_m)
    else:
        P += railing_run(cx - w / 2 + 6, 6, cx + w / 2 - 6, 6, z, 105, rail, fl.get("rail_mat", "steel"))
    if rng.random() < 0.6:                                   # a potted plant, sometimes a chair
        px = cx + rng.uniform(-w / 3, w / 3)
        P += [cyl(40, 40, [px, L * 0.6, z + 5], m=M("terracotta", (150, 84, 60))),
              blob([60, 60, 80], [px, L * 0.6, z + 45], m=M("leaves", (60, 104, 44)), seed=rng.randint(0, 999), bumpiness=0.45)]
    if rng.random() < 0.4:
        px = cx + rng.uniform(-w / 4, w / 4)
        P += [box([45, 45, 45], [px, L * 0.45, z + 5], m=rng.choice([MAT["wood"], MAT["frame_white"], MAT["frame_dark"]])),
              box([45, 6, 45], [px, L * 0.45 + 20, z + 50], m=MAT["wood"])]
    return P


def _base_floor(kind, width, bays, z, fh, fl, plan, rng, wall_m, t):
    """Ground floors that are not a row of openings: arcade (five-foot way), pilotis (columns, recessed lobby), glass lobby."""
    bay = width / bays
    P = []
    trim = m(plan.get("trim", "stone"))
    if kind == "arcade":
        D, pw, lt = fl.get("depth", 220), fl.get("pier_w", 55), fl.get("lintel", 60)
        for i in range(bays + 1):
            x = min(max(-width / 2 + i * bay, -width / 2 + pw / 2), width / 2 - pw / 2)
            P += [box([pw, t + 12, fh], [x, t / 2 - 6, z], m=trim), box([pw + 14, t + 20, 18], [x, t / 2 - 10, z + fh - lt - 18], m=trim),
                  box([pw + 10, t + 18, 20], [x, t / 2 - 9, z], m=trim)]
        P.append(box([width, t, lt], [0, t / 2, z + fh - lt], m=wall_m))
        P.append(box([width, D, 10], [0, D / 2, z + fh - 10], m=_shade(wall_m, 1.05)))
        tiles = [M("terracotta", (170, 96, 70)), M("plaster", (226, 220, 200)), M("plaster", (60, 90, 110))]
        for i in range(int(width / 60)):
            P.append(box([60, D, 2], [-width / 2 + (i + 0.5) * 60, D / 2, z], m=tiles[i % len(tiles)] if fl.get("tiles") else MAT["stone_dark"]))
        for b in range(bays):
            cx = -width / 2 + (b + 0.5) * bay
            P += transform(shopfront(cx, bay - pw - 10, fh - 20, frame=fl.get("frame", "frame_wood"), awning=None, rng=rng), (0, D, 0))
        for sg in (-1, 1):
            P.append(box([10, D, fh], [sg * (width / 2 - 5), D / 2, z], m=wall_m))
    elif kind == "pilotis":
        D = fl.get("depth", 380)
        P.append(box([width, D + t, 24], [0, (D + t) / 2, z + fh - 24], m=_shade(wall_m, 0.95)))
        for i in range(bays + 1):
            x = min(max(-width / 2 + i * bay, -width / 2 + 30), width / 2 - 30)
            if fl.get("square"):
                P.append(box([48, 48, fh - 24], [x, 60, z], m=wall_m))
            else:
                P.append(cyl(46, fh - 24, [x, 60, z], m=wall_m))
        gw = width * fl.get("lobby", 0.55)
        P += transform(_sys_curtain(gw, z, fh - 24, plan, rng, {"mullion": 140, "mullion_d": 8, "frame_m": MAT["frame_dark"]}, lobby=True), (0, D, 0))
        rest = (width - gw) / 2
        for sg in (-1, 1):
            P.append(box([rest, t, fh - 24], [sg * (gw / 2 + rest / 2), D + t / 2, z], m=_shade(wall_m, 0.85)))
        P.append(box([width, D, 3], [0, D / 2, z], m=MAT["stone_dark"]))
    elif kind == "lobby":
        P += _sys_curtain(width, z, fh, plan, rng, dict(plan.get("sys", {}), mullion=200), lobby=True)
        cw = min(width * 0.6, 700)
        P += [box([cw, 260, 16], [0, -130, z + fh - 110], m=MAT["steel"]), box([cw - 10, 4, 30], [0, -258, z + fh - 124], m=MAT["frame_dark"])]
        P += door(0, 200, 250, "glazed", "frame_alu", "stone_dark", reveal=6, rng=rng)
    return P


def facade(width, plan, rng):
    """Builds one facade from a plan:
    plan = {"wall": mat, "t": thickness, "bays": n, "floors": [floor, ...] bottom to top, "trim": mat,
            "string_courses": bool, "cornice": kind | None, "pilasters": bool, "quoins": bool, "plinth": mat | None,
            "skin": battens | boards | panels | None, "base_skin": rustication, "base_wall": mat, "sys": {frame / curtain params},
            "life": {"flowers", "ac", "streaks"}, "crown": fascia | screen, "pier_crown": cm, "bay_window": {...},
            "area_railing": bool, "ground": facade starts at street level, "at_top": facade reaches the roof}
    floor = {"h": storey height, "kind": "window" | "shop" | "entrance" | "blank" | "arcade" | "pilotis" | "lobby",
             "system": punched (default) | ribbon | curtain | frame, "window": {...window kwargs...}, "w", "wh", "sill",
             "jitter": 0-1 (irregular placement), "w_alt": second width, "balcony": None | "all" | "centre" | "alternate" | "ends" | set,
             "balcony_kind": slab | brackets | continuous | loggia, "rail", "rail_mat", "door_bay": index}"""
    wall_m = m(plan["wall"])
    base_m = m(plan.get("base_wall", plan["wall"]))
    t = plan.get("t", 30)
    bays = plan["bays"]
    bay = width / bays
    trim = plan.get("trim", "stone")
    ground = plan.get("ground", True)
    P = []
    walls = []

    def W(pcs, g=False):
        walls.extend((p, g) for p in pcs)
        return pcs

    z = 0
    for fi, fl in enumerate(plan["floors"]):
        fh = fl["h"]
        g = ground and fi == 0
        wm = base_m if g else wall_m
        kind = fl["kind"]
        if kind in ("arcade", "pilotis", "lobby"):
            P += _base_floor(kind, width, bays, z, fh, fl, plan, rng, wm, t)
            z += fh
            continue
        sysm = fl.get("system")
        if kind == "window" and sysm in ("ribbon", "curtain", "frame"):
            if sysm == "ribbon":
                P += _sys_ribbon(width, z, fh, fl, plan, rng, wm, t, lambda pcs: W(pcs, g))
                for b in range(bays):
                    bal = fl.get("balcony")
                    if bal == "ends" and b in (0, bays - 1) or bal == "all":
                        P += balcony(-width / 2 + (b + 0.5) * bay, z, bay - 30, fl.get("balcony_depth", 110), "slab", fl.get("rail", "tube"),
                                     fl.get("rail_mat", "frame_white"), fl.get("slab_mat", plan["wall"]))
            elif sysm == "curtain":
                P += _sys_curtain(width, z, fh, plan, rng, plan.get("sys", {}))
            else:
                P += _sys_frame(width, bays, z, fh, fl, plan, rng, t, lambda pcs: W(pcs, g))
            if fi > 0 and plan.get("string_courses"):
                P.append(box([width + 10, 8, 14], [0, -4, z - 7], m=m(trim)))
            z += fh
            continue
        bal = fl.get("balcony")
        cont = fl.get("balcony_kind") == "continuous" and bal == "all"
        for b in range(bays):
            cx = -width / 2 + (b + 0.5) * bay
            if kind == "shop":
                if b == fl.get("door_bay", -1):              # the residents' entrance between the shops
                    dw = min(bay - 60, 140)
                    P += W(_wall_bay(cx, bay, z, fh, (0, dw, 0, 280), wm, t), g)
                    P += door(cx, dw, 280, fl.get("door_kind", "double"), fl.get("door_mat", "wood_dark"), trim, reveal=t * 0.6, fanlight=False, rng=rng)
                    continue
                P += [box([bay, t, 0.01], [cx, t / 2, z], m=wm)]
                P += shopfront(cx, bay - 10, fh, frame=fl.get("frame", "frame_dark"), awning=fl.get("awning"), rng=rng)
                continue
            if kind == "entrance" and b == fl.get("door_bay", bays // 2):
                dw = fl.get("door_w", 120)
                P += W(_wall_bay(cx, bay, z, fh, (0, dw, 0, 230 + (50 if fl.get("fanlight") else 0)), wm, t), g)
                P += door(cx, dw, 230, fl.get("door_kind", "panel"), fl.get("door_mat", "wood_dark"), trim, reveal=t * 0.6, fanlight=fl.get("fanlight"), rng=rng)
                continue
            if kind == "blank":
                P += W(_wall_bay(cx, bay, z, fh, None, wm, t), g)
                continue
            w, wh, sz = fl.get("w", 120), fl.get("wh", 180), fl.get("sill", 80)
            pairs = fl.get("pair", False)
            win = dict(fl.get("window", plan.get("ground_window", {})))
            has_bal = (bal == "all" or (bal == "centre" and b == bays // 2) or (bal == "alternate" and b % 2 == 1) or (bal == "ends" and b in (0, bays - 1))
                       or (isinstance(bal, (set, list, tuple)) and b in bal))
            if has_bal and fl.get("balcony_kind") == "loggia":
                P += _loggia(cx, bay, z, fh, fl, plan, rng, wm, t, lambda pcs: W(pcs, g))
                continue
            dx = 0
            if not has_bal and fl.get("jitter"):             # irregular, staggered openings (contemporary, vernacular)
                if fl.get("w_alt") and rng.random() < 0.4:
                    w = fl["w_alt"]
                dx = rng.uniform(-1, 1) * fl["jitter"] * max(0, (bay - w) / 2 - 25)
            if has_bal:
                win = dict(win, kind="french")
                sz, wh = 0, min(fh - 50, 240)
            if pairs and not has_bal:
                ww = w / 2 - 10
                P += W(_wall_bay(cx, bay, z, fh, (0, w, sz, wh), wm, t), g)
                P.append(box([20, t, wh], [cx, t / 2, z + sz], m=wm))
                for sg in (-1, 1):
                    P += window(cx + sg * (ww / 2 + 10), z + sz, ww, wh, reveal=t * 0.5, rng=rng, **win)
            else:
                P += W(_wall_bay(cx, bay, z, fh, (dx, w, sz, wh), wm, t), g)
                P += window(cx + dx, z + sz, w, wh, reveal=t * 0.5, rng=rng, **win)
            if not has_bal and sz > 20:
                P += _life(cx + dx, z + sz, w, plan, wm, rng)
            if has_bal and not cont:
                bw = min(bay - 20, w + 120)
                P += balcony(cx, z, bw, fl.get("balcony_depth", 120), fl.get("balcony_kind", "slab"), fl.get("rail", "bars"), fl.get("rail_mat", "iron"),
                             fl.get("slab_mat", "concrete"))
        if cont:                                             # one balcony along the whole front, brackets at every bay line
            dep = fl.get("balcony_depth", 90)
            sm = m(fl.get("slab_mat", "concrete"))
            P += [box([width, dep, 14], [0, -dep / 2, z - 14], m=sm), box([width + 6, 8, 18], [0, -dep - 2, z - 18], m=sm)]
            for i in range(bays + 1):
                bx = min(max(-width / 2 + i * bay, -width / 2 + 10), width / 2 - 10)
                P += [box([14, dep * 0.7, 10], [bx, -dep * 0.35, z - 24], m=sm), box([14, dep * 0.4, 14], [bx, -dep * 0.2, z - 38], m=sm)]
            P += railing(-width / 2 + 3, width / 2 - 3, -dep + 6, z, 100, fl.get("rail", "ornate"), fl.get("rail_mat", "iron"), return_depth=dep - 8)
        if fi > 0 and plan.get("string_courses"):
            P.append(box([width + 10, 8, 14], [0, -4, z - 7], m=m(trim)))
        z += fh
    P += _skin(walls, plan, rng)
    bw_ = plan.get("bay_window")
    if bw_ and ground:                                       # projecting bay windows over the lower storeys (Victorian)
        k = bw_.get("floors", 2)
        H = sum(f["h"] for f in plan["floors"][:k])
        D = bw_.get("depth", 70)
        for b in bw_.get("bays", []):
            if b >= bays:
                continue
            cx = -width / 2 + (b + 0.5) * bay
            bwid = bay * 0.8
            mini = {"wall": plan["wall"], "base_wall": plan.get("base_wall"), "base_skin": plan.get("base_skin"), "t": 16, "bays": 1, "trim": trim,
                    "floors": [dict(f, kind="window", w=bwid - 50, wh=min(f["h"] - 110, 220), sill=70, balcony=None, jitter=0, system=None,
                                    window=dict(plan["floors"][-1].get("window", {}), shutters=None)) for f in plan["floors"][:k]]}
            if not mini["base_wall"]:
                mini.pop("base_wall")
            fp, _ = facade(bwid, mini, rng)
            P += transform(fp, (cx, -D, 0))
            P += [box([14, D, H], [cx + sg * (bwid / 2 - 7), -D / 2, 0], m=wall_m) for sg in (-1, 1)]
            P += [box([bwid + 16, D + 16, 14], [cx, -D / 2 - 4, H], m=m(trim)), box([bwid + 4, D + 4, 40], [cx, -D / 2, H + 14], m=MAT["frame_dark"])]
    if plan.get("area_railing") and ground:                  # railings at the pavement edge with a gap for the steps
        db = plan["floors"][0].get("door_bay", bays // 2)
        gx = -width / 2 + (db + 0.5) * bay
        P += railing_run(-width / 2, -160, gx - 70, -160, 0, 105, "ornate", "iron") + railing_run(gx + 70, -160, width / 2, -160, 0, 105, "ornate", "iron")
    if plan.get("plinth") and ground:
        P.append(box([width + 6, t + 10, 60], [0, t / 2 - 5, 0], m=m(plan["plinth"])))
    if plan.get("pilasters"):
        for b in range(bays + 1):
            P += pilaster(-width / 2 + b * width / bays, plan["floors"][0]["h"] if ground else 0, z, 36, trim)
    if plan.get("quoins"):
        for sg in (-1, 1):
            P += quoins(sg * width / 2, 0, z, trim)
    if plan.get("at_top", True):
        if plan.get("cornice"):
            P += cornice(width, z, 40, trim, plan["cornice"])
        if plan.get("crown") == "fascia":                    # heavy concrete band crowning a brutalist block
            P.append(box([width + 40, t + 40, 130], [0, (t - 40) / 2, z], m=m(trim)))
        if plan.get("crown") == "screen":                    # curtain wall mullions carried up past the roof as a screen
            sp = plan.get("sys", {})
            n = max(1, round(width / sp.get("mullion", 150)))
            P += [box([6, 30, 160], [-width / 2 + k * width / n, -10, z], m=sp.get("frame_m") or MAT["steel"]) for k in range(n + 1)]
            P.append(box([width, 30, 8], [0, -10, z + 152], m=sp.get("frame_m") or MAT["steel"]))
        if plan.get("pier_crown"):                           # art deco: piers rise above the parapet
            sp = plan.get("sys", {})
            pw, pd = sp.get("pier_w", 40), sp.get("pier_d", 30)
            for i in range(bays + 1):
                x = min(max(-width / 2 + i * bay, -width / 2 + pw / 2), width / 2 - pw / 2)
                ext = plan["pier_crown"] * (1.0 if i % 2 == 0 else 0.6)
                P.append(box([pw, t + pd, ext], [x, (t - pd) / 2, z], m=m(sp.get("pier_m", plan["wall"]))))
                P.append(box([pw + 10, t + pd + 6, 12], [x, (t - pd) / 2, z + ext], m=m(trim)))
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
        if f["kind"] in ("shop", "entrance", "arcade", "pilotis", "lobby"):
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
    bases = ("shop", "entrance", "arcade", "pilotis", "lobby")
    sysf = next((f for f in plan["floors"] if f.get("system") in ("curtain", "ribbon", "frame")), None)
    typ = next((f for f in plan["floors"] if f["kind"] == "window"), None)
    for f in plan["floors"]:
        b = dict(f, kind="window", balcony=None)
        if f["kind"] in bases and sysf:                      # the same window system as the storeys above
            b.update({k: v for k, v in sysf.items() if k not in ("h", "kind", "balcony")})
        elif f["kind"] in bases and typ:                     # the style's own windows, a little smaller
            b.update(w=typ.get("w", 110), wh=min(typ.get("wh", 150), f["h"] - 130), sill=90, system=None, jitter=typ.get("jitter", 0),
                     window=dict(typ.get("window", {}), shutters=None))
        elif f["kind"] in bases:
            b.update(w=110, wh=150, sill=90, window={"kind": "casement", "curtain": False, "sill": "concrete", "head": "lintel"})
        floors.append(b)
    return dict(plan, floors=floors, pilasters=False, quoins=False, cornice=None, bay_window=None, area_railing=False, base_skin=None)


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
             massing=None, party=None, street=("-y",), style=None, detail="high"):
    """A complete building of a type, varied by seed.
    shape rect: front facade on the street (-Y), back, sides, roof; origin = front centre at ground.
    shape L (corner building): two street facades meeting at the origin, wing A along +X (facing -Y), wing B along +Y
      (facing -X); width = wing A length, wing = wing B length; corner = square | chamfer | tower | balconies (default:
      chosen by the seed).
    massing: a massing model (see MASSINGS, or "auto" to let the type and seed pick): the building is composed of
      volumes on a lot (width x depth, front edge at y = 0, centred on x = 0) and every exposed face gets a facade.
      party: lot sides that touch neighbours and stay blank ("-x", "+x"); street: faces that get the shop / entrance
      ground floor and the main facade.
    style: None / "auto" / "modern" = a contemporary building from big primitives (unreal_kit.modern: every type in
      modern.KINDS: school, hospital, office_tower, hotel, mall, mosque, warehouse, ...); a historic style name (see STYLES): materials, window system,
      balconies, base, crown and roof. Implies massing="auto" unless a massing is given.
    detail: high (close-ups, every louvre and railing bar) | medium (streets, about half the parts) | low (districts
      seen from above or far away, about a quarter).
    seed: None = a new random building every call; pass a number to get the same one again.
    Returns parts."""
    global _LOD
    from . import modern
    if style in (None, "auto", "modern") and kind in modern.KINDS and massing in (None, "auto"):   # contemporary, from big primitives
        parts = modern.building(kind, seed=seed, width=width, depth=depth, floors=floors, detail=detail, site=False)
        LAST_MASSING.clear()
        LAST_MASSING.update(massing=modern.LAST["form"], storeys=modern.LAST["storeys"], volumes=modern.LAST["volumes"])
        return parts
    rng = random.Random(seed)
    old, _LOD = _LOD, DETAIL.get(detail, 2) if isinstance(detail, str) else int(detail)
    try:
        if massing or style:
            return massed(kind, massing or "auto", rng, width, depth, floors, party, street, style)
        if shape in ("L", "corner"):
            corner = corner or rng.choice(("square", "chamfer", "tower", "balconies"))
            return _l_shape(kind, rng, width, wing, depth, floors, corner)
        return _rect(kind, rng, width, depth, floors)
    finally:
        _LOD = old


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


LAST_MASSING = {}  # the last massed building: massing name, volumes and storeys (for checks)


def massed(kind, massing, rng, width=None, depth=None, floors=None, party=None, street=("-y",), style=None):
    """Builds a building of type `kind` from a massing model (name in MASSINGS or "auto") in an architectural style
    (name in STYLES, "auto", or None for the type's plain recipe)."""
    plan, w0, d0, roof, bay = _recipe(kind, rng, depth, floors)
    lang = {}
    if style:
        name = rng.choice(KIND_STYLES.get(kind, list(STYLES))) if style == "auto" else style
        lang = STYLES[name](kind, rng, plan, bay) or {}
        lang["name"] = name
        roof = lang.get("roof", roof)
        if massing in (None, "auto"):
            ok = [x for x in lang.get("massings", ["block"]) if x in KIND_MASSINGS.get(kind, list(MASSINGS))]
            massing = rng.choice(ok or ["block"])
    if massing in (None, "auto"):
        massing = rng.choice(KIND_MASSINGS.get(kind, ["block"]))
    gen = MASSINGS[massing]
    n = int(floors) if floors else len(plan["floors"])         # asked-for floors win over the style's own count
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
    if floors:
        # exact height: no volume above n storeys (towers, crowns, +1 wings), and the tallest part reaches n
        V = [v for v in V if v["f0"] < n]
        for v in V:
            v["n"] = max(1, min(v["n"], n - v["f0"]))
        tallest = max(v["f0"] + v["n"] for v in V)
        if tallest < n:
            for v in V:
                if v["f0"] + v["n"] == tallest:
                    v["n"] += n - tallest
    LAST_MASSING.clear()
    LAST_MASSING.update(massing=massing, volumes=[dict(v) for v in V], storeys=max(v["f0"] + v["n"] for v in V))
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
    curtain = not lang and kind in ("apartment", "office") and massing in ("podium_tower", "twin_towers", "setback", "stacked") and rng.random() < 0.6
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
        if f["kind"] not in ("window", "shop", "entrance", "blank", "arcade", "pilotis", "lobby"):
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
                        q = dict(plan, floors=[storey(k, v) for k in range(fi, fj)], ground=fi == 0, at_top=fj == v["f0"] + v["n"])
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
        rm = lang.get("roof_m") or rng.choice([M("roof_tile", (160, 80, 56)), M("slate_roof", (72, 74, 80)), M("roof_tile", (120, 70, 52))])
        highest = z1 >= max(Z(o["f0"] + o["n"]) for o in V) - 1
        if not carries and roof == "mansard":
            _mansard(P, V, v, z1, Z, plan, rng, bay, rm, lang)
            continue
        if not carries and roof == "gable_steep":
            _gable_steep(P, V, v, z1, Z, plan, rng, rm, lang)
            continue
        if not carries and roof == "overhang":
            P.append(box([vw + 180, vd + 180, 26], [cx, cy, z1], m=lang.get("overhang_m") or MAT["frame_white"]))
            P.append(box([vw + 184, vd + 184, 6], [cx, cy, z1 + 26], m=MAT["steel"]))
            continue
        if roof in ("hip", "gable") and not carries and massing not in ("stacked", "setback", "podium_tower", "twin_towers"):
            e = 50
            tile = rm
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
        if not carries and roof == "sawtooth":               # north-light factory roof over the parapet
            td, hh = rng.choice((560, 640)), 250
            y = v["y0"] + 40
            while y + td <= v["y1"] - 40:
                P.append({"shape": "extrude", "profile": [[v["x0"] + 40, y, z1 + 20], [v["x0"] + 40, y + td, z1 + 20], [v["x0"] + 40, y + td, z1 + 20 + hh]],
                          "direction": [vw - 80, 0, 0], "location": [0, 0, 0], **(lang.get("roof_m") or M("corrugated", (120, 124, 126)))})
                P.append(box([vw - 90, 3, hh - 30], [cx, y + td - 2, z1 + 32], m=MAT["glass"]))
                P += [box([5, 6, hh - 30], [v["x0"] + 40 + k * 160, y + td - 3, z1 + 32], m=MAT["frame_dark"]) for k in range(1, int((vw - 80) / 160))]
                y += td
        if not carries and lang.get("chimneys"):            # chimney stacks with pots along the party walls
            brick = M("brick", (130, 76, 60))
            for sx in (v["x0"] + 45, v["x1"] - 45):
                for sy in (cy - vd / 4, cy + vd / 4):
                    P.append(box([70, 110, 190], [sx, sy, z1 + 20], m=brick))
                    P.append(box([82, 122, 12], [sx, sy, z1 + 200], m=brick))
                    P += [cyl(22, 40, [sx, sy + o, z1 + 212], m=M("terracotta", (166, 92, 64))) for o in (-30, 0, 30)]
        if not carries and roof == "pergola":                # roof terrace: timber pergola, table, planters
            pw, pd = min(vw * 0.5, 650), min(vd * 0.5, 420)
            px, py = cx + rng.uniform(-vw / 6, vw / 6), cy
            wood = M("wood", (130, 92, 60))
            for sx in (-1, 1):
                for sy in (-1, 1):
                    P.append(box([14, 14, 250], [px + sx * pw / 2, py + sy * pd / 2, z1 + 20], m=wood))
            P += [box([pw + 40, 10, 22], [px, py + sy * pd / 2, z1 + 248], m=wood) for sy in (-1, 1)]
            P += [box([6, pd + 30, 14], [px - pw / 2 + k * pw / 10, py, z1 + 270], m=wood) for k in range(11)]
            P += [box([160, 80, 6], [px, py, z1 + 95], m=wood), box([10, 10, 75], [px, py, z1 + 20], m=MAT["iron"])]
            for k in range(4):
                P += [box([90, 40, 45], [px - pw / 2 + 60 + k * (pw - 120) / 3, py + pd / 2 + 60, z1 + 20], m=M("terracotta", (150, 84, 60))),
                      blob([90, 50, 70], [px - pw / 2 + 60 + k * (pw - 120) / 3, py + pd / 2 + 60, z1 + 60], m=M("leaves", (64, 110, 44)), seed=rng.randint(0, 999))]
        if lang.get("finial") and highest:                   # spire on the top of an art deco tower
            P += [cyl(min(vw, vd) * 0.35, 160, [cx, cy, z1 + 20], m=m(plan.get("trim", "stone"))),
                  cone(min(vw, vd) * 0.3, 420, [cx, cy, z1 + 180], m=M("steel", (200, 200, 196), roughness=0.25)),
                  cyl(12, 520, [cx, cy, z1 + 580], m=MAT["steel"])]
        if not carries and roof not in ("pergola",):          # rooftop plant on the highest roofs
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


def _edge_open(V, v, dirn, plane, ua, ub, z1, Z):
    """Share of a roof edge that is open to the sky (not against an equal or taller neighbour)."""
    runs = _exposed(ua, ub, _covers(V, v, dirn, plane, z1 - 2, z1 - 1, Z), z1 - 2, z1 - 1)
    return sum(b - a for a, b, free in runs if free) / max(1, ub - ua)


def _mansard(P, V, v, z1, Z, plan, rng, bay, rm, lang):
    """Paris mansard: a steep zinc or slate storey with dormers on every open side, flat top, chimneys."""
    x0, x1, y0, y1 = v["x0"], v["x1"], v["y0"], v["y1"]
    h, k = lang.get("mansard_h", 300), lang.get("mansard_in", 110)
    ins = {d: (k if _edge_open(V, v, d, pl, a, b, z1, Z) > 0.5 else 0) for d, pl, a, b in _faces(v)}
    zb = z1 + 10
    bottom = [[x0, y0, zb], [x1, y0, zb], [x1, y1, zb], [x0, y1, zb]]
    top = [[x0 + ins["-x"], y0 + ins["-y"], zb + h], [x1 - ins["+x"], y0 + ins["-y"], zb + h], [x1 - ins["+x"], y1 - ins["+y"], zb + h],
           [x0 + ins["-x"], y1 - ins["+y"], zb + h]]
    P.append({"shape": "loft", "sections": [bottom, top], "location": [0, 0, 0], "closed": True, "cap": True, **rm})
    P.append(box([x1 - x0 - ins["-x"] - ins["+x"] + 10, y1 - y0 - ins["-y"] - ins["+y"] + 10, 10], [(x0 + ins["-x"] + x1 - ins["+x"]) / 2,
                                                                                                  (y0 + ins["-y"] + y1 - ins["+y"]) / 2, zb + h], m=MAT["steel"]))
    frame = lang.get("dormer_frame", "frame_white")
    trim = m(plan.get("trim", "stone"))
    for d, pl, a, b in _faces(v):
        if not ins[d]:
            continue
        n = max(1, int((b - a) / bay))
        for i in range(n):
            u = a + (i + 0.5) * (b - a) / n
            dw, dh = 118, 196
            front = transform(_wall_bay(0, dw, 16, dh, (0, dw - 36, 22, dh - 56), trim, 10)
                              + window(0, 38, dw - 36, dh - 56, kind="casement", frame=frame, sill=None, head="none", reveal=5, panes=(2, 3), rng=rng),
                              (0, 16, 0))
            cheeks = [box([10, 120, dh], [sg * (dw / 2 - 5), 16 + 60, 16], m=rm) for sg in (-1, 1)]
            top_ = [{"shape": "roof", "size": [dw + 26, 136, 56], "location": [0, 16 + 60, 16 + dh], **rm},
                    box([dw + 30, 12, 14], [0, 10, 16 + dh - 4], m=trim)]
            _put(P, front + cheeks + top_, d, pl, u, zb)
    if lang.get("chimneys", True):
        brick = M("brick", (150, 92, 70))
        for sx in (x0 + 50, x1 - 50):
            P += [box([80, 160, 140], [sx, (y0 + y1) / 2, zb + h - 40], m=brick),
                  *[cyl(24, 46, [sx, (y0 + y1) / 2 + o, zb + h + 100], m=M("terracotta", (170, 96, 66))) for o in (-50, -16, 18, 52)]]


def _gable_steep(P, V, v, z1, Z, plan, rng, rm, lang):
    """Steep gable without eaves (Nordic): roof along the long side, gable ends in the wall cladding, a ridge cap."""
    x0, x1, y0, y1 = v["x0"], v["x1"], v["y0"], v["y1"]
    vw, vd = x1 - x0, y1 - y0
    cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
    pitch = lang.get("pitch", 0.55)
    gm = m(plan.get("skin_m", plan["wall"]))
    e = 18
    if vw >= vd:
        h = vd * pitch
        P.append({"shape": "roof", "size": [vd + 2 * e, vw + 2 * e, h], "location": [cx, cy, z1], "rotation": [0, 90, 0], **rm})
        for gx, dx in ((x0 - 2, 34), (x1 + 2, -34)):
            P.append({"shape": "extrude", "profile": [[gx, y0, z1], [gx, y1, z1], [gx, cy, z1 + h]], "direction": [dx, 0, 0], "location": [0, 0, 0], **gm})
        P.append(box([vw + 2 * e, 18, 10], [cx, cy, z1 + h + e * pitch - 4], m=rm))
    else:
        h = vw * pitch
        P.append({"shape": "roof", "size": [vw + 2 * e, vd + 2 * e, h], "location": [cx, cy, z1], **rm})
        for gy, dy in ((y0 - 2, 34), (y1 + 2, -34)):
            P.append({"shape": "extrude", "profile": [[x0, gy, z1], [x1, gy, z1], [cx, gy, z1 + h]], "direction": [0, dy, 0], "location": [0, 0, 0], **gm})
        P.append(box([18, vd + 2 * e, 10], [cx, cy, z1 + h + e * pitch - 4], m=rm))


# ------------------------------------------------------------------ 5. architectural styles
# A style is a facade language: materials and palette, window system and rhythm, balconies, base, crown and roof.
# Massing gives the silhouette; the style decides what it is made of and how it reads up close. Each function
# rewrites the type's plan in place and returns {"roof", "massings", ...}.

def _clean(plan, **kw):
    for k in ("base_wall",):
        plan.pop(k, None)
    plan.update(dict(string_courses=False, cornice=None, pilasters=False, quoins=False, plinth=None, skin=None, base_skin=None, sys={}, life={},
                     crown=None, pier_crown=None, bay_window=None, area_railing=False), **kw)


def _rgb(c, f):
    return tuple(max(0, min(255, int(x * f))) for x in c)


def _shop_ground(kind, rng, bay, frame, h=420, awning=None, door=True):
    g = {"h": h, "kind": "shop", "frame": frame, "awning": awning}
    if door and kind in ("apartment", "office"):
        g["door_bay"] = rng.choice((0, 1))
    return g


def _st_haussmann(kind, rng, plan, bay):
    """Paris, 1853-1914: dressed limestone, rusticated base with shops, wrought-iron balconies on the 2nd and 5th floors,
    tall French windows with carved surrounds, a cornice, then a zinc mansard with dormers."""
    stone = rng.choice([(226, 216, 194), (218, 206, 182), (210, 200, 180), (230, 222, 206)])
    _clean(plan, wall=M("limestone", stone), trim=M("limestone", _rgb(stone, 1.04)), t=42, string_courses=True, cornice=rng.choice(("moulded", "dentils")),
           base_skin="rustication", life={"flowers": 0.12, "streaks": 0.35})
    frame = rng.choice(("frame_white", "frame_white", "frame_dark"))
    win = {"kind": "french", "frame": frame, "sill": "stone", "head": rng.choice(("hood", "keystone", "lintel")), "surround": True, "panes": (2, 4)}
    plain = {"h": 310, "kind": "window", "w": bay * 0.42, "wh": 215, "sill": 18, "window": win, "balcony": "all", "balcony_kind": "slab",
             "balcony_depth": 24, "rail": "ornate", "rail_mat": "iron", "slab_mat": M("limestone", stone)}
    noble = dict(plain, h=350, wh=245, window=dict(win, head="hood"), balcony_kind="continuous", balcony_depth=95)
    top = dict(plain, h=290, wh=195, balcony_kind="continuous", balcony_depth=62)
    if kind in ("apartment", "office", "shophouse"):
        ground = _shop_ground(kind, rng, bay, rng.choice(("frame_dark", "frame_green", "wood_dark")), 430, rng.choice((None, "awning")))
    else:
        ground = {"h": 400, "kind": "entrance", "door_kind": "double", "door_mat": rng.choice(("wood_dark", "frame_green")), "fanlight": True,
                  "w": bay * 0.42, "wh": 260, "sill": 60, "window": dict(win, kind="casement")}
    n = rng.randint(5, 6) if kind != "townhouse" else 4
    fl = [ground, dict(plain), noble, dict(plain), dict(plain), top][:n]
    fl[-1] = top
    plan["floors"] = fl
    return {"roof": "mansard", "roof_m": rng.choice([M("steel", (118, 124, 130), roughness=0.45), M("slate_roof", (80, 84, 92))]),
            "massings": ["block", "courtyard", "L", "U", "T"], "chimneys": True}


def _st_warehouse(kind, rng, plan, bay):
    """19th-century brick warehouse / loft: brick piers, recessed spandrels, segmental-arched steel windows, dentil cornice."""
    brick = rng.choice([(150, 80, 62), (128, 72, 58), (112, 66, 54), (160, 106, 80), (96, 60, 50)])
    _clean(plan, wall=M("brick", brick), trim=M("concrete", (176, 170, 160)), t=46, cornice="dentils", plinth=M("granite", (96, 94, 92)),
           sys={"pier_w": 58, "pier_d": 14, "pier_m": M("brick", _rgb(brick, 0.82)), "spandrel_h": 88, "spandrel_d": -4, "spandrel_m": M("brick", brick),
                "head_h": 46, "win_head": rng.choice(("arch", "arch", "none")), "win_sill": M("concrete", (176, 170, 160))},
           life={"streaks": 0.3})
    frame = rng.choice(("frame_dark", "frame_green", M("painted_iron", (110, 32, 28), roughness=0.5)))
    win = {"kind": "fixed", "frame": frame, "panes": rng.choice(((4, 4), (3, 4), (4, 3))), "curtain": False}
    typ = {"h": 380, "kind": "window", "system": "frame", "window": win}
    ground = _shop_ground(kind, rng, bay, rng.choice(("frame_dark", "frame_green")), 460)
    n = rng.randint(3, 7)
    plan["floors"] = [ground] + [dict(typ) for _ in range(n - 1)]
    roof = "sawtooth" if n <= 3 and rng.random() < 0.7 else "flat"
    return {"roof": roof, "massings": ["block", "L", "U", "courtyard", "stepped"]}


def _st_brutalist(kind, rng, plan, bay):
    """1955-1980 brutalism: board-marked concrete, a deep structural grid or inset loggias with solid parapets, pilotis, heavy fascia."""
    conc = rng.choice([(176, 174, 166), (156, 154, 148), (190, 186, 176), (140, 138, 134)])
    variant = rng.choice(("grid", "loggia", "fins"))
    _clean(plan, wall=M("concrete", conc), trim=M("concrete", _rgb(conc, 0.92)), t=50, crown="fascia", life={"streaks": 0.5})
    win = {"kind": "modern", "frame": "frame_dark", "curtain": rng.random() < 0.5}
    if variant == "grid":
        plan["sys"] = {"pier_w": 46, "pier_d": 55, "spandrel_h": 110, "spandrel_d": 55, "head_h": 26, "box": 0}
        typ = {"h": 320, "kind": "window", "system": "frame", "window": win}
    elif variant == "fins":
        plan["sys"] = {"pier_w": 24, "pier_d": 95, "spandrel_h": 100, "spandrel_d": 18, "head_h": 22}
        typ = {"h": 320, "kind": "window", "system": "frame", "window": win}
    else:
        typ = {"h": 320, "kind": "window", "w": bay * 0.5, "wh": 170, "sill": 90, "window": dict(win, sill=None, head="none"), "balcony": "all",
               "balcony_kind": "loggia", "rail": "solid", "loggia_d": 170, "loggia_w": bay * 0.82}
    ground = {"h": 400, "kind": "pilotis", "depth": rng.choice((320, 420)), "square": rng.random() < 0.5, "lobby": 0.5}
    n = rng.randint(4, 12)
    plan["floors"] = [ground] + [dict(typ) for _ in range(n - 1)]
    return {"roof": "flat", "massings": ["slab", "podium_tower", "stacked", "H", "block", "terraced"]}


def _st_modernist(kind, rng, plan, bay):
    """International Style, 1927-1960: smooth white render, ribbon windows, pilotis, thin balcony slabs with tube rails, flat roof terrace."""
    white = rng.choice([(242, 240, 234), (236, 234, 226), (226, 228, 228), (240, 236, 220)])
    _clean(plan, wall=M("plaster", white), trim=M("plaster", _rgb(white, 0.97)), t=24, life={"streaks": 0.25})
    frame = rng.choice(("frame_dark", "frame_dark", "frame_white", M("painted_iron", (40, 70, 100), roughness=0.45)))
    typ = {"h": 300, "kind": "window", "system": "ribbon", "sill": rng.choice((90, 100, 110)), "head_h": rng.choice((30, 40)), "mullion": rng.choice((100, 120, 150)),
           "window": {"frame": frame}, "shade": rng.random() < 0.35, "balcony": rng.choice((None, "ends", "ends")), "rail": "tube",
           "rail_mat": M("painted_iron", (236, 236, 232), roughness=0.4), "balcony_depth": 110, "slab_mat": M("plaster", white)}
    ground = rng.choice([{"h": 360, "kind": "pilotis", "depth": 360, "lobby": 0.6},
                         {"h": 360, "kind": "entrance", "door_kind": "glazed", "door_mat": "frame_dark", "w": bay * 0.6, "wh": 190, "sill": 100,
                          "window": {"kind": "modern", "frame": frame, "sill": None, "head": "none"}}])
    n = rng.randint(3, 7) if kind != "villa" else 2
    plan["floors"] = [ground] + [dict(typ) for _ in range(n - 1)]
    return {"roof": rng.choice(("overhang", "pergola", "flat")), "massings": ["block", "slab", "stepped", "L", "notched", "terraced"]}


def _st_glass(kind, rng, plan, bay):
    """Contemporary curtain wall: tinted glass on a mullion grid, coloured spandrels, deep fins, glass lobby, crown screen."""
    tint = rng.choice([(120, 150, 170), (110, 140, 130), (90, 96, 104), (150, 130, 100), (150, 176, 196)])
    fm = rng.choice([M("steel", (150, 152, 156), roughness=0.3), M("painted_iron", (40, 42, 46), roughness=0.4), M("steel", (110, 100, 86), roughness=0.3)])
    _clean(plan, wall=M("steel", (60, 62, 66)), trim=fm, t=20, crown=rng.choice(("screen", None)),
           sys={"mullion": rng.choice((120, 150, 180)), "mullion_d": rng.choice((10, 16)), "fin_d": rng.choice((0, 0, 35, 55)), "fin_every": rng.choice((1, 2)),
                "spandrel_h": rng.choice((0, 90, 110)), "spandrel_m": M("steel", rng.choice([(40, 42, 46), (70, 64, 56), (50, 70, 80)]), roughness=0.3),
                "glass_m": M("glass", tint, opacity=0.62, roughness=0.03), "frame_m": fm})
    typ = {"h": 380, "kind": "window", "system": "curtain"}
    n = rng.randint(6, 14)
    plan["floors"] = [{"h": 520, "kind": "lobby"}] + [dict(typ) for _ in range(n - 1)]
    return {"roof": "flat", "massings": ["podium_tower", "twin_towers", "setback", "block", "stacked", "notched"]}


def _st_art_deco(kind, rng, plan, bay):
    """Art deco, 1925-1940: buff brick or limestone piers soaring past the parapet, recessed bronze or terracotta spandrels
    with chevrons, polished granite base, stepped setbacks and a spire."""
    stone = rng.choice([(222, 208, 182), (204, 180, 140), (196, 170, 128), (214, 200, 176)])
    sp_m = rng.choice([M("bronze", (120, 86, 50), roughness=0.35), M("painted_iron", (40, 70, 62), roughness=0.4), M("terracotta", (170, 104, 76))])
    _clean(plan, wall=M("brick", _rgb(stone, 0.95)), trim=M("limestone", stone), t=44, base_wall=M("granite", (44, 40, 42), roughness=0.25),
           plinth=None, pier_crown=rng.choice((150, 220, 300)),
           sys={"pier_w": 42, "pier_d": 32, "pier_m": M("limestone", stone), "spandrel_h": 96, "spandrel_d": -10, "spandrel_m": sp_m,
                "ornament": rng.choice(("chevron", "fluted")), "head_h": 22})
    win = {"kind": "fixed", "frame": rng.choice(("frame_dark", M("bronze", (110, 80, 50), roughness=0.35))), "panes": (2, 3), "curtain": False}
    typ = {"h": 360, "kind": "window", "system": "frame", "window": win}
    ground = _shop_ground(kind, rng, bay, M("bronze", (110, 80, 50), roughness=0.35), 500)
    n = rng.randint(8, 16)
    plan["floors"] = [ground] + [dict(typ) for _ in range(n - 1)]
    return {"roof": "flat", "massings": ["setback", "setback", "podium_tower", "stepped", "block"], "finial": rng.random() < 0.7}


def _st_mediterranean(kind, rng, plan, bay):
    """Southern Europe: pastel lime render, small casements with louvred shutters, iron balconies, flower boxes and AC units,
    a loggia under the roof, terracotta hip roof or a roof terrace with a pergola."""
    col = rng.choice([(214, 170, 110), (200, 126, 92), (228, 174, 160), (232, 206, 142), (240, 234, 222), (186, 110, 80), (222, 196, 160)])
    _clean(plan, wall=M("plaster", col), trim=M("plaster", (238, 232, 216)), t=36, string_courses=rng.random() < 0.5,
           cornice=rng.choice(("moulded", None)), plinth=M("sandstone", (176, 150, 116)), life={"flowers": 0.35, "ac": 0.12, "streaks": 0.4})
    shut = rng.choice(SHUTTERS + [M("painted_iron", (120, 150, 110), roughness=0.5), M("painted_iron", (70, 110, 140), roughness=0.5)])
    win = {"kind": "casement", "frame": rng.choice(("frame_white", "frame_wood")), "sill": "stone", "head": rng.choice(("lintel", "none", "lintel")),
           "panes": (2, 3), "shutters": shut}
    typ = {"h": 300, "kind": "window", "w": rng.choice((95, 105, 115)), "wh": rng.choice((145, 160, 170)), "sill": 85, "window": win, "jitter": 0.15,
           "balcony": rng.choice(("alternate", "centre", None, {0})), "balcony_kind": "brackets", "balcony_depth": 65, "rail": rng.choice(("bars", "ornate")),
           "rail_mat": "iron", "slab_mat": M("sandstone", (190, 170, 140))}
    top = dict(typ, balcony="all", balcony_kind="loggia", rail=rng.choice(("bars", "balustrade")), loggia_d=150) if rng.random() < 0.5 else dict(typ)
    ground = (_shop_ground(kind, rng, bay, rng.choice(("frame_dark", "frame_green", "wood_dark")), 380, rng.choice(("awning", None)))
              if kind in ("apartment", "shophouse") or rng.random() < 0.3 else
              {"h": 340, "kind": "entrance", "door_kind": rng.choice(("panel", "double")), "door_mat": rng.choice(("wood_dark", "frame_green", "wood")),
               "w": 100, "wh": 160, "sill": 100, "window": dict(win)})
    n = rng.randint(3, 6) if kind != "villa" else 2
    plan["floors"] = [ground] + [dict(typ) for _ in range(n - 2)] + [top]
    return {"roof": rng.choice(("hip", "pergola", "hip")), "roof_m": M("roof_tile", rng.choice([(176, 88, 58), (160, 80, 56), (190, 110, 72)])),
            "massings": ["block", "T", "L", "stepped", "terraced", "U"]}


def _st_nordic(kind, rng, plan, bay):
    """Scandinavian contemporary: timber battens or painted weatherboards, big picture windows with thin dark frames,
    inset glass loggias lined in timber, steep metal gable without eaves."""
    clad = rng.choice([("battens", (34, 34, 36)), ("battens", (78, 58, 42)), ("battens", (176, 136, 96)), ("battens", (138, 134, 126)),
                       ("boards", (140, 42, 32)), ("boards", (236, 234, 226)), ("boards", (220, 190, 110)), ("boards", (60, 70, 72))])
    wood = M("wood", clad[1]) if clad[0] == "battens" else M("painted_iron", clad[1], roughness=0.6)
    _clean(plan, wall=M("plaster", _rgb(clad[1], 0.7)) if clad[0] == "battens" else wood, trim=M("concrete", (120, 118, 114)), t=30, skin=clad[0], skin_m=wood,
           plinth=M("concrete", (110, 108, 104)))
    frame = rng.choice(("frame_dark", "frame_dark", "frame_white", "frame_alu"))
    win = {"kind": "modern", "frame": frame, "sill": None, "head": "none", "curtain": rng.random() < 0.4}
    typ = {"h": 300, "kind": "window", "w": bay * rng.uniform(0.45, 0.6), "w_alt": bay * 0.3, "wh": rng.choice((170, 190, 210)), "sill": 60, "window": win,
           "jitter": rng.choice((0.0, 0.35, 0.6)), "balcony": rng.choice(("alternate", None, "ends")), "balcony_kind": "loggia", "rail": "glass",
           "rail_mat": "steel", "loggia_m": M("wood", (176, 136, 96))}
    ground = {"h": 330, "kind": "entrance", "door_kind": "glazed", "door_mat": "frame_dark", "w": bay * 0.55, "wh": 200, "sill": 50, "window": win}
    if kind == "apartment" and rng.random() < 0.4:
        ground = _shop_ground(kind, rng, bay, "frame_dark", 380)
    n = rng.randint(3, 6) if kind != "villa" else 2
    plan["floors"] = [ground] + [dict(typ) for _ in range(n - 1)]
    return {"roof": rng.choice(("gable_steep", "gable_steep", "flat")), "roof_m": M("steel", rng.choice([(46, 48, 52), (70, 72, 76), (120, 60, 46)]), roughness=0.45),
            "massings": ["block", "stepped", "notched", "L", "T", "terraced"], "pitch": rng.choice((0.5, 0.6, 0.75))}


def _st_contemporary(kind, rng, plan, bay):
    """European new-build housing: brick or rainscreen panels in several tones, deep reveals, staggered tall windows of two widths,
    inset loggias, slim metal coping."""
    if rng.random() < 0.5:
        b = rng.choice([(90, 86, 84), (200, 196, 188), (150, 96, 70), (120, 100, 88), (176, 156, 128)])
        _clean(plan, wall=M("brick", b), trim=M("steel", (90, 92, 94)), t=44)
    else:
        base = rng.choice([(186, 180, 168), (120, 124, 120), (170, 120, 90), (210, 206, 196), (70, 80, 86)])
        tones = [M("concrete", _rgb(base, f), roughness=0.6) for f in (0.92, 1.0, 1.08)]
        _clean(plan, wall=M("concrete", base), trim=M("steel", (90, 92, 94)), t=40, skin="panels", skin_tones=tones,
               panel=rng.choice(((120, 60), (60, 120), (90, 300), (300, 60))))
    frame = rng.choice(("frame_dark", M("bronze", (100, 80, 60), roughness=0.4), "frame_alu"))
    win = {"kind": "modern", "frame": frame, "sill": None, "head": "none", "curtain": rng.random() < 0.5}
    typ = {"h": 300, "kind": "window", "w": bay * 0.38, "w_alt": bay * 0.66, "wh": rng.choice((210, 230, 250)), "sill": rng.choice((20, 40, 60)), "window": win,
           "jitter": rng.choice((0.5, 0.8)), "balcony": rng.choice(("alternate", "ends", {1})), "balcony_kind": "loggia", "rail": rng.choice(("glass", "bars")),
           "rail_mat": "frame_dark", "life": {}}
    ground = _shop_ground(kind, rng, bay, "frame_dark", 400) if rng.random() < 0.6 else {"h": 360, "kind": "lobby"}
    n = rng.randint(4, 8)
    plan["floors"] = [ground] + [dict(typ) for _ in range(n - 1)]
    plan["sys"] = {"mullion": 150, "frame_m": MAT["frame_dark"]}
    return {"roof": "flat", "massings": ["block", "notched", "stacked", "terraced", "stepped", "U", "courtyard", "L"]}


def _st_victorian(kind, rng, plan, bay):
    """London terrace, 1840-1900: stock brick over a rusticated stucco ground floor, sash windows with stone heads,
    a two-storey bay window, iron area railings, a parapet with cornice and chimney stacks."""
    brick = rng.choice([(196, 166, 116), (176, 150, 108), (150, 84, 64), (130, 76, 60)])
    stucco = rng.choice([(236, 228, 208), (226, 222, 214), (240, 236, 228)])
    _clean(plan, wall=M("brick", brick), base_wall=M("plaster", stucco), base_skin="rustication", trim=M("plaster", stucco), t=36, cornice="moulded",
           string_courses=True, area_railing=True, life={"flowers": 0.15, "streaks": 0.25})
    win = {"kind": "sash", "frame": "frame_white", "sill": "stone", "head": rng.choice(("hood", "keystone", "lintel")), "surround": rng.random() < 0.4,
           "panes": rng.choice(((2, 2), (2, 4), (1, 2)))}
    first = {"h": 340, "kind": "window", "w": bay * 0.42, "wh": 230, "sill": 30, "window": win, "balcony": "all", "balcony_kind": "slab", "balcony_depth": 26,
             "rail": "ornate", "rail_mat": "iron", "slab_mat": M("plaster", stucco)}
    upper = {"h": 300, "kind": "window", "w": bay * 0.38, "wh": 180, "sill": 80, "window": win}
    top = dict(upper, h=270, wh=130, window=dict(win, head="lintel"))
    if kind == "shophouse":
        ground = _shop_ground(kind, rng, bay, rng.choice(("frame_green", "frame_dark", "wood_dark")), 400, rng.choice(("awning", None)), door=False)
    else:
        ground = {"h": 360, "kind": "entrance", "door_kind": "panel", "door_mat": rng.choice(("wood_dark", "frame_green", "frame_dark", M("painted_iron", (120, 30, 30)))),
                  "fanlight": True, "door_bay": 0, "w": bay * 0.42, "wh": 210, "sill": 70, "window": win}
        if rng.random() < 0.7:
            plan["bay_window"] = {"bays": [plan["bays"] - 1] if plan["bays"] > 1 else [], "floors": 2 if rng.random() < 0.6 else 1, "depth": 75}
    n = rng.randint(3, 5)
    plan["floors"] = [ground, first] + [dict(upper) for _ in range(n - 3)] + [top]
    return {"roof": "flat", "massings": ["block", "T"], "chimneys": True}


def _st_straits(kind, rng, plan, bay):
    """Straits / Peranakan shophouse (Singapore, Penang, Malacca): pastel stucco, five-foot-way arcade with tiled floor,
    paired French windows with timber louvred shutters, pilasters, tiled roof."""
    col = rng.choice([(170, 210, 190), (230, 180, 180), (240, 214, 150), (160, 190, 214), (200, 220, 160), (240, 232, 214)])
    _clean(plan, wall=M("plaster", col), trim=M("plaster", (246, 244, 236)), t=30, pilasters=True, string_courses=True, cornice="moulded",
           life={"flowers": 0.2, "streaks": 0.3})
    shut = rng.choice([M("wood", (96, 66, 44)), M("painted_iron", (40, 90, 70), roughness=0.5), M("painted_iron", (60, 90, 130), roughness=0.5)])
    win = {"kind": "french", "frame": rng.choice(("frame_wood", "frame_white")), "sill": None, "head": rng.choice(("arch", "lintel", "hood")),
           "shutters": shut, "panes": (1, 3)}
    typ = {"h": 340, "kind": "window", "w": bay * 0.36, "wh": 230, "sill": 25, "window": win, "balcony": None}
    ground = {"h": 400, "kind": "arcade", "depth": 210, "tiles": True, "frame": rng.choice(("frame_wood", "frame_green", "frame_dark"))}
    n = rng.randint(2, 3)
    plan["floors"] = [ground] + [dict(typ) for _ in range(n - 1)]
    return {"roof": rng.choice(("gable", "hip")), "roof_m": M("roof_tile", rng.choice([(166, 86, 60), (150, 74, 52)])), "massings": ["block", "T"]}


STYLES = {"haussmann": _st_haussmann, "warehouse": _st_warehouse, "brutalist": _st_brutalist, "modernist": _st_modernist, "glass": _st_glass,
          "art_deco": _st_art_deco, "mediterranean": _st_mediterranean, "nordic": _st_nordic, "contemporary": _st_contemporary,
          "victorian": _st_victorian, "straits": _st_straits}
KIND_STYLES = {
    "apartment": ["haussmann", "warehouse", "brutalist", "modernist", "glass", "art_deco", "mediterranean", "nordic", "contemporary"],
    "office": ["glass", "art_deco", "brutalist", "modernist", "warehouse", "contemporary", "haussmann"],
    "loft": ["warehouse", "contemporary", "brutalist", "nordic"],
    "townhouse": ["victorian", "haussmann", "mediterranean", "nordic", "straits", "contemporary"],
    "shophouse": ["straits", "victorian", "mediterranean", "haussmann"],
    "villa": ["mediterranean", "modernist", "nordic"],
}


# ------------------------------------------------------------------ 6. any footprint (GIS plots, real building outlines)

def _signed_area(pts):
    return 0.5 * sum(pts[i][0] * pts[(i + 1) % len(pts)][1] - pts[(i + 1) % len(pts)][0] * pts[i][1] for i in range(len(pts)))


def _is_convex(pts):
    n, sign = len(pts), 0
    for i in range(n):
        ax, ay = pts[(i + 1) % n][0] - pts[i][0], pts[(i + 1) % n][1] - pts[i][1]
        bx, by = pts[(i + 2) % n][0] - pts[(i + 1) % n][0], pts[(i + 2) % n][1] - pts[(i + 1) % n][1]
        c = ax * by - ay * bx
        if abs(c) < 1e-6:
            continue
        s = 1 if c > 0 else -1
        if sign and s != sign:
            return False
        sign = sign or s
    return True


def inset_polygon(pts, d):
    """Shrinks a polygon (positive area) by d cm: exact offsets when convex, a scale toward the centre otherwise."""
    n = len(pts)
    if n < 3 or d <= 0:
        return list(pts)
    cx = sum(p[0] for p in pts) / n
    cy = sum(p[1] for p in pts) / n
    if not _is_convex(pts):
        r = abs(_signed_area(pts)) ** 0.5
        s = max(0.5, 1 - 2 * d / max(r, 1))
        return [(cx + (p[0] - cx) * s, cy + (p[1] - cy) * s) for p in pts]
    out = []
    for i in range(n):
        a0, a1, b1 = pts[i - 1], pts[i], pts[(i + 1) % n]
        d1 = (a1[0] - a0[0], a1[1] - a0[1])
        d2 = (b1[0] - a1[0], b1[1] - a1[1])
        l1, l2 = math.hypot(*d1) or 1, math.hypot(*d2) or 1
        d1, d2 = (d1[0] / l1, d1[1] / l1), (d2[0] / l2, d2[1] / l2)
        n1, n2 = (-d1[1], d1[0]), (-d2[1], d2[0])               # inward normals (positive area)
        p1 = (a0[0] + n1[0] * d, a0[1] + n1[1] * d)
        p2 = (a1[0] + n2[0] * d, a1[1] + n2[1] * d)
        den = d1[0] * d2[1] - d1[1] * d2[0]
        if abs(den) < 1e-9:
            out.append(p2)
            continue
        wx, wy = p2[0] - p1[0], p2[1] - p1[1]
        t = (wx * d2[1] - wy * d2[0]) / den
        out.append((p1[0] + d1[0] * t, p1[1] + d1[1] * t))
    return out if _signed_area(out) > 0 else list(pts)


def footprint(polygon, kind="apartment", style="auto", floors=None, height=None, seed=None, street_edges=None, detail="medium", roof=None,
              wall=None):
    """A building on ANY footprint polygon [[x, y(, z)], ...] in world cm (a GIS plot, a real building outline):
    every edge gets a facade of the style (street_edges: indices of edges facing a street get the main facade with
    shops and doors; None = the longest edge), roof flat with parapet (any shape) or hip / mansard (convex shapes).
    floors or height (cm) set the size; detail "mass" = a quick massing block (thousands of plots at once).
    Returns parts in world coordinates (place them at (0, 0, 0))."""
    global _LOD
    pts = [(float(p[0]), float(p[1])) for p in polygon]
    zs = [float(p[2]) for p in polygon if len(p) > 2]
    z0 = sum(zs) / len(zs) if zs else 0.0
    if len(pts) > 3 and math.hypot(pts[0][0] - pts[-1][0], pts[0][1] - pts[-1][1]) < 1:
        pts.pop()
    n = len(pts)
    edges = set(street_edges) if street_edges is not None else None
    if _signed_area(pts) < 0:                                  # positive area: outward normal of edge (dx, dy) is (dy, -dx)
        pts = pts[::-1]
        if edges is not None:
            edges = {(n - 2 - e) % n for e in edges}
    lengths = [math.hypot(pts[(i + 1) % n][0] - pts[i][0], pts[(i + 1) % n][1] - pts[i][1]) for i in range(n)]
    if not edges:
        edges = {max(range(n), key=lambda i: lengths[i])}
    rng = random.Random(seed)
    mass = detail == "mass"
    old, _LOD = _LOD, (0 if mass else DETAIL.get(detail, 1) if isinstance(detail, str) else int(detail))
    try:
        plan, _w, _d, roof0, bay = _recipe(kind, rng, None, floors)
        lang = {}
        if style:
            name = rng.choice(KIND_STYLES.get(kind, list(STYLES))) if style == "auto" else style
            lang = STYLES[name](kind, rng, plan, bay) or {}
            roof0 = lang.get("roof", roof0)
        roof = roof or roof0
        if wall:
            plan["wall"] = m(wall)
        pf = plan["floors"]
        typical = pf[-2] if len(pf) > 2 else (pf[1] if len(pf) > 1 else dict(pf[0], kind="window", balcony=None))
        if typical["kind"] != "window":
            typical = dict(typical, kind="window")
        top_f = pf[-1] if len(pf) > 1 else typical
        count = int(floors or (max(1, round(height / typical["h"])) if height else len(pf)))
        storeys = []
        for i in range(count):
            f = dict(pf[0] if i == 0 else (top_f if i == count - 1 and count > 2 else (pf[i] if i < len(pf) - 1 else typical)))
            if f["kind"] not in ("window", "shop", "entrance", "blank", "arcade", "pilotis", "lobby"):
                f["kind"] = "window"
            f["h"] = pf[0]["h"] if i == 0 else typical["h"]
            storeys.append(f)
        top_z = z0 + sum(f["h"] for f in storeys)
        cx = sum(p[0] for p in pts) / n
        cy = sum(p[1] for p in pts) / n
        local = [[round(p[0] - cx, 1), round(p[1] - cy, 1)] for p in pts]
        wall_m = m(plan["wall"])
        trim = m(plan.get("trim", "concrete"))
        P = []
        if mass:                                               # massing model: body, glazed floor lines, roof band
            P.append(extrude(local, top_z - z0, [cx, cy, z0], m=wall_m))
            for i in range(1, count):
                zf = z0 + pf[0]["h"] + (i - 1) * typical["h"]
                P.append(extrude([[x * 1.004, y * 1.004] for x, y in local], 60, [cx, cy, zf + 90], m=MAT["glass"]))
            P.append(extrude([[x * 1.01, y * 1.01] for x, y in local], 30, [cx, cy, top_z], m=trim))
            return P
        t = plan.get("t", 30)
        for i in range(n):
            a, b = pts[i], pts[(i + 1) % n]
            L = lengths[i]
            if L < 20:
                continue
            ux, uy = (b[0] - a[0]) / L, (b[1] - a[1]) / L
            nx, ny = uy, -ux                                   # outward
            yaw = math.degrees(math.atan2(nx, -ny))            # facade local -y -> outward
            mx, my = (a[0] + b[0]) / 2, (a[1] + b[1]) / 2
            if L < bay * 0.55:
                P += transform([box([L, t, top_z - z0], [0, t / 2, 0], m=wall_m)], (mx, my, z0), yaw)
                continue
            q = dict(plan, floors=[dict(f) for f in storeys], ground=True, at_top=True)
            if i not in edges:
                q = _back(q)
            q = _with_width(q, L, bay)
            fp, _ = facade(L, q, rng)
            P += transform(fp, (mx, my, z0), yaw)
        P.append(extrude(local, 4, [cx, cy, z0 + 0.5], m=MAT["concrete"]))      # ground floor slab
        convex = _is_convex(pts)
        rm = lang.get("roof_m") or rng.choice([M("roof_tile", (160, 80, 56)), M("slate_roof", (72, 74, 80))])
        if roof in ("hip", "gable", "gable_steep") and convex and n <= 8:
            e = 1.06
            ring = [[cx + x * e, cy + y * e, top_z] for x, y in local]
            h = min(800.0, (abs(_signed_area(pts)) ** 0.5) * 0.38)
            apex = [[cx + x * 0.04, cy + y * 0.04, top_z + h] for x, y in local]
            P.append({"shape": "loft", "sections": [ring, apex], "location": [0, 0, 0], "closed": True, "cap": True, **rm})
        elif roof == "mansard" and convex:
            ring = [[cx + x, cy + y, top_z + 10] for x, y in local]
            inner = inset_polygon(pts, 110)
            top = [[p[0], p[1], top_z + 310] for p in inner]
            P.append({"shape": "loft", "sections": [ring, top], "location": [0, 0, 0], "closed": True, "cap": True, **rm})
        else:                                                  # flat roof: slab, parapet with coping, plant
            P.append(extrude(local, 20, [cx, cy, top_z], m=MAT["concrete"]))
            for i in range(n):
                a, b = pts[i], pts[(i + 1) % n]
                L = lengths[i]
                if L < 10:
                    continue
                ux, uy = (b[0] - a[0]) / L, (b[1] - a[1]) / L
                yaw = math.degrees(math.atan2(uy, ux))
                ix, iy = -uy, ux                               # inward
                mx, my = (a[0] + b[0]) / 2 + ix * t / 2, (a[1] + b[1]) / 2 + iy * t / 2
                P += [box([L + t, t, 90], [mx, my, top_z], rot=[0, yaw, 0], m=wall_m),
                      box([L + t + 8, t + 10, 8], [mx, my, top_z + 90], rot=[0, yaw, 0], m=trim)]
            for _ in range(rng.randint(1, 3)):
                P.append(box([rng.choice((120, 180)), rng.choice((90, 120)), rng.choice((90, 140))],
                             [cx + rng.uniform(-150, 150), cy + rng.uniform(-150, 150), top_z + 20], m=M("steel", (170, 172, 172), roughness=0.5)))
        return P
    finally:
        _LOD = old
