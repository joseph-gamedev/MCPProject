"""
Creates the RuntimeToolsets builder materials in the plugin's content folder (editor only, run once):

    UnrealEditor-Cmd.exe MCPProject.uproject -run=pythonscript -script="<abs path>/Scripts/create_materials.py" -unattended -nullrhi

/RuntimeToolsets/Materials/M_RT_PBR    opaque PBR with world-projected procedural surfaces (no UVs needed):
                                       Color, Roughness, Metallic, Specular, Emissive + EmissiveStrength,
                                       Style (0 smooth, 1 brick, 2 blocks, 3 marble, 4 plaster, 5 concrete, 6 roof tiles,
                                       7 planks, 8 brushed metal, 9 ground, 10 windows, 11 curtain wall; TileCm = storey height for 10-11), TileCm, Relief (bump cm), Variation,
                                       PartWeathering (0..1, -1 = scene Weathering): dirt, streaks, dust; per-object tint jitter
/RuntimeToolsets/Materials/M_RT_Glass  translucent glass: Color, Opacity, Roughness, Metallic
"""

import os

import unreal

FOLDER = "/RuntimeToolsets/Materials"
lib = unreal.MaterialEditingLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()


def fresh_material(name, folder=None):
    """Existing asset: clear its graph and reuse it (keeps references); otherwise create it."""
    folder = folder or FOLDER
    mat = unreal.load_asset(f"{folder}/{name}.{name}")
    if mat:
        lib.delete_all_material_expressions(mat)
    else:
        mat = tools.create_asset(name, folder, unreal.Material, unreal.MaterialFactoryNew())
    # Shaders for these uses must be compiled up front: cooked builds cannot add them later
    # (otherwise PCG instances, spline meshes and Niagara meshes fall back to the default material).
    for usage in ("used_with_instanced_static_meshes", "used_with_spline_meshes", "used_with_niagara_mesh_particles"):
        try:
            mat.set_editor_property(usage, True)
        except Exception:
            pass
    return mat


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


