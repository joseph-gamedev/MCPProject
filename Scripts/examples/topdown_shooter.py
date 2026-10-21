"""
A top-down arena shooter made only from tool calls: WASD + mouse aim, an automatic blaster with magazine/reload and
reserve ammo, robots that patrol and shoot back, lava, health and ammo pickups, an exit that opens when all robots
are destroyed, HUD, game over / win, restart.

    python Scripts/examples/topdown_shooter.py [--clear] [--out shot.jpg]

Controls: WASD move, mouse aim, hold left mouse to fire, Q reload, Shift sprint, R restart.
"""

import argparse
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from rtbuild import Runtime, box, cyl, dome, sphere, mat  # noqa: E402

FLOOR = mat("concrete", color=[0.32, 0.33, 0.35], tile=200)
WALL = mat("concrete", color=[0.5, 0.5, 0.52], style="blocks", tile=120, relief=1)
CRATE = mat("wood", color=[0.45, 0.28, 0.12], tile=30)
METAL = mat("steel", color=[0.25, 0.27, 0.3], roughness=0.35)
Z = 20.0  # arena floor height (above the map floor, so they never overlap)


def robot_parts():
    red = mat("plaster", color=[0.7, 0.08, 0.05], roughness=0.4)
    return [
        cyl(90, 20, at=[0, 0, 0], **METAL),
        cyl(70, 90, at=[0, 0, 20], **red),
        dome(70, 40, at=[0, 0, 110], **METAL),
        box([30, 50, 14], at=[38, 0, 120], **mat("light", color=[1, 0.2, 0.1], emissive=[1, 0.15, 0.05], emissive_strength=8)),  # eye
        box([60, 14, 14], at=[50, 30, 70], **METAL),  # gun arm
        box([60, 14, 14], at=[50, -30, 70], **METAL),
    ]


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

    arena = rt.group("Arena")
    # ---- level: floor, walls, cover
    parts = [box([5000, 5000, 40], at=[0, 0, Z - 40], **FLOOR)]
    for x, y, w, d in [(0, 2500, 5100, 100), (0, -2500, 5100, 100), (2500, 0, 100, 5100), (-2500, 0, 100, 5100),
                       (-800, 900, 1200, 80), (900, -700, 80, 1400), (1400, 1200, 900, 80), (-1500, -1200, 80, 900)]:
        parts.append(box([w, d, 180], at=[x, y, Z], bevel=6, **WALL))
    for x, y in [(-300, -300), (300, 400), (-1600, 300), (1800, -1600), (-600, -1800), (600, 1900), (2000, 300)]:
        parts.append(box([160, 160, 140], at=[x, y, Z], bevel=8, **CRATE))
    rt.build("Level", parts, parent=arena)

    lava = rt.call("build.create_primitive", shape="box", size=[700, 500, 6], location=[-1300, -400, Z], name="lava", tags=["lava"], parent=arena,
                   material="light", color=[1, 0.25, 0.02], emissive=[1, 0.2, 0], emissive_strength=6)["id"]
    exitpad = rt.call("build.create_primitive", shape="cylinder", size=[260, 260, 10], location=[2100, 2100, Z], name="exit", tags=["exit"],
                      parent=arena, material="light", color=[0.2, 1, 0.4], emissive=[0.1, 1, 0.3], emissive_strength=2)["id"]

    # ---- robots
    spots = [(-1800, 1800, [1, 0, 0], 700), (1200, 400, [0, 1, 0], 600), (-200, -1200, [1, 0, 0], 900), (1700, -1100, [0, 1, 0], 500),
             (-1900, -1900, [1, 0, 0], 500), (300, 1300, [1, 0, 0], 600)]
    for i, (x, y, axis, dist) in enumerate(spots):
        rt.build(f"robot_{i}", robot_parts(), location=[x, y, Z], tags=["enemy"], parent=arena)
        rt.call("logic.set_motion", targets=[f"Arena/robot_{i}"], motion={"type": "patrol", "axis": axis, "distance": dist, "speed": 160, "offset": i * 0.9})

    # ---- pickups
    for i, (x, y) in enumerate([(-2100, 600), (800, -2100), (2200, -300)]):
        rt.build(f"medkit_{i}", [box([70, 70, 40], at=[0, 0, 0], **mat("white")), box([50, 14, 42], at=[0, 0, 0], **mat("red")),
                                  box([14, 50, 42], at=[0, 0, 0], **mat("red"))], location=[x, y, Z + 30], tags=["medkit"], parent=arena)
    for i, (x, y) in enumerate([(-400, 2100), (1500, 1800), (-2100, -800), (100, -600)]):
        rt.build(f"ammo_{i}", [box([60, 40, 40], at=[0, 0, 0], **mat("green", color=[0.15, 0.3, 0.1])),
                                box([20, 42, 12], at=[0, 0, 40], **mat("gold"))], location=[x, y, Z + 30], tags=["ammo"], parent=arena)
    rt.call("logic.set_motion", targets=["tag:medkit", "tag:ammo"], motion={"type": "bob", "amplitude": 12, "period": 1.4, "spin": [0, 90, 0]})
    rt.call("logic.set_solid", targets=["tag:enemy", "tag:medkit", "tag:ammo", "tag:lava", "tag:exit"], solid=False)

    # ---- combat
    rt.call("logic.spawn_player", mode="topdown", location=[-2100, 2100, Z], speed=600, camera_distance=1900, camera_pitch=72, reload_key="Q")
    rt.call("logic.define_health", targets=["player"], max=10, invulnerable=0.6, on_death="none")
    rt.call("logic.define_health", targets=["tag:enemy"], max=4, on_death="squash", points_var="score", points=100)
    rt.call("logic.define_hitzone", targets=["tag:lava"], damage=1, mode="stay", interval=0.4, affects="player", type="fire")
    rt.call("logic.define_weapon", name="blaster", damage=1, rate=8, auto=True, speed=3200, magazine=12, reload=1.1, ammo_var="reserve",
            spread=3, hits="tag:enemy", bullet={"size": 22, "color": [0.3, 0.8, 1.0]})
    rt.call("logic.define_weapon", name="robot_gun", damage=1, rate=1.2, speed=1300, spread=4, hits="player", lifetime=2.2,
            bullet={"size": 28, "color": [1.0, 0.25, 0.1]})
    rt.call("logic.equip", owner="player", weapon="blaster")
    rt.call("logic.equip", owner="tag:enemy", weapon="robot_gun")

    # ---- the game
    rt.call("logic.define_vars", replace=True, vars={"score": 0, "reserve": 48, "state": "play", "kills": 0, "best": 0})
    rt.call("logic.define_rules", rules=[
        {"name": "robots_shoot", "on": "timer", "filter": "robots", "condition": "state == 'play'", "actions": [
            {"for": "e", "in": "nodes('tag:enemy')", "do": [
                {"if": "alive(e) && dist(pos(e), pos('player')) < 1400", "then": [{"fire": "e", "at": "pos('player')"}]}]}]},
        {"name": "begin", "on": "start", "actions": [{"timer": "robots", "delay": 0.5, "repeat": True}]},
        {"name": "robot_down", "on": "died", "target": "tag:enemy", "actions": [
            {"set": "kills", "value": "kills + 1"},
            {"if": "len(nodes('tag:enemy')) <= 1", "then": [{"set": "state", "value": "'exit_open'"}]}]},
        {"name": "hurt_flash", "on": "damaged", "target": "player", "actions": [{"log": "ouch {event.amount}"}]},
        {"name": "player_dead", "on": "died", "target": "player", "actions": [
            {"set": "state", "value": "'over'"}, {"set": "best", "value": "max(best, score)"}, {"player": "freeze"}]},
        {"name": "medkit", "on": "touch", "target": "tag:medkit", "condition": "health.player < 10", "actions": [
            {"heal": "'player'", "amount": "4"},
            {"tween": "event.target", "move": [0, 0, 120], "scale": 0.1, "duration": 0.3, "then": "hide"}]},
        {"name": "ammo", "on": "touch", "target": "tag:ammo", "actions": [
            {"set": "reserve", "value": "reserve + 24"},
            {"tween": "event.target", "move": [0, 0, 120], "scale": 0.1, "duration": 0.3, "then": "hide"}]},
        {"name": "exit", "on": "touch", "target": "tag:exit", "condition": "state == 'exit_open'", "actions": [
            {"set": "score", "value": "score + 500 + max(0, round(1500 - time * 10))"}, {"set": "best", "value": "max(best, score)"},
            {"set": "state", "value": "'won'"}, {"player": "freeze"}]},
        {"name": "restart", "on": "key", "key": "R", "actions": [{"restart": True, "keep": ["best"]}]},
        {"name": "restart_button", "on": "button", "filter": "restart", "actions": [{"restart": True, "keep": ["best"]}]},
    ])
    rt.call("logic.define_ui", items=[
        {"name": "hud", "kind": "text", "anchor": [0.5, 0.04], "font_size": 22,
         "text": "HP {health.player}/10     AMMO {ammo.player == null ? 12 : ammo.player} | {reserve}     ROBOTS {len(nodes('tag:enemy'))}     SCORE {score}     BEST {best}"},
        {"name": "goal", "kind": "text", "anchor": [0.5, 0.1], "font_size": 18, "color": [0.4, 1, 0.5, 1],
         "text": "{state == 'exit_open' ? 'All robots down - reach the green exit!' : 'Destroy all robots'}", "visible_if": "state == 'play' || state == 'exit_open'"},
        {"name": "banner", "kind": "text", "anchor": [0.5, 0.42], "font_size": 46, "color": [1, 0.85, 0.2, 1], "visible_if": "state == 'won' || state == 'over'",
         "text": "{state == 'won' ? 'ARENA CLEARED!  Score ' + score : 'YOU WERE DESTROYED'}"},
        {"name": "restart", "kind": "button", "text": "Play again (R)", "anchor": [0.5, 0.55], "font_size": 24, "visible_if": "state == 'won' || state == 'over'"},
        {"name": "help", "kind": "text", "anchor": [0.5, 0.97], "font_size": 14, "background": [0, 0, 0, 0.35],
         "text": "WASD move   Mouse aim   Hold left mouse fire   Q reload   Shift sprint   Avoid lava   R restart"},
    ])
    rt.call("lighting.apply_mood", mood="noon")
    rt.call("logic.start")
    st = rt.call("logic.get_state", log_limit=3)
    print("player:", st.get("player", {}).get("location"), "health:", st.get("health"), "errors:", [e for e in st["log"] if "errors" in e])
    if args.out:
        rt.call("ops.wait", seconds=2)
        rt.screenshot(args.out, show_ui=True)
        print("saved", args.out)


if __name__ == "__main__":
    main()
