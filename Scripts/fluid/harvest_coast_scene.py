"""Harvests FluidNinja LIVE's Water_Dense_Sea demo beach so the toolsets can rebuild it (editor only, run after adding the pack):

    UnrealEditor-Cmd.exe MCPProject.uproject -run=pythonscript -script="<repo>/Scripts/fluid/harvest_coast_scene.py" -unattended -nullrhi

- the three simulations (wet sand 65 m, water 100 m, water 200 m) -> presets /Game/RuntimeFluid/Presets/FX_coast_* (FluidNinja's own
  PresetWrite), with their extents, rotation and output links;
- the sculpted landscape and the Blueprint that feeds it the simulation -> a level of its own, /Game/RuntimeFluid/Scenes/L_SeaCoast_Ground
  (a Landscape cannot be made at runtime; level.load_level_instance streams it in);
- every static mesh (rocks, wrecks, mountains, moon) -> mesh, materials, transform, tags.
Writes Scripts/kit_examples/sea_coast_scene.json (merged with the runtime harvest of lights / fog / sky / particles).
The demo level itself is put back as it was (the landscape is moved out only in memory and the level is not saved).
"""

import json
import os

import unreal

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
OUT = os.path.join(ROOT, "Scripts", "kit_examples", "sea_coast_scene.json")
LEVEL = "/Game/FluidNinjaLive/Levels/Water_Dense_Sea"
GROUND = "/Game/RuntimeFluid/Scenes/L_SeaCoast_Ground"
PRESETS = "/Game/RuntimeFluid/Presets"
SIMS = {"NinjaLive_Sand": "coast_sand", "NinjaLive_Water_100m": "coast_water", "NinjaLive_Water_200m_DISABLED": "coast_water_far"}

eal = unreal.EditorAssetLibrary
sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def tr(t):
    r = t.rotation.rotator()
    return {"location": [round(t.translation.x, 2), round(t.translation.y, 2), round(t.translation.z, 2)],
            "rotation": [round(r.pitch, 3), round(r.yaw, 3), round(r.roll, 3)],
            "scale": [round(t.scale3d.x, 5), round(t.scale3d.y, 5), round(t.scale3d.z, 5)]}


def sim_component(actor):
    for c in actor.get_components_by_class(unreal.ActorComponent):
        if "NinjaLiveComponent" in c.get_class().get_name():
            return c
    return None


def prop(obj, name):
    try:
        return obj.get_editor_property(name)
    except Exception:
        return None


unreal.EditorLoadingAndSavingUtils.load_map(LEVEL)
actors = sub.get_all_level_actors()
scene = {"source": LEVEL, "ground_level": GROUND, "sims": {}, "meshes": []}

# simulations -> presets
preset_class = unreal.load_asset("/Game/FluidNinjaLive/Presets/NP_default").get_class()
for a in actors:
    name = SIMS.get(a.get_actor_label())
    comp = sim_component(a) if name else None
    if not comp:
        continue
    path = f"{PRESETS}/FX_{name}"
    preset = unreal.load_asset(path) if eal.does_asset_exist(path) else None
    if preset is None or preset.get_class() != preset_class:
        if preset is not None:
            eal.delete_asset(path)
        preset = eal.duplicate_asset("/Game/FluidNinjaLive/Presets/NP_default", path)
    comp.set_editor_property("Preset", preset)
    comp.call_method("PresetWrite")
    eal.save_loaded_asset(preset)
    ext = comp.get_editor_property("ExtentsXYZ")
    hosted = []
    for n in a.get_components_by_class(unreal.NiagaraComponent):
        if n.get_asset() and "NinjaLiveCore" not in n.get_asset().get_name():
            hosted.append({"name": n.get_name(), "system": n.get_asset().get_path_name(), **tr(n.get_relative_transform())})
    scene["sims"][name] = {"label": a.get_actor_label(), "preset": preset.get_path_name(), **tr(a.get_actor_transform()),
                           "extents": [ext.x, ext.y, ext.z], "drive_tag": str(prop(comp, "DriveNiagaraComponentInTaggedActors")),
                           "auto_activate": bool(prop(comp, "auto_activate")), "hidden": a.is_hidden_ed(), "hosted": hosted,
                           "secondary_materials": [m.get_path_name() for m in (prop(comp, "SecondaryOutputMaterials") or []) if m],
                           "primary_materials": [m.get_path_name() for m in (prop(comp, "PrimaryOutputMaterials") or []) if m]}
    unreal.log(f"COAST sim {name}: {preset.get_path_name()}")

