"""EXAMPLE: reusable game pieces built from unreal_kit.rules and unreal_kit.ui (players, score and lives, pickups,
hazards, goals, patrols, timers). Read, copy and adapt.

    import unreal_kit as uk, game_pieces as g
    g.side_player(at=[0, 0, 100]); g.score_and_lives(lives=3); g.pickups("tag:coin"); g.hazard("tag:barrel")
    g.goal("flag"); uk.rules.start()
"""

import unreal_kit as uk
from unreal_kit import tools, ui
from unreal_kit.rules import *  # noqa: F401,F403

# ------------------------------------------------------------------ players

SIDE_CONTROLS = [
    {"key": "D", "action": "Move", "direction": [1, 0, 0]}, {"key": "Right", "action": "Move", "direction": [1, 0, 0]},
    {"key": "A", "action": "Move", "direction": [-1, 0, 0]}, {"key": "Left", "action": "Move", "direction": [-1, 0, 0]},
    {"key": "SpaceBar", "action": "Jump"},
    {"key": "W", "action": "Climb", "direction": [0, 0, 1]}, {"key": "Up", "action": "Climb", "direction": [0, 0, 1]},
    {"key": "S", "action": "Climb", "direction": [0, 0, -1]}, {"key": "Down", "action": "Climb", "direction": [0, 0, -1]},
    {"key": "LeftShift", "action": "Run"}, {"key": "F", "action": "Fire"},
]

TOPDOWN_CONTROLS = [
    {"key": "W", "action": "Move", "direction": [1, 0, 0]}, {"key": "S", "action": "Move", "direction": [-1, 0, 0]},
    {"key": "D", "action": "Move", "direction": [0, 1, 0]}, {"key": "A", "action": "Move", "direction": [0, -1, 0]},
    {"key": "SpaceBar", "action": "Jump"}, {"key": "LeftShift", "action": "Run"},
    {"key": "LeftMouseButton", "action": "Fire"}, {"key": "F", "action": "Fire"},
]


def side_player(at, walk_speed=600, jump=950, camera_distance=2200, camera_height=250, body=None, fixed_screen=None, **movement):
    """2D side view player (plane X/Z). fixed_screen = {"center": [x, y, z], "width": cm} gives one fixed arcade screen
    (orthographic) instead of a following camera."""
    mv = {"walk_speed": walk_speed, "jump_velocity": jump, "gravity_scale": 2.2, "air_control": 0.85, "climb_speed": 280}
    mv.update(movement)
    if fixed_screen:
        c = fixed_screen["center"]
        cam = {"mode": "Fixed", "location": [c[0], c[1] + 5000, c[2]], "look_at_point": c, "ortho_width": fixed_screen.get("width", 3000)}
    else:
        cam = {"mode": "Follow", "offset": [0, camera_distance, camera_height], "lag": 0.12}
    args = dict(location=list(at), controls=SIDE_CONTROLS, movement=mv, camera=cam, plane_normal=[0, 1, 0], facing="Movement")
    if body:
        args["body"] = body
    return tools.character.create(**args)


def topdown_player(at, walk_speed=600, camera_height=1500, camera_back=900, aim_with_mouse=True, body=None, **movement):
    """Top-down / twin-stick player: WASD move, faces the mouse (or movement), camera above and behind."""
    mv = {"walk_speed": walk_speed, "jump_velocity": 0}
    mv.update(movement)
    cam = {"mode": "Follow", "offset": [-camera_back, 0, camera_height], "lag": 0.1}
    args = dict(location=list(at), controls=TOPDOWN_CONTROLS, movement=mv, camera=cam, facing="Mouse" if aim_with_mouse else "Movement")
    if body:
        args["body"] = body
    return tools.character.create(**args)


def third_person_player(at, walk_speed=500, run_speed=900, camera_distance=450, body=None):
    """Behind-the-shoulder style: WASD on world axes, camera trailing at eye height."""
    cam = {"mode": "Follow", "offset": [-camera_distance, 0, 220], "lag": 0.15, "look_offset": [0, 0, 120]}
    args = dict(location=list(at), controls=TOPDOWN_CONTROLS, movement={"walk_speed": walk_speed, "run_speed": run_speed, "jump_velocity": 700},
                camera=cam, facing="Movement")
    if body:
        args["body"] = body
    return tools.character.create(**args)


def climbable(targets):
    """Ladders, vines, ropes: the player's climb keys move along them through floors."""
    return tools.character.set_climbable(targets=list(targets) if not isinstance(targets, str) else [targets])


# ------------------------------------------------------------------ game pieces