SURFACE_HLSL = r"""
struct RTSurface
{
    float H21(float2 p)
    {
        p = frac(p * float2(123.34, 456.21));
        p += dot(p, p + 45.32);
        return frac(p.x * p.y);
    }
    float VNoise(float2 p)
    {
        float2 i = floor(p);
        float2 f = frac(p);
        f = f * f * (3.0 - 2.0 * f);
        float a = H21(i), b = H21(i + float2(1, 0)), c = H21(i + float2(0, 1)), d = H21(i + float2(1, 1));
        return lerp(lerp(a, b, f.x), lerp(c, d, f.x), f.y);
    }
    float Fbm(float2 p)
    {
        float s = 0.0, a = 0.5;
        for (int k = 0; k < 4; k++) { s += a * VNoise(p); p = p * 2.03 + 17.1; a *= 0.5; }
        return s / 0.9375;
    }
    float Joints(float2 uv, float2 size, float joint, float offset, out float2 id)
    {
        float row = floor(uv.y / size.y);
        float x = uv.x / size.x + offset * fmod(abs(row), 2.0);
        id = float2(floor(x), row);
        float2 f = float2(frac(x), frac(uv.y / size.y));
        float2 d = min(f, 1.0 - f) * size;
        float e = min(d.x, d.y);
        float w = max(fwidth(e), 1e-4);
        return 1.0 - smoothstep(joint - w, joint + w, e);
    }
    // x albedo multiplier, y roughness multiplier, z height 0..1, w joint mask
    float4 Eval(float2 uv, float style, float tile, float var)
    {
        float2 id = 0;
        float n = Fbm(uv / tile * 4.0);
        float big = Fbm(uv / (tile * 12.0));
        if (style < 0.5)
            return float4(1.0 - var * (big - 0.5) * 2.0, 1.0, 0.0, 0.0);
        if (style < 1.5)
        {
            float j = Joints(uv, float2(tile, tile * 0.32), tile * 0.02, 0.5, id);
            float tint = 1.0 + (H21(id) - 0.5) * var * 2.5 + (n - 0.5) * 0.15;
            return float4(tint, lerp(1.0, 1.15, j), (1.0 - j) * (0.85 + 0.15 * n), j);
        }
        if (style < 2.5)
        {
            float j = Joints(uv, float2(tile, tile * 0.5), tile * 0.008, 0.5, id);
            float tint = 1.0 + (H21(id) - 0.5) * var * 1.6 + (n - 0.5) * var;
            return float4(tint, 1.0 + (n - 0.5) * 0.2, (1.0 - j) * (0.8 + 0.2 * n), j * 0.7);
        }
        if (style < 3.5)
        {
            float2 q = uv / tile;
            float v = abs(sin((q.x + q.y * 0.6) * 3.0 + Fbm(q * 2.0) * 6.0));
            float vein = pow(saturate(1.0 - v), 14.0);
            float slab = Joints(uv, float2(tile, tile), tile * 0.002, 0.0, id);
            return float4(1.0 - vein * 0.55 + (n - 0.5) * var + (H21(id) - 0.5) * var, 1.0 - vein * 0.3, 1.0 - slab, slab * 0.4);
        }
        if (style < 4.5)
            return float4(1.0 + (n - 0.5) * var * 2.0 + (big - 0.5) * var, 1.0 + (n - 0.5) * 0.3, n, 0.0);
        if (style < 5.5)
        {
            float j = Joints(uv, float2(tile, tile * 0.5), tile * 0.004, 0.0, id);
            float tint = 1.0 + (H21(id) - 0.5) * var + (n - 0.5) * var;
            return float4(tint, 1.0 + (n - 0.5) * 0.2, (1.0 - j) * (0.9 + 0.1 * n), j * 0.6);
        }
        if (style < 6.5)
        {
            float rowH = tile * 0.7;
            float row = floor(uv.y / rowH);
            float x = uv.x / tile + 0.5 * fmod(abs(row), 2.0);
            id = float2(floor(x), row);
            float fy = frac(uv.y / rowH);
            float edge = min(frac(x), 1.0 - frac(x));
            float w = max(fwidth(edge), 1e-4);
            float gap = 1.0 - smoothstep(0.03 - w, 0.03 + w, edge);
            float h = (1.0 - fy) * (1.0 - gap) * (0.9 + 0.1 * n);
            float tint = 1.0 + (H21(id) - 0.5) * var * 2.0 - fy * 0.15;
            return float4(tint, 1.0, h, gap * 0.8);
        }
        if (style < 7.5)
        {
            float j = Joints(float2(uv.y, uv.x), float2(tile * 12.0, tile), tile * 0.015, 0.37, id);
            float grain = Fbm(float2(uv.x / tile * 8.0, uv.y / (tile * 20.0)));
            float tint = 1.0 + (H21(id) - 0.5) * var * 2.0 + (grain - 0.5) * 0.35;
            return float4(tint, 1.0 + (grain - 0.5) * 0.2, (1.0 - j) * (0.9 + 0.1 * grain), j * 0.8);
        }
        if (style < 8.5)
        {
            float b = VNoise(float2(uv.x / tile * 0.5, uv.y / tile * 60.0));
            return float4(1.0 + (b - 0.5) * 0.12 + (big - 0.5) * var, 1.0 + (b - 0.5) * 0.5, b * 0.2, 0.0);
        }
        if (style > 10.5)
        {
            // curtain wall: glass with slim mullions every ~1.5 m and an opaque band at each floor (tile = storey height)
            float2 q = uv / float2(tile * 0.45, tile);
            float2 f = frac(q);
            id = floor(q);
            float mul = 1.0 - smoothstep(0.0, max(fwidth(f.x), 1e-4) * 1.5, min(f.x, 1.0 - f.x) - 0.025);
            float band = 1.0 - smoothstep(0.0, max(fwidth(f.y), 1e-4) * 1.5, min(f.y, 1.0 - f.y) - 0.07);
            float frame = max(mul, band);
            float pane = 0.9 + 0.2 * H21(id);
            return float4(lerp(0.55 * pane, 1.0, frame), lerp(0.08, 1.0, frame), frame, frame);
        }
        if (style > 9.5)
        {
            // punched windows: one per bay per storey (tile = storey height), shopfronts on the ground floor
            float bay = tile * 1.05;
            float2 q = float2(uv.x / bay, uv.y / tile);
            id = floor(q);
            float2 f = frac(q);
            bool shop = uv.y < tile * 1.25;
            float2 lo = shop ? float2(0.04, 0.06) : float2(0.2, 0.3);
            float2 hi = shop ? float2(0.96, 0.82) : float2(0.8, 0.86);
            float2 d = min(f - lo, hi - f);
            float2 w = max(fwidth(q) * 1.5, 1e-4);
            float win = smoothstep(0.0, w.x, d.x) * smoothstep(0.0, w.y, d.y);
            float sill = (!shop && f.y < lo.y && f.y > lo.y - 0.05 && f.x > lo.x - 0.03 && f.x < hi.x + 0.03) ? 1.0 : 0.0;
            float glass = 0.07 + 0.12 * H21(id + 0.37);
            return float4(lerp(1.0 + (n - 0.5) * var + sill * 0.25, glass, win), lerp(1.0, 0.1, win), 1.0 - win * 0.9, sill * 0.5);
        }
        float g = Fbm(uv / tile * 8.0);
        return float4(1.0 + (n - 0.5) * var * 3.0 + (g - 0.5) * var * 2.0, 1.0 + (g - 0.5) * 0.2, n * 0.6 + g * 0.4, 0.0);
    }
    // Weathering mask 0..1: splash dirt on the bottom of walls, rain streaks down vertical faces, large grime patches.
    float Dirt(float3 p, float3 a, float2 uv)
    {
        float vert = saturate(1.0 - a.z * 1.4);
        float splash = (1.0 - saturate(p.z / 90.0)) * vert;
        float u = a.x > a.y ? p.y : p.x;
        float streak = pow(VNoise(float2(u / 28.0, p.z / 1400.0)), 3.0) * vert;
        float grime = Fbm(uv / 260.0);
        return saturate(splash * 0.9 + streak * 0.8 + (grime - 0.45) * 0.7);
    }
};
RTSurface S;
float3 nrm = normalize(N);
float3 an = abs(nrm);
float2 uv = an.z > 0.7 ? P.xy : (an.x > an.y ? P.yz : P.xz);
// window styles only on walls: roofs and floors get the plain surface
float st = (Style > 9.5 && an.z > 0.7) ? 0.0 : Style;
float4 sv = S.Eval(uv, st, max(Tile, 1.0), Variation);
// per-object random value (object position): every building/prop differs a little in colour and roughness
float objR = S.H21(Obj.xy * 0.00731 + Obj.z * 0.00517 + 3.1);
float W = saturate(PartWeathering >= 0.0 ? PartWeathering : Weathering);
float dirt = W > 0.001 ? S.Dirt(P, an, uv) * W : 0.0;
"""

