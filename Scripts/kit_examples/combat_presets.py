"""EXAMPLE presets for combat and AI: weapons, enemies with brains, health, damage zones, spawners and game-feel presets.

    import combat_presets as c
    c.feel_presets()                                   # "hit", "death", "shot", "pickup" feedback presets
    c.player_health(100)
    c.blaster("blaster"); tools.logic.equip(owner="player", weapon="blaster")
    c.guard_brain()                                    # patrol -> chase on sight -> search -> return
    c.enemies("tag:guard", brain="guard", health=60, weapon="enemy_gun", points=100)
    c.waves("bots", template="bot_template", waves=[5, 8, 12], points=[[0, 2000, 0], [2000, 0, 0]])

All of it is data for logic.* tools; rules can react to "damaged", "died", "state" (brain state changes) events.
"""

from unreal_kit import tools


def _list(t):
    return [t] if isinstance(t, str) else list(t)


# ------------------------------------------------------------------ feel

def feel_presets():
    """Standard game-feel presets: hit (shake + red flash + short freeze), death, shot (tiny shake), pickup (pop text)."""
    tools.logic.define_feedback(name="hit", steps=[{"type": "shake", "duration": 0.18, "intensity": 0.6},
                                                   {"type": "flash", "intensity": 0.35, "color": [1, 0.1, 0.1, 1], "duration": 0.12},
                                                   {"type": "freeze", "duration": 0.05}])
    tools.logic.define_feedback(name="death", steps=[{"type": "shake", "duration": 0.35, "intensity": 1.0},
                                                     {"type": "slowmo", "duration": 0.4, "scale": 0.35}])
    tools.logic.define_feedback(name="shot", steps=[{"type": "shake", "duration": 0.06, "intensity": 0.15}])
    tools.logic.define_feedback(name="pickup", steps=[{"type": "text", "text": "+", "duration": 0.6, "color": [1, 0.85, 0.3, 1]}])


# ------------------------------------------------------------------ health

def player_health(max_=100, invulnerable=1.0, bar=True):
    """Player health with hit feedback; adds a health bar to the HUD layer when bar=True."""
    r = tools.logic.define_health(targets=["player"], max=max_, invulnerable=invulnerable, hit_feedback="hit")
    if bar:
        tools.logic.set_hud(low_health=True, hit_flash=True, damage_numbers=True)
    return r


def health(targets, max_=50, points=0, points_var="score", loot=None, on_death=None, resistances=None):
    args = {"targets": _list(targets), "max": max_, "hit_feedback": "hit", "death_feedback": "death"}
    if points:
        args.update(points=points, points_var=points_var)
    if loot:
        args["loot"] = loot
    if on_death:
        args["on_death"] = on_death
    if resistances:
        args["resistances"] = resistances
    return tools.logic.define_health(**args)


def damage_zone(targets, damage=20, mode="enter", interval=0.5, affects="player", type_="generic", knockback=600):
    """Spikes, lava, saw blades, explosions: hurts whatever overlaps."""
    return tools.logic.define_hitzone(targets=_list(targets), damage=damage, mode=mode, interval=interval, affects=affects, type=type_,
                                      knockback=knockback)


# ------------------------------------------------------------------ weapons

def blaster(name="blaster", damage=20, rate=6, speed=6000, hits="tag:enemy", **kw):
    return tools.logic.define_weapon(name=name, damage=damage, rate=rate, speed=speed, hits=hits, feedback="shot", auto=True, **kw)


def shotgun(name="shotgun", damage=12, per_shot=7, spread=12, rate=1.2, magazine=6, reload=1.4, hits="tag:enemy", **kw):
    return tools.logic.define_weapon(name=name, damage=damage, per_shot=per_shot, spread=spread, rate=rate, magazine=magazine, reload=reload,
                                     auto_reload=True, hits=hits, feedback="shot", **kw)


def rifle(name="rifle", damage=34, rate=2, range_=8000, hits="tag:enemy", **kw):
    return tools.logic.define_weapon(name=name, mode="hitscan", damage=damage, rate=rate, range=range_, hits=hits, feedback="shot", **kw)


def missiles(name="missiles", damage=60, rate=0.8, homing=180, hits="tag:enemy", **kw):
    return tools.logic.define_weapon(name=name, damage=damage, rate=rate, homing=homing, lock_range=6000, lock_angle=30, speed=3000,
                                     type="explosion", hits=hits, **kw)


def enemy_gun(name="enemy_gun", damage=10, rate=1.2, speed=2500, **kw):
    return tools.logic.define_weapon(name=name, damage=damage, rate=rate, speed=speed, hits="player", spread=3, **kw)


def equip(owner, weapon):
    return tools.logic.equip(owner=owner, weapon=weapon)


