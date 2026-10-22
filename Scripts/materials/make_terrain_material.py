"""Builds the terrain ground material /RuntimeToolsets/Materials/M_RT_Terrain (used by build.create_terrain).

    UnrealEditor-Cmd.exe MCPProject.uproject -run=pythonscript -script="<repo>/Scripts/materials/make_terrain_material.py" -unattended -nullrhi

Blend per pixel (the terrain bakes R = wet near rivers, G = bare earth on pads and roads into vertex colours):
  grass (two shades in noise patches) -> earth (moderate slopes, pads, roads, patches) -> rock (steep slopes, scanned
  granite) -> mud (wet) -> snow (above SnowLine, not on cliffs). Parameters are set per biome by the builder.
"""

import os
import sys

import unreal

HERE = os.path.dirname(os.path.abspath(__file__))
os.environ["FOLIAGE_LIBRARY_ONLY"] = "1"
sys.path.insert(0, os.path.join(HERE, "..", "foliage"))
import import_foliage as F  # noqa: E402

mel, eal, tools = F.mel, F.eal, F.tools
e, connect = F.expr, F.connect
PATH, NAME = "/RuntimeToolsets/Materials", "M_RT_Terrain"


def vec(m, name, x, y, rgb):
    p = e(m, unreal.MaterialExpressionVectorParameter, x, y, parameter_name=name, default_value=unreal.LinearColor(rgb[0], rgb[1], rgb[2], 1))
    mask = e(m, unreal.MaterialExpressionComponentMask, x + 150, y, r=True, g=True, b=True, a=False)
    connect(p, "", mask, "")
    return mask


def scalar(m, name, x, y, v):
    return e(m, unreal.MaterialExpressionScalarParameter, x, y, parameter_name=name, default_value=v)


def const(m, x, y, v):
    return e(m, unreal.MaterialExpressionConstant, x, y, r=v)


def op(m, cls, a, b, x, y, a_out="", b_out=""):
    n = e(m, cls, x, y)
    connect(a, a_out, n, "A")
    connect(b, b_out, n, "B")
    return n


def lerp(m, a, b, alpha, x, y, alpha_out=""):
    n = e(m, unreal.MaterialExpressionLinearInterpolate, x, y)
    connect(a, "", n, "A")
    connect(b, "", n, "B")
    connect(alpha, alpha_out, n, "Alpha")
    return n


def smoothstep(m, lo, hi, value, x, y, value_out=""):
    n = e(m, unreal.MaterialExpressionSmoothStep, x, y)
    connect(lo, "", n, "Min")
    connect(hi, "", n, "Max")
    connect(value, value_out, n, "Value")
    return n


def sat(m, a, x, y, a_out=""):
    n = e(m, unreal.MaterialExpressionSaturate, x, y)
    connect(a, a_out, n, "")
    return n