def score_and_lives(lives=3, title=None, save_slot="game", theme=None):
    """score / hi / lives / over variables, a HUD, a game-over panel with a restart button, R to restart,
    and a saved high score. Use lose_life() in your own rules; win(message) ends with a win banner."""
    # ui is imported at the top
    define_vars(score=0, hi=0, lives=lives, over=False, won=False, banner="", banner_until=0)
    add_rules([
        rule("kit_load_hi", "start", [load(save_slot)]),
        rule("kit_hi", "var_changed", [set_("hi", "score")], filter="score", condition="score > hi"),
        rule("kit_restart_key", "key", [restart(["hi"])], key="R"),
        rule("kit_restart_button", "button", [restart(["hi"])], filter="kit_again"),
        rule("kit_game_over", "var_changed", [set_("over", "true"), player("freeze"), save(save_slot, ["hi"])], filter="lives",
             condition="lives <= 0 && !over"),
    ])
    items = [ui.hud("kit_hud", [("SCORE", "{score}"), ("HIGH", "{hi}"), ("LIVES", "{lives}")], corner="top-left", title=title),
             ui.banner("kit_banner", "{banner}", visible_if="time < banner_until && !over"),
             ui.panel_message("kit_over", "GAME OVER", "Score {score}  -  high score {hi}", button=("kit_again", "Play again"), visible_if="over && !won"),
             ui.panel_message("kit_win", "{banner}", "Score {score}", button=("kit_again", "Play again"), visible_if="won")]
    return ui.define(items, theme=theme)


def lose_life(respawn_delay=1.2):
    """Actions for 'the player got hit': lives - 1, feedback, respawn while lives remain."""
    return [add("lives", -1), {"set": "banner", "value": "lives > 0 ? 'OUCH!' : ''"}, {"set": "banner_until", "value": "time + 1.2"},
            if_("lives > 0", [player("respawn")])]


def win(message="YOU WIN!"):
    return [set_("won", "true"), set_("over", "true"), {"set": "banner", "value": f"'{message}'"}, player("freeze")]


def show_banner(text_expr, seconds=2.0):
    return [{"set": "banner", "value": text_expr}, {"set": "banner_until", "value": f"time + {seconds}"}]


def pickups(targets, points=10, name=None, effect=True):
    """Collectibles: touching one hides it and adds points (restored on restart)."""
    name = name or "kit_pickup_" + "".join(c for c in str(targets) if c.isalnum())
    tools.logic.set_solid(targets=[targets] if isinstance(targets, str) else list(targets), solid=False)
    acts = [hide("event.target"), add("score", points)]
    if effect:
        acts.append(tween("event.target", scale=0.2, move=[0, 0, 120], duration=0.25))
    return add_rules([rule(name, "touch", acts, target=targets)])


def hazard(targets, name=None, solid=False):
    """Anything that costs a life on touch (enemies, barrels, spikes, lava). Moving/pooled objects too."""
    name = name or "kit_hazard_" + "".join(c for c in str(targets) if c.isalnum())
    tools.logic.watch_contacts(name=name, a=[targets] if isinstance(targets, str) else list(targets), b=["player"], margin=5)
    if not solid:
        tools.logic.set_solid(targets=[targets] if isinstance(targets, str) else list(targets), solid=False)
    return add_rules([rule(name, "contact", lose_life(), filter=name, condition="!over")])


def goal(target, message="YOU WIN!", bonus_expr=None):
    """Reaching the target wins (adds an optional bonus expression to the score first)."""
    tools.logic.watch_contacts(name="kit_goal", a=[target], b=["player"], margin=30)
    acts = ([add("score", bonus_expr)] if bonus_expr else []) + win(message)
    return add_rules([rule("kit_goal", "contact", acts, filter="kit_goal", condition="!over")])


def patrol(targets, axis=(1, 0, 0), distance=600, speed=150, face=True):
    """Simple back-and-forth movers (platforms, simple enemies)."""
    return tools.logic.set_motion(targets=[targets] if isinstance(targets, str) else list(targets),
                                  motion={"type": "Patrol", "axis": list(axis), "distance": distance, "speed": speed, "face": face})


def pool(name, template, size=10):
    return tools.logic.define_pool(name=name, template=template, size=size)


def every(seconds, actions, name, condition="!over", start_delay=None):
    """Repeating timer rule (spawns, enemy fire, ticks): starts with the game."""
    return add_rules([rule(f"{name}_start", "start", [timer(name, start_delay if start_delay is not None else seconds, repeat=False)]),
                      rule(name, "timer", list(actions) + [timer(name, seconds)], filter=name, condition=condition)])
