#include "PresentationSkills.h"
#include "RuntimeStyleToolset.h"

URuntimeOutdoorLightingSkill::URuntimeOutdoorLightingSkill()
{
	Description = TEXT("Apply whenever setting up or changing outdoor lighting: sun, sky, clouds, fog, exposure, time of day or weather.");
	Toolsets = {TEXT("lighting"), TEXT("reflection"), TEXT("capture"), TEXT("style")};
	Instructions = TEXT(R"SKILL(# Outdoor lighting

## An existing level (it came with its own lighting and camera)
Leave the level's lighting and the player's camera alone unless the player asks to change them: build into the scene as it is.
Look with capture.scene_capture (it never moves the player's view). If the look was changed, lighting.restore_level puts the
designed lighting back exactly (one undo step) and camera.reset_camera gives the view back. Projects can lock both
(bProtectLevel): then lighting, camera and style tools refuse, and only your own objects can be added and changed.

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


URuntimeBuildFromImageSkill::URuntimeBuildFromImageSkill()
{
	Description = TEXT("Apply whenever the player gives a reference image or photo (any subject: building, ruin, vehicle, prop, landscape) to rebuild in the game.");
	Toolsets = {TEXT("build"), TEXT("capture"), TEXT("scene"), TEXT("ops")};
	Instructions = TEXT(R"SKILL(# Build from a reference image

The method works for any subject. Accuracy comes from the loop (look, build, render from the same angle, compare, fix),
not from detail. Be quick: the fastest result that matches wins. Aim for 6-10 tool calls; 5 minutes is the hard ceiling
(stop there and report what still differs). Skip a compare round when the render already matches.

## 1. Read the image (write it down before building, a few short lines)
- Subject and its main volumes, largest first (body, wings, towers, roof, base / chassis, cabin, wheels ...).
- Proportions: width : depth : height of the whole, and of each volume relative to it. Repeats: how many floors, bays,
  columns, windows, steps, wheels per side. Count twice.
- Scale: use the player's number if given; else a known size in the picture (door 2.2 m, person 1.8 m, floor 3-3.5 m,
  car 4.5 m, step 17 cm). Everything else follows from the proportions.
- Shapes per volume: flat or pitched roof, dome, arch, cylinder, taper, curved profile (revolve / extrude / sweep).
- Colours and materials of 3-6 areas, sampled from the image (linear 0-1), and which builder material preset fits each.
- The camera: which side is seen (front, three-quarter), eye height (street level about 1.6 m, aerial), lens (phone
  photo about 65 degrees horizontal). Hidden sides: continue what is visible, simply; do not invent features.

## Small calls, batched (speed)
Never write one giant call: a long tool call takes minutes to write and nothing appears meanwhile. Keep every create call
small (one volume or one repeated feature, under about 40 parts), and send several of them together in one ops.run_batch.
Let generator shapes make the repeats from a few numbers instead of listing parts: openings (a wall with a grid of
windows: rows, cols, margin), column, steps, tiers, roof, arch, dome, lattice; build.array copies a part in a row, ring
or grid. Something visible should exist after the first batch, within a minute.

## 2. Mass (one batch)
If the scene is empty, the first call of the batch is a floor (box about 1.5x the footprint, 20 cm thick, neutral) and
the last is camera.frame_nodes on it, so the player sees work appear at once.
One ops.run_batch of small creates: every main volume at its true size and position, simple shapes, rough colours.
Then look twice:
- capture.scene_capture from the pose that matches the photo (location + look_at + fov, same aspect). Reuse the exact
  same pose every round so the comparison is fair.
- build.capture_view once (front / right / top): checks proportions the photo angle hides.

## 3. Compare and fix (at most 2 rounds)
Put the render next to the reference in your mind and list the 3 biggest differences, silhouette and proportion first
(too wide, roof too low, tower in the wrong place, wrong number of floors), never small details. Fix all 3 in one call
batch (scene.delete_node and rebuild the volume, or scene.move_node / scene.scale_node), render the same pose, repeat once.
Do not add detail while the silhouette is still wrong.

## 4. Detail (one or two batches)
Add what makes it recognisable, as small creates in ops.run_batch: openings (windows, doors, arches) as generator grids
matching the counted repeats, frames and bands, columns (one column + build.array), railings, trim, the signature
feature (spire, clock, dome lantern, spoiler). Recount the repeats on the render.

## 5. Finish
Set the sampled colours and materials (build.set_material), render the matched pose once more and show it. Report in a
few lines: what matches and what still differs (and what one more round would fix).

## Rules
- Geometry first, detail last; same camera pose every round.
- Many small calls in a batch, never one huge call and never one part per call.
- The image stays with you: tools never need it. Do not change the level's lighting or the player's camera to compare
  (scene_capture renders any pose without moving the player).
)SKILL");
}
