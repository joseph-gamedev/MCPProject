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


def build(name=NAME, folder=PATH, weather=None, wet_param=None):
    """weather: a function(mat, outputs) wiring the outputs {property: node} itself (weather variant). wet_param: a
    function(mat, x, y) -> node giving the weather's ground wetness 0-1: mud then spreads over flat, low-lying ground."""
    path = f"{folder}/{name}"
    outputs = {}

    def out(node, prop):
        if weather:
            outputs[prop] = node
        else:
            mel.connect_material_property(node, "", prop)
    m = unreal.load_asset(path)   # (the registry may not have scanned plugin content yet)
    if m:
        mel.delete_all_material_expressions(m)
        for leftover in mel.get_material_expressions(m):   # unconnected ones can survive the call above
            mel.delete_material_expression(m, leftover)
    else:
        m = tools.create_asset(name, folder, unreal.Material, unreal.MaterialFactoryNew())
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
    granite = "/RuntimeToolsets/Textures/T_RT_Granite"
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
    # beach: sand below the sand line (not on cliffs), rippled; darker, glossier wet sand in a band above the sea level
    sand, wet_sand = vec(m, "Sand", X + 600, 1800, (0.62, 0.52, 0.36)), vec(m, "WetSand", X + 600, 1950, (0.36, 0.29, 0.2))
    # sand ripples: SandTex is set at runtime from the project's texture.sand_ripple slot; SandRipple 0 (default) = off
    sand_col = op(m, unreal.MaterialExpressionMultiply, sand, fine, X + 900, 1800)
    if True:
        rtex = e(m, unreal.MaterialExpressionTextureObjectParameter, X + 300, 2100, parameter_name="SandTex",
                 texture=unreal.load_asset("/Engine/EngineResources/Black"))
        rsize = scalar(m, "SandTile", X + 300, 2250, 350.0)
        r2 = e(m, unreal.MaterialExpressionAppendVector, X + 450, 2300)
        r3 = e(m, unreal.MaterialExpressionAppendVector, X + 500, 2250)
        connect(rsize, "", r2, "A")
        connect(rsize, "", r2, "B")
        connect(r2, "", r3, "A")
        connect(rsize, "", r3, "B")
        rwa = F.world_aligned(m, "/Engine/Functions/Engine_MaterialFunctions01/Texturing/WorldAlignedTexture", rtex, r3, X + 700, 2100)
        rmask_r = e(m, unreal.MaterialExpressionComponentMask, X + 900, 2100, r=True, g=False, b=False, a=False)
        connect(rwa, "XYZ Texture", rmask_r, "")
        ripple = lerp(m, const(m, X + 1000, 2200, 0.82), const(m, X + 1000, 2260, 1.12), rmask_r, X + 1100, 2150)
        ripple = lerp(m, const(m, X + 1150, 2050, 1.0), ripple, scalar(m, "SandRipple", X + 1000, 2320, 0.0), X + 1150, 2150)
        sand_col = op(m, unreal.MaterialExpressionMultiply, sand_col, ripple, X + 1200, 1850)
    below = sat(m, op(m, unreal.MaterialExpressionDivide, op(m, unreal.MaterialExpressionSubtract, scalar(m, "SandLine", X + 400, 1900, -1e9), wz, X + 600, 2050),
                      const(m, X + 600, 2120, 120.0), X + 800, 2050), X + 1000, 2050)
    band = scalar(m, "WetBand", X + 400, 2400, 90.0)
    wet = sat(m, op(m, unreal.MaterialExpressionDivide, op(m, unreal.MaterialExpressionSubtract, op(m, unreal.MaterialExpressionAdd, scalar(m, "SeaLevel", X + 400, 2330, -1e9),
                                                                                                    band, X + 600, 2350), wz, X + 800, 2350), band, X + 1000, 2400), X + 1100, 2400)
    sand_col = lerp(m, sand_col, op(m, unreal.MaterialExpressionMultiply, wet_sand, fine, X + 1200, 1950), wet, X + 1400, 1900)
    smask_sand = op(m, unreal.MaterialExpressionMultiply, below, op(m, unreal.MaterialExpressionSubtract, const(m, X + 1100, 2150, 1.0), rmask, X + 1200, 2150),
                    X + 1300, 2100)
    base = lerp(m, base, sand_col, smask_sand, X + 2000, 1700)
    sand_wet = op(m, unreal.MaterialExpressionMultiply, smask_sand, wet, X + 1500, 2300)
    # footprints and tracks painted by a fluid simulation (fluid.add sand_trails / snow_trails): TrailTex covers the world
    # rectangle from TrailMin (x, y) of size TrailSize (x, y); the fluid toolset feeds both while the simulation runs
    tmin, tsize = vec(m, "TrailMin", X + 300, 2600, (0.0, 0.0, 0.0)), vec(m, "TrailSize", X + 300, 2680, (1.0, 1.0, 1.0))
    rect_min = e(m, unreal.MaterialExpressionComponentMask, X + 500, 2600, r=True, g=True, b=False, a=False)
    connect(tmin, "", rect_min, "")
    rect_size = e(m, unreal.MaterialExpressionComponentMask, X + 500, 2680, r=True, g=True, b=False, a=False)
    connect(tsize, "", rect_size, "")
    wxy = e(m, unreal.MaterialExpressionComponentMask, X + 300, 2760, r=True, g=True, b=False, a=False)
    connect(wp, "", wxy, "")
    uv = op(m, unreal.MaterialExpressionDivide, op(m, unreal.MaterialExpressionSubtract, wxy, rect_min, X + 700, 2700), rect_size, X + 900, 2700)
    trail_tex = e(m, unreal.MaterialExpressionTextureSampleParameter2D, X + 1100, 2650, parameter_name="TrailTex",
                  texture=unreal.load_asset("/Engine/EngineResources/Black"), sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
    connect(uv, "", trail_tex, "UVs")
    trail_r = e(m, unreal.MaterialExpressionComponentMask, X + 1300, 2650, r=True, g=False, b=False, a=False)
    connect(trail_tex, "RGB", trail_r, "")
    trail = sat(m, op(m, unreal.MaterialExpressionMultiply, trail_r, scalar(m, "TrailStrength", X + 1300, 2750, 0.0), X + 1450, 2700), X + 1600, 2700)
    base = lerp(m, base, op(m, unreal.MaterialExpressionMultiply, base, const(m, X + 1700, 2800, 0.62), X + 1850, 2750), trail, X + 2050, 1900)
    sand_wet = op(m, unreal.MaterialExpressionMax, sand_wet, op(m, unreal.MaterialExpressionMultiply, trail, const(m, X + 1700, 2850, 0.8), X + 1850, 2850),
                  X + 2000, 2400)
    # wet mud by rivers; with weather, wet ground turns to mud on flat ground in patches (Mud in MPC_RT_Style scales it)
    mud_mask, mud_out = vc, "R"
    if wet_param:
        style = unreal.load_asset("/RuntimeToolsets/Materials/MPC_RT_Style.MPC_RT_Style")
        mud_amt = e(m, unreal.MaterialExpressionCollectionParameter, X + 1500, 1300, collection=style, parameter_name="Mud")
        unreal.log(f"TERRAIN mud param: {style} / {mud_amt.get_editor_property('parameter_name')}")
        flat = op(m, unreal.MaterialExpressionSubtract, const(m, X + 1300, 1400, 1.0), sat(m, op(m, unreal.MaterialExpressionMultiply, slope,
                  const(m, X + 1300, 1450, 4.0), X + 1400, 1450), X + 1500, 1450), X + 1600, 1400)
        spread = smoothstep(m, const(m, X + 1500, 1550, 0.25), const(m, X + 1500, 1600, 0.7), macro, X + 1600, 1550)
        wetmud = sat(m, op(m, unreal.MaterialExpressionMultiply, op(m, unreal.MaterialExpressionMultiply, wet_param(m, X + 1500, 1200), mud_amt, X + 1700, 1250),
                           op(m, unreal.MaterialExpressionMultiply, flat, spread, X + 1700, 1450), X + 1800, 1300), X + 1900, 1300)
        rmud = e(m, unreal.MaterialExpressionComponentMask, X + 1700, 1150, r=True, g=False, b=False, a=False)
        connect(vc, "", rmud, "")
        mud_mask, mud_out = op(m, unreal.MaterialExpressionMax, rmud, wetmud, X + 2000, 1200), ""
    base = lerp(m, base, mud, mud_mask, X + 2100, -150, alpha_out=mud_out)
    # snow above the snow line, not on cliffs
    above = sat(m, op(m, unreal.MaterialExpressionDivide, op(m, unreal.MaterialExpressionSubtract, wz, scalar(m, "SnowLine", X + 400, 700, 1e9), X + 600, 650),
                      const(m, X + 600, 760, 300.0), X + 800, 650), X + 1000, 650)
    smask = op(m, unreal.MaterialExpressionMultiply, above, op(m, unreal.MaterialExpressionSubtract, const(m, X + 1000, 760, 1.0), rmask, X + 1150, 760),
               X + 1300, 700)
    base = lerp(m, base, snow, smask, X + 2300, 0)
    out(base, unreal.MaterialProperty.MP_BASE_COLOR)
    rough = lerp(m, const(m, X + 2100, 300, 0.95), const(m, X + 2100, 380, 0.4), mud_mask, X + 2300, 300, alpha_out=mud_out)
    rough = lerp(m, rough, const(m, X + 2300, 420, 0.28), sand_wet, X + 2450, 350)   # wet sand at the waterline shines
    out(rough, unreal.MaterialProperty.MP_ROUGHNESS)
    out(const(m, X + 2300, 450, 0.3), unreal.MaterialProperty.MP_SPECULAR)
    if weather:
        weather(m, outputs)
    mel.recompile_material(m)
    eal.save_loaded_asset(m)
    unreal.log("TERRAIN material built: " + name)


if not os.environ.get("RT_MATERIALS_LIBRARY_ONLY"):
    build()