TAILS = {
    "albedo": ("""
float3 baseC = Color * lerp(1.0, max(sv.x, 0.0), Pattern);
float3 jointC = (Style > 0.5 && Style < 1.5) ? float3(0.55, 0.53, 0.5) : Color * 0.6;
float3 c = lerp(baseC, jointC, saturate(sv.w) * Pattern);
c *= 1.0 + (objR - 0.5) * 0.14 * saturate(Variation * 3.0 + 0.25);
c = lerp(c, c * float3(0.52, 0.48, 0.43), dirt);
float top = saturate((nrm.z - 0.75) / 0.25) * W * (0.25 + 0.5 * S.Fbm(uv / 90.0));
c = lerp(c, lerp(c, float3(0.42, 0.40, 0.36), 0.5), top * 0.6);
float lum = dot(c, float3(0.2126, 0.7152, 0.0722));
c = max(lerp(float3(lum, lum, lum), c, Saturation), 0.0) * Tint.rgb;
if (Steps > 0.5)
{
    // flat palette: quantize in a perceptual space
    float3 g = pow(c, 1.0 / 2.2);
    g = floor(g * Steps + 0.5) / Steps;
    c = pow(max(g, 0.0), 2.2);
}
return c;
""", "CMOT_FLOAT3"),
    "rough": ("\nreturn lerp(1.0, sv.y, Pattern) * (1.0 + dirt * 0.35) * (1.0 + (objR - 0.5) * 0.2);\n", "CMOT_FLOAT1"),
    "normal": ("""
float hgt = sv.z * Relief * Pattern;
float3 dpdx = ddx(P);
float3 dpdy = ddy(P);
float3 outN = nrm;
float3 r1 = cross(dpdy, nrm);
float3 r2 = cross(nrm, dpdx);
float det = dot(dpdx, r1);
if (abs(det) > 1e-12 && Relief > 0.0 && Pattern > 0.0)
{
    float3 grad = sign(det) * (ddx(hgt) * r1 + ddy(hgt) * r2);
    outN = normalize(abs(det) * nrm - grad);
}
if (Faceted > 0.01)
{
    // flat shading: the true face normal from screen-space derivatives (low-poly look)
    float3 fn = normalize(cross(dpdy, dpdx));
    fn = dot(fn, nrm) < 0.0 ? -fn : fn;
    outN = normalize(lerp(outN, fn, saturate(Faceted)));
}
return outN;
""", "CMOT_FLOAT3"),
    "rim": ("""
float fres = pow(saturate(1.0 - dot(nrm, normalize(Cam))), 3.0);
return RimColor.rgb * fres * RimStrength;
""", "CMOT_FLOAT3"),
}

