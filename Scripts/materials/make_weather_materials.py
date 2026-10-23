"""Weather-aware builder materials for projects with Ultra Dynamic Sky (editor only, run once after adding the pack):

    UnrealEditor-Cmd.exe MCPProject.uproject -run=pythonscript -script="<repo>/Scripts/materials/make_weather_materials.py" -unattended -nullrhi

/Game/RuntimeWeather/M_RT_Terrain_Weather  M_RT_Terrain + Ultra Dynamic Weather landscape effects: wet ground and puddles
                                           (rain ripples) when it rains, snow and dust cover, and mud that spreads over flat
                                           ground as it gets wet (MPC_RT_Style "Mud" scales it).
/Game/RuntimeWeather/M_RT_PBR_Weather      M_RT_PBR + surface weather effects: buildings, roads and props get wet, snowy
                                           or dusty with the weather.

The plugin keeps working without the pack: the builder uses these when they exist and the plain materials otherwise.
The weather actor drives every effect through the UltraDynamicWeather_Parameters collection (dry when there is none).
"""

import os
import sys

import unreal

HERE = os.path.dirname(os.path.abspath(__file__))
os.environ["RT_MATERIALS_LIBRARY_ONLY"] = "1"
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, ".."))
import create_materials as CM  # noqa: E402
import make_terrain_material as TM  # noqa: E402

lib = unreal.MaterialEditingLibrary
FOLDER = "/Game/RuntimeWeather"
UDS = "/Game/UltraDynamicSky/Materials/Weather"
MPC = "/Game/UltraDynamicSky/Materials/Weather/UltraDynamicWeather_Parameters.UltraDynamicWeather_Parameters"
ATTR = {unreal.MaterialProperty.MP_BASE_COLOR: "BaseColor", unreal.MaterialProperty.MP_METALLIC: "Metallic",
        unreal.MaterialProperty.MP_SPECULAR: "Specular", unreal.MaterialProperty.MP_ROUGHNESS: "Roughness",
        unreal.MaterialProperty.MP_EMISSIVE_COLOR: "EmissiveColor", unreal.MaterialProperty.MP_NORMAL: "Normal",
        unreal.MaterialProperty.MP_AMBIENT_OCCLUSION: "AmbientOcclusion"}


def wrap(function_name, switches, tangent_normals=None):
    """Material outputs -> MakeMaterialAttributes -> the Ultra Dynamic Weather function -> the material's attributes."""
    fn = unreal.load_asset(f"{UDS}/{function_name}.{function_name}")
    if not fn:
        raise RuntimeError(f"Ultra Dynamic Sky function {function_name} not found: add the pack first")

    def apply(mat, outputs):
        mat.set_editor_property("use_material_attributes", True)
        if tangent_normals is not None:
            mat.set_editor_property("tangent_space_normal", tangent_normals)
        make = lib.create_material_expression(mat, unreal.MaterialExpressionMakeMaterialAttributes, 300, 0)
        for prop, node in outputs.items():
            lib.connect_material_expressions(node, "", make, ATTR[prop])
        call = lib.create_material_expression(mat, unreal.MaterialExpressionMaterialFunctionCall, 600, 0)
        call.set_editor_property("material_function", fn)
        lib.connect_material_expressions(make, "", call, "Material Attributes")
        for i, name in enumerate(switches):  # the function's static on/off inputs must be wired
            on = lib.create_material_expression(mat, unreal.MaterialExpressionStaticBool, 400, 200 + i * 80)
            on.set_editor_property("value", True)
            lib.connect_material_expressions(on, "", call, name)
        lib.connect_material_property(call, "", unreal.MaterialProperty.MP_MATERIAL_ATTRIBUTES)
    return apply


def wet(mat, x, y):
    """The weather's ground wetness 0-1 (UltraDynamicWeather_Parameters "Wet")."""
    node = lib.create_material_expression(mat, unreal.MaterialExpressionCollectionParameter, x, y)
    node.set_editor_property("collection", unreal.load_asset(MPC))
    node.set_editor_property("parameter_name", "Wet")
    unreal.log(f"WEATHER wet param: {node.get_editor_property('collection')} / {node.get_editor_property('parameter_name')}")
    return node


if not unreal.EditorAssetLibrary.does_directory_exist(FOLDER):
    unreal.EditorAssetLibrary.make_directory(FOLDER)
STYLE = CM.style_collection()  # first: rebuilding it later would give "Mud" a new id and break the terrain's reference
# terrain: no normal map, so tangent-space normals (the landscape function blends its own puddle / snow normals)
TM.build("M_RT_Terrain_Weather", FOLDER, weather=wrap("Dynamic_Landscape_Weather_Effects_V3", ["Apply Wetness/Puddles", "Apply Snow/Dust"], tangent_normals=True), wet_param=wet)
CM.build_pbr(STYLE, "M_RT_PBR_Weather", FOLDER, weather=wrap("Surface_Weather_Effects", ["Apply Wetness", "Apply Snow / Dust"]))
unreal.log("WEATHER materials built")
