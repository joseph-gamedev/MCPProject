"""
SKY ACES: an arcade dogfight made only from tool calls.

Hangar: three jets on turntables (Falcon, Viper, Raptor) with prices and stats; buy, select and upgrade (engine, armor,
guns, missiles) with credits that are saved between sessions. Launch: fly over an ocean with islands and clouds against
three waves of enemy jets that orbit, spot you, chase with lead pursuit, shoot, and evade when damaged. Guns and
lock-on homing missiles, target brackets, missile lock, gun reticle, health / speed / altitude / throttle HUD, explosions.

    python Scripts/examples/dogfight.py [--clear] [--out shot.jpg]

Controls: mouse or arrows steer, A/D roll, W/S throttle, Shift boost, left mouse / F guns, right mouse / G missiles.
"""

import argparse
import math
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from rtbuild import Runtime, box, cyl, cone, sphere, dome, loft, panel, mat, extrude  # noqa: E402

HANGAR = (0.0, -300000.0, 0.0)     # hangar far from the battle area
PED_GAP = 2200.0
JETS = [
    {"name": "Falcon", "price": 0, "speed": 4200, "turn": 70, "health": 10, "guns": 1.0, "missiles": 2,
     "desc": "Balanced multirole fighter", "style": "falcon", "paint": [0.32, 0.37, 0.43], "accent": [0.1, 0.3, 0.75]},
    {"name": "Viper", "price": 600, "speed": 4900, "turn": 88, "health": 8, "guns": 1.25, "missiles": 2,
     "desc": "Light, fast and very agile", "style": "viper", "paint": [0.75, 0.08, 0.06], "accent": [0.9, 0.9, 0.9]},
    {"name": "Raptor", "price": 1500, "speed": 5300, "turn": 78, "health": 14, "guns": 1.5, "missiles": 4,
     "desc": "Stealth air-superiority fighter", "style": "raptor", "paint": [0.17, 0.19, 0.21], "accent": [0.6, 0.62, 0.65]},
]


# ------------------------------------------------------------------ jet models (nose +X, up +Z)

def body_rings(stations, sides=14):
    rings = []
    for x, ry, rz, zc in stations:
        rings.append([[x, ry * math.cos(2 * math.pi * i / sides), zc + rz * math.sin(2 * math.pi * i / sides)] for i in range(sides)])
    return rings


