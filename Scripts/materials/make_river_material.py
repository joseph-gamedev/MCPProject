"""Builds the flowing river material /RuntimeToolsets/Materials/M_RT_River (used by build.create_river).

    UnrealEditor-Cmd.exe MCPProject.uproject -run=pythonscript -script="<repo>/Scripts/materials/make_river_material.py" -unattended -nullrhi

Single Layer Water shading: the water's colour and depth come from Scattering and Absorption (per metre). Ripple normals
are two layers of gradient noise in the river's own space (UV U = metres downstream, V across), moving downstream with
time at FlowSpeed, so the current follows every bend. Foam (vertex colour R near the banks, broken up by noise) shows
through Opacity.
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
PATH, NAME = "/RuntimeToolsets/Materials", "M_RT_River"


def scalar(m, name, x, y, v):
    return e(m, unreal.MaterialExpressionScalarParameter, x, y, parameter_name=name, default_value=v)


def const(m, x, y, v):
    return e(m, unreal.MaterialExpressionConstant, x, y, r=v)


def vec(m, name, x, y, rgb):
    p = e(m, unreal.MaterialExpressionVectorParameter, x, y, parameter_name=name, default_value=unreal.LinearColor(rgb[0], rgb[1], rgb[2], 1))
    mask = e(m, unreal.MaterialExpressionComponentMask, x + 160, y, r=True, g=True, b=True, a=False)
    connect(p, "", mask, "")
    return mask


def op(m, cls, a, b, x, y, a_out="", b_out=""):
    n = e(m, cls, x, y)
    connect(a, a_out, n, "A")
    connect(b, b_out, n, "B")
    return n


def mask(m, src, x, y, out="", **ch):
    n = e(m, unreal.MaterialExpressionComponentMask, x, y, **{k: ch.get(k, False) for k in ("r", "g", "b", "a")})
    connect(src, out, n, "")
    return n


def append(m, a, b, x, y):
    n = e(m, unreal.MaterialExpressionAppendVector, x, y)
    connect(a, "", n, "A")
    connect(b, "", n, "B")
    return n


def ripple_layer(m, u, v, t, speed, scale, drift, x, y):
    """Gradient noise at (U - t*speed, V, t*drift) * scale; returns the gradient (RGB)."""
    pu = op(m, unreal.MaterialExpressionSubtract, u, op(m, unreal.MaterialExpressionMultiply, t, speed, x, y - 60), x + 150, y)
    pos = append(m, append(m, pu, v, x + 300, y), op(m, unreal.MaterialExpressionMultiply, t, const(m, x + 150, y + 80, drift), x + 300, y + 80), x + 450, y)
    pos = op(m, unreal.MaterialExpressionMultiply, pos, const(m, x + 450, y + 80, scale), x + 600, y)
    n = e(m, unreal.MaterialExpressionVectorNoise, x + 750, y, noise_function=unreal.VectorNoiseFunction.VNF_GRADIENT_ALU, quality=1, tiling=False)
    connect(pos, "", n, "Position")
    return n


def build():
    path = f"{PATH}/{NAME}"
    if eal.does_asset_exist(path):
        m = unreal.load_asset(path)
        mel.delete_all_material_expressions(m)
    else:
        m = tools.create_asset(NAME, PATH, unreal.Material, unreal.MaterialFactoryNew())
    m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_SINGLE_LAYER_WATER)
    m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_OPAQUE)
    m.set_editor_property("tangent_space_normal", True)
    X = -2600
    uv = e(m, unreal.MaterialExpressionTextureCoordinate, X, 0)
    u = mask(m, uv, X + 150, 0, r=True)
    width = scalar(m, "WidthM", X, 120, 15.0)
    v = op(m, unreal.MaterialExpressionMultiply, mask(m, uv, X + 150, 120, g=True), width, X + 300, 120)
    t = e(m, unreal.MaterialExpressionTime, X, 260)
    flow = scalar(m, "FlowSpeed", X, 340, 1.0)
    speed_a = op(m, unreal.MaterialExpressionMultiply, flow, const(m, X, 420, 0.9), X + 150, 380)
    speed_b = op(m, unreal.MaterialExpressionMultiply, flow, const(m, X, 500, 1.4), X + 150, 460)
    ga = ripple_layer(m, u, v, t, speed_a, 0.45, 0.15, X + 400, 0)
    gb = ripple_layer(m, u, v, t, speed_b, 1.3, 0.35, X + 400, 400)
    grad = op(m, unreal.MaterialExpressionAdd, ga, op(m, unreal.MaterialExpressionMultiply, gb, const(m, X + 1300, 480, 0.45), X + 1400, 420), X + 1550, 200)
    ripple = scalar(m, "Ripple", X + 1400, 600, 1.0)
    k = op(m, unreal.MaterialExpressionMultiply, ripple, const(m, X + 1400, 680, -0.18), X + 1550, 620)
    gxy = op(m, unreal.MaterialExpressionMultiply, mask(m, grad, X + 1700, 200, r=True, g=True), k, X + 1850, 200)
    normal = e(m, unreal.MaterialExpressionNormalize, X + 2150, 200)
    connect(append(m, gxy, const(m, X + 1850, 320, 1.0), X + 2000, 200), "", normal, "")
    mel.connect_material_property(normal, "", unreal.MaterialProperty.MP_NORMAL)
    # foam: vertex colour R near the banks, broken by the slow ripple layer's value
    vc = e(m, unreal.MaterialExpressionVertexColor, X + 1400, 900)
    noise_v = mask(m, ga, X + 1400, 1000, a=True)
    breakup = e(m, unreal.MaterialExpressionSmoothStep, X + 1600, 1000)
    connect(const(m, X + 1450, 1080, -0.2), "", breakup, "Min")
    connect(const(m, X + 1450, 1140, 0.5), "", breakup, "Max")
    connect(noise_v, "", breakup, "Value")
    foam = e(m, unreal.MaterialExpressionSaturate, X + 2100, 950)
    connect(op(m, unreal.MaterialExpressionMultiply, op(m, unreal.MaterialExpressionMultiply, vc, breakup, X + 1800, 950, a_out="R"),
               const(m, X + 1800, 1100, 0.85), X + 1950, 950), "", foam, "")
    foam_ctl = op(m, unreal.MaterialExpressionMultiply, foam, scalar(m, "Foam", X + 2000, 1100, 1.0), X + 2200, 1000)
    mel.connect_material_property(vec(m, "FoamColor", X + 2200, 800, (0.75, 0.72, 0.65)), "", unreal.MaterialProperty.MP_BASE_COLOR)
    mel.connect_material_property(foam_ctl, "", unreal.MaterialProperty.MP_OPACITY)
    rough = e(m, unreal.MaterialExpressionLinearInterpolate, X + 2300, 1200)
    connect(scalar(m, "Roughness", X + 2100, 1200, 0.05), "", rough, "A")
    connect(const(m, X + 2100, 1280, 0.6), "", rough, "B")
    connect(foam_ctl, "", rough, "Alpha")
    mel.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    mel.connect_material_property(const(m, X + 2300, 1400, 0.5), "", unreal.MaterialProperty.MP_SPECULAR)
    # single layer water: the body of the water
    out = e(m, unreal.MaterialExpressionSingleLayerWaterMaterialOutput, X + 2600, 600)
    connect(vec(m, "Scattering", X + 2200, 500, (0.004, 0.012, 0.015)), "", out, "ScatteringCoefficients")
    connect(vec(m, "Absorption", X + 2200, 600, (0.35, 0.06, 0.025)), "", out, "AbsorptionCoefficients")
    connect(const(m, X + 2200, 700, 0.1), "", out, "PhaseG")
    connect(const(m, X + 2200, 780, 1.0), "", out, "ColorScaleBehindWater")   # 0 would hide everything under the surface
    vec(m, "SurfaceColor", X + 2200, 380, (0.1, 0.14, 0.14))  # kept for tools that read it; SLW colours come from the coefficients
    mel.recompile_material(m)
    eal.save_loaded_asset(m)
    unreal.log("RIVER material built")


build()
