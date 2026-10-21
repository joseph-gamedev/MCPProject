"""
rtbuild: build detailed structures by writing a small generator script instead of making one tool call per part.

Parts are plain dicts (shape recipes); Runtime.build() sends them as build.create_assembly calls of up to 500 parts,
each merged into one object with a material per distinct look. Loops, helpers and real measurements give the detail.

    import sys; sys.path.insert(0, r"H:/aiworks/MCP/MCPProject/Scripts")
    from rtbuild import *
    rt = Runtime()                                      # http://127.0.0.1:8018/mcp (the running game)
    P = Parts()
    P += box([800, 800, 60], **mat("limestone"))         # plinth
    for x, y in polar(8, 300):                           # 8 columns on a circle
        P += column(40, 400, at=[x, y, 60], **mat("marble"))
    P += dome(700, 300, at=[0, 0, 460], **mat("copper"))
    rt.build("Tholos", P)
    rt.screenshot("tholos.jpg")

Units: centimetres, Z up. Rotations are [pitch, yaw, roll] degrees. Colors are linear RGB 0-1.
Shapes and parameters: build.get_guide (or session.describe_toolset build).
"""

import base64
import json
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mcp_smoke_test import McpClient, PROTOCOL_VERSION  # noqa: E402

__all__ = [
    "Runtime", "Parts", "mat", "box", "beam", "cyl", "cone", "sphere", "dome", "tube", "arch", "roof", "column", "steps", "tiers",
    "lattice", "openings", "revolve", "onion", "loft", "taper", "frustum", "extrude", "panel", "sweep", "pipe", "raw",
    "ring", "rect", "arch_outline", "polar", "lerp", "spline", "rotate_z",
]


# ------------------------------------------------------------------ materials

def mat(preset, **overrides):
    """Material fields for a part: mat("brick", tile=22), mat("painted_iron", color=[0.26, 0.17, 0.1], roughness=0.5)."""
    d = {"material": preset}
    d.update(overrides)
    return d


# ------------------------------------------------------------------ math helpers

def lerp(a, b, t):
    if isinstance(a, (list, tuple)):
        return [x + (y - x) * t for x, y in zip(a, b)]
    return a + (b - a) * t


def rotate_z(p, deg, center=(0, 0)):
    a = math.radians(deg)
    x, y = p[0] - center[0], p[1] - center[1]
    out = [center[0] + x * math.cos(a) - y * math.sin(a), center[1] + x * math.sin(a) + y * math.cos(a)]
    return out + list(p[2:])


def polar(n, radius, start_deg=0.0, center=(0, 0), z=None):
    """n points on a circle: [(x, y)] or [(x, y, z)]."""
    pts = []
    for i in range(n):
        a = math.radians(start_deg + 360.0 * i / n)
        p = (center[0] + radius * math.cos(a), center[1] + radius * math.sin(a))
        pts.append(p if z is None else p + (z,))
    return pts


def ring(n, rx, z=0.0, ry=None, center=(0, 0), start_deg=0.0):
    """Closed ring of n [x,y,z] points (counter-clockwise from above). ry for ellipses."""
    ry = rx if ry is None else ry
    return [[center[0] + rx * math.cos(math.radians(start_deg + 360.0 * i / n)),
             center[1] + ry * math.sin(math.radians(start_deg + 360.0 * i / n)), z] for i in range(n)]


def rect(w, d, z=0.0, center=(0, 0)):
    """Rectangle ring of 4 [x,y,z] points, counter-clockwise."""
    cx, cy = center
    return [[cx - w / 2, cy - d / 2, z], [cx + w / 2, cy - d / 2, z], [cx + w / 2, cy + d / 2, z], [cx - w / 2, cy + d / 2, z]]


def spline(points, samples=8):
    """Catmull-Rom through 2D/3D points: a smooth polyline (profiles, paths)."""
    pts = [list(p) for p in points]
    if len(pts) < 3:
        return pts
    ext = [pts[0]] + pts + [pts[-1]]
    out = []
    for i in range(1, len(ext) - 2):
        p0, p1, p2, p3 = ext[i - 1], ext[i], ext[i + 1], ext[i + 2]
        for s in range(samples):
            t = s / samples
            t2, t3 = t * t, t * t * t
            out.append([0.5 * (2 * b + (-a + c) * t + (2 * a - 5 * b + 4 * c - d) * t2 + (-a + 3 * b - 3 * c + d) * t3)
                        for a, b, c, d in zip(p0, p1, p2, p3)])
    out.append(pts[-1])
    return out


def arch_outline(width, height, pointed=False, steps=12):
    """2D (u, v) outline of an arch opening: rectangle with a round or pointed top. Base at v=0, centred on u=0."""
    r = width / 2
    spring = height - (r * (1.15 if pointed else 1.0))
    pts = [[-r, 0], [r, 0], [r, spring]]
    for i in range(1, steps):
        t = i / steps
        if pointed:
            # two arcs meeting at the apex
            a = math.pi * t
            pts.append([r * math.cos(a) * (1 - 0.15 * math.sin(a)), spring + (height - spring) * math.sin(a) ** 0.8])
        else:
            a = math.pi * t
            pts.append([r * math.cos(a), spring + r * math.sin(a)])
    pts.append([-r, spring])
    return pts


