"""Turns FluidNinja LIVE demo-level setups into reusable effects for the fluid toolset (editor only, run after adding the pack):

    UnrealEditor-Cmd.exe MCPProject.uproject -run=pythonscript -script="<repo>/Scripts/fluid/harvest_fluid_effects.py" -unattended -nullrhi

For each effect: the demo simulation's settings are written into a preset (/Game/RuntimeFluid/Presets/FX_<effect>, FluidNinja's
own PresetWrite), and the actors working with it (particle sources feeding the simulation, the surfaces / volumes it
drives, sparks) are recorded relative to it. The manifest is written as C++ (RuntimeToolsetsPresentation/Private/Generated/
FluidEffects.gen.inl) so the packaged game has it; the presets are cooked from /Game/RuntimeFluid.
"""

import json
import math
import os

import unreal

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
OUT_INL = os.path.join(ROOT, "Plugins", "RuntimeToolsetsPacks", "Source", "RuntimeToolsetsPacks", "Private", "Generated", "FluidEffects.gen.inl")
OUT_JSON = os.path.join(ROOT, "Saved", "FluidEffects.json")
FOLDER = "/Game/RuntimeFluid/Presets"

# effect: (demo level, simulation label, what it is)
EFFECTS = {
    "bonfire": ("Fire_Smoke_Explosive", "BonfireSim2", "tall bonfire flames, camera-facing, fed by a bonfire source"),
    "fire": ("Fire_Smoke_Explosive", "Bonfire6", "small campfire flames"),
    "candle": ("Fire_Smoke_Explosive", "CandleFlame1", "candle flame"),
    "smoke_column": ("Fire_Smoke_Explosive", "SmokeColumn1_Heavy_Up", "rising volumetric smoke column"),
    "heavy_smoke": ("Fire_Smoke_Explosive", "HeavySmoke2", "thick billowing smoke"),
    "explosion": ("Fire_Smoke_Explosive", "ExplosiveSmokeAirborne2", "explosion blast cloud"),
    "flamethrower": ("Fire_Smoke_Explosive", "TurretFlameThrower", "flamethrower jet"),
    "magic": ("Fire_Smoke_Explosive", "MagicCauldronSim", "magic cauldron vapour"),
    "fog": ("Fog_Mist", "Ninja SIM 40m - FVOL 200m", "low-lying fog that swirls around things moving through it"),
    "mist": ("Fog_Mist", "Ninja SIM 32m - HVOL 32m - FVOL 20", "volumetric mist bank"),
    "mud": ("Viscous_Blood_Mud", "NinjaLive_Mud1", "viscous mud that flows and is pushed around"),
    "blood": ("Viscous_Blood_Mud", "NinjaLive_Blood1", "viscous blood pool"),
    "snow_trails": ("Snow", "Ninja PAINT 100m - FVOL 200m", "snow surface that keeps footprints and tracks"),
    "snow_drift": ("Snow", "Ninja SIM 50m - SVOL 50m - FVOL 20", "drifting blowing snow"),
    "sand_trails": ("Sand", "Ninja PAINT 100m - FVOL 200m", "sand surface that keeps footprints and tracks"),
    "sandstorm": ("Sand", "Ninja SIM 50m - SVOL 50m - FVOL 20", "drifting blowing sand"),
    "lava": ("Lava_Sparse", "NinjaLive_PawnTrail", "lava field that flows around anything crossing it"),
    "lava_smoke": ("Lava_Sparse", "Lava2_SmokeSim", "smoke rising from lava"),
}

sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
tools = unreal.AssetToolsHelpers.get_asset_tools()
levels = {a.split("/")[-1].split(".")[0]: a.split(".")[0] for a in unreal.EditorAssetLibrary.list_assets("/Game/FluidNinjaLive/Levels", recursive=True) if "." in a}
preset_class = unreal.load_asset("/Game/FluidNinjaLive/Presets/NP_default").get_class()
for junk in ("FX_test", "FX_test2"):
    if unreal.EditorAssetLibrary.does_asset_exist(f"{FOLDER}/{junk}"):
        unreal.EditorAssetLibrary.delete_asset(f"{FOLDER}/{junk}")
if not unreal.EditorAssetLibrary.does_directory_exist(FOLDER):
    unreal.EditorAssetLibrary.make_directory(FOLDER)


def sim_component(actor):
    for c in actor.get_components_by_class(unreal.ActorComponent):
        if "NinjaLiveComponent" in c.get_class().get_name():
            return c
    return None


def overrides(niagara_component):
    """User-parameter overrides set on a placed Niagara component (meshes, materials, numbers, vectors)."""
    out = {}
    try:
        store = niagara_component.get_editor_property("override_parameters")
    except Exception:
        return out
    text = str(store)
    out["_raw"] = text[:400]
    return out


