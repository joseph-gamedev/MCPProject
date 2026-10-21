"""
Top-down stealth shooter made only from tool calls.

Guards run an AI brain: they patrol with a vision cone, spot you (!), attack while strafing, chase your last known
position when they lose you (?), hear your shots, and return to their post. You carry a blaster (hand-held, recoil,
magazine + reserve ammo, Q to reload). Game HUD: health bar with damage ghost, ammo counter, score, objective,
enemy health bars, floating damage numbers, hit flash, low-health pulse, crosshair cursor. Juice: screen shake,
freeze-frame on kills, pooled explosion puffs.

    python Scripts/examples/topdown_shooter.py [--clear] [--out shot.jpg]

Controls: WASD move, mouse aim, hold left mouse fire, Q reload, Shift sprint, R restart.
"""

import argparse
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from rtbuild import Runtime, box, cyl, dome, mat  # noqa: E402

Z = 20.0
FLOOR = mat("concrete", color=[0.22, 0.23, 0.25], style="concrete", tile=250)
WALL = mat("concrete", color=[0.42, 0.43, 0.46], style="blocks", tile=120, relief=1)
CRATE = mat("wood", color=[0.45, 0.28, 0.12], tile=30)
METAL = mat("steel", color=[0.22, 0.24, 0.27], roughness=0.35)
HUD_BG = [0.02, 0.03, 0.05, 0.55]


