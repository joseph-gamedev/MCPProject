"""
Creates the RuntimeToolsets builder materials in the plugin's content folder (editor only, run once):

    UnrealEditor-Cmd.exe MCPProject.uproject -run=pythonscript -script="<abs path>/Scripts/create_materials.py" -unattended -nullrhi

/RuntimeToolsets/Materials/M_RT_PBR    opaque PBR: Color, Roughness, Metallic, Specular, Emissive + EmissiveStrength,
                                       Variation (world-space noise on the colour) and VariationScale
/RuntimeToolsets/Materials/M_RT_Glass  translucent glass: Color, Opacity, Roughness, Metallic
"""

import unreal

FOLDER = "/RuntimeToolsets/Materials"
lib = unreal.MaterialEditingLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()


def fresh_material(name):
    path = f"{FOLDER}/{name}"
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        unreal.EditorAssetLibrary.delete_asset(path)
    return tools.create_asset(name, FOLDER, unreal.Material, unreal.MaterialFactoryNew())


def scalar(mat, name, value, x, y):
    node = lib.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, x, y)
    node.set_editor_property("parameter_name", name)
    node.set_editor_property("default_value", value)
    return node


def vector(mat, name, value, x, y):
    node = lib.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, x, y)
    node.set_editor_property("parameter_name", name)
    node.set_editor_property("default_value", unreal.LinearColor(*value))
    return node


def build_pbr():
    mat = fresh_material("M_RT_PBR")
    color = vector(mat, "Color", (0.8, 0.8, 0.8, 1.0), -900, -300)

    # Subtle world-space variation so large flat surfaces read as stone/plaster instead of CG.
    world = lib.create_material_expression(mat, unreal.MaterialExpressionWorldPosition, -1300, 0)
    scale = scalar(mat, "VariationScale", 0.004, -1300, 120)
    scaled = lib.create_material_expression(mat, unreal.MaterialExpressionMultiply, -1100, 0)
    lib.connect_material_expressions(world, "", scaled, "A")
    lib.connect_material_expressions(scale, "", scaled, "B")
    noise = lib.create_material_expression(mat, unreal.MaterialExpressionNoise, -900, 0)
    noise.set_editor_property("scale", 1.0)
    noise.set_editor_property("levels", 4)
    noise.set_editor_property("output_min", 0.0)
    noise.set_editor_property("output_max", 1.0)
    lib.connect_material_expressions(scaled, "", noise, "Position")
    variation = scalar(mat, "Variation", 0.12, -900, 200)
    one_minus = lib.create_material_expression(mat, unreal.MaterialExpressionOneMinus, -700, 200)
    lib.connect_material_expressions(variation, "", one_minus, "")
    lerp = lib.create_material_expression(mat, unreal.MaterialExpressionLinearInterpolate, -500, 100)
    lib.connect_material_expressions(one_minus, "", lerp, "A")
    one = lib.create_material_expression(mat, unreal.MaterialExpressionConstant, -700, 300)
    one.set_editor_property("r", 1.0)
    lib.connect_material_expressions(one, "", lerp, "B")
    lib.connect_material_expressions(noise, "", lerp, "Alpha")
    tinted = lib.create_material_expression(mat, unreal.MaterialExpressionMultiply, -300, -200)
    lib.connect_material_expressions(color, "", tinted, "A")
    lib.connect_material_expressions(lerp, "", tinted, "B")
    lib.connect_material_property(tinted, "", unreal.MaterialProperty.MP_BASE_COLOR)

    lib.connect_material_property(scalar(mat, "Roughness", 0.5, -300, 0), "", unreal.MaterialProperty.MP_ROUGHNESS)
    lib.connect_material_property(scalar(mat, "Metallic", 0.0, -300, 100), "", unreal.MaterialProperty.MP_METALLIC)
    lib.connect_material_property(scalar(mat, "Specular", 0.5, -300, 200), "", unreal.MaterialProperty.MP_SPECULAR)

    emissive = vector(mat, "Emissive", (0.0, 0.0, 0.0, 1.0), -500, 400)
    strength = scalar(mat, "EmissiveStrength", 0.0, -500, 550)
    glow = lib.create_material_expression(mat, unreal.MaterialExpressionMultiply, -300, 400)
    lib.connect_material_expressions(emissive, "", glow, "A")
    lib.connect_material_expressions(strength, "", glow, "B")
    lib.connect_material_property(glow, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

    lib.recompile_material(mat)
    unreal.EditorAssetLibrary.save_loaded_asset(mat)
    unreal.log("Created M_RT_PBR")


def build_glass():
    mat = fresh_material("M_RT_Glass")
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    mat.set_editor_property("translucency_lighting_mode", unreal.TranslucencyLightingMode.TLM_SURFACE)
    lib.connect_material_property(vector(mat, "Color", (0.55, 0.75, 0.85, 1.0), -400, -200), "", unreal.MaterialProperty.MP_BASE_COLOR)
    lib.connect_material_property(scalar(mat, "Opacity", 0.35, -400, 0), "", unreal.MaterialProperty.MP_OPACITY)
    lib.connect_material_property(scalar(mat, "Roughness", 0.05, -400, 100), "", unreal.MaterialProperty.MP_ROUGHNESS)
    lib.connect_material_property(scalar(mat, "Metallic", 0.0, -400, 200), "", unreal.MaterialProperty.MP_METALLIC)
    lib.connect_material_property(scalar(mat, "Specular", 1.0, -400, 300), "", unreal.MaterialProperty.MP_SPECULAR)
    lib.recompile_material(mat)
    unreal.EditorAssetLibrary.save_loaded_asset(mat)
    unreal.log("Created M_RT_Glass")


build_pbr()
build_glass()
