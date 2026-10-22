#include "GameRecipeSkills.h"

URuntimeArcadeShooterSkill::URuntimeArcadeShooterSkill()
{
	Description = TEXT("Apply when making a fixed-screen arcade shooter (Galaga, Space Invaders, Galaxian, Centipede): ship at the bottom, "
		"enemy formations that swoop and dive, waves, lives, high score.");
	Toolsets = {TEXT("logic"), TEXT("build"), TEXT("scene"), TEXT("lighting")};
	Instructions = TEXT(R"SKILL(# Arcade shooter recipe (Galaga-style)

Aim for this result: a black starfield, a ship at the bottom that slides left/right and fires straight up, 40 colorful
flat "sprite" enemies that fly in from the sides into a formation, sway, then dive-bomb the ship in loops while shooting,
and a clean arcade HUD (high score, score, stage, ships). Work in this order and check each step with a screenshot
(capture.screenshot {show_ui:true, max_dim:1600}).

## 1. Coordinates and camera
Top-down, camera straight down: screen up = +X, screen right = +Y. Ship at X=0, formation rows at X = 2000..2800,
playfield half width 1450 (Y). Everything flies at Z=60.
logic.spawn_player {mode:"topdown", body:"ship", location:[0,0,20], speed:1100,
  aim_yaw:0 (always face and fire up, never toward the mouse), move_axes:[0,1,0] (left/right only),
  camera_follow:false, camera_pitch:90, camera_distance:6700, camera_lead:1420, fov:50}
This frames X = -250..3100. If the top row is cut off, move rows down or raise camera_distance.

## 2. Space and sprites
- Floor: one huge box, top at z=0, near-black plaster (the ship slides on it).
- Side rails at Y = +-1570: tall thin boxes, dim blue emissive.
- Starfield: two build.create_assembly sheets "stars_0"/"stars_1" with ~140 small spheres each (10-18 cm, white/blue/
  yellow, emissive 1-3), the second moved 4100 further +X; rule on start tweens both {"move":[-4100,0,0],
  "duration":14,"ease":"linear","loop":"loop"} for endless scrolling.
  IMPORTANT: logic.set_solid {targets:["tag:enemy","stars_0","stars_1"], solid:false}; a moving solid carries the player.
- Sprites: flat extruded outlines read like pixel art from above. Part:
  {"shape":"extrude","profile":[[40,0],[20,14],[-50,14],[-60,0],[-50,-14],[20,-14]],"height":70,"location":[0,0,0],
   "material":"plaster","color":[1,0.15,0.15],"emissive":[1,0.15,0.15],"emissive_strength":0.9}
  Mirror every outline across y=0. Enemies face -X (nose toward the ship), the ship faces +X. Use 4-6 parts per sprite:
  body, wings, accent color, eyes (small 12-sided discs). Emissive 0.8-1.0 keeps colors saturated; more washes out.
  Ship: white fuselage, white wings, red wing tips, blue cockpit (~240 cm wide). Bee: yellow body discs + blue wings.
  Butterfly: white body + big red wings + blue accents. Boss: green disc body + purple wings + yellow horns (bigger).
- Build each enemy once as a template (tags ["enemy","bee"]) and make pools: logic.define_pool {name:"bee",
  template:"tpl_bee", size:24}. lighting.apply_mood {mood:"night"}.

## 3. Combat
- define_weapon "laser" {damage:1, rate:4, speed:4200, lifetime:0.9, hits:"tag:enemy", bullet:{size:26, color:[0.5,0.9,1]}}
  equip player. "enemy_shot" {rate:10, speed:1250, lifetime:3, hits:"player", bullet:{size:24, color:[1,0.25,0.2]}} equip tag:enemy.
- define_health player {max:1, invulnerable:2, on_death:"none"}; per kind {max:1 or 2 for bosses, on_death:"release"}.
- define_hitzone {targets:["tag:enemy"], damage:1, affects:"player", mode:"enter"} (diving into the ship kills it).

## 4. Variables
slots: list of {kind,x,y} (4 bosses / 8+8 butterflies / 10+10 bees, 230 cm apart), ids:[], diving:[], cands:[],
n_diving:0, alive_n:0, score:0, hi:20000, stage:1, lives:3, over:false, banner:"", banner_until:0.

## 5. Rules (exact shapes that work)
- start: [{"load":"galaga"}, {"emit":"stage"}] plus the star scroll tweens.
- stage (on event "stage"): reset ids/diving/n_diving/alive_n, {"cancel_timer":"dive"}, banner "STAGE n", then
  {"for":"k","in":"range(len(slots))","do":[{"let":"f","value":"slots[k]"},
    {"let":"entry","value":"[3700, (k % 2 == 0 ? -1 : 1) * 2300, 60]"},
    {"if":"f.kind == 'bee'","then":[{"spawn":"bee","at":"entry","as":"p"}, {"push":"ids","value":"p.id"},
      {"set":"alive_n","value":"alive_n + 1"},
      {"tween":"p.id","location":"=[f.x, f.y, 60]","duration":1.3,"delay":"1.4 + k * 0.05","ease":"out_cubic"}]},
    ...same for each kind (spawn takes a literal pool name, so branch on the kind)]},
  {"timer":"sway","delay":"5"}, {"timer":"attack","delay":"5.5"}.
- attack (timer): {"timer":"dive","delay":"max(0.55, 2.0 - stage * 0.18)","repeat":true}. Start diving only after the
  entry: a new tween on a node REPLACES its running tween, so an entry tween would cancel a dive.
- sway (timer): for e in ids, if alive(e) && !contains(diving, e): tween e {"move":[0,140,0],"duration":2,
  "loop":"pingpong","ease":"in_out_sine"}.
- dive (timer, condition "!over && n_diving < min(1 + stage, 5)"): collect free alive ids into cands, pick one, push to
  diving, n_diving+1, then a loop-out tween that chains with emit:
  {"tween":"e","move":"=[220, q[1] > 0 ? 260 : -260, 0]","rotate":[0,360,0],"duration":0.6,"ease":"out_sine","emit":"dive2"}
- dive2 (event, condition "alive(event.target)"): {"let":"p","value":"pos('player')"}, {"fire":"event.target",
  "at":"pos('player')"}, tween event.target {"location":"=[-600, p[1] + rand_int(-280, 280), 60]",
  "duration":"=max(1.0, 1.9 - stage * 0.1)","ease":"in_sine","emit":"dive3"}.
- dive3: let f = slots[index_of(ids, event.target)]; tween {"location":"=[3700, f.y, 60]","duration":0.02,"emit":"dive4"}.
- dive4: tween {"location":"=[f.x, f.y, 60]","duration":1.1,"ease":"out_cubic","emit":"home"}.
- home: remove from diving ({"set_item":"diving","index":"index_of(diving, event.target)","value":"''"}),
  n_diving-1, restart the sway tween.
- Scoring: on died target "tag:bee" etc. (BEFORE the bookkeeping rule): score + (contains(diving, event.target) ? 100 : 50).
  Bosses: on damaged tag:boss with event.health > 0 -> tween color to show the hit.
- enemy_down (died tag:enemy): small shake feedback, drop from diving, alive_n-1; when 0 -> cancel dive timer and
  {"timer":"next_stage","delay":"2.5"} -> stage+1, emit stage.
- hi: var_changed score with score > hi -> hi = score.
- ship_down (died player): explosion feedback, lives-1, {"player":"teleport","value":"[-4000,0,40]"} (park off screen);
  lives > 0 -> timer respawn 1.6 s -> {"player":"respawn"} + {"health":"'player'","max":"1","value":"1"} + banner READY;
  else over = true, cancel dive, {"player":"freeze"}, {"save":"galaga","vars":["hi"]}.
- R key and a "Play again" button: {"restart":true,"keep":["hi"]}.

## 6. HUD (layout UI, see the game_ui skill)
Left column (anchor "left"): big two-color title (size 40, weight black), controls in muted text.
Right column (anchor "right"): label/value pairs HIGH SCORE (red) / {hi}, 1UP / {score}, STAGE / {stage}, SHIPS with a
row of tiny ship icons: a child with "repeat":"max(0, lives)". Center banner "{banner}" with visible_if
"time < banner_until && !over". Game-over panel (visible_if over) with score, high score and a primary "Play again" button.
Theme: {"colors":{"panel":"#05060CE6","accent":"#FF3B30","accent_text":"#FFFFFF"},"text_shadow":false}.

## Gotchas (each cost a broken build before)
- delay fields of tween and timer are plain expressions ("1.4 + k * 0.05"); other tween fields use "=expr".
- rand() takes no arguments; use rand_int(a, b) or a + rand() * (b - a).
- logic.tween (the tool) takes targets:[...]; the rule action is {"tween":"<expr>", ...}.
- A dive chain stalls if any step errors: read logic.get_state log errors after every change.
- Verify by playing: input.inject_key {key:"F", duration:2} fires; A/D move; then logic.get_state (score, alive_n,
  n_diving, lives) and a screenshot showing a diver below the formation.
)SKILL");
}

