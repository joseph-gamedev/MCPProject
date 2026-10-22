"""Look and review: physical lighting moods, visual styles, cameras, and screenshots for the self-check loop.

    from unreal_kit import look
    look.mood("Sunset")                       # sun lux + sky + matching locked exposure + AO/fog
    look.style("realistic", weathering=0.3)   # or "toon", "clay", "low_poly", ... (style.list_presets)
    look.eye_level([-3000, -2400], look_at=[0, 0, 500])
    look.review("shots", targets=["Castle"])  # saves overview + eye-level + detail images to check before calling it done

Rules that keep scenes looking right: change brightness with mood/sun (exposure follows), never with exposure_bias;
cameras at eye height (1.6-1.7 m) and 60-75 degree FOV for realism; check images after every big change.
"""

import math
import os

from . import call, save_image, tools

MOODS = ["Noon", "Morning", "Sunset", "Night", "Overcast", "Foggy"]


def mood(name="Noon"):
    """Physical light preset (sun lux, sky, locked exposure, AO, contact shadows, fog). See MOODS."""
    return tools.lighting.apply_mood(mood=name)


def time_of_day(hour, yaw=None):
    args = {"hour": hour}
    if yaw is not None:
        args["yaw"] = yaw
    return tools.lighting.set_time_of_day(**args)


def style(preset="realistic", weathering=None, **material):
    """Visual style preset; weathering (0 clean - 1 grimy) applies scene-wide dirt, streaks and dust."""
    tools.style.apply(preset=preset)
    if weathering is not None or material:
        m = dict(material)
        if weathering is not None:
            m["Weathering"] = weathering
        tools.style.apply(material=m, lighting_enabled=False)


def fog(density=0.02, start=0, color=None, volumetric=False):
    args = {"density": density, "start_distance": start, "volumetric": volumetric}
    if color:
        args["color"] = color
    return tools.lighting.set_fog(**args)


# ------------------------------------------------------------------ cameras

def eye_level(at_xy, look_at, height=165, fov=65, ground=0):
    """A person's view (most realistic framing)."""
    return tools.camera.set_camera(location=[at_xy[0], at_xy[1], ground + height], look_at=list(look_at), fov=fov)


def orbit_view(center, distance, yaw=-135, pitch=-22, fov=50):
    """Three-quarter view of something: yaw around it, pitch looking down."""
    a, p = math.radians(yaw), math.radians(pitch)
    loc = [center[0] + math.cos(a) * math.cos(p) * distance, center[1] + math.sin(a) * math.cos(p) * distance,
           center[2] - math.sin(p) * distance]
    return tools.camera.set_camera(location=loc, look_at=list(center), fov=fov)


def frame(targets, direction=None, fov=None):
    args = {"targets": [targets] if isinstance(targets, str) else list(targets)}
    if direction:
        args["direction"] = direction
    if fov:
        args["fov"] = fov
    return tools.camera.frame_nodes(**args)


def bounds(target):
    """World bounds of a node: (center, size)."""
    b = tools.scene.get_node(target=target).get("bounds", {})
    return b.get("center"), b.get("size")


# ------------------------------------------------------------------ captures

def screenshot(path, max_dim=1600, show_ui=False, wait=1.0):
    """Saves what the camera sees (waits a moment for lighting/streaming to settle)."""
    if wait:
        call("ops.wait", seconds=wait)
    tools.capture.screenshot(max_dim=max_dim, show_ui=show_ui)
    return save_image(path)


def capture_from(path, location, look_at, fov=65, width=1600, height=900):
    """Renders a view without moving the player camera."""
    tools.capture.scene_capture(location=list(location), look_at=list(look_at), fov=fov, width=width, height=height)
    return save_image(path)


def review(folder, targets=None, center=None, size=None, prefix="review"):
    """Self-check set: overview (3/4 from above), two eye-level views and a close detail view. Returns the image
    paths: LOOK at them and fix what is wrong (floating objects, intersections, flat lighting, wrong scale, empty
    ground, repeated colours) before saying the scene is done."""
    os.makedirs(folder, exist_ok=True)
    if targets and (center is None or size is None):
        c, s = bounds(targets[0] if isinstance(targets, (list, tuple)) else targets)
        center = center or c
        size = size or s
    center = center or [0, 0, 0]
    size = size or [4000, 4000, 1500]
    r = max(size[0], size[1])
    shots = [
        ("overview", [center[0] - r * 0.9, center[1] - r * 1.1, center[2] + r * 0.6], center, 55),
        ("eye_a", [center[0] - r * 0.75, center[1] - r * 0.35, 165], [center[0], center[1], 300 + size[2] * 0.25], 65),
        ("eye_b", [center[0] + r * 0.4, center[1] - r * 0.8, 165], [center[0], center[1], 300 + size[2] * 0.25], 65),
        ("detail", [center[0] - r * 0.28, center[1] - r * 0.5, 180], [center[0] - r * 0.15, center[1] - r * 0.2, 250], 50),
    ]
    paths = []
    for name, loc, look, fov in shots:
        p = os.path.join(folder, f"{prefix}_{name}.png")
        capture_from(p, loc, look, fov)
        paths.append(p)
    return paths
