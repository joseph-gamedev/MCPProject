"""
Galaga-style arcade shooter, built entirely from tool calls (what an AI agent does from a prompt):

  * fixed overhead playfield with a scrolling starfield; the ship moves left/right only and fires straight up
  * bees, butterflies and two-hit boss ships swoop in from the sides into formation, sway, then dive-bomb and shoot
  * diving enemies score double; stages get faster; 3 lives; the high score is saved
  * HUD with the layout UI: high score, score, stage, lives; stage banner; game-over panel with "play again"

    python Scripts/examples/galaga.py [--url http://127.0.0.1:8018/mcp] [--clear] [--out shot.jpg]

Controls: A/D or arrows to move, left mouse or F to fire, R to restart.
"""

import argparse
import json
import math
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from rtbuild import *  # noqa: E402,F403

HIDE_Z = -60000
FIELD_W = 1450          # half width of the playfield (world Y), screen right = +Y
TOP = 2820              # formation top (world X), screen up = +X
Z = 60                  # flight height of everything


def glow(color, strength=0.9):
    return mat("plaster", color=color, emissive=color, emissive_strength=strength, roughness=0.6)


def poly(points, height=70, z=0, mirror=True, **kw):
    """A flat sprite part: the outline (x forward, y right) extruded upward. mirror adds the left half."""
    pts = list(points)
    if mirror:
        pts = pts + [[x, -y] for x, y in reversed(pts) if abs(y) > 1e-6]
    return extrude(pts, height, at=[0, 0, z], **kw)


def disc(r, x=0, y=0, n=12, height=70, z=0, **kw):
    return extrude([[x + r * math.cos(2 * math.pi * i / n), y + r * math.sin(2 * math.pi * i / n)] for i in range(n)], height, at=[0, 0, z], **kw)


# ---- sprites (enemies face -X, toward the player) -------------------------------------------------------------------

def player_ship():
    white, red, blue = glow([0.95, 0.95, 1.0], 1.2), glow([1.0, 0.12, 0.1]), glow([0.15, 0.45, 1.0])
    return [
        poly([[120, 0], [60, 18], [-40, 22], [-70, 10], [-70, 0]], height=60, **white),        # fuselage
        poly([[10, 22], [-40, 95], [-75, 100], [-60, 22]], height=50, **white),                 # wings
        poly([[-40, 95], [-30, 120], [-80, 122], [-75, 100]], height=55, z=5, **red),           # wing tips
        poly([[70, 0], [40, 10], [10, 10], [10, 0]], height=70, z=5, **blue),                   # cockpit
        poly([[-70, 10], [-95, 14], [-95, 0]], height=50, **red),                                # engine
    ]


def bee():
    yellow, blue, red = glow([1.0, 0.8, 0.05]), glow([0.15, 0.35, 1.0]), glow([1.0, 0.1, 0.1])
    return [
        disc(36, x=0, **yellow), disc(24, x=-38, **yellow),
        poly([[10, 20], [40, 85], [-10, 95], [-30, 30]], height=50, **blue),
        poly([[-14, 0], [-14, 34], [-6, 34], [-6, 0]], height=75, z=0, **blue),
        disc(7, x=-50, y=12, height=80, **red), disc(7, x=-50, y=-12, height=80, **red),
    ]


def butterfly():
    red, white, blue = glow([1.0, 0.15, 0.15]), glow([1.0, 1.0, 1.0], 1.2), glow([0.2, 0.4, 1.0])
    return [
        poly([[40, 0], [20, 14], [-50, 14], [-60, 0]], height=70, **white),
        poly([[20, 14], [70, 70], [40, 110], [-10, 60]], height=50, **red),
        poly([[-10, 20], [-20, 90], [-60, 80], [-40, 20]], height=50, **red),
        poly([[30, 30], [50, 70], [30, 80]], height=60, z=5, **blue),
        disc(8, x=-58, y=10, height=80, **blue), disc(8, x=-58, y=-10, height=80, **blue),
    ]


def boss():
    green, purple, yellow = glow([0.1, 0.9, 0.3]), glow([0.6, 0.2, 1.0]), glow([1.0, 0.85, 0.1])
    return [
        disc(48, x=10, **green),
        poly([[30, 40], [80, 120], [20, 135], [-30, 60]], height=55, **purple),
        poly([[-30, 20], [-70, 60], [-90, 30], [-60, 10]], height=60, **green),
        poly([[-40, 0], [-40, 18], [-95, 22], [-95, 0]], height=65, **yellow),
        disc(10, x=-30, y=24, height=80, **yellow), disc(10, x=-30, y=-24, height=80, **yellow),
    ]