def jet_parts(style, paint, accent):
    P = mat("painted_iron", color=paint, roughness=0.45, metallic=0.4)
    A = mat("painted_iron", color=accent, roughness=0.4, metallic=0.4)
    GLASS = mat("tinted_glass", color=[0.05, 0.12, 0.18], opacity=0.75)
    DARK = mat("steel", color=[0.08, 0.08, 0.09], roughness=0.3)
    GLOW = mat("light", color=[1, 0.5, 0.15], emissive=[1, 0.45, 0.1], emissive_strength=12)
    parts = []
    # fuselage: pointed nose to the engine section
    stations = [(760, 3, 3, 0), (640, 30, 28, 0), (480, 55, 52, 4), (250, 75, 70, 8), (0, 85, 75, 6), (-300, 85, 72, 4), (-560, 70, 62, 2), (-650, 60, 55, 2)]
    if style == "raptor":
        stations = [(780, 3, 3, 0), (640, 38, 26, 0), (420, 80, 48, 4), (150, 125, 62, 6), (-200, 135, 58, 4), (-560, 115, 50, 2), (-660, 100, 45, 2)]
    parts.append(loft(body_rings(stations), smooth_angle=50, **P))
    parts.append(sphere(100, at=[330, 0, 55], scale=[3.0, 1.0, 1.0], **GLASS))  # canopy
    # wings (flat plates, thickness 14)
    if style == "falcon":
        wing = [[150, 70], [-300, 520], [-430, 520], [-380, 70]]
    elif style == "viper":
        wing = [[260, 60], [-420, 470], [-520, 470], [-470, 60]]
    else:
        wing = [[260, 110], [-260, 600], [-420, 600], [-520, 110]]
    for sgn in (1, -1):
        prof = [[x, y * sgn] for x, y in wing]
        if sgn < 0:
            prof = list(reversed(prof))
        parts.append(_extrude(prof, 14, [0, 0, -6], P))
        # wingtip stripe
        tip = [[wing[1][0] + 10, wing[1][1] * sgn], [wing[2][0], wing[2][1] * sgn]]
        parts.append(box([abs(tip[0][0] - tip[1][0]) + 20, 40, 16], at=[(tip[0][0] + tip[1][0]) / 2, (wing[1][1] - 20) * sgn, -7], **A))
        # tailplanes
        tp = [[-460, 50], [-620, 250], [-690, 250], [-660, 50]]
        prof = [[x, y * sgn] for x, y in tp]
        if sgn < 0:
            prof = list(reversed(prof))
        parts.append(_extrude(prof, 10, [0, 0, 0], P))
    if style == "viper":  # canards
        for sgn in (1, -1):
            c = [[470, 40], [380, 170], [340, 170], [360, 40]]
            prof = [[x, y * sgn] for x, y in c]
            parts.append(_extrude(prof if sgn > 0 else list(reversed(prof)), 8, [0, 0, 10], A))
    # vertical tails
    fin = [[0, 0], [260, 0], [150, 330], [60, 330]]
    if style == "raptor":
        for sgn in (1, -1):
            f = panel([[u - 640, v] for u, v in fin], 12, at=[0, 80 * sgn, 40], yaw=0, **P)
            f["rotation"] = [0, 0, 22 * sgn]
            parts.append(f)
    else:
        parts.append(panel([[u - 650, v] for u, v in fin], 12, at=[0, 6, 50], yaw=0, **P))
        parts.append(panel([[u - 560, v * 0.4 + 200] for u, v in [[0, 0], [120, 0], [100, 60], [20, 60]]], 14, at=[0, 7, 50], yaw=0, **A))
    # intakes, nozzle(s), afterburner
    for sgn in (1, -1):
        parts.append(box([200, 40, 70], at=[120, 70 * sgn, -40], **DARK))
    nozzles = [(-30,), (30,)] if style == "raptor" else [(0,)]
    for (y,) in nozzles:
        parts.append(cyl(95 if style != "raptor" else 75, 70, at=[-640, y * 1.6, 4], rot=[90, 0, 0], **DARK))
        parts.append(cone(80 if style != "raptor" else 62, 120, at=[-700, y * 1.6, 4], rot=[90, 0, 0], **GLOW))
    # missiles under the wings
    for sgn in (1, -1):
        parts.append(cyl(22, 260, at=[60, 300 * sgn, -26], rot=[-90, 0, 0], **mat("white")))
    return parts


def _extrude(profile, thickness, at, m):
    d = extrude(profile, thickness, at=at, **m)
    d["pivot"] = "base"
    return d


def missile_parts():
    return [cyl(26, 200, at=[-100, 0, 0], rot=[-90, 0, 0], **mat("white")),
            cone(26, 50, at=[100, 0, 0], rot=[-90, 0, 0], **mat("red")),
            cone(30, 90, at=[-100, 0, 0], rot=[90, 0, 0], **mat("light", color=[1, 0.6, 0.2], emissive=[1, 0.5, 0.1], emissive_strength=25))]


# ------------------------------------------------------------------ world

def ocean_and_islands(rt, root):
    import random
    r = random.Random(11)
    parts = [box([600000, 600000, 40], at=[0, 0, -40], **mat("water", color=[0.02, 0.09, 0.14], roughness=0.08))]
    for _ in range(14):
        x, y = r.uniform(-60000, 60000), r.uniform(-60000, 60000)
        d = r.uniform(5000, 14000)
        parts.append(dome(d * 1.15, 200, at=[x, y, -60], **mat("sand", color=[0.7, 0.62, 0.45], tile=500)))
        parts.append(dome(d, r.uniform(800, 2500), at=[x, y, 0], **mat("grass", color=[0.12, 0.3, 0.08], tile=600, relief=3)))
        if r.random() < 0.6:
            parts.append(cone(d * 0.45, r.uniform(2500, 6500), at=[x + r.uniform(-800, 800), y, 300], **mat("stone", color=[0.38, 0.36, 0.33], tile=400, relief=6)))
    rt.build("World", parts, parent=root)
    clouds = []
    for _ in range(70):
        x, y, z = r.uniform(-70000, 70000), r.uniform(-70000, 70000), r.uniform(4500, 9000)
        for k in range(4):
            clouds.append(sphere(r.uniform(1500, 3200), at=[x + r.uniform(-1500, 1500), y + r.uniform(-900, 900), z + r.uniform(-300, 300)],
                                 scale=[1.6, 1.1, 0.6], **mat("white", color=[0.95, 0.96, 0.98], roughness=1.0)))
    rt.build("Clouds", clouds, parent=root)