URuntimeGameUiSkill::URuntimeGameUiSkill()
{
	Description = TEXT("Apply when making any HUD, menu, inventory, shop, scoreboard or dialog: layout UI that looks like a polished web/game UI.");
	Toolsets = {TEXT("logic")};
	Instructions = TEXT(R"SKILL(# Game UI with layout items

Use layout items in logic.define_ui (kinds panel, row, column, grid, stack, text, button, bar, spacer, divider). They
lay out and align themselves like HTML + flexbox; never position many separate items by anchor fractions.

## Structure
- One top-level item per screen area, placed with anchor: "top-left", "top", "top-right", "left", "center", "right",
  "bottom-left", "bottom", "bottom-right" (an edge margin is added for you; offset:[x,y] nudges).
- panel = card (theme background, border, radius, padding). Inside: rows and columns with gap; grow:1 on the child that
  should take the space; {"kind":"spacer","grow":1} pushes the rest to the far end; divider separates sections.
- Lists and slots: one child with "repeat": 12 (or "len(bag)") and "as": "i"; text and conditions use i
  (bag[i]); a repeated button fires one rule (filter = its name) that reads event.i.
- grid {columns:4, gap:8} for inventories; give slots width/height so the grid is even.

## Style
- Theme once: define_ui {theme:{colors:{accent:"#E8B04B", panel:"#11141CF0", ...}, radius:12, padding:18, font_size:15}}.
  Color tokens: text, muted, subtle, accent, accent_text, panel, surface, surface_hover, border, success, warning,
  danger, info, gold; "panel/60" = 60% opacity; add your own tokens (rarities, teams).
- Hierarchy: section labels size "xs" weight bold color muted in caps; values weight bold; titles size "xl"; numbers
  of a pair right-aligned with a spacer between label and value.
- Buttons: variant primary for the main action, ghost for secondary/close, danger for destructive; disabled_if instead
  of hiding buttons the player cannot use yet (prices they cannot afford).
- Live state: style_if [{if, background, border_color, color}] (selected tab, rarity borders, low health); bound
  colors "{db[id].hex}"; tooltip templates for details ("{tips[bag[i]]}"; empty text = no tooltip).
- Icons without image assets: draw them from small shapes (columns with width/height/radius/background, rotate:45 for
  gems and swords) inside a 40x40 stack.
- Keep text short so it never wraps inside small slots; hide names in small hotbar slots, show counts as a corner badge.

## Check
Take capture.screenshot {show_ui:true, max_dim:1600} for every screen state (open each window, fill the inventory,
trigger level-up) and look for overlapping text, clipped names, unreadable disabled buttons and misaligned rows.
)SKILL");
}