KINDS = {"bee": (bee, 1, 50, 100), "butterfly": (butterfly, 1, 80, 160), "boss": (boss, 2, 150, 400)}


def formation():
    """Slot list {kind, x, y}: 4 bosses, 2 rows of 8 butterflies, 2 rows of 10 bees."""
    out = []
    for kind, x, n in [("boss", TOP, 4), ("butterfly", TOP - 230, 8), ("butterfly", TOP - 430, 8), ("bee", TOP - 650, 10), ("bee", TOP - 850, 10)]:
        for i in range(n):
            out.append({"kind": kind, "x": x, "y": round((i - (n - 1) / 2) * 230)})
    return out


def build_scene(rt):
    # Space: a black floor the ship slides on (its top is z=0), two starfield sheets that scroll forever, side rails.
    P = Parts()
    P += box([20000, 20000, 40], at=[1500, 0, -40], **mat("plaster", color=[0.0, 0.0, 0.01], roughness=1.0))
    for y in (-FIELD_W - 120, FIELD_W + 120):
        P += box([4400, 40, 300], at=[1500, y, 0], **glow([0.05, 0.12, 0.4], 0.8))
    rt.build("Space", P)
    import random
    rnd = random.Random(7)
    palette = [[1, 1, 1], [0.6, 0.8, 1], [1, 0.9, 0.5], [1, 0.5, 0.5], [0.6, 1, 0.7]]
    for sheet in range(2):
        stars = Parts()
        for _ in range(140):
            c = rnd.choice(palette)
            stars += sphere(rnd.choice([10, 14, 18]), at=[rnd.uniform(-600, 3500), rnd.uniform(-3200, 3200), -10], **glow(c, rnd.uniform(1.0, 3.0)))
        rt.build(f"stars_{sheet}", stars)
        if sheet == 1:
            rt.call("scene.move_node", target="stars_1", location=[4100, 0, 0])
    # Ship model (the player's body) and enemy templates for the pools.
    rt.call("build.create_assembly", name="ship", parts=player_ship(), location=[0, 0, HIDE_Z])
    for i, (kind, (fn, hp, pts, dive_pts)) in enumerate(KINDS.items()):
        rt.call("build.create_assembly", name=f"tpl_{kind}", parts=fn(), location=[i * 400, 1000, HIDE_Z], tags=["enemy", kind])
        rt.call("logic.define_pool", name=kind, template=f"tpl_{kind}", size=24 if kind != "boss" else 6)