# static meshes
for a in actors:
    if not isinstance(a, unreal.StaticMeshActor):
        continue
    c = a.static_mesh_component
    if not c or not c.static_mesh:
        continue
    defaults = [m.get_path_name() if m else None for m in [c.static_mesh.get_material(i) for i in range(c.get_num_materials())]]
    mats = [m.get_path_name() if m else None for m in c.get_materials()]
    scene["meshes"].append({"label": a.get_actor_label(), "mesh": c.static_mesh.get_path_name(), **tr(a.get_actor_transform()),
                            "materials": mats if mats != defaults else [], "tags": [str(t) for t in a.tags],
                            "collision": str(c.get_collision_enabled()).split(".")[-1].split(":")[0], "hidden": a.is_hidden_ed(),
                            "physics": bool(c.get_editor_property("body_instance").get_editor_property("simulate_physics")),
                            "mass_kg": round(c.get_editor_property("body_instance").get_editor_property("mass_in_kg_override"), 1),
                            "attach_to": a.get_attach_parent_actor().get_actor_label() if a.get_attach_parent_actor() else ""})

# landscape (+ the Blueprint feeding it the simulation) -> its own level
ground = [a for a in actors if isinstance(a, unreal.LandscapeProxy) or a.get_actor_label() == "Drive Landscape with SimData"]
scene["ground"] = [{"label": a.get_actor_label(), "class": a.get_class().get_path_name(), **tr(a.get_actor_transform()),
                    "tags": [str(t) for t in a.tags]} for a in ground]
keep = {g["label"] for g in scene["ground"]}
os.makedirs(os.path.dirname(OUT), exist_ok=True)
old = json.load(open(OUT)) if os.path.exists(OUT) else {}
old.update(scene)
open(OUT, "w").write(json.dumps(old, indent=1))
# a copy of the demo level keeping only the ground (moving a Landscape between levels needs the interactive editor)
unreal.EditorLoadingAndSavingUtils.load_map("/Game/Maps/Sandbox")   # the demo level must not be open while it is copied
if eal.does_asset_exist(GROUND):
    eal.delete_asset(GROUND)
if not eal.duplicate_asset(LEVEL, GROUND):
    raise RuntimeError("could not copy the demo level")
unreal.EditorLoadingAndSavingUtils.load_map(GROUND)
gone = [a for a in sub.get_all_level_actors() if a.get_actor_label() not in keep and not isinstance(a, (unreal.WorldSettings, unreal.LandscapeProxy))
        and "LandscapeStreamingProxy" not in a.get_class().get_name()]
sub.destroy_actors(gone)
# the demo's level script (rock spawner, attaching the simulations to its character) must not come along
level_bp = unreal.find_object(None, GROUND + ".L_SeaCoast_Ground:PersistentLevel.L_SeaCoast_Ground")
if level_bp:
    for g in list(unreal.BlueprintEditorLibrary.list_graphs(level_bp)):
        unreal.BlueprintEditorLibrary.remove_graph(level_bp, g)
    unreal.BlueprintEditorLibrary.compile_blueprint(level_bp)
unreal.log(f"COAST ground level keeps {[a.get_actor_label() for a in sub.get_all_level_actors()]}")
unreal.EditorLoadingAndSavingUtils.save_map(unreal.EditorLevelLibrary.get_editor_world(), GROUND)
os.makedirs(os.path.dirname(OUT), exist_ok=True)
old = json.load(open(OUT)) if os.path.exists(OUT) else {}
old.update(scene)
open(OUT, "w").write(json.dumps(old, indent=1))
unreal.log(f"COAST harvest done: {len(scene['sims'])} sims, {len(scene['meshes'])} meshes, ground {[g['label'] for g in scene['ground']]}")
