"""
A one-level side-scrolling platformer made only from tool calls: a running/jumping hero, ground with pits, brick and
question blocks, pipes, a moving platform, spinning coins, patrolling enemies you can stomp, spikes, a checkpoint,
a flagpole goal, lives, score, game over / course clear and restart.

    python Scripts/examples/platformer.py [--clear] [--out shot.jpg]

Controls: A/D or arrows run, Shift sprint, Space/W/Up jump (hold for higher), R restart.
"""

import argparse
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from rtbuild import Runtime, box, cyl, cone, dome, sphere, mat  # noqa: E402

Z = 2000.0  # ground top; the level floats above the map floor so pits are real pits
B = 100.0   # block size

GROUND = mat("grass", color=[0.18, 0.42, 0.1], tile=80, relief=2)
DIRT = mat("stone", color=[0.42, 0.26, 0.14], tile=60, relief=2)
BRICK = mat("brick", color=[0.55, 0.22, 0.1], tile=50)
QBLOCK = mat("gold", color=[1.0, 0.65, 0.05], roughness=0.35, metallic=0.2, emissive=[0.5, 0.3, 0.0], emissive_strength=0.6)
PIPE = mat("plaster", color=[0.05, 0.45, 0.08], roughness=0.3)
STONE = mat("stone", color=[0.45, 0.4, 0.36], tile=100, relief=1.5)


def ground_parts(segments):
    parts = []
    for x0, x1 in segments:
        w = x1 - x0
        parts.append(box([w, 600, 60], at=[(x0 + x1) / 2, 0, Z - 60], **GROUND))
        parts.append(box([w, 600, 900], at=[(x0 + x1) / 2, 0, Z - 960], **DIRT))
    return parts