def hangar(rt, root):
    hx, hy, hz = HANGAR
    parts = [box([9000, 5000, 40], at=[hx - 2200, hy, hz - 40], **mat("concrete", color=[0.25, 0.26, 0.28], style="concrete", tile=400)),
             box([9000, 100, 2600], at=[hx - 2200, hy + 2500, hz], **mat("steel", color=[0.18, 0.2, 0.23], roughness=0.5)),
             box([100, 5000, 2600], at=[hx + 2300, hy, hz], **mat("steel", color=[0.18, 0.2, 0.23])),
             box([100, 5000, 2600], at=[hx - 6700, hy, hz], **mat("steel", color=[0.18, 0.2, 0.23]))]
    for i in range(3):
        parts.append(cyl(1900, 60, at=[hx + i * PED_GAP, hy, hz], detail="high", **mat("steel", color=[0.3, 0.32, 0.35], roughness=0.3)))
        parts.append(box([1600, 30, 400], at=[hx - i * PED_GAP, hy + 2440, hz + 1500], **mat("light", color=[0.3, 0.6, 1], emissive=[0.2, 0.5, 1], emissive_strength=2)))
    for k in range(8):
        parts.append(box([200, 4800, 30], at=[hx + 1800 - k * 1100, hy, hz + 2560], **mat("light", color=[1, 0.95, 0.85], emissive=[1, 0.95, 0.85], emissive_strength=6)))
    rt.build("HangarHall", parts, parent=root)
    rt.call("build.create_primitive", shape="tube", size=[2000, 2000, 20], thickness=60, location=[hx, hy, hz + 62], name="spot", parent=root,
            material="light", color=[1, 0.7, 0.1], emissive=[1, 0.6, 0.1], emissive_strength=8, detail="high")
    for i, j in enumerate(JETS):
        rt.build(f"show_{j['style']}", jet_parts(j["style"], j["paint"], j["accent"]), location=[hx - i * PED_GAP, hy, hz + 260], parent=root)
        rt.call("logic.set_motion", targets=[f"Arena/show_{j['style']}"], motion={"type": "none", "spin": [0, 25, 0]})
        # the flying copy waits far below the ocean until it becomes the player's body
        rt.build(f"fly_{j['style']}", jet_parts(j["style"], j["paint"], j["accent"]), location=[0, 0, -200000], parent=root)


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
    hx, hy, hz = HANGAR
    root = rt.group("Arena")
    ocean_and_islands(rt, root)
    hangar(rt, root)

    # ---- enemies, effects, missiles (pools)
    rt.build("bandit", jet_parts("falcon", [0.12, 0.12, 0.13], [0.8, 0.08, 0.05]), location=[0, 0, -210000], tags=["bandit"], parent=root)
    rt.call("logic.define_pool", name="bandits", template="Arena/bandit", size=8)
    rt.build("missile", missile_parts(), location=[0, 0, -220000], parent=root)
    rt.call("logic.define_pool", name="missiles", template="Arena/missile", size=10)
    boom = rt.call("build.create_primitive", shape="sphere", size=[300, 300, 300], location=[0, 0, -230000], name="boom", parent=root,
                   material="light", color=[1, 0.45, 0.1], emissive=[1, 0.4, 0.05], emissive_strength=30)["id"]
    rt.call("logic.define_pool", name="booms", template=boom, size=8)
    rt.call("logic.set_solid", targets=["tag:bandit"], solid=False)

    rt.call("logic.define_feedback", name="guns", steps=[{"type": "shake", "duration": 0.05, "intensity": 0.08}])
    rt.call("logic.define_feedback", name="kill", steps=[{"type": "spawn", "pool": "booms", "scale": 9, "duration": 0.7}, {"type": "shake", "duration": 0.3, "intensity": 0.4}])
    rt.call("logic.define_feedback", name="hit", steps=[{"type": "shake", "duration": 0.25, "intensity": 1.0}])

    # ---- player jet (spawned on launch)
    rt.call("logic.spawn_player", mode="flight", location=[0, 0, 5000], yaw=0, speed=4200, turn=70, radius=300, auto_spawn=False,
            body="Arena/fly_falcon", secondary="missile", camera_distance=1500, camera_height=380, fov=72)
    rt.call("logic.define_health", targets=["player"], max=10, invulnerable=0.25, on_death="none", hit_feedback="hit")
    rt.call("logic.define_health", targets=["tag:bandit"], max=6, on_death="release", death_feedback="kill")
    rt.call("logic.define_weapon", name="guns", damage=1, rate=12, auto=True, speed=16000, spread=0.6, lifetime=1.1, muzzle=650, radius=60,
            hits="tag:bandit", feedback="guns", bullet={"size": 40, "color": [1.0, 0.85, 0.3]}, pool_size=60)
    rt.call("logic.define_weapon", name="missile", damage=6, rate=1.2, magazine=2, reload=8, speed=3500, homing=110, lock_range=16000,
            lock_angle=22, lifetime=6, muzzle=500, radius=120, hits="tag:bandit", pool="missiles")
    rt.call("logic.define_weapon", name="bandit_guns", damage=1, rate=5, burst=3, burst_gap=0.08, speed=14000, spread=1.6, lifetime=1.2,
            muzzle=650, radius=60, hits="player", bullet={"size": 40, "color": [1.0, 0.2, 0.1]}, pool_size=60)
    rt.call("logic.equip", owner="player", weapon="guns")
    rt.call("logic.equip", owner="tag:bandit", weapon="bandit_guns")

    # ---- enemy AI
    rt.call("logic.define_brain", name="fighter", start="patrol", vision={"radius": 22000, "angle": 170, "show_cone": False},
            flight={"speed": 3600, "turn_rate": 55, "min_altitude": 1500, "orbit_radius": 5000}, states=[
                {"name": "patrol", "move": "patrol", "transitions": [{"when": "sees", "to": "attack"}]},
                {"name": "attack", "move": "chase", "fire": True, "fire_angle": 9, "fire_range": 8000, "keep_distance": 1200,
                 "transitions": [{"when": "health_pct < 0.4 && time_in_state > 4", "to": "evade"}, {"when": "!sees && since_seen > 5", "to": "patrol"}]},
                {"name": "evade", "move": "flee", "speed": 4400, "turn_rate": 70,
                 "transitions": [{"when": "time_in_state > 3.5", "to": "attack"}]},
            ])
    rt.call("logic.assign_brain", targets=["tag:bandit"], brain="fighter")
    rt.call("logic.define_spawner", name="bandits", pool="bandits", around_player={"radius": [11000, 16000], "height": [3500, 6500]},
            interval=[1.0, 2.5], autostart=False, wave_gap=4, start_delay=2, waves=[{"count": 3, "max_alive": 3}, {"count": 5, "max_alive": 4}, {"count": 7, "max_alive": 5}])

    # ---- game data and rules
    jets_var = [{k: v for k, v in j.items() if k not in ("style", "paint", "accent")} | {"body": f"Arena/fly_{j['style']}"} for j in JETS]
    rt.call("logic.define_vars", replace=True, vars={
        "jets": jets_var, "sel": 0, "credits": 400, "owned": ["Falcon"], "upg": {"engine": 0, "armor": 0, "guns": 0, "missiles": 0},
        "state": "menu", "wave": 0, "kills": 0, "earned": 0, "banner_until": 0, "best": 0, "msg": "", "msg_until": 0})
    cam = lambda: {"call": "camera.set_camera", "args": {"location": f"=[{hx} - sel * {PED_GAP} + 900, {hy} - 3600, {hz} + 1400]",
                                                          "look_at": f"=[{hx} - sel * {PED_GAP}, {hy}, {hz} + 350]", "fov": 55}}
    spot = lambda: {"tween": "'Arena/spot'", "location": f"=[{hx} - sel * {PED_GAP}, {hy}, {hz} + 62]", "duration": 0.35, "ease": "out_back"}
    keep = ["credits", "owned", "upg", "sel", "best"]
    msg = lambda text: [{"set": "msg", "value": text}, {"set": "msg_until", "value": "time + 2.5"}]

    def upgrade(stat):
        return {"name": f"up_{stat}", "on": "button", "filter": f"up_{stat}", "actions": [
            {"if": f"upg.{stat} >= 5", "then": msg("'Already at max level'"),
             "else": [{"if": f"credits < 150 * (upg.{stat} + 1)", "then": msg("'Not enough credits'"),
                       "else": [{"set": "credits", "value": f"credits - 150 * (upg.{stat} + 1)"},
                                {"set": "upg", "value": f"set(upg, '{stat}', upg.{stat} + 1)"},
                                {"save": "skyaces", "vars": keep}] + msg(f"'{stat.upper()} upgraded'")}]}]}

    rt.call("logic.define_rules", replace=True, rules=[
        {"name": "boot", "on": "start", "actions": [{"load": "skyaces"}, {"set": "state", "value": "'menu'"}, cam(), spot()]},
        {"name": "next", "on": "button", "filter": "next", "condition": "state == 'menu'", "actions": [{"set": "sel", "value": "(sel + 1) % len(jets)"}, cam(), spot()]},
        {"name": "prev", "on": "button", "filter": "prev", "condition": "state == 'menu'", "actions": [{"set": "sel", "value": "(sel + len(jets) - 1) % len(jets)"}, cam(), spot()]},
        {"name": "buy", "on": "button", "filter": "buy", "condition": "!contains(owned, jets[sel].name)", "actions": [
            {"if": "credits < jets[sel].price", "then": msg("'Not enough credits'"),
             "else": [{"set": "credits", "value": "credits - jets[sel].price"}, {"push": "owned", "value": "jets[sel].name"},
                      {"save": "skyaces", "vars": keep}] + msg("jets[sel].name + ' purchased!'")}]},
        upgrade("engine"), upgrade("armor"), upgrade("guns"), upgrade("missiles"),
        {"name": "launch", "on": "button", "filter": "launch", "condition": "state == 'menu' && contains(owned, jets[sel].name)", "actions": [
            {"set": "state", "value": "'fight'"}, {"set": "kills", "value": "0"}, {"set": "earned", "value": "0"},
            {"player": "set", "fields": {"body": "=jets[sel].body", "speed": "=jets[sel].speed * (1 + 0.06 * upg.engine)",
                                         "turn": "=jets[sel].turn * (1 + 0.03 * upg.engine)"}},
            {"player": "spawn"},
            {"health": "'player'", "max": "jets[sel].health * (1 + 0.2 * upg.armor)", "value": "jets[sel].health * (1 + 0.2 * upg.armor)"},
            {"weapon": "guns", "set": {"damage": "=jets[sel].guns * (1 + 0.2 * upg.guns)"}},
            {"weapon": "missile", "set": {"magazine": "=jets[sel].missiles + upg.missiles"}},
            {"spawner": "bandits", "op": "start"}]},
        {"name": "wave", "on": "wave_start", "filter": "bandits", "actions": [{"set": "wave", "value": "event.wave"}, {"set": "banner_until", "value": "time + 2.5"}]},
        {"name": "kill", "on": "died", "target": "tag:bandit", "actions": [
            {"set": "kills", "value": "kills + 1"}, {"set": "earned", "value": "earned + 50 + 10 * wave"}]},
        {"name": "won", "on": "waves_complete", "condition": "state == 'fight'", "actions": [
            {"set": "earned", "value": "earned + 300"}, {"set": "credits", "value": "credits + earned"}, {"set": "best", "value": "max(best, kills)"},
            {"set": "state", "value": "'won'"}, {"player": "freeze"}, {"spawner": "bandits", "op": "stop"}, {"save": "skyaces", "vars": keep}]},
        {"name": "lost", "on": "died", "target": "player", "condition": "state == 'fight'", "actions": [
            {"feedback": "kill"}, {"set": "credits", "value": "credits + earned"}, {"set": "best", "value": "max(best, kills)"},
            {"set": "state", "value": "'lost'"}, {"player": "freeze"}, {"spawner": "bandits", "op": "stop"}, {"save": "skyaces", "vars": keep}]},
        {"name": "back", "on": "button", "filter": "back", "actions": [{"restart": True, "keep": keep}]},
        {"name": "back_key", "on": "key", "key": "Escape", "condition": "state != 'menu'", "actions": [{"restart": True, "keep": keep}]},
    ])

    # ---- UI
    BG = [0.02, 0.03, 0.06, 0.7]
    clear = [0, 0, 0, 0]
    menu = "state == 'menu'"
    fight = "state == 'fight'"
    over = "state == 'won' || state == 'lost'"
    owned = "contains(owned, jets[sel].name)"
    items = [
        # menu
        {"name": "title", "kind": "text", "anchor": [0.5, 0.06], "font_size": 40, "background": clear, "color": [1, 0.8, 0.25, 1], "visible_if": menu, "text": "SKY  ACES"},
        {"name": "credits", "kind": "text", "anchor": [0.98, 0.06], "font_size": 24, "background": BG, "visible_if": menu, "text": "CREDITS  {credits}"},
        {"name": "jetname", "kind": "text", "anchor": [0.02, 0.2], "font_size": 34, "background": BG, "visible_if": menu,
         "text": "{jets[sel].name}{" + owned + " ? '' : '   -   ' + jets[sel].price + ' CR'}"},
        {"name": "jetdesc", "kind": "text", "anchor": [0.02, 0.27], "font_size": 16, "background": clear, "color": [0.8, 0.85, 1, 1], "visible_if": menu, "text": "{jets[sel].desc}"},
    ]
    stats = [("SPEED", "jets[sel].speed * (1 + 0.06 * upg.engine)", "6500", [0.3, 0.8, 1, 1]),
             ("AGILITY", "jets[sel].turn", "100", [0.4, 1, 0.5, 1]),
             ("ARMOR", "jets[sel].health * (1 + 0.2 * upg.armor)", "30", [1, 0.75, 0.3, 1]),
             ("FIREPOWER", "jets[sel].guns * (1 + 0.2 * upg.guns)", "3.5", [1, 0.35, 0.3, 1])]
    for i, (label, value, mx, col) in enumerate(stats):
        items.append({"name": f"stat_{i}", "kind": "bar", "anchor": [0.02, 0.33 + i * 0.055], "size": [380, 26], "value": value, "max": mx, "fill": col,
                      "background": BG, "font_size": 13, "ghost": False, "visible_if": menu, "text": label})
    items.append({"name": "missiles_stat", "kind": "text", "anchor": [0.02, 0.555], "font_size": 15, "background": clear, "visible_if": menu,
                  "text": "MISSILES  {jets[sel].missiles + upg.missiles}"})
    items += [
        {"name": "prev", "kind": "button", "anchor": [0.36, 0.86], "font_size": 22, "visible_if": menu, "background": [0.15, 0.2, 0.3, 1], "text": "<  PREV"},
        {"name": "next", "kind": "button", "anchor": [0.64, 0.86], "font_size": 22, "visible_if": menu, "background": [0.15, 0.2, 0.3, 1], "text": "NEXT  >"},
        {"name": "launch", "kind": "button", "anchor": [0.5, 0.86], "font_size": 26, "visible_if": menu + " && " + owned, "background": [0.85, 0.35, 0.05, 1], "text": "LAUNCH"},
        {"name": "buy", "kind": "button", "anchor": [0.5, 0.86], "font_size": 22, "visible_if": menu + " && !" + owned, "background": [0.1, 0.55, 0.2, 1],
         "text": "BUY  {jets[sel].price} CR"},
        {"name": "upg_title", "kind": "text", "anchor": [0.98, 0.2], "font_size": 22, "background": BG, "visible_if": menu, "text": "UPGRADES  (all jets)"},
    ]
    for i, stat in enumerate(["engine", "armor", "guns", "missiles"]):
        items.append({"name": f"up_{stat}", "kind": "button", "anchor": [0.98, 0.28 + i * 0.07], "font_size": 16, "visible_if": menu,
                      "background": [0.12, 0.25, 0.45, 1],
                      "text": f"{stat.upper()}  Lv {{upg.{stat}}}/5     {{upg.{stat} >= 5 ? 'MAX' : (150 * (upg.{stat} + 1)) + ' CR'}}"})
    items += [
        {"name": "msg", "kind": "text", "anchor": [0.5, 0.76], "font_size": 18, "background": BG, "color": [1, 0.9, 0.4, 1], "visible_if": "time < msg_until", "text": "{msg}"},
        {"name": "help_menu", "kind": "text", "anchor": [0.5, 0.96], "font_size": 13, "background": clear, "color": [1, 1, 1, 0.7], "visible_if": menu,
         "text": "Flight: mouse/arrows steer  -  A/D roll  -  W/S throttle  -  Shift boost  -  LMB/F guns  -  RMB/G missiles  -  Esc back to hangar"},
        # fight
        {"name": "hp", "kind": "bar", "anchor": [0.02, 0.05], "size": [360, 28], "value": "health.player", "max": "jets[sel].health * (1 + 0.2 * upg.armor)",
         "fill": [0.9, 0.15, 0.1, 1], "background": BG, "font_size": 14, "visible_if": fight + " || " + over, "text": "HULL  {round(health.player)}"},
        {"name": "throttle", "kind": "bar", "anchor": [0.02, 0.105], "size": [360, 14], "value": "player.throttle", "max": "1", "fill": [0.3, 0.8, 1, 1],
         "background": BG, "font_size": 1, "ghost": False, "visible_if": fight, "text": ""},
        {"name": "flight", "kind": "text", "anchor": [0.02, 0.16], "font_size": 18, "background": clear, "visible_if": fight,
         "text": "SPD {round(player.speed * 0.036)} km/h    ALT {round(player.altitude / 100)} m{player.boost ? '    BOOST' : ''}"},
        {"name": "wave_hud", "kind": "text", "anchor": [0.98, 0.05], "font_size": 22, "background": BG, "visible_if": fight,
         "text": "WAVE {wave}/3     BANDITS {len(nodes('tag:bandit'))}     KILLS {kills}"},
        {"name": "earned", "kind": "text", "anchor": [0.98, 0.11], "font_size": 16, "background": clear, "color": [1, 0.85, 0.3, 1], "visible_if": fight, "text": "+{earned} CR"},
        {"name": "missiles_hud", "kind": "text", "anchor": [0.98, 0.9], "font_size": 30, "background": BG, "visible_if": fight,
         "text": "MSL  {ammo['player#missile'] == null ? jets[sel].missiles + upg.missiles : ammo['player#missile']}{reloading['player#missile'] ? '  (reloading)' : ''}"},
        {"name": "lock", "kind": "text", "anchor": [0.5, 0.62], "font_size": 18, "background": clear, "color": [1, 0.2, 0.15, 1],
         "visible_if": fight + " && player.lock != null && player.lock != ''", "text": "MISSILE LOCK - fire with RMB / G"},
        {"name": "wave_banner", "kind": "text", "anchor": [0.5, 0.3], "font_size": 44, "background": clear, "color": [1, 0.85, 0.3, 1],
         "visible_if": fight + " && time < banner_until", "text": "WAVE {wave}"},
        # results
        {"name": "result", "kind": "text", "anchor": [0.5, 0.4], "font_size": 50, "background": [0, 0, 0, 0.7], "color": [1, 0.85, 0.25, 1], "visible_if": over,
         "text": "{state == 'won' ? 'MISSION COMPLETE' : 'SHOT DOWN'}"},
        {"name": "result2", "kind": "text", "anchor": [0.5, 0.5], "font_size": 22, "background": clear, "visible_if": over,
         "text": "Kills {kills}     Credits earned {earned}     Total {credits}"},
        {"name": "back", "kind": "button", "anchor": [0.5, 0.6], "font_size": 22, "background": [0.1, 0.45, 0.9, 1], "visible_if": over, "text": "RETURN TO HANGAR"},
    ]
    rt.call("logic.define_ui", replace=True, items=items)
    rt.call("logic.set_hud", markers=True, enemy_bars=True, damage_numbers=True, hit_flash=True, low_health=True, alert_icons=False)
    rt.call("logic.set_input_mode", mode="cursor", lock_camera=True)
    rt.call("lighting.apply_mood", mood="morning")
    rt.call("logic.start")
    st = rt.call("logic.get_state", log_limit=5)
    print("state:", st["vars"]["state"], "errors:", [e for e in st["log"] if "errors" in e])
    if args.out:
        rt.call("ops.wait", seconds=2)
        rt.screenshot(args.out, show_ui=True)
        print("saved", args.out)


if __name__ == "__main__":
    main()
