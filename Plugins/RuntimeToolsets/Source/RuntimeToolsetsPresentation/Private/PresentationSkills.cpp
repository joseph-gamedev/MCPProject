#include "PresentationSkills.h"
#include "RuntimeStyleToolset.h"

URuntimeOutdoorLightingSkill::URuntimeOutdoorLightingSkill()
{
	Description = TEXT("Apply whenever setting up or changing outdoor lighting: sun, sky, clouds, fog, exposure, time of day or weather.");
	Toolsets = {TEXT("lighting"), TEXT("reflection"), TEXT("capture"), TEXT("style")};
	Instructions = TEXT(R"SKILL(# Outdoor lighting

Unreal's outdoor light is a system of six parts. The **Directional Light (sun)** drives all of them: change it first.

| Part | Role |
|---|---|
| Directional Light (sun) | Direction and intensity of direct light and shadows. Sky, atmosphere and clouds need it to have intensity. |
| Sky Atmosphere | Sky colour and aerial perspective, driven by the sun's angle. Sun intensity 0 = black sky. |
| Sky Light | Ambient fill captured from the sky; keeps shadows from going black. |
| Volumetric Cloud | Clouds lit by the sun. Sun intensity 0 = no visible clouds. |
| Exponential Height Fog | Local fog near the ground; depth at short and medium range. Global haze is the atmosphere's job. |
| Post Process | Exposure, white balance, grading. Not part of the sky, but decides the final look. |

## Sun orientation
- Pitch is elevation: 0 = on the horizon, -90 = overhead. Sunset/sunrise: -2 to -10. Morning or afternoon: -25 to -45.
- Yaw is compass direction: 0 = North, 180 = South.
- lighting.set_time_of_day maps an hour to a sun angle; lighting.set_sun sets rotation, intensity, temperature and shadow
  softness (source_angle, soft_source_angle) directly.

## Tools first, then components
lighting.get_environment reads every part at once: sun, sky light, atmosphere, clouds (with the cloud material's coverage and
density), fog and post process (with the exposure mode). The lighting tools cover the usual settings: lighting.set_sun,
lighting.set_sky, lighting.set_atmosphere (Rayleigh/Mie), lighting.set_clouds (coverage, density, altitude, layer height,
shadows), lighting.set_fog, lighting.set_post_process (exposure bias and auto exposure min/max). For anything else, read and
write properties on the actor's **component**, never on the actor: actors.list_components first, then
reflection.get_properties / reflection.set_properties on the component (e.g. SkyAtmosphereComponent, VolumetricCloudComponent,
DirectionalLightComponent). Spawn missing parts with actors.spawn_actor (class /Script/Engine.SkyAtmosphere,
/Script/Engine.VolumetricCloud, /Script/Engine.SkyLight, /Script/Engine.ExponentialHeightFog).

## Colour comes from the atmosphere
The Sky Atmosphere already turns warm as the sun drops. Do not push light temperature or white balance to fake a sunset:
it stacks and over-saturates. Keep light temperature at 5500-6500 K and nudge it by at most 500 K as a last step.

## Clouds before colour
Too much cloud coverage blankets the sky and is the usual cause of "no blue sky" or "everything orange". Check cloud
coverage (lighting.get_environment, clouds.coverage) before touching sky colour, light temperature, Rayleigh scattering or post process.
- Scattered clouds with visible sky: global coverage -0.2 to 0.0, density 0.007-0.010, bottom altitude 3-5 km, layer height 5-8 km.

## Brightness and exposure
Use physical intensities: direct sun 80,000-120,000 lux, hazy 25,000-50,000, overcast 5,000-15,000, late afternoon
1,000-6,000, sunset 300-1,000. Engine defaults (~10 lux) look flat.
Exposure bias depends on the exposure mode; read it first (lighting.get_environment, post_process.exposure_mode):
- Auto (min and max brightness set, e.g. lighting.set_post_process exposure_min/exposure_max): bias is a small offset, -2 to +2 EV, steps of 0.5.
- Manual or no limits: realistic lux needs a large negative bias: direct sun -10 to -14 EV, hazy -7 to -10, overcast -4 to -7,
  late afternoon -2 to -5, sunset -1 to -3.

## Sunset fog
Haze at sunset comes from the atmosphere. Keep height fog subtle (density 0.000002-0.00002, start distance 2000-5000 cm)
and never use it to fake horizon glow.

## Overcast recipe
1. Clouds low and thick (lighting.set_clouds): coverage 0.5-0.75, density 0.02-0.04, bottom altitude 0.5-2 km, layer height 8-14 km, cast cloud
   shadows on, sky light occlusion 0.75-0.9.
2. Soft sun (lighting.set_sun): source angle 2-5 degrees (15-30 when the sun disc is hidden), soft angle 40-70.
3. Atmosphere (lighting.set_atmosphere): Mie scattering scale 0.01-0.05; Rayleigh colour about (0.45, 0.56, 0.75); restore (0.175, 0.41, 1.0) for clear sky.
4. Sky light intensity 1.0-1.5, height fog density 0.01-0.02, exposure bias from 0 and iterate.
lighting.apply_mood "overcast" is a quick starting point; refine with the values above.

## Screenshot loop (at most 3 cycles)
1. Baseline: capture.screenshot and read it; read the sun, sky, cloud and exposure values.
2. Apply the change, sun first.
3. Screenshot again and judge it against the request; say what you see.
4. Refine and repeat, three screenshots at most. If it still is not right, stop and report what is missing.
)SKILL");
}