manifest = {}
current = None
for effect, (level, label, about) in EFFECTS.items():
    if level not in levels:
        unreal.log_warning(f"FLUIDFX {effect}: level {level} missing")
        continue
    if current != level:
        unreal.EditorLoadingAndSavingUtils.load_map(levels[level])
        current = level
    actors = sub.get_all_level_actors()
    sim = next((a for a in actors if a.get_actor_label().startswith(label) and sim_component(a)), None)
    if not sim:
        unreal.log_warning(f"FLUIDFX {effect}: simulation {label} not found")
        continue
    comp = sim_component(sim)
    # the simulation's settings -> a preset asset (FluidNinja's own writer)
    name = "FX_" + effect
    path = f"{FOLDER}/{name}"
    eal = unreal.EditorAssetLibrary
    preset = unreal.load_asset(path) if eal.does_asset_exist(path) else None
    if preset is not None and preset.get_class() != preset_class:
        eal.delete_asset(path)
        preset = None
    if preset is None:
        preset = eal.duplicate_asset("/Game/FluidNinjaLive/Presets/NP_default", path)  # same class as the pack's presets
    if preset is None:
        unreal.log_warning(f"FLUIDFX {effect}: could not create {path}")
        continue
    comp.set_editor_property("Preset", preset)
    comp.call_method("PresetWrite")
    unreal.EditorAssetLibrary.save_loaded_asset(preset)
    st = sim.get_actor_transform()
    ext = comp.get_editor_property("ExtentsXYZ")
    radius = max(abs(ext.x), abs(ext.y)) * 100 * 0.75 + 300
    drive = str(comp.get_editor_property("DriveNiagaraComponentInTaggedActors"))
    def prop(obj, k):
        try:
            return str(obj.get_editor_property(k))
        except Exception:
            return ""
    # output links (surfaces / volumes the simulation draws on: copied with it) vs interaction links (scenery it flows
    # around: left out)
    output_tags = {prop(comp, k): k for k in ("DriveNiagaraComponentInTaggedActors", "Apply1stOutMatToActorsWithTag", "Apply2ndOutMatToActorsWithTag",
                                              "Apply3rdOutMatToActorsWithTag")}
    output_tags = {t: k for t, k in output_tags.items() if t and t != "None"}
    tag_props = set(output_tags) | {t for t in [prop(sim, "TrackActorPrimitiveComponentsWithTag")] if t and t != "None"}
    companions = []
    for a in actors:
        if a == sim or sim_component(a):
            continue
        tags = {str(t) for t in a.tags}
        linked = bool(tags & tag_props)
        outputs = sorted(output_tags[t] for t in tags & set(output_tags))
        if linked and not outputs:
            continue  # scenery the fluid interacts with, not part of the effect
        d = (a.get_actor_location() - st.translation).length()
        nc = [c for c in a.get_components_by_class(unreal.NiagaraComponent) if c.get_asset()]
        if not nc and not linked:
            continue
        if not linked and d > radius:
            continue
        rel = a.get_actor_transform().make_relative(st)
        r = rel.rotation.rotator()
        entry = {"label": a.get_actor_label(), "class": a.get_class().get_path_name(), "tags": sorted(tags), "linked": linked, "outputs": outputs,
                 "loc": [round(rel.translation.x, 1), round(rel.translation.y, 1), round(rel.translation.z, 1)],
                 "rot": [round(r.pitch, 2), round(r.yaw, 2), round(r.roll, 2)], "scale": [round(rel.scale3d.x, 3), round(rel.scale3d.y, 3), round(rel.scale3d.z, 3)]}
        if nc:
            entry["system"] = nc[0].get_asset().get_path_name()
            entry["overrides"] = overrides(nc[0])
        meshes = [c for c in a.get_components_by_class(unreal.StaticMeshComponent) if c.static_mesh]
        if meshes and not nc:
            entry["mesh"] = meshes[0].static_mesh.get_path_name()
            entry["materials"] = [m.get_path_name() for m in meshes[0].get_materials() if m]
        companions.append(entry)
    # systems placed on the simulation actor itself (fog volumes, blizzard, ground snow): the simulation feeds them its output
    own = []
    for c in sim.get_components_by_class(unreal.NiagaraComponent):
        if c.get_asset() and "NinjaLiveCore" not in c.get_asset().get_name():
            rl, rr, rs = c.get_editor_property("relative_location"), c.get_editor_property("relative_rotation"), c.get_editor_property("relative_scale3d")
            own.append({"system": c.get_asset().get_path_name(), "loc": [round(rl.x, 1), round(rl.y, 1), round(rl.z, 1)],
                        "rot": [round(rr.pitch, 2), round(rr.yaw, 2), round(rr.roll, 2)], "scale": [round(rs.x, 3), round(rs.y, 3), round(rs.z, 3)]})
    s = st.scale3d
    manifest[effect] = {"own": own, "about": about, "level": level, "label": label, "preset": preset.get_path_name(), "sim_class": sim.get_class().get_path_name(),
                        "sim_scale": [round(s.x, 3), round(s.y, 3), round(s.z, 3)], "sim_rot": [round(st.rotation.rotator().pitch, 2), round(st.rotation.rotator().yaw, 2),
                                                                                                  round(st.rotation.rotator().roll, 2)],
                        "extents": [ext.x, ext.y, ext.z], "drive_tag": drive, "output_tags": output_tags, "companions": companions}
    unreal.log(f"FLUIDFX {effect}: preset {preset.get_path_name()}, {len(companions)} companions")