def _place(d, at=None, rot=None, scale=None):
    if at is not None:
        d["location"] = list(at)
    if rot is not None:
        d["rotation"] = list(rot)
    if scale is not None:
        d["scale"] = list(scale)
    return d


def _part(shape, at=None, rot=None, scale=None, **kw):
    d = {"shape": shape}
    d.update(kw)
    return _place(d, at, rot, scale)


# ------------------------------------------------------------------ parts (each returns a part dict)

def box(size, at=None, rot=None, pivot="base", **kw):
    return _part("box", at, rot, size=list(size), pivot=pivot, **kw)


def beam(a, b, thickness, depth=None, **kw):
    """Square/rectangular member from point a to point b (trusses, rails, struts)."""
    dx, dy, dz = b[0] - a[0], b[1] - a[1], b[2] - a[2]
    length = math.sqrt(dx * dx + dy * dy + dz * dz)
    yaw = math.degrees(math.atan2(dy, dx))
    pitch = math.degrees(math.atan2(dz, math.hypot(dx, dy)))
    mid = [(a[0] + b[0]) / 2, (a[1] + b[1]) / 2, (a[2] + b[2]) / 2]
    return box([length + thickness * 0.5, thickness, depth or thickness], at=mid, rot=[pitch, yaw, 0], pivot="center", **kw)


def cyl(diameter, height, at=None, rot=None, **kw):
    return _part("cylinder", at, rot, size=[diameter, diameter, height], **kw)


def cone(diameter, height, at=None, rot=None, top_ratio=0.0, **kw):
    return _part("cone", at, rot, size=[diameter, diameter, height], top_ratio=top_ratio, **kw)


def sphere(diameter, at=None, **kw):
    return _part("sphere", at, None, size=[diameter, diameter, diameter], **kw)


def dome(diameter, height, at=None, rot=None, **kw):
    return _part("dome", at, rot, size=[diameter, diameter, height], **kw)


def tube(diameter, height, thickness, at=None, rot=None, **kw):
    return _part("tube", at, rot, size=[diameter, diameter, height], thickness=thickness, **kw)


def arch(width, depth, height, at=None, rot=None, **kw):
    return _part("arch", at, rot, size=[width, depth, height], **kw)


def roof(width, depth, height, at=None, rot=None, **kw):
    """Gable roof: ridge along Y."""
    return _part("roof", at, rot, size=[width, depth, height], **kw)


def column(diameter, height, at=None, **kw):
    return _part("column", at, None, size=[diameter, diameter, height], **kw)


def steps(width, depth, height, count, at=None, rot=None, **kw):
    return _part("steps", at, rot, size=[width, depth, height], count=count, **kw)


def tiers(width, depth, height, count, taper=0.4, at=None, rot=None, **kw):
    return _part("tiers", at, rot, size=[width, depth, height], count=count, taper=taper, **kw)


def lattice(width, depth, height, levels=6, taper=1.0, beam=None, bracing=True, at=None, rot=None, **kw):
    d = _part("lattice", at, rot, size=[width, depth, height], levels=levels, taper=taper, bracing=bracing, **kw)
    if beam:
        d["beam"] = beam
    return d


def openings(width, depth, height, rows, cols, opening=None, arched=False, at=None, rot=None, **kw):
    """Grid of window/door cutters; use as the tool of a subtract op (see box(..., ops=[...]))."""
    d = _part("openings", at, rot, size=[width, depth, height], rows=rows, cols=cols, arched=arched, **kw)
    if opening:
        d["opening"] = list(opening)
    return d


def revolve(profile, segments=32, at=None, rot=None, angle=360, **kw):
    """Lathe: profile [[radius, z], ...] spun around Z (vases, finials, drums, bulbs)."""
    return _part("revolve", at, rot, profile=[list(p) for p in profile], segments=segments, angle=angle, **kw)


def onion(radius, height, at=None, neck=0.55, bulge=1.18, tip=0.04, segments=40, samples=6, **kw):
    """Onion dome: drum-width neck, swelling bulb, concave sweep to a point."""
    ctrl = [[radius * neck, 0], [radius * bulge, height * 0.28], [radius * 0.95, height * 0.5], [radius * 0.45, height * 0.74],
            [radius * 0.12, height * 0.9], [radius * tip, height * 0.97], [0, height]]
    prof = [[max(0.0, p[0]), p[1]] for p in spline(ctrl, samples)]
    return revolve([[0, 0]] + prof, segments, at, **kw)


def loft(sections, closed=True, cap=True, at=None, rot=None, smooth_angle=40, **kw):
    """Skin rings (same point count each) into a surface: towers that taper and twist, curved legs, bulbs."""
    return _part("loft", at, rot, sections=[[list(p) for p in s] for s in sections], closed=closed, cap=cap, smooth_angle=smooth_angle, **kw)