STYLE_SCALARS = [("Faceted", 0.0), ("Pattern", 1.0), ("Steps", 0.0), ("Saturation", 1.0), ("RimStrength", 0.0), ("Weathering", 0.25), ("Mud", 1.0)]
STYLE_VECTORS = [("Tint", (1.0, 1.0, 1.0, 1.0)), ("RimColor", (1.0, 0.9, 0.7, 1.0))]


def style_collection():
    """Global art-style switches shared by every builder material (set at runtime by the style toolset)."""
    name = "MPC_RT_Style"
    mpc = unreal.load_asset(f"{FOLDER}/{name}.{name}")
    if not mpc:
        mpc = tools.create_asset(name, FOLDER, unreal.MaterialParameterCollection, unreal.MaterialParameterCollectionFactoryNew())
    # keep the parameters already there (materials reference them by id; new ids would break every built material)
    scalars = list(mpc.get_editor_property("scalar_parameters"))
    vectors = list(mpc.get_editor_property("vector_parameters"))
    have = {str(p.get_editor_property("parameter_name")) for p in scalars + vectors}
    for n, v in STYLE_SCALARS:
        if n not in have:
            p = unreal.CollectionScalarParameter()
            p.set_editor_property("parameter_name", n)
            p.set_editor_property("default_value", v)
            scalars.append(p)
    for n, v in STYLE_VECTORS:
        if n not in have:
            p = unreal.CollectionVectorParameter()
            p.set_editor_property("parameter_name", n)
            p.set_editor_property("default_value", unreal.LinearColor(*v))
            vectors.append(p)
    mpc.set_editor_property("scalar_parameters", scalars)
    mpc.set_editor_property("vector_parameters", vectors)
    unreal.EditorAssetLibrary.save_loaded_asset(mpc)
    unreal.log("Created MPC_RT_Style")
    return mpc


def collection_param(mat, mpc, name, x, y):
    node = lib.create_material_expression(mat, unreal.MaterialExpressionCollectionParameter, x, y)
    node.set_editor_property("collection", mpc)
    node.set_editor_property("parameter_name", name)
    return node