os.makedirs(os.path.dirname(OUT_JSON), exist_ok=True)
open(OUT_JSON, "w").write(json.dumps(manifest, indent=1))
os.makedirs(os.path.dirname(OUT_INL), exist_ok=True)
# compact: only what the toolset uses (scenery meshes and override dumps stay in the JSON for reference)
# effects the demo levels feed from moving props get a source of their own; ones that need a second demo simulation are left out
FEED = {"blood": "/Game/FluidNinjaLive/Input/Niagara/NS_WriteDataCh_GPU_Source_Radial.NS_WriteDataCh_GPU_Source_Radial",
        "explosion": "/Game/FluidNinjaLive/Input/Niagara/NS_WriteDataCh_GPU_Source_BurstRadial.NS_WriteDataCh_GPU_Source_BurstRadial"}
DROP = {"lava_smoke", "fog"}   # fog: its volumes rely on settings stored only on the demo level's copy; mist covers it
ABOUT = {"mist": "fog or mist bank that swirls around things moving through it"}
slim = {}
for k, v in manifest.items():
    if k in DROP:
        continue
    keep = [{f: c[f] for f in ("label", "tags", "loc", "rot", "scale", "system", "mesh", "materials") if f in c} for c in v["companions"]
            if c.get("system") or "/Meshes/Planes/" in c.get("mesh", "")]
    if k in FEED:
        keep.append({"label": "Source", "system": FEED[k], "loc": [0, 0, 0], "rot": [0, 0, 0], "scale": [1, 1, 1]})
    slim[k] = {f: v[f] for f in ("about", "preset", "sim_rot", "extents", "output_tags")}
    slim[k]["own"] = v.get("own", [])
    slim[k]["about"] = ABOUT.get(k, slim[k]["about"])
    slim[k]["companions"] = keep
# the coast demo's simulations (Scripts/fluid/harvest_coast_scene.py + the runtime harvest in sea_coast_scene.json)
COAST_JSON = os.path.join(ROOT, "Scripts", "kit_examples", "sea_coast_scene.json")
if os.path.exists(COAST_JSON):
    coast = json.load(open(COAST_JSON))
    nia = coast.get("niagara", {})
    for k, about in (("coast_water", "shore water with breaking waves and whitewater rolling onto a beach (100 m; follow_player keeps it around the player)"),
                     ("coast_sand", "wet sand at the water's edge that keeps footprints (65 m; follow_player keeps it around the player)")):
        sim = coast["sims"].get(k)
        if not sim:
            continue
        e = {"about": about, "preset": sim["preset"], "sim_rot": sim["rotation"], "extents": sim["extents"], "output_tags": {}, "exact": True,
             "own": [], "companions": []}
        sea = nia.get("sea_surface", {}).get("params", {}).get("ClampingValue")
        if sea is not None:
            e["sea_level"] = sea          # fluid.add location z = this water surface
            e["ref_z"] = sim["location"][2]
        if k == "coast_sand":
            e["drive_landscape"] = True   # the landscape-driver Blueprint paints wet sand and footprints from this simulation
        for h in sim.get("hosted", []):
            e["own"].append({"system": h["system"], "loc": h["location"], "rot": h["rotation"], "scale": h["scale"],
                             "params": nia.get("whitewater", {}).get("params", {})})
        if sim.get("drive_tag") not in (None, "None", "") and "sea_surface" in nia:
            tag = sim["drive_tag"]
            e["output_tags"][tag] = "DriveNiagaraComponentInTaggedActors"
            ss = nia["sea_surface"]
            yaw = math.radians(sim["rotation"][1])
            dx, dy = ss["location"][0] - sim["location"][0], ss["location"][1] - sim["location"][1]
            e["companions"].append({"label": "SeaSurface", "system": ss["system"], "tags": [tag], "params": ss["params"],
                                    "loc": [round(dx * math.cos(yaw) + dy * math.sin(yaw), 2), round(-dx * math.sin(yaw) + dy * math.cos(yaw), 2),
                                            round(ss["location"][2] - sim["location"][2], 2)],
                                    "rot": [0, -sim["rotation"][1], 0], "scale": [1, 1, 1]})
        slim[k] = e
text = json.dumps(slim, separators=(",", ":"))
chunks = [text[i:i + 8000] for i in range(0, len(text), 8000)]  # MSVC string literals are limited to 16 KB
with open(OUT_INL, "w", newline="\n") as f:
    f.write("// Generated by Scripts/fluid/harvest_fluid_effects.py from the FluidNinja LIVE demo levels. Do not edit.\n")
    f.write("static const TCHAR* const GFluidEffectsJsonParts[] = {\n")
    for c in chunks:
        f.write("\tTEXT(R\"FXJ(" + c + ")FXJ\"),\n")
    f.write("};\n")
unreal.log("FLUIDFX harvest done: " + ", ".join(manifest))