URuntimeArtStyleSkill::URuntimeArtStyleSkill()
{
	Description = TEXT("Apply whenever the user asks for a look or art style (realistic, stylized, low poly, toon, retro, noir, painterly) or says a scene looks wrong.");
	Toolsets = {TEXT("style"), TEXT("lighting"), TEXT("build"), TEXT("pcg")};
	Instructions = TEXT(R"SKILL(# Art style

A style has three layers, and style.apply sets them together: material switches on every builder material (flat shading,
surface detail, colour steps, saturation, tint, rim light), a post-process pass (cel bands, ink outlines, painterly
filter, pixelation, posterize, grading) and the lighting mood.

## Presets
{Presets}

## What a style cannot change
Shading cannot fix shapes. Primitive geometry (sphere trees, box houses) reads as a toy in every style; it suits lowpoly,
toon and stylized. For **realistic**:
- Build with real proportions and detail: trims, window frames, roof overhangs, uneven ground, no perfectly flat planes.
- Prefer real assets when the project has them: PCG example graphs and meshes (pcg.list_graphs) instead of primitives.
- Muted, natural colours (saturation below 1, no pure greens); vary every material.
- Physical lighting (see the outdoor_lighting skill): sun at 25-45 degrees elevation, atmosphere, a few clouds, soft fog.
- Fill space: scatter rocks, bushes and grass with pcg.scatter; empty ground looks fake.

## Tuning
- Tweak without restarting: style.apply with only the layer you change, e.g. post {"Outline": 0.6} or material {"Saturation": 1.2}.
- Lowpoly needs low-detail geometry (detail "low" on curved parts) so the facets show.
- Toon reads best with bright flat colours and a sun high enough for clear light/shadow bands.
- Noir: low sun (long shadows), high contrast, black and white.
- Check with a screenshot after each change; judge silhouettes and value contrast first, colour second.
)SKILL");
}

FString URuntimeArtStyleSkill::GeneratePrompt(const FString& InitialInstructions) const
{
	// Live preset list (projects add presets in Config/RuntimeToolsets/StylePresets.json).
	FString List;
	for (const FRuntimeStylePresetInfo& P : URuntimeStyleToolset::ListPresets().Presets)
	{
		List += FString::Printf(TEXT("- **%s**: %s\n"), *P.Name, *P.Description);
	}
	return InitialInstructions.Replace(TEXT("{Presets}"), List.IsEmpty() ? TEXT("(see style.list_presets)") : *List);
}

URuntimeVfxPlacementSkill::URuntimeVfxPlacementSkill()
{
	Description = TEXT("Apply whenever placing or spawning a visual effect: smoke, fire, explosion, sparks, dust, magic, impacts, ambient particles.");
	Toolsets = {TEXT("vfx"), TEXT("scene"), TEXT("capture")};
	Instructions = TEXT(R"SKILL(# Placing visual effects (adapted from Epic's Niagara placement and fundamentals skills)

## Pick, do not invent
- Use an existing Niagara system. Ask for or confirm the asset when the user did not name one; do not place the first
  match blindly. /Niagara/DefaultAssets/DefaultSystem.DefaultSystem is only a placeholder (a fountain of sprites).
- If nothing fits, say so instead of faking it with an unrelated effect.

## Lasting or one-shot
- Lasting effects (fire, smoke, waterfalls, ambient dust) are scene nodes: vfx.spawn_system. They can be moved, grouped,
  tuned and undone.
- Bursts (explosions, impacts, muzzle flashes, sparks) are vfx.spawn_one_shot: they remove themselves when they finish.
  A looping system never finishes; the result says looping = true, then use vfx.spawn_system and vfx.deactivate instead.

## Placement
- Effects that sit on something (fire, smoke, puddles, dust) use snap_to_ground, or a location on the surface. Floating
  effects above the ground look wrong.
- 1 unit = 1 cm: check size against the real subject (a campfire is about 100 cm across, a torch flame 20-30 cm).
  Scale the node, or use the system's size user variable when it has one.
- Attach effects that must follow an object with parent (the node it belongs to).

## Tuning
- First contact: vfx.get_summary lists user variables (the system's external controls) with values, emitters, CPU/GPU and
  renderers. Only user variables can change at runtime: vfx.set_user_variable (numbers, true/false, [x,y,z], [r,g,b,a]).
- Hue shifts do nothing on a white base colour; set a coloured base first.
- Range requests ("10 to 20") need randomness inside the system; a single user value cannot express a range.
- Swap the effect on the same node with vfx.set_system (keeps position and tags).

## Check
Take a screenshot (capture.screenshot) after placing: is it visible, the right size, on the surface, and not hidden in
geometry? Too many live effects cost frame time: vfx.list_active shows what is running.
)SKILL");
}

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