def surface_node(mat, mode, x, y, sources):
    node = lib.create_material_expression(mat, unreal.MaterialExpressionCustom, x, y)
    tail, out = TAILS[mode]
    node.set_editor_property("code", SURFACE_HLSL + tail)
    node.set_editor_property("output_type", getattr(unreal.CustomMaterialOutputType, out))
    node.set_editor_property("description", "RT surface " + mode)
    inputs = []
    for name in sources:
        ci = unreal.CustomInput()
        ci.set_editor_property("input_name", name)
        inputs.append(ci)
    node.set_editor_property("inputs", inputs)
    for name, src in sources.items():
        lib.connect_material_expressions(src, "", node, name)
    return node


def vertex_or(mat, param, channel, x, y):
    """lerp(param, the part's own value, UseVertexColor): assemblies carry each part's linear colour in UV1 (r, g) and
    UV2 (b, roughness), so parts that differ only in colour or roughness share one material section."""
    tc1 = lib.create_material_expression(mat, unreal.MaterialExpressionTextureCoordinate, x - 500, y + 40)
    tc1.set_editor_property("coordinate_index", 1)
    tc2 = lib.create_material_expression(mat, unreal.MaterialExpressionTextureCoordinate, x - 500, y + 120)
    tc2.set_editor_property("coordinate_index", 2)
    use = scalar(mat, "UseVertexColor", 0.0, x - 400, y + 200)
    mix = lib.create_material_expression(mat, unreal.MaterialExpressionLinearInterpolate, x, y)
    if channel == "rgb":
        mask = lib.create_material_expression(mat, unreal.MaterialExpressionComponentMask, x - 250, y - 60)
        for c, v in zip("rgba", (True, True, True, False)):
            mask.set_editor_property(c, v)
        lib.connect_material_expressions(param, "", mask, "")
        b = lib.create_material_expression(mat, unreal.MaterialExpressionComponentMask, x - 350, y + 120)
        for c, v in zip("rgba", (True, False, False, False)):
            b.set_editor_property(c, v)
        lib.connect_material_expressions(tc2, "", b, "")
        rgb = lib.create_material_expression(mat, unreal.MaterialExpressionAppendVector, x - 250, y + 60)
        lib.connect_material_expressions(tc1, "", rgb, "A")
        lib.connect_material_expressions(b, "", rgb, "B")
        lib.connect_material_expressions(mask, "", mix, "A")
        lib.connect_material_expressions(rgb, "", mix, "B")
    else:
        g = lib.create_material_expression(mat, unreal.MaterialExpressionComponentMask, x - 350, y + 120)
        for c, v in zip("rgba", (False, True, False, False)):
            g.set_editor_property(c, v)
        lib.connect_material_expressions(tc2, "", g, "")
        lib.connect_material_expressions(param, "", mix, "A")
        lib.connect_material_expressions(g, "", mix, "B")
    lib.connect_material_expressions(use, "", mix, "Alpha")
    return mix


