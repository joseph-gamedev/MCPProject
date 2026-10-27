"""Builds the recipes ops.run_recipe runs (stored batches of ordinary tool calls) and writes them into the game:

    python Scripts/recipes/make_recipes.py

sea_coast: FluidNinja LIVE's Water_Dense_Sea demo beach, from Scripts/kit_examples/sea_coast_scene.json (made by
Scripts/fluid/harvest_coast_scene.py): the sculpted coast ground (a Landscape streamed in as a level instance), its sun, sky,
fog and grading, 59 rocks / wrecks / mountains, the moving balls and the drone, a third-person character and the water and
wet sand that travel with it, and floating barrels. Parts: lighting, props, player (skippable with run_recipe skip).
"""

import json
import os

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
OUT = os.path.join(ROOT, "Plugins", "RuntimeToolsetsPacks", "Source", "RuntimeToolsetsPacks", "Private", "Generated", "OpsRecipes.gen.inl")
SKIP = {"Mobility", "AttachSocketName", "ComponentTags", "AssetUserData", "AssetUserDataEditorOnly", "PrimaryComponentTick", "bIsActive",
        "BodyInstance", "bReplicates", "bReplicateUsingRegisteredSubObjectList", "bEditableWhenInherited", "UpdatedComponent",
        "UpdatedPrimitive", "bIsEditorOnly", "bCanEverAffectNavigation"}
LIGHTING = ("DirectionalLight", "SkyLight", "SkyAtmosphere", "ExponentialHeightFog", "VolumetricCloud", "PostProcessVolume", "PointLight")


def call(tool, part=None, **arguments):
    c = {"tool": tool, "arguments": arguments}
    if part:
        c["part"] = part
    return c


def sea_coast():
    s = json.load(open(os.path.join(ROOT, "Scripts", "kit_examples", "sea_coast_scene.json")))
    calls = [call("scene.clear_all", confirm=True),
             call("level.load_level_instance", map=s["ground_level"], location=[0, 0, 0], name="SeaCoastGround", blocking=True)]
    calls.append(call("lighting.hide_environment", "lighting", hidden=True))
    for name, a in s["actors"].items():
        part = "lighting" if a["class"].split(".")[-1] in LIGHTING else "props"
        args = {"class": a["class"], "location": a["location"], "rotation": a["rotation"], "scale": a.get("scale") or [1, 1, 1], "name": name}
        if a.get("tags"):
            args["tags"] = a["tags"]
        calls.append(call("actors.spawn_actor", part, **args))
        calls.append(call("actors.set_transform", part, target=name, rotation=a["rotation"]))   # a new sun is aimed by default
        values = {k: v for k, v in a.get("actor", {}).items() if k not in SKIP and v not in (None, "")}
        if values:
            calls.append(call("reflection.set_properties", part, target=name, values=values))
        for comp, c in a.get("components", {}).items():
            values = {k: v for k, v in c.get("props", {}).items() if k not in SKIP and v not in (None, "")}
            if comp == "RotatingMovement":   # added to that ball by hand in the demo
                calls.append(call("actors.add_component", part, target=name, name=comp, **{"class": c["class"]}))
            if values:
                calls.append(call("reflection.set_properties", part, target=f"{name}#{comp}", values=values))
    for m in s["meshes"]:
        if m.get("hidden"):
            continue
        args = {"asset": m["mesh"].split(".")[0], "location": m["location"], "rotation": m["rotation"], "scale": m["scale"], "name": m["label"],
                "collision": m.get("collision", "QUERY_AND_PHYSICS") != "NO_COLLISION"}
        if m.get("tags"):
            args["tags"] = m["tags"]
        calls.append(call("build.place_mesh", "props", **args))
        if m.get("materials"):
            calls.append(call("reflection.set_property", "props", target=f"{m['label']}#StaticMeshComponent0", path="OverrideMaterials", value=m["materials"]))
        if m.get("physics") and "floatie" not in m["label"]:
            calls.append(call("physics.set_simulate", "props", target=m["label"], enabled=True))
    path = {}   # attaching moves a node under its parent: later references use its new path
    for m in s["meshes"]:
        if m.get("attach_to") and not m.get("hidden"):
            parent = path.get(m["attach_to"], m["attach_to"])
            calls.append(call("actors.attach", "props", child=path.get(m["label"], m["label"]), parent=parent, keep_world=True))
            path[m["label"]] = parent + "/" + m["label"]
    p = s["player"]
    calls.append(call("character.third_person", "player", location=p["location"], yaw=p["yaw"],
                  character_class="/Game/FluidNinjaLive/Meshes/ThirdPersonCharacter.ThirdPersonCharacter_C"))
    sea = s["niagara"]["sea_surface"]["params"]["ClampingValue"]   # coast effects are placed by their sea level
    for k, label in (("coast_sand", "WetSand"), ("coast_water", "Sea")):
        loc = s["sims"][k]["location"]
        calls.append(call("fluid.add", preset=k, location=[loc[0], loc[1], sea], name=label, follow_player=True))
    floats = [m["label"] for m in s["meshes"] if "floatie" in m["label"] and not m.get("hidden")]
    if floats:
        calls.append(call("fluid.float", "props", targets=floats, water_level=sea))
    return {"about": "FluidNinja LIVE's sea-coast demo beach: sculpted sand coast, shore water with breaking waves and whitewater, "
                     "wet sand that keeps footprints, rocks, wrecks and floating barrels, midday sun and sky, and a third-person "
                     "character the water and sand travel with (WASD + mouse). Needs the FluidNinja LIVE pack.",
            "calls": calls}


recipes = {"sea_coast": sea_coast()}
text = json.dumps(recipes, separators=(",", ":"))
chunks = [text[i:i + 8000] for i in range(0, len(text), 8000)]   # MSVC string literals are limited to 16 KB
os.makedirs(os.path.dirname(OUT), exist_ok=True)
with open(OUT, "w", newline="\n") as f:
    f.write("// Generated by Scripts/recipes/make_recipes.py. Do not edit.\n")
    f.write("static const TCHAR* const GOpsRecipesJsonParts[] = {\n")
    for c in chunks:
        f.write("\tTEXT(R\"RCP(" + c + ")RCP\"),\n")
    f.write("};\n")
for k, v in recipes.items():
    parts = {}
    for c in v["calls"]:
        parts[c.get("part", "-")] = parts.get(c.get("part", "-"), 0) + 1
    print(k, len(v["calls"]), "calls", parts, f"{len(text) // 1024} KB in {len(chunks)} chunks")