def robot_parts():
    red = mat("plaster", color=[0.65, 0.07, 0.05], roughness=0.4)
    return [
        cyl(90, 20, at=[0, 0, 0], **METAL),
        box([22, 26, 40], at=[0, -22, 14], **METAL), box([22, 26, 40], at=[0, 22, 14], **METAL),
        cyl(72, 80, at=[0, 0, 40], **red),
        dome(72, 38, at=[0, 0, 120], **METAL),
        box([26, 50, 14], at=[36, 0, 126], **mat("light", color=[1, 0.25, 0.1], emissive=[1, 0.15, 0.05], emissive_strength=10)),
        box([70, 14, 14], at=[50, 34, 86], **METAL), box([70, 14, 14], at=[50, -34, 86], **METAL),
        cyl(8, 20, at=[86, 34, 86], rot=[-90, 0, 0], **mat("black")), cyl(8, 20, at=[86, -34, 86], rot=[-90, 0, 0], **mat("black")),
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
    parts = [box([5000, 5000, 40], at=[0, 0, Z - 40], **FLOOR)]
    for x, y, w, d in [(0, 2500, 5100, 100), (0, -2500, 5100, 100), (2500, 0, 100, 5100), (-2500, 0, 100, 5100),
                       (-800, 900, 1200, 80), (900, -700, 80, 1400), (1400, 1200, 900, 80), (-1500, -1200, 80, 900), (300, -1700, 900, 80)]:
        parts.append(box([w, d, 180], at=[x, y, Z], bevel=6, **WALL))
    for x, y in [(-300, -300), (300, 400), (-1600, 300), (1800, -1600), (-600, -1800), (600, 1900), (2000, 300), (-2000, -400)]:
        parts.append(box([150, 150, 120], at=[x, y, Z], bevel=8, **CRATE))
    for x, y in [(-1000, 2000), (1900, 2100), (-2100, -2100), (2100, -2100), (0, 0)]:  # floor lights
        parts.append(cyl(120, 4, at=[x, y, Z], **mat("light", color=[0.4, 0.7, 1.0], emissive=[0.3, 0.6, 1.0], emissive_strength=3)))
    rt.build("Level", parts, parent=arena)

    rt.call("build.create_primitive", shape="box", size=[700, 500, 6], location=[-1300, -400, Z], name="lava", tags=["lava"], parent=arena,
            material="light", color=[1, 0.25, 0.02], emissive=[1, 0.2, 0], emissive_strength=6)
    rt.call("build.create_primitive", shape="cylinder", size=[260, 260, 10], location=[2100, 2100, Z], name="exit", tags=["exit"],
            parent=arena, material="light", color=[0.2, 1, 0.4], emissive=[0.1, 1, 0.3], emissive_strength=2)

    guards = [(-1700, 1700, [1, 0, 0], 800), (1300, 300, [0, 1, 0], 700), (-200, -1200, [1, 0, 0], 900), (1700, -1100, [0, 1, 0], 600),
              (-1900, -1700, [1, 0, 0], 500), (300, 1400, [1, 0, 0], 700)]
    for i, (x, y, axis, dist) in enumerate(guards):
        rt.build(f"guard_{i}", robot_parts(), location=[x, y, Z], tags=["enemy"], parent=arena)

    for i, (x, y) in enumerate([(-2100, 600), (800, -2100), (2200, -300)]):
        rt.build(f"medkit_{i}", [box([70, 70, 40], at=[0, 0, 0], **mat("white")), box([50, 14, 42], at=[0, 0, 0], **mat("red")),
                                  box([14, 50, 42], at=[0, 0, 0], **mat("red"))], location=[x, y, Z + 30], tags=["medkit"], parent=arena)
    for i, (x, y) in enumerate([(-400, 2100), (1500, 1800), (-2100, -800), (100, -600)]):
        rt.build(f"ammo_{i}", [box([60, 40, 40], at=[0, 0, 0], **mat("green", color=[0.15, 0.3, 0.1])),
                                box([20, 42, 12], at=[0, 0, 40], **mat("gold"))], location=[x, y, Z + 30], tags=["ammo"], parent=arena)
    rt.call("logic.set_motion", targets=["tag:medkit", "tag:ammo"], motion={"type": "bob", "amplitude": 12, "period": 1.4, "spin": [0, 90, 0]})
    rt.call("logic.set_solid", targets=["tag:enemy", "tag:medkit", "tag:ammo", "tag:lava", "tag:exit"], solid=False)

    # ---- juice
    puff = rt.call("build.create_primitive", shape="sphere", size=[60, 60, 60], location=[0, 0, -5000], name="puff", parent=arena,
                   material="light", color=[1, 0.6, 0.2], emissive=[1, 0.5, 0.1], emissive_strength=8)["id"]
    rt.call("logic.define_pool", name="puff", template=puff, size=8)
    rt.call("logic.define_feedback", name="shot", steps=[{"type": "shake", "duration": 0.06, "intensity": 0.18}])
    rt.call("logic.define_feedback", name="robot_death", steps=[{"type": "freeze", "duration": 0.05}, {"type": "shake", "duration": 0.25, "intensity": 0.7},
                                                                  {"type": "spawn", "pool": "puff", "scale": 5, "duration": 0.35}])
    rt.call("logic.define_feedback", name="player_hit", steps=[{"type": "shake", "duration": 0.2, "intensity": 0.9}])

    # ---- combat
    rt.call("logic.spawn_player", mode="topdown", location=[-2100, 2100, Z], speed=600, camera_distance=1900, camera_pitch=72, reload_key="Q")
    rt.call("logic.define_health", targets=["player"], max=10, invulnerable=0.5, on_death="none", hit_feedback="player_hit")
    rt.call("logic.define_health", targets=["tag:enemy"], max=5, on_death="squash", points_var="score", points=100, death_feedback="robot_death")
    rt.call("logic.define_hitzone", targets=["tag:lava"], damage=1, mode="stay", interval=0.4, affects="player", type="fire")
    rt.call("logic.define_weapon", name="blaster", damage=1, rate=8, auto=True, speed=3400, magazine=12, reload=1.1, ammo_var="reserve",
            spread=2.5, hits="tag:enemy", feedback="shot", noise=1800, bullet={"size": 20, "color": [0.3, 0.8, 1.0]})
    rt.call("logic.define_weapon", name="robot_gun", damage=1, rate=1.6, burst=2, burst_gap=0.12, speed=1500, spread=5, hits="player",
            lifetime=2.0, bullet={"size": 26, "color": [1.0, 0.25, 0.1]})
    rt.call("logic.equip", owner="player", weapon="blaster")
    rt.call("logic.equip", owner="tag:enemy", weapon="robot_gun")

    # ---- guard AI
    rt.call("logic.define_brain", name="guard", vision={"radius": 1300, "angle": 80, "eye_height": 120}, hearing={"radius": 1800},
            turn_speed=240, start="patrol", states=[
                {"name": "patrol", "move": "patrol", "speed": 150, "face": "move",
                 "transitions": [{"when": "sees", "to": "alert"}, {"when": "heard", "to": "search"}]},
                {"name": "alert", "move": "stop", "face": "target", "icon": "!",
                 "transitions": [{"when": "time_in_state > 0.45", "to": "attack"}]},
                {"name": "attack", "move": "strafe", "speed": 260, "keep_distance": 600, "face": "target", "fire": True, "icon": "!",
                 "transitions": [{"when": "!player_alive", "to": "return"}, {"when": "!sees && since_seen > 1.5", "to": "search"}]},
                {"name": "search", "move": "to_last_known", "speed": 260, "face": "move", "icon": "?",
                 "transitions": [{"when": "sees", "to": "alert"}, {"when": "at_goal", "to": "look"}, {"when": "time_in_state > 8", "to": "return"}]},
                {"name": "look", "move": "stop", "face": "look_around", "icon": "?",
                 "transitions": [{"when": "sees", "to": "alert"}, {"when": "time_in_state > 3", "to": "return"}]},
                {"name": "return", "move": "home", "speed": 180, "face": "move",
                 "transitions": [{"when": "sees", "to": "alert"}, {"when": "at_home", "to": "patrol"}]},
            ])
    for i, (x, y, axis, dist) in enumerate(guards):
        rt.call("logic.assign_brain", targets=[f"Arena/guard_{i}"], brain="guard", patrol={"axis": axis, "distance": dist})

    # ---- rules
    rt.call("logic.define_vars", replace=True, vars={"score": 0, "reserve": 48, "state": "play", "best": 0})
    rt.call("logic.define_rules", replace=True, rules=[
        {"name": "robot_down", "on": "died", "target": "tag:enemy", "actions": [
            {"if": "len(nodes('tag:enemy')) <= 1", "then": [{"set": "state", "value": "'exit_open'"}]}]},
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

    # ---- HUD
    clear = [0, 0, 0, 0]
    rt.call("logic.set_hud", enemy_bars=True, alert_icons=True, damage_numbers=True, hit_flash=True, low_health=True)
    rt.call("logic.define_ui", replace=True, items=[
        {"name": "hp", "kind": "bar", "anchor": [0.02, 0.05], "size": [360, 30], "value": "health.player", "max": "10",
         "fill": [0.85, 0.12, 0.1, 1], "background": HUD_BG, "font_size": 15, "text": "HP  {health.player} / 10"},
        {"name": "score", "kind": "text", "anchor": [0.02, 0.115], "font_size": 20, "background": clear, "color": [1, 0.9, 0.5, 1],
         "text": "SCORE  {score}      BEST  {best}"},
        {"name": "robots", "kind": "text", "anchor": [0.98, 0.05], "font_size": 22, "background": HUD_BG,
         "text": "ROBOTS LEFT  {len(nodes('tag:enemy'))}"},
        {"name": "objective", "kind": "text", "anchor": [0.98, 0.11], "font_size": 15, "background": clear, "color": [0.5, 1, 0.6, 1],
         "visible_if": "state == 'play' || state == 'exit_open'",
         "text": "{state == 'exit_open' ? '> Reach the green exit' : '> Destroy all robots  (they hear gunfire)'}"},
        {"name": "ammo", "kind": "text", "anchor": [0.98, 0.9], "font_size": 44, "background": HUD_BG,
         "text": "{ammo.player}  /  {reserve}"},
        {"name": "ammo_label", "kind": "text", "anchor": [0.98, 0.83], "font_size": 14, "background": clear, "color": [0.7, 0.8, 1, 1],
         "text": "{reloading.player ? 'RELOADING...' : (ammo.player == 0 ? 'EMPTY - press Q' : 'BLASTER')}"},
        {"name": "banner", "kind": "text", "anchor": [0.5, 0.42], "font_size": 52, "background": [0, 0, 0, 0.7], "color": [1, 0.85, 0.2, 1],
         "visible_if": "state == 'won' || state == 'over'", "text": "{state == 'won' ? 'ARENA CLEARED' : 'YOU WERE DESTROYED'}"},
        {"name": "banner_score", "kind": "text", "anchor": [0.5, 0.51], "font_size": 22, "background": clear,
         "visible_if": "state == 'won' || state == 'over'", "text": "Score {score}     Best {best}"},
        {"name": "restart", "kind": "button", "text": "PLAY AGAIN  (R)", "anchor": [0.5, 0.6], "font_size": 22,
         "background": [0.1, 0.45, 0.9, 1], "visible_if": "state == 'won' || state == 'over'"},
        {"name": "help", "kind": "text", "anchor": [0.02, 0.97], "font_size": 13, "background": clear, "color": [1, 1, 1, 0.7],
         "text": "WASD move  -  Mouse aim  -  LMB fire  -  Q reload  -  Shift sprint  -  R restart"},
    ])
    rt.call("lighting.apply_mood", mood="noon")
    rt.call("lighting.set_sun", rotation=[-60, 35, 0], temperature=6000)
    rt.call("logic.start")
    st = rt.call("logic.get_state", log_limit=3)
    print("brains:", st.get("brains"), "errors:", [e for e in st["log"] if "errors" in e])
    if args.out:
        rt.call("ops.wait", seconds=2)
        rt.screenshot(args.out, show_ui=True)
        print("saved", args.out)


if __name__ == "__main__":
    main()