def build_pbr(mpc, name="M_RT_PBR", folder=None, weather=None):
    """weather: a function(mat, outputs) that wires the surface outputs {property: node} itself (weather variants)."""
    mat = fresh_material(name, folder)
    outputs = {}

    def out(node, prop):
        if weather:
            outputs[prop] = node
        else:
            lib.connect_material_property(node, "", prop)
    mat.set_editor_property("tangent_space_normal", False)
    world = lib.create_material_expression(mat, unreal.MaterialExpressionWorldPosition, -1500, 0)
    normal = lib.create_material_expression(mat, unreal.MaterialExpressionVertexNormalWS, -1500, 150)
    sources = {
        "P": world,
        "N": normal,
        "Color": vertex_or(mat, vector(mat, "Color", (0.8, 0.8, 0.8, 1.0), -1900, -300), "rgb", -1500, -300),
        "Style": scalar(mat, "Style", 0.0, -1500, 300),
        "Tile": scalar(mat, "TileCm", 100.0, -1500, 400),
        "Relief": scalar(mat, "Relief", 0.0, -1500, 500),
        "Variation": scalar(mat, "Variation", 0.12, -1500, 600),
        "Cam": lib.create_material_expression(mat, unreal.MaterialExpressionCameraVectorWS, -1500, 700),
        "Obj": lib.create_material_expression(mat, unreal.MaterialExpressionObjectPositionWS, -1500, 800),
        # per-part weathering 0..1; -1 = use the scene's Weathering (style collection)
        "PartWeathering": scalar(mat, "PartWeathering", -1.0, -1500, 900),
    }
    for i, (n, _) in enumerate(STYLE_SCALARS + STYLE_VECTORS):
        sources[n] = collection_param(mat, mpc, n, -1800, -300 + i * 110)
    albedo = surface_node(mat, "albedo", -900, -200, sources)
    rough = surface_node(mat, "rough", -900, 50, sources)
    nrm = surface_node(mat, "normal", -900, 300, sources)
    out(albedo, unreal.MaterialProperty.MP_BASE_COLOR)
    rmul = lib.create_material_expression(mat, unreal.MaterialExpressionMultiply, -500, 50)
    lib.connect_material_expressions(vertex_or(mat, scalar(mat, "Roughness", 0.5, -1000, 0), "a", -700, 0), "", rmul, "A")
    lib.connect_material_expressions(rough, "", rmul, "B")
    rsat = lib.create_material_expression(mat, unreal.MaterialExpressionSaturate, -350, 50)
    lib.connect_material_expressions(rmul, "", rsat, "")
    out(rsat, unreal.MaterialProperty.MP_ROUGHNESS)
    out(nrm, unreal.MaterialProperty.MP_NORMAL)
    out(scalar(mat, "Metallic", 0.0, -300, 200), unreal.MaterialProperty.MP_METALLIC)
    out(scalar(mat, "Specular", 0.5, -300, 300), unreal.MaterialProperty.MP_SPECULAR)
    emissive = vector(mat, "Emissive", (0.0, 0.0, 0.0, 1.0), -500, 450)
    strength = scalar(mat, "EmissiveStrength", 0.0, -500, 600)
    glow = lib.create_material_expression(mat, unreal.MaterialExpressionMultiply, -300, 450)
    lib.connect_material_expressions(emissive, "", glow, "A")
    lib.connect_material_expressions(strength, "", glow, "B")
    rim = surface_node(mat, "rim", -900, 550, sources)
    total = lib.create_material_expression(mat, unreal.MaterialExpressionAdd, -200, 500)
    lib.connect_material_expressions(glow, "", total, "A")
    lib.connect_material_expressions(rim, "", total, "B")
    out(total, unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    if weather:
        weather(mat, outputs)
    lib.recompile_material(mat)
    unreal.EditorAssetLibrary.save_loaded_asset(mat)
    unreal.log("Created " + name)


PP_HLSL = r"""
struct PPS
{
    float Lum(float3 c) { return dot(c, float3(0.2126, 0.7152, 0.0722)); }
    float Depth(float2 uv) { return SceneTextureLookup(uv, PPI_SceneDepth, false).r; }
    float3 Nrm(float2 uv) { return SceneTextureLookup(uv, PPI_WorldNormal, false).rgb; }
    float3 Col(float2 uv) { return SceneTextureLookup(uv, PPI_PostProcessInput0, false).rgb; }
};
PPS F;
float2 UV = GetDefaultSceneTextureUV(Parameters, PPI_PostProcessInput0);
float2 T = View.BufferSizeAndInvSize.zw;   // one pixel in UV
// the pixel itself comes from the scene texture inputs (ST0-ST3), already sampled at the pixelated UV by the graph
float3 col = ST0.rgb;
float depth = ST1.r;
bool sky = depth > 2000000.0;

// Painterly: Kuwahara filter (keeps edges, flattens detail into brush-like patches).
if (Paint > 0.01 && !sky)
{
    int R = clamp((int)PaintRadius, 1, 5);
    float3 m[4];
    float3 v[4];
    for (int q = 0; q < 4; q++) { m[q] = 0; v[q] = 0; }
    float n = (R + 1) * (R + 1);
    [loop] for (int y = -R; y <= R; y++)
    {
        [loop] for (int x = -R; x <= R; x++)
        {
            float3 c = F.Col(UV + float2(x, y) * T * PaintScale);
            if (x <= 0 && y <= 0) { m[0] += c; v[0] += c * c; }
            if (x >= 0 && y <= 0) { m[1] += c; v[1] += c * c; }
            if (x <= 0 && y >= 0) { m[2] += c; v[2] += c * c; }
            if (x >= 0 && y >= 0) { m[3] += c; v[3] += c * c; }
        }
    }
    float best = 1e20;
    float3 pick = col;
    for (int k = 0; k < 4; k++)
    {
        float3 mean = m[k] / n;
        float3 var = abs(v[k] / n - mean * mean);
        float s = var.r + var.g + var.b;
        if (s < best) { best = s; pick = mean; }
    }
    col = lerp(col, pick, Paint);
}

// Toon: quantize the lighting into bands while keeping the surface colour.
if (Toon > 0.01 && !sky)
{
    float3 base = ST3.rgb;
    float3 light = col / max(base, 0.02);
    float L = F.Lum(light);
    float q = max(floor(L * Bands + 0.5) / Bands, ShadowFloor);
    float3 toon = L > 1e-4 ? base * light * (q / L) : base * q;
    col = lerp(col, toon, Toon);
}

// Ink outlines from depth and normal discontinuities.
if (Outline > 0.01)
{
    float2 o = T * OutlineWidth;
    float3 n0 = ST2.rgb;
    float dsum = 0.0;
    float nsum = 0.0;
    float2 offs[4] = { float2(o.x, 0), float2(-o.x, 0), float2(0, o.y), float2(0, -o.y) };
    for (int i = 0; i < 4; i++)
    {
        float d = F.Depth(UV + offs[i]);
        dsum += abs(min(d, 2000000.0) - min(depth, 2000000.0));
        nsum += 1.0 - saturate(dot(n0, F.Nrm(UV + offs[i])));
    }
    float de = saturate(dsum / max(min(depth, 2000000.0), 1.0) * DepthSens - 0.15);
    float ne = sky ? 0.0 : saturate(nsum * NormalSens - 0.25);
    float e = saturate(max(de, ne));
    col = lerp(col, OutlineColor.rgb, e * Outline);
}

// Grading.
float lum = F.Lum(col);
col = lerp(float3(lum, lum, lum), col, Saturation);
col = max((col - 0.18) * Contrast + 0.18, 0.0) * Tint.rgb;
if (Levels > 1.5)
{
    float3 g = pow(saturate(col / (col + 1.0)), 1.0 / 2.2);
    g = floor(g * Levels + 0.5) / Levels;
    g = pow(g, 2.2);
    col = g / max(1.0 - g, 0.001);
}
return col;
"""

PP_SCALARS = [("PixelSize", 0.0), ("Paint", 0.0), ("PaintRadius", 3.0), ("PaintScale", 1.0), ("Toon", 0.0), ("Bands", 3.0), ("ShadowFloor", 0.2),
              ("Outline", 0.0), ("OutlineWidth", 1.0), ("DepthSens", 8.0), ("NormalSens", 1.5), ("Saturation", 1.0), ("Contrast", 1.0), ("Levels", 0.0)]
PP_VECTORS = [("OutlineColor", (0.02, 0.02, 0.03, 1.0)), ("Tint", (1.0, 1.0, 1.0, 1.0))]


def build_post():
    mat = fresh_material("M_PP_Style")
    mat.set_editor_property("material_domain", unreal.MaterialDomain.MD_POST_PROCESS)
    loc = getattr(unreal.BlendableLocation, "BL_SCENE_COLOR_BEFORE_DOF", None) or getattr(unreal.BlendableLocation, "BL_BEFORE_TRANSLUCENCY")
    mat.set_editor_property("blendable_location", loc)
    node = lib.create_material_expression(mat, unreal.MaterialExpressionCustom, -400, 0)
    node.set_editor_property("code", PP_HLSL)
    node.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    node.set_editor_property("description", "RT art style")
    sources = {}
    for i, (n, v) in enumerate(PP_SCALARS):
        sources[n] = scalar(mat, n, v, -1100, -600 + i * 90)
    for i, (n, v) in enumerate(PP_VECTORS):
        sources[n] = vector(mat, n, v, -1100, 800 + i * 150)
    # Pixel art: snap the screen UV to PixelSize-pixel cells in the graph (cell 1 = unchanged) and sample the scene there.
    uv = lib.create_material_expression(mat, unreal.MaterialExpressionTextureCoordinate, -1700, 1300)
    vs = lib.create_material_expression(mat, unreal.MaterialExpressionViewSize, -1700, 1400)
    one = lib.create_material_expression(mat, unreal.MaterialExpressionConstant, -1700, 1500)
    one.set_editor_property("r", 1.0)
    cell = lib.create_material_expression(mat, unreal.MaterialExpressionMax, -1500, 1500)
    lib.connect_material_expressions(sources["PixelSize"], "", cell, "A")
    lib.connect_material_expressions(one, "", cell, "B")
    px = lib.create_material_expression(mat, unreal.MaterialExpressionMultiply, -1500, 1300)
    lib.connect_material_expressions(uv, "", px, "A")
    lib.connect_material_expressions(vs, "", px, "B")
    div = lib.create_material_expression(mat, unreal.MaterialExpressionDivide, -1350, 1300)
    lib.connect_material_expressions(px, "", div, "A")
    lib.connect_material_expressions(cell, "", div, "B")
    fl = lib.create_material_expression(mat, unreal.MaterialExpressionFloor, -1250, 1300)
    lib.connect_material_expressions(div, "", fl, "")
    half = lib.create_material_expression(mat, unreal.MaterialExpressionAdd, -1150, 1300)
    half.set_editor_property("const_b", 0.5)
    lib.connect_material_expressions(fl, "", half, "A")
    back = lib.create_material_expression(mat, unreal.MaterialExpressionMultiply, -1050, 1300)
    lib.connect_material_expressions(half, "", back, "A")
    lib.connect_material_expressions(cell, "", back, "B")
    snapped = lib.create_material_expression(mat, unreal.MaterialExpressionDivide, -950, 1300)
    lib.connect_material_expressions(back, "", snapped, "A")
    lib.connect_material_expressions(vs, "", snapped, "B")
    # Scene texture nodes: the pixel at the (snapped) UV, and the lookups available to the custom code.
    for i, tid in enumerate(["PPI_POST_PROCESS_INPUT0", "PPI_SCENE_DEPTH", "PPI_WORLD_NORMAL", "PPI_BASE_COLOR"]):
        st = lib.create_material_expression(mat, unreal.MaterialExpressionSceneTexture, -800, 900 + i * 120)
        st.set_editor_property("scene_texture_id", getattr(unreal.SceneTextureId, tid))
        lib.connect_material_expressions(snapped, "", st, "UVs")
        sources["ST%d" % i] = st
    inputs = []
    for name in sources:
        ci = unreal.CustomInput()
        ci.set_editor_property("input_name", name)
        inputs.append(ci)
    node.set_editor_property("inputs", inputs)
    for name, src in sources.items():
        lib.connect_material_expressions(src, "Color" if name.startswith("ST") else "", node, name)
    lib.connect_material_property(node, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    lib.recompile_material(mat)
    unreal.EditorAssetLibrary.save_loaded_asset(mat)
    unreal.log("Created M_PP_Style")


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


if not os.environ.get("RT_MATERIALS_LIBRARY_ONLY"):
    build_pbr(style_collection())
    build_glass()
    build_post()