URuntimeRealisticSceneSkill::URuntimeRealisticSceneSkill()
{
	Description = TEXT("Apply when the scene should look real (cities, streets, houses, interiors, landscapes, product shots): real scale, "
		"physical light and exposure, believable materials, detail and wear, eye-level cameras, and a screenshot review loop.");
	Toolsets = {TEXT("build"), TEXT("lighting"), TEXT("style"), TEXT("camera"), TEXT("capture"), TEXT("materials")};
	Instructions = TEXT(R"SKILL(# Realistic scenes

Realism comes from many small truths at once. Missing any one of these reads as "CG": wrong scale, flat light,
pure colours, perfectly sharp edges, perfectly clean surfaces, empty ground, a camera nobody would stand at.

## Fastest path: the Python kit
session_get_build_info -> scripting.kit. With it (see the python_kit skill):
    import unreal_kit as uk; from unreal_kit import world as w, look
    look.mood("Morning"); look.style("realistic", weathering=0.3)
    w.city(blocks_x=3, blocks_y=2, seed=4)              # streets, curbs, props, varied buildings, ground (about 30 s)
    uk.place("Shop", w.building(1600, 1200, 4, "brick", shop=True), at=[x, y, 0])
    look.review("shots", targets="City")                # then LOOK at the images and fix
The generators already follow every rule below. Without the kit, follow the rules by hand.

## Effort levels (ask or pick by the request)
- quick: blockout with correct scale and light, one review pass.
- balanced (default): full facade detail, street furniture, two review passes.
- detailed: plus interiors behind windows, unique corner buildings, vegetation variety, three passes, detail shots.

## 1 Scale (centimetres)
storey 300-330 (ground floor shop 400-450) | door 210-240 x 90-120 | window 120-160 high, sill 90 above the floor
person 170 | counter/table 75-90 | chair seat 45 | stair step 17 x 28 | railing 100-110 | curb 15
lane 300-350 | sidewalk 200-400 | street lamp 600-900, every 2500 | street tree 600-1100 | car 450 x 180 x 145
Check with build.measure; a wrong door or storey height ruins everything else.

## 2 Light (lighting.apply_mood sets all of this; do not fight it)
Noon 100000 lux sun (EV 14.6) | Morning 40000 | Sunset 9000, low warm sun | Overcast 9000 soft | Night 0.6 + lamps.
Exposure is locked to the mood. Change brightness only with apply_mood / set_sun intensity, NEVER exposure_bias
and never 90000 lux with auto exposure (white image). Low sun angles (morning/sunset) show form best.

## 3 Materials (sRGB colour of the real thing; never pure white or black)
white paint 220-230 | concrete 150-170 | asphalt 50-60 | red brick 140,87,70 | limestone 200,190,168 | grass 88,104,54
wood 96,66,44 | dark metal 50 | chrome/steel metallic 1, roughness 0.2-0.5 | glass: tinted_glass preset
Roughness: polished 0.1-0.2, painted 0.4-0.6, concrete/plaster 0.8-0.95. Vary colour 5-10% between buildings.
style.apply material {Weathering: 0.2-0.4}: dirt at the wall base, rain streaks, dust on tops. Per part "weathering".

## 4 Geometry and detail
- Boxes get small bevels automatically (edges catch light). Keep it on.
- Walls have thickness (25-40). Windows are RECESSED 15-20 with frame, sill and lintel; doors recessed with a step.
- Facades: plinth at the base, string course between floors, cornice at the top, downspouts at corners.
- Roofs: parapet + rooftop units / stair head / water tank, or a pitched roof with overhang.
- Nothing floats, nothing intersects visibly (build.check_overlaps), everything sits on the ground.
- Ground is never empty: curbs, markings, lamps, trees with pits, benches, bins, bollards, parked cars, grass edges.
- Repetition kills realism: vary width, height, colour, window type and roof between neighbours.

## 5 Camera
People see the world from 160-170 cm with a 60-75 degree FOV. Shoot hero images at eye level, slightly
off-axis, with something in the foreground. High aerials show layout but look like a model. No tilted horizons.
Do not put the camera inside geometry (look at the image: a wall filling the frame means move it).

## 6 Review loop (mandatory)
After each big step capture: overview, two eye-level views, one detail (look.review or capture.scene_capture).
Look for: floating/intersecting parts, wrong scale next to a door or car, flat or blown-out light, pure colours,
empty ground, repeated buildings, missing roofs or backs, gaps at corners. Fix, capture again. Report what you
checked and what you fixed; do not say "done" from code alone.

## 7 Budget
A district of 6 blocks is about 50000 parts / 2.3M triangles and builds in about 30 s. Use detail "low" on small
round parts, one assembly per building (not one actor per part), and ops.run_batch / unreal_kit.place_many.
)SKILL");
}