def build_rules():
    R = []
    banner = lambda text: [{"set": "banner", "value": text}, {"set": "banner_until", "value": "time + 2.4"}]
    R.append({"name": "start", "on": "start", "actions": [{"load": "galaga"}, {"emit": "stage"}]})
    R.append({"name": "scroll", "on": "start", "actions": [
        {"tween": "'stars_0'", "move": [-4100, 0, 0], "duration": 14, "ease": "linear", "loop": "loop"},
        {"tween": "'stars_1'", "move": [-4100, 0, 0], "duration": 14, "ease": "linear", "loop": "loop"}]})
    # A stage: every slot gets an enemy that swoops in from the left or right edge.
    R.append({"name": "stage", "on": "event", "filter": "stage", "actions": [
        {"set": "ids", "value": "[]"}, {"set": "diving", "value": "[]"}, {"set": "n_diving", "value": "0"}, {"set": "alive_n", "value": "0"},
        {"cancel_timer": "dive"}] + banner("'STAGE ' + str(stage)") + [
        {"for": "k", "in": "range(len(slots))", "do": [
            {"let": "f", "value": "slots[k]"},
            {"let": "entry", "value": f"[{TOP + 900}, (k % 2 == 0 ? -1 : 1) * 2300, {Z}]"}]
            + [{"if": f"f.kind == '{kind}'", "then": [
                {"spawn": kind, "at": "entry", "as": "p"},
                {"push": "ids", "value": "p.id"},
                {"set": "alive_n", "value": "alive_n + 1"},
                {"tween": "p.id", "location": f"=[f.x, f.y, {Z}]", "duration": 1.3, "delay": "1.4 + k * 0.05", "ease": "out_cubic"}]} for kind in KINDS]},
        {"timer": "sway", "delay": "5"},
        {"timer": "attack", "delay": "5.5"}]})
    # Diving starts only once everyone is in formation (an entry tween would cancel a dive).
    R.append({"name": "attack", "on": "timer", "filter": "attack", "condition": "!over", "actions": [
        {"timer": "dive", "delay": "max(0.55, 2.0 - stage * 0.18)", "repeat": True}]})
    R.append({"name": "sway", "on": "timer", "filter": "sway", "actions": [
        {"for": "e", "in": "ids", "do": [{"if": "alive(e) && !contains(diving, e)", "then": [
            {"tween": "e", "move": [0, 140, 0], "duration": 2.0, "loop": "pingpong", "ease": "in_out_sine"}]}]}]})
    # Dive-bomb: loop out of formation, swoop at the ship (shooting), leave the bottom, come back in from the top.
    R.append({"name": "dive", "on": "timer", "filter": "dive", "condition": "!over && n_diving < min(1 + stage, 5)", "actions": [
        {"set": "cands", "value": "[]"},
        {"for": "e", "in": "ids", "do": [{"if": "alive(e) && !contains(diving, e)", "then": [{"push": "cands", "value": "e"}]}]},
        {"if": "len(cands) > 0", "then": [
            {"let": "e", "value": "pick(cands)"}, {"let": "q", "value": "pos(e)"},
            {"push": "diving", "value": "e"}, {"set": "n_diving", "value": "n_diving + 1"},
            {"tween": "e", "move": "=[220, q[1] > 0 ? 260 : -260, 0]", "rotate": [0, 360, 0], "duration": 0.6, "ease": "out_sine", "emit": "dive2"}]}]})
    R.append({"name": "dive2", "on": "event", "filter": "dive2", "condition": "alive(event.target)", "actions": [
        {"let": "p", "value": "pos('player')"},
        {"fire": "event.target", "at": "pos('player')"},
        {"tween": "event.target", "location": f"=[-600, p[1] + rand_int(-280, 280), {Z}]", "duration": "=max(1.0, 1.9 - stage * 0.1)", "ease": "in_sine",
         "emit": "dive3"}]})
    R.append({"name": "dive2_shot", "on": "event", "filter": "dive2", "condition": "alive(event.target) && stage >= 2", "actions": [
        {"timer": "volley", "delay": "0.45"}]})
    R.append({"name": "volley", "on": "timer", "filter": "volley", "actions": [
        {"for": "e", "in": "diving", "do": [{"if": "e != '' && alive(e) && pos(e)[0] > pos('player')[0] + 600", "then": [{"fire": "e", "at": "pos('player')"}]}]}]})
    R.append({"name": "dive3", "on": "event", "filter": "dive3", "condition": "alive(event.target)", "actions": [
        {"let": "f", "value": "slots[index_of(ids, event.target)]"},
        {"tween": "event.target", "location": f"=[{TOP + 900}, f.y, {Z}]", "duration": 0.02, "emit": "dive4"}]})
    R.append({"name": "dive4", "on": "event", "filter": "dive4", "condition": "alive(event.target)", "actions": [
        {"let": "f", "value": "slots[index_of(ids, event.target)]"},
        {"tween": "event.target", "location": f"=[f.x, f.y, {Z}]", "duration": 1.1, "ease": "out_cubic", "emit": "home"}]})
    R.append({"name": "home", "on": "event", "filter": "home", "actions": [
        {"set_item": "diving", "index": "index_of(diving, event.target)", "value": "''"}, {"set": "n_diving", "value": "max(0, n_diving - 1)"},
        {"tween": "event.target", "move": [0, 140, 0], "duration": 2.0, "loop": "pingpong", "ease": "in_out_sine"}]})
    # Scoring (diving enemies are worth more), then bookkeeping and stage clear.
    for kind, (_, _, pts, dive_pts) in KINDS.items():
        R.append({"name": f"score_{kind}", "on": "died", "target": f"tag:{kind}", "actions": [
            {"set": "score", "value": f"score + (contains(diving, event.target) ? {dive_pts} : {pts})"}]})
    R.append({"name": "boss_hurt", "on": "damaged", "target": "tag:boss", "condition": "event.health > 0", "actions": [
        {"tween": "event.target", "color": [0.6, 0.2, 1.0], "duration": 0.15}]})
    R.append({"name": "enemy_down", "on": "died", "target": "tag:enemy", "actions": [
        {"feedback": "pop"},
        {"if": "contains(diving, event.target)", "then": [
            {"set_item": "diving", "index": "index_of(diving, event.target)", "value": "''"}, {"set": "n_diving", "value": "max(0, n_diving - 1)"}]},
        {"set": "alive_n", "value": "alive_n - 1"},
        {"if": "alive_n <= 0 && !over", "then": [{"cancel_timer": "dive"}, {"timer": "next_stage", "delay": "2.5"}]}]})
    R.append({"name": "next_stage", "on": "timer", "filter": "next_stage", "actions": [{"set": "stage", "value": "stage + 1"}, {"emit": "stage"}]})
    R.append({"name": "hi", "on": "var_changed", "filter": "score", "condition": "score > hi", "actions": [{"set": "hi", "value": "score"}]})
    # Losing a ship: explode, park it off-screen, respawn after a moment; no ships left -> game over.
    R.append({"name": "ship_down", "on": "died", "target": "player", "actions": [
        {"feedback": "boom"}, {"set": "lives", "value": "lives - 1"},
        {"player": "teleport", "value": "[-4000, 0, 40]"},
        {"if": "lives > 0", "then": [{"timer": "respawn", "delay": "1.6"}] + banner("'READY'"),
         "else": [{"set": "over", "value": "true"}, {"cancel_timer": "dive"}, {"player": "freeze"}, {"save": "galaga", "vars": ["hi"]}]}]})
    R.append({"name": "respawn", "on": "timer", "filter": "respawn", "actions": [
        {"player": "respawn"}, {"health": "'player'", "max": "1", "value": "1"}]})
    R.append({"name": "restart_key", "on": "key", "key": "R", "actions": [{"restart": True, "keep": ["hi"]}]})
    R.append({"name": "restart_btn", "on": "button", "filter": "again", "actions": [{"restart": True, "keep": ["hi"]}]})
    return R