def taper(r0, r1, height, sides=24, at=None, rot=None, **kw):
    """Round frustum (r1 can be 0)."""
    return loft([ring(sides, r0, 0), ring(sides, max(r1, 0.01), height)], at=at, rot=rot, **kw)


def frustum(w0, d0, w1, d1, height, at=None, rot=None, shift=(0, 0), **kw):
    """Rectangular frustum; shift moves the top (leaning legs, buttresses)."""
    return loft([rect(w0, d0, 0), rect(w1, d1, height, center=shift)], at=at, rot=rot, smooth_angle=10, **kw)


def extrude(outline, height, at=None, rot=None, **kw):
    """Footprint [[x,y], ...] pushed up by height."""
    return _part("extrude", at, rot, profile=[list(p) for p in outline], height=height, **kw)


def panel(outline_uv, depth, at=(0, 0, 0), yaw=0.0, **kw):
    """Flat 2D outline [[u,v]] stood on a facade: u along yaw, v up; pushed by depth toward the facade's front (+normal)."""
    a = math.radians(yaw)
    t = (math.cos(a), math.sin(a))
    n = (math.sin(a), -math.cos(a))
    prof = [[at[0] + u * t[0], at[1] + u * t[1], at[2] + v] for u, v in outline_uv]
    return {"shape": "extrude", "profile": prof, "direction": [n[0] * depth, n[1] * depth, 0.0], **kw}


def sweep(profile, path, closed=False, at=None, rot=None, **kw):
    """2D profile [[x,y]] swept along a 3D path [[x,y,z]] (rails, cornices, cables, arch ribs)."""
    return _part("sweep", at, rot, profile=[list(p) for p in profile], path=[list(p) for p in path], closed=closed, **kw)


def pipe(path, diameter, sides=8, closed=False, **kw):
    r = diameter / 2
    return sweep([[r * math.cos(2 * math.pi * i / sides), r * math.sin(2 * math.pi * i / sides)] for i in range(sides)], path, closed, **kw)


def raw(vertices, triangles, at=None, rot=None, **kw):
    """Any mesh: vertices [[x,y,z]], triangles [[a,b,c]] counter-clockwise seen from outside."""
    return _part("raw", at, rot, vertices=[list(v) for v in vertices], triangles=[list(t) for t in triangles], **kw)


# ------------------------------------------------------------------ collection + runtime

class Parts(list):
    """A list of part dicts; P += part or P += [parts]."""

    def __iadd__(self, other):
        if isinstance(other, dict):
            self.append(other)
        else:
            self.extend(other)
        return self


class Runtime:
    def __init__(self, url="http://127.0.0.1:8018/mcp", activity=None):
        self.client = McpClient(url)
        self.client.request("initialize", {"protocolVersion": PROTOCOL_VERSION, "capabilities": {},
                                           "clientInfo": {"name": "rtbuild", "version": "1.0"}})
        self.client.notify("notifications/initialized")
        self.images = []
        if activity is not None:
            self.call("ui.show_activity", enabled=bool(activity), **({"status": activity} if isinstance(activity, str) else {}))

    def call(self, tool, **args):
        payload, images, is_error = self.client.call("session_call_tool", {"name": tool, "arguments": args})
        if is_error or not payload.get("ok", True):
            raise RuntimeError(f"{tool} failed: {json.dumps(payload)[:2000]}")
        self.images = images
        # UFUNCTION tools wrap results in {"returnValue": ...}: expose an object returnValue's fields directly.
        ret = payload.get("returnValue")
        if isinstance(ret, dict):
            merged = dict(ret)
            merged.update(payload)
            return merged
        return payload

    def group(self, name, location=(0, 0, 0), parent=None):
        args = {"name": name, "location": list(location)}
        if parent:
            args["parent"] = parent
        return self.call("scene.create_group", **args)["id"]

    def build(self, name, parts, parent=None, chunk=500, **kw):
        """Sends parts as assemblies of up to `chunk` parts. Returns the node ids."""
        ids = []
        parts = list(parts)
        for i in range(0, len(parts), chunk):
            args = dict(kw, name=name if i == 0 else f"{name}_{i // chunk + 1}", parts=parts[i:i + chunk])
            if parent:
                args["parent"] = parent
            ids.append(self.call("build.create_assembly", **args)["id"])
        return ids

    def _save(self, path):
        if path and self.images:
            with open(path, "wb") as f:
                f.write(base64.b64decode(self.images[0]["data"]))
        return path

    def screenshot(self, path=None, max_dim=1280, **kw):
        self.call("capture.screenshot", max_dim=max_dim, **kw)
        return self._save(path)

    def views(self, path=None, under=None, size=512):
        args = {"size": size}
        if under:
            args["under"] = under
        self.call("build.capture_view", **args)
        return self._save(path)
