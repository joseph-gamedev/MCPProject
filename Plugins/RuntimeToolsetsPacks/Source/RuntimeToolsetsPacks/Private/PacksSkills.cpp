#include "PacksSkills.h"

URuntimeWaterFluidsSkill::URuntimeWaterFluidsSkill()
{
	Description = TEXT("Apply for water, seas, beaches, lakes, rivers, waves, floating objects, and fluid effects (fire, smoke, mist, mud, blood, lava, snow / sand tracks).");
	Toolsets = {TEXT("fluid"), TEXT("ops"), TEXT("character"), TEXT("build"), TEXT("sky")};
	Instructions = TEXT(R"SKILL(# Water, fluids and beaches

All of it is real-time fluid simulation (FluidNinja LIVE). fluid.list shows what this build has.

## A beach, quickly
- **The pack's demo beach, complete:** `ops.run_recipe {"name": "sea_coast"}` (one call, a few seconds): sculpted sand coast,
  breaking waves with whitewater, wet sand that keeps footprints, rocks, wrecks, floating barrels, midday sun and sky, and a
  third-person character the water travels with. Skip parts with `skip: ["props"]`, `["player"]` or `["lighting"]`, then change
  anything with the usual tools (it is ordinary nodes).
- **Your own coast on generated terrain:**
  1. `build.create_terrain` with land that slopes below sea level, e.g. location z -700, biome desert, a hill
     `{at:[-9000,0], radius:10500, height:1000}` (sea level 0 crosses its slope).
  2. `character.third_person` on the land (the water follows a player).
  3. `fluid.add {"preset": "coast_water", "location": [x, y, 0], "follow_player": true}`: **location z is the sea level**.
     The water reads the ground's height by itself (shallows, foam lines, waves on the shore), on any terrain or meshes.
  4. Footprints in sand: `sand_trails` is a flat square (default 100 m; size_meters 20-40 for a beach): put it on a level
     pad above the waterline (`build.create_terrain` flatten `{at, radius, blend, height}`) at the pad's height. coast_sand
     only works on the recipe's coast ground.
  Ground height anywhere: `physics.line_trace` straight down (start z high, end z low) returns the hit z; use it to find
  where the slope crosses sea level and where to put things.
  5. `sky.setup` for sun and weather; `fluid.float` for anything that should bob on the water.

## Lakes, pools, rivers, seas without a shore
`fluid.add` presets lake / pool / sea / sea_wavy / river...: the surface is a tiled plane at location z, so put it in a basin
(`build.create_terrain` hills with negative height) or it disappears under the ground.

## Effects (fluid.add preset = effect name)
bonfire, fire, candle, flamethrower, magic, smoke_column, heavy_smoke, explosion, mist (fog bank; turns on volumetric fog),
mud, blood, lava, snow_trails, sand_trails, snow_drift, sandstorm. Flat ones (mud, blood, lava, trails) lie on the ground at
location; fire and smoke stand on it. View fire and smoke from within ~20 m. `fluid.add_source` feeds or stirs a simulation
(hoses, jets, vortex, bursts); `fluid.ambient` adds weather particles (blizzard, sandstorm, rain, sparks).

## What disturbs a fluid
Characters (character.third_person has a real skeleton), vehicles and anything that generates overlap events moving through
its area. Builder meshes do not by default. The fluid does not push objects back, except `fluid.float` objects bobbing on it.

## Floating things
`fluid.float {"targets": [...]}` turns physics on and keeps them at the water surface (the nearest water's sea level, or
`water_level`): `buoyancy` 1.2 rides low, 3 rides high; `drag` 0.3 drifts, 3 settles. Good floaters: the pack's
`/Game/FluidNinjaLive/Meshes/Wreck/wreck4_Mesh01` (a barrel-sized tank, what the recipe floats), crates and cylinders made
with build.create_primitive. Remove a simulation with `fluid.remove {"id": ...}`; `scene.clear_all` also unloads a recipe's ground.

## Tuning
`fluid.get_settings` lists a simulation's behaviour (SimSpeed: lower = thicker and slower; BrushStrength / SkeletalMeshBrushSize:
how hard and wide things push; DensityAccumulation: how fast it fills back in; VelocityFeedback: how long motion carries on)
and its surface material's numbers and colours; `fluid.set_settings` changes them live. Mud that looks too liquid: SimSpeed 2-3.

## Pack props
Rocks, boulders, ship wrecks and planes are under /Game/FluidNinjaLive/Meshes (`level.find_assets`); place them with
`build.place_mesh`. Rocks tagged `SDFSource` make water flow and foam around them.
)SKILL");
}

URuntimeProjectAssetsSkill::URuntimeProjectAssetsSkill()
{
	Description = TEXT("Apply when placing trees, plants, grass, rocks, wind, or real building modules and props (doors, windows, columns, street furniture).");
	Toolsets = {TEXT("build"), TEXT("pcg"), TEXT("materials"), TEXT("level")};
	Instructions = TEXT(R"SKILL(# Real nature and architecture assets

## Nature: real assets, never blobs
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

## Architecture: scanned surfaces, modules and props
Material presets brick, plaster, concrete, marble, wood, paving, roof_tile (and styles blocks/planks/roof_tiles) use
scanned photo textures automatically, tinted by the part colour; "scanned": false keeps the procedural look.
Real modules in /Game/RuntimeArchitecture/Modules (build.place_mesh {asset}, or uk.assets.place): door_panel,
door_painted, door_double, window_sash, window_sash_small, window_shutters, window_tall, window_wide, balcony,
cornice_2m, string_course_2m, pediment - pivot on the wall plane at the bottom centre, pointing -Y, so place them at the
wall's front face rotated to its facing; column_tuscan, column_doric, balustrade_2m, railing_iron_2m, steps_entrance,
chimney, dormer stand free. Props in /Game/RuntimeArchitecture/Props: street_lamp, fire_hydrant, metal_trash_can,
painted_wooden_bench, planter_box, concrete_road_barrier, wooden_crate and more. Use them for hero and close-up
buildings; keep the procedural builders for distant blocks.
)SKILL");
}