# ------------------------------------------------------------------ brains

def guard_brain(name="guard", sight=2500, fov=90, speed=250, chase_speed=450, weapon=None, backend="Native"):
    """Patrol -> chase when it sees the player (fires if armed) -> search the last known spot -> return home."""
    chase = {"name": "chase", "move": "chase", "speed": chase_speed, "keep_distance": 600 if weapon else 0, "face": "target",
             "transitions": [{"when": "!sees && since_seen > 1", "to": "search"}, {"when": "!player_alive", "to": "patrol"}]}
    if weapon:
        chase.update(fire=True, weapon=weapon, fire_range=sight, fire_angle=15)
    states = [
        {"name": "patrol", "move": "patrol", "speed": speed, "face": "move", "transitions": [{"when": "sees", "to": "chase"}, {"when": "heard", "to": "search"}]},
        chase,
        {"name": "search", "move": "to_last_known", "speed": speed * 1.3, "face": "look_around",
         "transitions": [{"when": "sees", "to": "chase"}, {"when": "time_in_state > 6", "to": "return"}]},
        {"name": "return", "move": "home", "speed": speed, "face": "move", "transitions": [{"when": "sees", "to": "chase"}, {"when": "at_home", "to": "patrol"}]},
    ]
    return tools.logic.define_brain(name=name, states=states, vision={"radius": sight, "angle": fov, "show_cone": True}, hearing={"radius": sight * 0.6}, backend=backend)


def chaser_brain(name="chaser", speed=380):
    """Always runs at the player (zombies, slimes)."""
    return tools.logic.define_brain(name=name, states=[{"name": "chase", "move": "chase", "speed": speed, "face": "target", "transitions": []}],
                                    vision={"radius": 100000, "angle": 360})


def turret_brain(name="turret", weapon="enemy_gun", sight=3000):
    """Stands still, turns to the player and fires while it sees them."""
    return tools.logic.define_brain(name=name, states=[
        {"name": "idle", "move": "stop", "face": "look_around", "transitions": [{"when": "sees", "to": "fire"}]},
        {"name": "fire", "move": "stop", "face": "target", "fire": True, "weapon": weapon, "fire_range": sight, "fire_angle": 10,
         "transitions": [{"when": "!sees && since_seen > 1.5", "to": "idle"}]}], vision={"radius": sight, "angle": 120})


def coward_brain(name="coward", speed=420, flee_distance=900):
    """Wanders; runs away when the player comes close (animals, civilians)."""
    return tools.logic.define_brain(name=name, states=[
        {"name": "wander", "move": "wander", "speed": speed * 0.4, "radius": 800, "transitions": [{"when": f"distance < {flee_distance}", "to": "flee"}]},
        {"name": "flee", "move": "flee", "speed": speed, "transitions": [{"when": f"distance > {flee_distance * 2}", "to": "wander"}]}],
        vision={"radius": flee_distance * 2, "angle": 360})


def enemies(targets, brain="guard", health_=50, weapon=None, points=100, patrol=None):
    """Turns nodes into enemies: tag check, health with points, optional weapon, a brain (patrol route relative to start)."""
    targets = _list(targets)
    health(targets, health_, points=points)
    if weapon:
        tools.logic.equip(owner=targets[0] if len(targets) == 1 else targets[0], weapon=weapon)
    args = {"targets": targets, "brain": brain}
    if patrol:
        args["patrol"] = patrol
    return tools.logic.assign_brain(**args)


def waves(name, template, waves, points=None, around_player=None, interval=1.2, max_alive=6, wave_gap=4):
    """Spawner with waves: waves = [count, count, ...] or [{count, interval, max_alive}]. template = a node used as the
    enemy (give it health/brain via rules on 'spawned' or use a tagged template)."""
    tools.logic.define_pool(name=f"{name}_pool", template=template, size=max(max_alive * 2, 8))
    w = [{"count": x} if isinstance(x, int) else x for x in waves]
    args = {"name": name, "pool": f"{name}_pool", "waves": w, "interval": interval, "max_alive": max_alive, "wave_gap": wave_gap}
    if points:
        args["points"] = [list(p) for p in points]
    else:
        args["around_player"] = around_player or {"radius": [1500, 2500], "height": 0}
    return tools.logic.define_spawner(**args)


def interactable(targets, prompt, key="E", requires=None, locked_prompt=None, once=False):
    """Doors, chests, switches: a key prompt nearby; rules {"on": "interact", "target": ...} do the effect."""
    args = {"targets": _list(targets), "prompt": prompt, "key": key, "once": once}
    if requires:
        args.update(requires=requires, locked_prompt=locked_prompt or "Locked")
    return tools.logic.define_interactable(**args)