URuntimePythonKitSkill::URuntimePythonKitSkill()
{
	Description = TEXT("Apply when a task needs many objects or many steps (levels, cities, games with lots of rules): use the unreal_kit "
		"Python package instead of single tool calls.");
	Toolsets = {TEXT("session"), TEXT("ops")};
	Instructions = TEXT(R"SKILL(# unreal_kit (Python)

One script replaces hundreds of tool calls, and the generators carry tested know-how.

## Get it
session_get_build_info -> scripting: "kit" is a URL (relay) or a file path (local game), "kit_how" says how.
    curl -A agent -o unreal_kit.zip <kit>          (any User-Agent; Python's default is blocked by the relay)
    import sys; sys.path.insert(0, "unreal_kit.zip"); import unreal_kit as uk
Connection is automatic: UNREAL_RELAY + UNREAL_TOKEN (set by Connect.bat) or the local game
(UNREAL_LOCAL_MCP, default http://127.0.0.1:8018/mcp). uk.connect(url=...) to choose.

## Every tool
uk.tools.<toolset>.<tool>(**args): uk.tools.lighting.apply_mood(mood="Noon"). help(uk.tools.logic) lists tools and
arguments. Optional arguments left out use the tool's default. Errors raise uk.ToolError with message and hint.
uk.call("build.create_primitive", shape="box") also works; uk.batch([...]) runs up to 100 calls per request;
uk.save_image(path) writes the image of the last capture call.

## Generators (help(module) for all of them)
world: building(width, depth, floors, style=brick|plaster|stone|office|glass, roof=flat|gable|hip, shop), facade,
       window, door, shopfront, street(length), plaza, tree, lamp_post, bench, car, city_block, city(blocks_x, blocks_y).
       They return part lists: uk.place(name, parts, at) builds one object (500+ parts are split under a group).
game:  side_player / topdown_player / third_person_player, climbable, score_and_lives, lose_life, win, pickups,
       hazard, goal, patrol, every(seconds, actions), rule/set_/add/if_/emit/timer/tween... (rule data), start, state, errors.
ui:    hud, banner, panel_message, menu, inventory, panel/row/column/grid/text/button/bar, THEMES, define.
combat: feel_presets, player_health, health, damage_zone, blaster/shotgun/rifle/missiles/enemy_gun, guard_brain,
       chaser_brain, turret_brain, coward_brain, enemies, waves, interactable.
look:  mood, style(preset, weathering), fog, eye_level, orbit_view, frame, screenshot, capture_from, review.

## Pattern
1. Write the whole scene/game as one script with functions and a seed; run it; read the printed results.
2. look.review(...) or screenshots -> look at them -> change parameters -> run again (scripts are cheap to rerun:
   start with tools.scene.clear_all(confirm=True) or build into a group and delete it).
3. Extend instead of fighting: copy a generator into your script and change it (they are plain Python).
)SKILL");
}