def build():
    path = f"{PATH}/{NAME}"
    if eal.does_asset_exist(path):
        m = unreal.load_asset(path)
        mel.delete_all_material_expressions(m)
    else:
        m = tools.create_asset(NAME, PATH, unreal.Material, unreal.MaterialFactoryNew())
    m.set_editor_property("used_with_nanite", False)
    m.set_editor_property("tangent_space_normal", False)
    X = -2400
    # inputs
    vc = e(m, unreal.MaterialExpressionVertexColor, X, 0)
    nrm = e(m, unreal.MaterialExpressionVertexNormalWS, X, 300)
    nz = e(m, unreal.MaterialExpressionComponentMask, X + 200, 300, r=False, g=False, b=True, a=False)
    connect(nrm, "", nz, "")
    slope = op(m, unreal.MaterialExpressionSubtract, const(m, X + 200, 400, 1.0), nz, X + 400, 300)       # 0 flat .. 1 vertical
    wp = e(m, unreal.MaterialExpressionWorldPosition, X, 600)
    wz = e(m, unreal.MaterialExpressionComponentMask, X + 200, 600, r=False, g=False, b=True, a=False)
    connect(wp, "", wz, "")
    macro = e(m, unreal.MaterialExpressionNoise, X, 900, scale=0.0006, levels=3, output_min=0.0, output_max=1.0, quality=1)
    patch = e(m, unreal.MaterialExpressionNoise, X, 1100, scale=0.0015, levels=4, output_min=0.0, output_max=1.0, quality=1)
    fine = e(m, unreal.MaterialExpressionNoise, X, 1300, scale=0.02, levels=2, output_min=0.85, output_max=1.1, quality=1)
    # colours
    grass_a, grass_b = vec(m, "GrassA", X + 600, -600, (0.1, 0.16, 0.045)), vec(m, "GrassB", X + 600, -450, (0.15, 0.19, 0.06))
    dirt, rock = vec(m, "Dirt", X + 600, -300, (0.2, 0.15, 0.1)), vec(m, "Rock", X + 600, -150, (0.3, 0.29, 0.27))
    mud, snow = vec(m, "Mud", X + 600, 0, (0.12, 0.1, 0.07)), vec(m, "Snow", X + 600, 150, (0.9, 0.92, 0.95))
    # grass: two shades by large patches, broken up by fine noise
    grass = op(m, unreal.MaterialExpressionMultiply, lerp(m, grass_a, grass_b, macro, X + 900, -550), fine, X + 1100, -550)
    # earth: moderate slopes, bare pads/roads (G), and scattered patches
    dslope = smoothstep(m, scalar(m, "DirtSlope0", X + 600, 400, 0.18), scalar(m, "DirtSlope1", X + 600, 480, 0.34), slope, X + 900, 400)
    dpatch = smoothstep(m, scalar(m, "PatchStart", X + 600, 1100, 0.62), const(m, X + 600, 1180, 0.8), patch, X + 900, 1100)
    dmask = sat(m, op(m, unreal.MaterialExpressionAdd, op(m, unreal.MaterialExpressionAdd, dslope, dpatch, X + 1100, 600), vc, X + 1300, 600, b_out="G"),
                X + 1500, 600)
    ground = lerp(m, grass, op(m, unreal.MaterialExpressionMultiply, dirt, fine, X + 1100, -300), dmask, X + 1700, -450)
    # rock: steep slopes, scanned granite when present
    rock_slope = scalar(m, "RockSlope", X + 600, 700, 0.45)
    rmask = smoothstep(m, rock_slope, op(m, unreal.MaterialExpressionAdd, rock_slope, const(m, X + 600, 800, 0.15), X + 800, 760), slope, X + 1000, 700)
    granite = "/Game/RuntimeArchitecture/Surfaces/granite_diff"
    rock_col = rock
    if eal.does_asset_exist(granite):
        tex = e(m, unreal.MaterialExpressionTextureObjectParameter, X + 300, 1500, parameter_name="RockTex", texture=unreal.load_asset(granite))
        size = scalar(m, "RockTile", X + 300, 1650, 400.0)
        t3 = e(m, unreal.MaterialExpressionAppendVector, X + 500, 1650)
        t2 = e(m, unreal.MaterialExpressionAppendVector, X + 450, 1700)
        connect(size, "", t2, "A")
        connect(size, "", t2, "B")
        connect(t2, "", t3, "A")
        connect(size, "", t3, "B")
        wa = F.world_aligned(m, "/Engine/Functions/Engine_MaterialFunctions01/Texturing/WorldAlignedTexture", tex, t3, X + 700, 1500)
        rock_col = op(m, unreal.MaterialExpressionMultiply, rock, op(m, unreal.MaterialExpressionMultiply, wa, const(m, X + 900, 1600, 2.2), X + 1000, 1500,
                                                                     a_out="XYZ Texture"), X + 1200, -150)
    base = lerp(m, ground, rock_col, rmask, X + 1900, -300)
    # wet mud by rivers
    base = lerp(m, base, mud, vc, X + 2100, -150, alpha_out="R")
    # snow above the snow line, not on cliffs
    above = sat(m, op(m, unreal.MaterialExpressionDivide, op(m, unreal.MaterialExpressionSubtract, wz, scalar(m, "SnowLine", X + 400, 700, 1e9), X + 600, 650),
                      const(m, X + 600, 760, 300.0), X + 800, 650), X + 1000, 650)
    smask = op(m, unreal.MaterialExpressionMultiply, above, op(m, unreal.MaterialExpressionSubtract, const(m, X + 1000, 760, 1.0), rmask, X + 1150, 760),
               X + 1300, 700)
    base = lerp(m, base, snow, smask, X + 2300, 0)
    mel.connect_material_property(base, "", unreal.MaterialProperty.MP_BASE_COLOR)
    rough = lerp(m, const(m, X + 2100, 300, 0.95), const(m, X + 2100, 380, 0.4), vc, X + 2300, 300, alpha_out="R")
    mel.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    mel.connect_material_property(const(m, X + 2300, 450, 0.3), "", unreal.MaterialProperty.MP_SPECULAR)
    mel.recompile_material(m)
    eal.save_loaded_asset(m)
    unreal.log("TERRAIN material built")


build()