def goomba_parts():
    brown = mat("plaster", color=[0.35, 0.16, 0.06], roughness=0.7)
    return [
        box([34, 26, 18], at=[-12, -16, 0], **mat("black", color=[0.05, 0.03, 0.02])),
        box([34, 26, 18], at=[-12, 16, 0], **mat("black", color=[0.05, 0.03, 0.02])),
        cyl(50, 30, at=[0, 0, 14], **mat("plaster", color=[0.85, 0.72, 0.55])),
        dome(110, 70, at=[0, 0, 40], **brown),
        cyl(110, 8, at=[0, 0, 36], **brown),
        sphere(20, at=[42, -14, 62], **mat("white")),
        sphere(20, at=[42, 14, 62], **mat("white")),
        sphere(9, at=[50, -14, 66], **mat("black")),
        sphere(9, at=[50, 14, 66], **mat("black")),
        box([6, 26, 6], at=[46, -16, 76], rot=[0, 0, 20], **mat("black")),
        box([6, 26, 6], at=[46, 16, 76], rot=[0, 0, -20], **mat("black")),
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

    level = rt.group("Level")
    # ---- terrain
    rt.build("Ground", ground_parts([(-800, 2600), (2950, 5200), (5650, 9800)]), parent=level)
    deco = [box([30000, 6000, 40], at=[4500, -3300, Z - 100], **mat("grass", color=[0.1, 0.3, 0.07], tile=300))]  # backdrop field
    for x, s in [(300, 1.0), (3600, 1.4), (6400, 1.1), (8200, 0.8)]:  # hills behind
        deco.append(dome(1600 * s, 900 * s, at=[x, -1800, Z - 60], **mat("grass", color=[0.12, 0.35, 0.08], tile=150)))
    for x, z in [(800, 1500), (2900, 1800), (5000, 1400), (7200, 1700), (9300, 1500)]:  # clouds
        for dx, d in [(-140, 220), (0, 300), (150, 240)]:
            deco.append(sphere(d, at=[x + dx, -2500, Z + z], **mat("white", color=[0.95, 0.95, 0.97], roughness=0.9)))
    for x in (1300, 4600, 7000):  # bushes
        for dx, d in [(-80, 150), (0, 200), (90, 150)]:
            deco.append(sphere(d, at=[x + dx, -350, Z - 40], **mat("grass", color=[0.08, 0.3, 0.06], relief=4, tile=40)))
    rt.build("Scenery", deco, parent=level)

    # ---- pipes
    for i, (x, h) in enumerate([(1900, 200), (3500, 300), (4400, 250), (7300, 200)]):
        rt.build(f"pipe_{i}", [cyl(170, h - 60, at=[x, 0, Z], **PIPE), cyl(200, 60, at=[x, 0, Z + h - 60], **PIPE)], parent=level)

    # ---- blocks: bricks and question blocks
    def block(name, x, z, material, tags):
        return rt.call("build.create_primitive", shape="box", size=[B, B, B], location=[x, 0, Z + z], name=name, tags=tags,
                       parent=level, bevel=4, **material)["id"]
    for i, x in enumerate([900, 1000, 1100, 1200, 1300]):
        if x in (1000, 1200):
            block(f"qblock_{i}", x, 400, QBLOCK, ["qblock"])
        else:
            block(f"brick_{i}", x, 400, BRICK, ["brick"])
    block("qblock_hi", 1100, 800, QBLOCK, ["qblock"])
    for i, x in enumerate([6000, 6100, 6200]):
        block(f"brick_b{i}", x, 450, BRICK, ["brick"])
    block("qblock_b", 6300, 450, QBLOCK, ["qblock"])

    # ---- staircase to the flag
    stairs = []
    for i in range(6):
        for j in range(i + 1):
            stairs.append(box([B, B * 2, B], at=[8000 + i * B, 0, Z + j * B], bevel=3, **STONE))
    rt.build("Stairs", stairs, parent=level)

    # ---- moving platform over the second pit
    plat = rt.call("build.create_primitive", shape="box", size=[300, 200, 40], location=[5250, 0, Z - 40], name="platform",
                   tags=["platform"], parent=level, bevel=4, **mat("wood", color=[0.4, 0.25, 0.1]))["id"]
    rt.call("logic.set_motion", targets=[plat], motion={"type": "patrol", "axis": [1, 0, 0], "distance": 250, "speed": 140})

    # ---- coins
    coin_spots = [(600, 150), (700, 150), (800, 150), (1000, 650), (1100, 1000), (1200, 650),
                  (2650, 350), (2775, 450), (2900, 350),  # arc over the first pit
                  (3500, 500), (4400, 450), (5300, 400), (5450, 450), (6100, 650), (6200, 650),
                  (6800, 150), (6900, 150), (7000, 150), (7100, 150), (8500, 800)]
    for i, (x, z) in enumerate(coin_spots):
        rt.call("build.create_primitive", shape="cylinder", size=[60, 60, 12], location=[x, 0, Z + z], rotation=[0, 0, 90],
                name=f"coin_{i}", tags=["coin"], parent=level, material="gold", detail="high", emissive=[1, 0.7, 0.1], emissive_strength=1.5)
    rt.call("logic.set_motion", targets=["tag:coin"], motion={"type": "bob", "amplitude": 10, "period": 1.6, "spin": [0, 180, 0]})

    # ---- enemies
    for i, (x, dist) in enumerate([(2200, 350), (3000, 400), (3900, 400), (6600, 500), (7600, 300)]):
        rt.build(f"goomba_{i}", goomba_parts(), location=[x, 0, Z], tags=["enemy"], parent=level)
        rt.call("logic.set_motion", targets=[f"Level/goomba_{i}"], motion={"type": "patrol", "axis": [1, 0, 0], "distance": dist,
                "speed": 110 + 15 * i, "face": True, "offset": i * 0.7, "waddle": [9, 3]})
    spikes = [cone(70, 90, at=[4800 + k * 75, 0, Z], **mat("steel", color=[0.6, 0.6, 0.65])) for k in range(4)]
    rt.build("spikes", spikes, tags=["hazard"], parent=level)

    # ---- checkpoint and goal
    rt.build("checkpoint", [cyl(20, 300, at=[5800, 0, Z], **mat("white")),
                            box([120, 10, 80], at=[5860, 0, Z + 210], **mat("plaster", color=[0.1, 0.3, 0.9]))], tags=["checkpoint"], parent=level)
    rt.build("flagpole", [box([B * 1.2, B * 1.2, B * 0.6], at=[9000, 0, Z], **STONE), cyl(22, 1100, at=[9000, 0, Z + 60], **mat("steel", color=[0.3, 0.6, 0.3])),
                          sphere(60, at=[9000, 0, Z + 1160], **mat("gold")),
                          box([160, 10, 110], at=[9090, 0, Z + 960], **mat("plaster", color=[0.1, 0.7, 0.15]))], tags=["goal"], parent=level)
    castle = [box([700, 500, 450], at=[9500, -400, Z], **BRICK), box([300, 300, 300], at=[9500, -400, Z + 450], **BRICK),
              box([160, 60, 260], at=[9500, -140, Z], **mat("black"))]
    for k in range(5):
        castle.append(box([90, 500, 90], at=[9200 + k * 150, -400, Z + 450], **BRICK))
    rt.build("castle", castle, parent=level)
    rt.call("logic.set_solid", targets=["tag:coin", "tag:enemy", "tag:hazard", "tag:checkpoint", "tag:goal"], solid=False)
    # Pools: the coin that pops out of a question block, and the floating "+score" marker.
    pop = rt.call("build.create_primitive", shape="cylinder", size=[60, 60, 12], location=[0, 0, Z + 5000], rotation=[0, 0, 90], name="pop_coin",
                  parent=level, material="gold", emissive=[1, 0.7, 0.1], emissive_strength=2)["id"]
    rt.call("logic.define_pool", name="pop", template=pop, size=6)
    star = rt.call("build.create_primitive", shape="sphere", size=[50, 50, 50], location=[0, 0, Z + 5000], name="poof",
                   parent=level, material="white", emissive=[1, 1, 1], emissive_strength=3)["id"]
    rt.call("logic.define_pool", name="poof", template=star, size=6)

    # ---- the game
    rt.call("logic.define_vars", replace=True, vars={"coins": 0, "score": 0, "lives": 3, "state": "play", "best": 0, "used": []})
    hurt = {"emit": "hurt"}
    rt.call("logic.define_rules", rules=[
        {"name": "coin", "on": "touch", "target": "tag:coin", "condition": "state == 'play'", "actions": [
            {"tween": "event.target", "move": [0, 0, 160], "scale": 0.2, "duration": 0.25, "ease": "out_quad", "then": "hide"},
            {"set": "coins", "value": "coins + 1"}, {"set": "score", "value": "score + 100"},
            {"if": "coins % 25 == 0", "then": [{"set": "lives", "value": "lives + 1"}]}]},
        {"name": "question_block", "on": "touch", "target": "tag:qblock", "condition": "event.data.from_below && !contains(used, event.target)", "actions": [
            {"push": "used", "value": "event.target"},
            {"tween": "event.target", "move": [0, 0, 35], "duration": 0.09, "ease": "out_quad", "loop": "yoyo"},
            {"tween": "event.target", "color": [0.35, 0.2, 0.08], "duration": 0.2, "add": True},
            {"spawn": "pop", "at": "[event.location[0], 0, event.location[2] + 110]", "as": "c"},
            {"tween": "c.id", "move": [0, 0, 260], "rotate": [0, 720, 0], "duration": 0.45, "ease": "out_cubic", "then": "release"},
            {"set": "coins", "value": "coins + 1"}, {"set": "score", "value": "score + 200"}]},
        {"name": "brick_bump", "on": "touch", "target": "tag:brick", "condition": "event.data.from_below", "actions": [
            {"tween": "event.target", "move": [0, 0, 25], "duration": 0.08, "ease": "out_quad", "loop": "yoyo"}]},
        {"name": "stomp", "on": "touch", "target": "tag:enemy", "condition": "state == 'play' && event.data.from_above", "actions": [
            {"tween": "event.target", "scale": [1.5, 1.5, 0.15], "duration": 0.3, "ease": "out_back", "then": "hide"},
            {"spawn": "poof", "at": "[event.location[0], 0, event.location[2] + 60]", "as": "f"},
            {"tween": "f.id", "scale": 4, "move": [0, 0, 80], "duration": 0.3, "ease": "out_quad", "then": "release"},
            {"player": "bounce", "value": "950"}, {"set": "score", "value": "score + 200"}]},
        {"name": "enemy_hit", "on": "touch", "target": "tag:enemy",
         "condition": "state == 'play' && !event.data.from_above && !player.invulnerable", "actions": [hurt]},
        {"name": "spikes", "on": "touch", "target": "tag:hazard", "condition": "state == 'play' && !player.invulnerable", "actions": [hurt]},
        {"name": "fell", "on": "fall", "condition": "state == 'play'", "actions": [hurt]},
        {"name": "hurt", "on": "event", "filter": "hurt", "actions": [
            {"set": "lives", "value": "lives - 1"},
            {"if": "lives <= 0", "then": [{"set": "state", "value": "'over'"}, {"player": "freeze"}],
             "else": [{"player": "respawn"}]}]},
        {"name": "checkpoint", "on": "touch", "target": "tag:checkpoint", "actions": [{"player": "checkpoint"}]},
        {"name": "goal", "on": "touch", "target": "tag:goal", "condition": "state == 'play'", "actions": [
            {"set": "score", "value": "score + 1000 + max(0, round(2000 - time * 10))"}, {"set": "state", "value": "'won'"},
            {"set": "best", "value": "max(best, score)"}, {"player": "freeze"}]},
        {"name": "restart_key", "on": "key", "key": "R", "actions": [{"restart": True, "keep": ["best"]}]},
        {"name": "restart_button", "on": "button", "filter": "restart", "actions": [{"restart": True, "keep": ["best"]}]},
    ])
    rt.call("logic.define_ui", items=[
        {"name": "hud", "kind": "text", "anchor": [0.5, 0.05], "font_size": 24,
         "text": "SCORE {score}     COINS {coins}     LIVES {lives}     TIME {floor(time)}     BEST {best}"},
        {"name": "banner", "kind": "text", "anchor": [0.5, 0.4], "font_size": 48, "visible_if": "state != 'play'",
         "color": [1, 0.85, 0.2, 1], "text": "{state == 'won' ? 'COURSE CLEAR!  Score ' + score : 'GAME OVER'}"},
        {"name": "restart", "kind": "button", "text": "Play again (R)", "anchor": [0.5, 0.55], "font_size": 24, "visible_if": "state != 'play'"},
        {"name": "help", "kind": "text", "anchor": [0.5, 0.96], "font_size": 14, "background": [0, 0, 0, 0.35],
         "text": "A/D or arrows: run   Shift: sprint   Space: jump (hold = higher)   Stomp enemies, collect coins, reach the flag   R: restart"},
    ])
    rt.call("logic.spawn_player", location=[0, 0, Z], kill_z=Z - 700, camera_distance=1500, camera_height=180, camera_pitch=-4)
    rt.call("lighting.apply_mood", mood="noon")
    rt.call("logic.start")
    st = rt.call("logic.get_state", log_limit=3)
    print("player:", st.get("player"), "errors:", [e for e in st["log"] if "errors" in e])
    if args.out:
        rt.call("ops.wait", seconds=2)
        rt.screenshot(args.out, show_ui=True)
        print("saved", args.out)


if __name__ == "__main__":
    main()