def mini_ship():
    """A tiny ship icon for the lives row, drawn with UI shapes."""
    s = lambda w, h, r, bg, **kw: {"kind": "column", "width": w, "height": h, "radius": r, "background": bg, **kw}
    return {"kind": "column", "align": "center", "gap": 0, "children": [
        s(4, 8, 2, "#7FB2FF"), s(10, 10, 3, "#F2F2FF"),
        {"kind": "row", "gap": 0, "children": [s(7, 6, 2, "#FF3B30"), s(8, 6, 1, "#F2F2FF"), s(7, 6, 2, "#FF3B30")]}]}


def build_ui():
    label = lambda t, c: {"kind": "text", "text": t, "size": "sm", "weight": "black", "color": c}
    big = lambda t: {"kind": "text", "text": t, "size": "xl", "weight": "bold", "color": "white"}
    side = {"name": "side", "kind": "column", "anchor": "right", "offset": [-30, 0], "gap": 22, "width": 220, "children": [
        {"kind": "column", "gap": 2, "children": [label("HIGH SCORE", "#FF3B30"), big("{hi}")]},
        {"kind": "column", "gap": 2, "children": [label("1UP", "#FF3B30"), big("{score}")]},
        {"kind": "column", "gap": 2, "children": [label("STAGE", "#5AC8FA"), big("{stage}")]},
        {"kind": "column", "gap": 6, "children": [label("SHIPS", "#5AC8FA"),
                                                  {"kind": "row", "gap": 8, "children": [mini_ship() | {"repeat": "max(0, lives)", "as": "n"}]}]},
    ]}
    title = {"name": "title", "kind": "column", "anchor": "left", "offset": [30, 0], "gap": 6, "width": 240, "children": [
        {"kind": "text", "text": "STAR", "size": 40, "weight": "black", "color": "#5AC8FA"},
        {"kind": "text", "text": "SWARM", "size": 40, "weight": "black", "color": "#FF3B30", "margin": [-14, 0, 0, 0]},
        {"kind": "divider", "color": "#FFFFFF22", "margin": [10, 0, 10, 0]},
        {"kind": "text", "text": "A / D  or  arrows    move\nLeft mouse  or  F    fire\nR    restart", "size": "sm", "color": "muted"},
        {"kind": "text", "text": "Divers score double", "size": "sm", "weight": "bold", "color": "#FFD60A", "margin": [6, 0, 0, 0]}]}
    banner = {"name": "banner", "kind": "column", "anchor": "center", "offset": [0, -60], "align": "center", "visible_if": "time < banner_until && !over",
              "children": [{"kind": "text", "text": "{banner}", "size": 34, "weight": "black", "color": "#5AC8FA"}]}
    over = {"name": "gameover", "kind": "panel", "anchor": "center", "width": 360, "align": "center", "gap": 12, "visible_if": "over",
            "background": "#05060CF2", "border_color": "#FF3B30", "children": [
                {"kind": "text", "text": "GAME OVER", "size": 34, "weight": "black", "color": "#FF3B30", "h_align": "center"},
                {"kind": "text", "text": "Score {score}   ·   Stage {stage}", "weight": "medium", "h_align": "center"},
                {"kind": "text", "text": "{score >= hi && score > 0 ? 'NEW HIGH SCORE!' : 'High score ' + str(hi)}", "size": "sm", "weight": "bold",
                 "color": "#FFD60A", "h_align": "center"},
                {"name": "again", "kind": "button", "text": "Play again", "variant": "primary", "h_align": "center", "margin": [6, 0, 0, 0]}]}
    return [side, title, banner, over]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://127.0.0.1:8018/mcp")
    ap.add_argument("--clear", action="store_true")
    ap.add_argument("--out", default=None)
    args = ap.parse_args()
    rt = Runtime(args.url)
    if args.clear:
        rt.call("logic.stop")
        rt.call("logic.remove_player")
        rt.call("scene.clear_all", confirm=True)

    build_scene(rt)
    rt.call("logic.set_solid", targets=["tag:enemy", "stars_0", "stars_1"], solid=False)
    rt.call("logic.define_vars", vars={
        "slots": formation(), "ids": [], "diving": [], "cands": [], "n_diving": 0, "alive_n": 0,
        "score": 0, "hi": 20000, "stage": 1, "lives": 3, "over": False, "banner": "", "banner_until": 0})

    rt.call("logic.define_feedback", name="pop", steps=[{"type": "shake", "duration": 0.05, "intensity": 0.12}])
    rt.call("logic.define_feedback", name="boom", steps=[{"type": "shake", "duration": 0.4, "intensity": 0.9}, {"type": "flash", "intensity": 0.5, "color": [1, 0.3, 0.1]}])
    rt.call("logic.spawn_player", mode="topdown", body="ship", location=[0, 0, 20], speed=1100, aim_yaw=0, move_axes=[0, 1, 0],
            camera_follow=False, camera_pitch=90, camera_distance=6700, camera_lead=1420, fov=50)
    rt.call("logic.define_health", targets=["player"], max=1, invulnerable=2.0, on_death="none")
    rt.call("logic.define_weapon", name="laser", damage=1, rate=4, speed=4200, lifetime=0.9, hits="tag:enemy", bullet={"size": 26, "color": [0.5, 0.9, 1.0]})
    rt.call("logic.define_weapon", name="enemy_shot", damage=1, rate=10, speed=1250, lifetime=3, hits="player", bullet={"size": 24, "color": [1.0, 0.25, 0.2]})
    rt.call("logic.equip", owner="player", weapon="laser")
    rt.call("logic.equip", owner="tag:enemy", weapon="enemy_shot")
    for kind, (_, hp, _, _) in KINDS.items():
        rt.call("logic.define_health", targets=[f"tag:{kind}"], max=hp, on_death="Release")
    rt.call("logic.define_hitzone", targets=["tag:enemy"], damage=1, affects="player", mode="enter")

    rt.call("logic.define_rules", rules=build_rules(), replace=True)
    rt.call("logic.define_ui", items=build_ui(), replace=True,
            theme={"colors": {"panel": "#05060CE6", "accent": "#FF3B30", "accent_text": "#FFFFFF"}, "text_shadow": False})
    rt.call("lighting.apply_mood", mood="night")
    rt.call("logic.start")
    st = rt.call("logic.get_state", log_limit=30)
    print("errors:", json.dumps([e for e in st.get("log", []) if e.get("errors")])[:1500])
    if args.out:
        rt.call("ops.wait", seconds=6)
        rt.screenshot(args.out, show_ui=True, max_dim=1600)
        print("saved", args.out)


if __name__ == "__main__":
    main()
