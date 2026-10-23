#include "GameRecipeSkills.h"
#include "Generated/KitCheatSheet.gen.inl"

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
	Instructions = FString(TEXT(R"SKILL(# Realistic scenes

Realism comes from many small truths at once. Missing any one of these reads as "CG": wrong scale, flat light,
pure colours, perfectly sharp edges, perfectly clean surfaces, empty ground, a camera nobody would stand at.

## Specific, not generic
A named place or thing must be recognisable: list its signature features first (layout, building types, materials,
roofs, landmarks, street life) and build those; a generic city with a new name is a failure. See skill python_kit.

## Fastest path: one call
Cities, towns, streets: uk.district(preset or styles=...) builds a styled district in one call (skill python_kit, FASTEST).
Then add what makes the place unique (landmarks, water, parks) with uk.parts and uk.buildings.
For a custom generator, the example script kit_examples/city.py already
follows every rule below (buildings with recessed windows, cornices, rooftop clutter, streets with curbs and props,
whole districts): copy and adapt it instead of starting from zero.
For close-up, detailed buildings use uk.buildings (from unreal_kit import buildings as B): B.building(kind, seed=,
width=, floors=) gives a finished building with real windows (reveals, sashes, sills, heads, shutters, glass with rooms
behind), railings, balconies, doors and shopfronts; kinds apartment, townhouse, shophouse, villa, office, loft.
shape="L" makes a corner building (corner=square|chamfer|tower|balconies); wing A runs +X facing -Y, wing B +Y facing -X.
Rect buildings: origin = front centre at ground, front faces -Y. Place many with uk.place_many([(name, parts, (x,y,z)), ...]);
rotate with uk.parts.transform(parts, (0,0,0), yaw). Change seeds per lot so neighbours differ.
Massing (the building's overall form) first: B.building(kind, seed=, massing="auto" | podium_tower | twin_towers |
setback | terraced | slab | courtyard | U | L | T | H | stacked | notched | stepped, width=lot width, depth=lot depth,
party=("-x","+x") for row buildings, street=("-y",) or ("-y","-x") on corners). Volumes share one storey grid; every
visible face gets a facade, step-backs become terraces with railings. Mix massings along a street for a real skyline.
Style is what makes buildings differ up close: B.building(kind, seed=, style="auto" | haussmann | warehouse | brutalist |
modernist | glass | art_deco | mediterranean | nordic | contemporary | victorian | straits). Each style sets materials, window
system (punched, ribbon, curtain wall, pier-and-spandrel frame, inset loggias), cladding, base (shops, arcade, pilotis,
glass lobby), crown and roof (mansard with dormers, steep gable, sawtooth, overhang, roof-terrace pergola). Keep one or two
styles per street (a real place has a character) and vary seeds and widths within it; party=("-x","+x") for terraces. Then save finished pieces with
scene.save_template so later scenes reuse them (scene.place_template).
)SKILL")) + FString(TEXT(R"SKILL(
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

## 4a Ground: real terrain, not a flat floor
Outdoor scenes start with build.create_terrain {size:[x,y], height (relief cm), feature_size, ridged, biome
(temperate|tropical|desert|savanna|alpine|arctic), flatten:[{at,radius}] pads for villages and fields, carve:[{path,
width, depth}] rivers (always downhill; the result's rivers[] gives the water surface) or {kind:"road"}}. Its collision
is the exact surface, so pcg.scatter, snap_to_ground and characters follow the slopes. Areas for scatter are polygons
with straight edges (uk.assets.forest does this); a closed exclusion spline keeps its whole inside clear.

## 4b Nature: real assets, never blobs
Trees, palms, bushes, ferns, grass and rocks are real meshes packaged with the game, all with scanned bark and leaf
textures. Generated in Blender (3 variants each, real heights): aspen, birch, maple, japanese_maple, oak, pine, fir,
willow, bottlebrush, palm_coconut, palm_date, bush, boxwood, flower_bush, fern, grass, tall_grass, wildflowers.
Scanned models (Poly Haven): island_tree (3), jacaranda_tree, nettle_plant, pachira_aquatica, grass_medium (2),
grass_bermuda, fern, dandelion, celandine, periwinkle_plant, boulder, rock - best for close-up hero spots. level.find_assets {asset_class:"StaticMesh",
folder_path:"/Game/RuntimeFoliage"} lists them. One: build.place_mesh {asset}. Many: pcg.scatter with items
[{asset, weight, scale:[0.85,1.15]}] inside a closed spline (forest, lawn) or along a spline with offset (street
trees, palm avenue). Python: uk.assets.forest / along / ground_cover / place. Mix 2-4 species that fit the place
(palms for tropics/Dubai, oak+maple+birch for Europe, fir+pine for mountains); add grass/ferns under trees.
Wind (live, all foliage): uk.assets.wind(strength, speed, direction, gustiness, sway, flutter) or materials.set_collection_param
{collection:"/Game/RuntimeFoliage/Materials/MPC_Wind.MPC_Wind", name: WindStrength|WindSpeed|Gustiness|SwayAmount|LeafFlutter|WindDirection}.
Default 0.4 = light breeze; 0.15 calm; 1 windy; 2-3 storm. Match the mood (still sunset, breezy coast, stormy overcast).

## 4c Architecture: scanned surfaces, modules and props
Material presets brick, plaster, concrete, marble, wood, paving, roof_tile (and styles blocks/planks/roof_tiles) use
scanned photo textures automatically, tinted by the part colour; "scanned": false keeps the procedural look.
Real modules in /Game/RuntimeArchitecture/Modules (build.place_mesh {asset}, or uk.assets.place): door_panel,
door_painted, door_double, window_sash, window_sash_small, window_shutters, window_tall, window_wide, balcony,
cornice_2m, string_course_2m, pediment - pivot on the wall plane at the bottom centre, pointing -Y, so place them at the
wall's front face rotated to its facing; column_tuscan, column_doric, balustrade_2m, railing_iron_2m, steps_entrance,
chimney, dormer stand free. Props in /Game/RuntimeArchitecture/Props: street_lamp, fire_hydrant, metal_trash_can,
painted_wooden_bench, planter_box, concrete_road_barrier, wooden_crate and more. Use them for hero and close-up
buildings; keep the procedural builders for distant blocks.

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
)SKILL"));
}

URuntimePythonKitSkill::URuntimePythonKitSkill()
{
	Description = TEXT("Apply when a task needs many objects or many steps (levels, cities, landmarks, games with lots of rules) and you can run "
		"Python: use the unreal_kit shipped with the game to generate fast. Includes the full API, so no inspection is needed.");
	Toolsets = {TEXT("session"), TEXT("ops"), TEXT("build"), TEXT("scene")};
	Instructions = FString(TEXT(R"SKILL(# unreal_kit: fast generation with Python

## FASTEST (answer in under a minute: do this first, read the rest only if you need more)
RULE: anything over ~20 parts or repeated is ONE Python script run with Bash, never hand-written JSON parts.
NEVER save memories or notes about how to use this game: this guidance changes with every build and wins over them.
Kit: game on this machine -> session_get_build_info scripting.kit; game in the cloud (stream) -> the path in the
UNREAL_KIT environment variable (Connect.bat sets it). Never search the disk.
    import sys, os; sys.path.insert(0, os.environ.get("UNREAL_KIT") or r"<scripting.kit>"); import unreal_kit as uk
    uk.district("manhattan")            # whole district in one call (~10-30 s); presets: manhattan paris london downtown
                                        # mediterranean scandinavian singapore soviet industrial; blocks=(3, 2); seed=None
    uk.district(styles={"nordic": 3, "contemporary": 1}, block=(4000, 3000))   # any other place: pick a style mix
    from unreal_kit import buildings as B; uk.place("Hero", B.building("office", style="art_deco"), at=(0, 0, 0))
    uk.gis.create(size=(40000, 30000), layout="blocks")      # land plots (any polygon) with streets, zoning, id labels
    uk.gis.load_geojson(path) / uk.gis.load_georegions(path)   # plots from GeoJSON or GeoRegions files
    uk.gis.build("P12", style="modernist"); uk.gis.build_all(zoning="residential", limit=200)   # buildings fitted to plots
Every run is new (random seed, printed); pass seed=N to repeat one. Big scenes: detail="low"; close-ups: "high".
Write one script with real detail (loops: a landmark is 500-3000 parts, not 20 boxes), run it, look at one screenshot, fix what looks wrong, answer. Do not read example files for a city.

Why scripts: one short script builds thousands of parts with loops and sends them in a few requests.
It connects by itself (this machine, or the relay from UNREAL_RELAY + UNREAL_TOKEN). Do not download code; do not print
environment variables.
Nature (trees, palms, bushes, grass): uk.assets uses real packaged meshes - never build plants from spheres.
Everything you need is in the cheat sheet below: do not inspect modules or read example files unless you must.

## Make it SPECIFIC, never a clone of the example
1. Before code, write down 5-10 signature features of what was asked (real references, not generic):
   layout (grid, radial, organic, waterfront), building types and their proportions, materials and colours,
   roofs and skyline, landmarks, street furniture, vegetation, signage, vehicles, light/mood.
   New York: long thin blocks on a grid, brownstone rows with stoops, setback art-deco towers with spires, water
   tanks, yellow cabs, wide avenues. Paris: Haussmann 6-storey cream stone, mansard grey roofs, iron balconies,
   boulevards with trees, cafe awnings. Tokyo: narrow mixed-height buildings, signs, wires, no front yards.
2. Encode each feature as a small function or parameter and plug it into the example hooks:
   city.add_style(...), city.building(style=dict|name, bay, win_w, win_h, setbacks, crown, roof),
   city.city(block_w, block_d, street_w, make_building=..., make_street=..., extras=...), city_block(lots=...).
3. Landmarks and anything unique: your own parts with uk.parts (box, cyl, extrude, sweep, beam, grid, ring).
4. Never deliver the example's defaults unchanged (same style mix, same blocks, same props): that is the clone.
5. Vary with seeds and ranges so neighbours differ, but keep the place's rules consistent.

## Pattern
1. scene.list_templates: reuse what exists (scene.place_template).
2. One script: features -> hooks -> uk.place_many / city.city(...). Print counts and ids.
3. Look: uk.look.review(...) or capture.scene_capture at eye level -> fix -> rerun (clear first).
4. scene.save_template the finished unique pieces (landmarks, building types) with clear descriptions.
Materials: a preset (PRESETS) or a palette name (PALETTE); uk.check_parts / place_many reject typos before building.
)SKILL")) + KitCheatSheet;
}
