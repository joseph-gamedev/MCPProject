"""
Creates the RuntimeToolsets builder materials in the plugin's content folder (editor only, run once):

    UnrealEditor-Cmd.exe MCPProject.uproject -run=pythonscript -script="<abs path>/Scripts/create_materials.py" -unattended -nullrhi

/RuntimeToolsets/Materials/M_RT_PBR    opaque PBR with world-projected procedural surfaces (no UVs needed):
                                       Color, Roughness, Metallic, Specular, Emissive + EmissiveStrength,
                                       Style (0 smooth, 1 brick, 2 blocks, 3 marble, 4 plaster, 5 concrete, 6 roof tiles,
                                       7 planks, 8 brushed metal, 9 ground), TileCm, Relief (bump cm), Variation
/RuntimeToolsets/Materials/M_RT_Glass  translucent glass: Color, Opacity, Roughness, Metallic
"""

import unreal

FOLDER = "/RuntimeToolsets/Materials"
lib = unreal.MaterialEditingLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()


def fresh_material(name):
    """Existing asset: clear its graph and reuse it (keeps references); otherwise create it."""
    mat = unreal.load_asset(f"{FOLDER}/{name}.{name}")
    if mat:
        lib.delete_all_material_expressions(mat)
    else:
        mat = tools.create_asset(name, FOLDER, unreal.Material, unreal.MaterialFactoryNew())
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
        float g = Fbm(uv / tile * 8.0);
        return float4(1.0 + (n - 0.5) * var * 3.0 + (g - 0.5) * var * 2.0, 1.0 + (g - 0.5) * 0.2, n * 0.6 + g * 0.4, 0.0);
    }
};
RTSurface S;
float3 nrm = normalize(N);
float3 an = abs(nrm);
float2 uv = an.z > 0.7 ? P.xy : (an.x > an.y ? P.yz : P.xz);
float4 sv = S.Eval(uv, Style, max(Tile, 1.0), Variation);
"""

TAILS = {
    "albedo": ("""
float3 baseC = Color * lerp(1.0, max(sv.x, 0.0), Pattern);
float3 jointC = (Style > 0.5 && Style < 1.5) ? float3(0.55, 0.53, 0.5) : Color * 0.6;
float3 c = lerp(baseC, jointC, saturate(sv.w) * Pattern);
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
    "rough": ("\nreturn lerp(1.0, sv.y, Pattern);\n", "CMOT_FLOAT1"),
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

STYLE_SCALARS = [("Faceted", 0.0), ("Pattern", 1.0), ("Steps", 0.0), ("Saturation", 1.0), ("RimStrength", 0.0)]
STYLE_VECTORS = [("Tint", (1.0, 1.0, 1.0, 1.0)), ("RimColor", (1.0, 0.9, 0.7, 1.0))]


def style_collection():
    """Global art-style switches shared by every builder material (set at runtime by the style toolset)."""
    name = "MPC_RT_Style"
    mpc = unreal.load_asset(f"{FOLDER}/{name}.{name}")
    if not mpc:
        mpc = tools.create_asset(name, FOLDER, unreal.MaterialParameterCollection, unreal.MaterialParameterCollectionFactoryNew())
    scalars, vectors = [], []
    for n, v in STYLE_SCALARS:
        p = unreal.CollectionScalarParameter()
        p.set_editor_property("parameter_name", n)
        p.set_editor_property("default_value", v)
        scalars.append(p)
    for n, v in STYLE_VECTORS:
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


def build_pbr(mpc):
    mat = fresh_material("M_RT_PBR")
    mat.set_editor_property("tangent_space_normal", False)
    world = lib.create_material_expression(mat, unreal.MaterialExpressionWorldPosition, -1500, 0)
    normal = lib.create_material_expression(mat, unreal.MaterialExpressionVertexNormalWS, -1500, 150)
    sources = {
        "P": world,
        "N": normal,
        "Color": vector(mat, "Color", (0.8, 0.8, 0.8, 1.0), -1500, -300),
        "Style": scalar(mat, "Style", 0.0, -1500, 300),
        "Tile": scalar(mat, "TileCm", 100.0, -1500, 400),
        "Relief": scalar(mat, "Relief", 0.0, -1500, 500),
        "Variation": scalar(mat, "Variation", 0.12, -1500, 600),
        "Cam": lib.create_material_expression(mat, unreal.MaterialExpressionCameraVectorWS, -1500, 700),
    }
    for i, (n, _) in enumerate(STYLE_SCALARS + STYLE_VECTORS):
        sources[n] = collection_param(mat, mpc, n, -1800, -300 + i * 110)
    albedo = surface_node(mat, "albedo", -900, -200, sources)
    rough = surface_node(mat, "rough", -900, 50, sources)
    nrm = surface_node(mat, "normal", -900, 300, sources)
    lib.connect_material_property(albedo, "", unreal.MaterialProperty.MP_BASE_COLOR)
    rmul = lib.create_material_expression(mat, unreal.MaterialExpressionMultiply, -500, 50)
    lib.connect_material_expressions(scalar(mat, "Roughness", 0.5, -700, 0), "", rmul, "A")
    lib.connect_material_expressions(rough, "", rmul, "B")
    rsat = lib.create_material_expression(mat, unreal.MaterialExpressionSaturate, -350, 50)
    lib.connect_material_expressions(rmul, "", rsat, "")
    lib.connect_material_property(rsat, "", unreal.MaterialProperty.MP_ROUGHNESS)
    lib.connect_material_property(nrm, "", unreal.MaterialProperty.MP_NORMAL)
    lib.connect_material_property(scalar(mat, "Metallic", 0.0, -300, 200), "", unreal.MaterialProperty.MP_METALLIC)
    lib.connect_material_property(scalar(mat, "Specular", 0.5, -300, 300), "", unreal.MaterialProperty.MP_SPECULAR)
    emissive = vector(mat, "Emissive", (0.0, 0.0, 0.0, 1.0), -500, 450)
    strength = scalar(mat, "EmissiveStrength", 0.0, -500, 600)
    glow = lib.create_material_expression(mat, unreal.MaterialExpressionMultiply, -300, 450)
    lib.connect_material_expressions(emissive, "", glow, "A")
    lib.connect_material_expressions(strength, "", glow, "B")
    rim = surface_node(mat, "rim", -900, 550, sources)
    total = lib.create_material_expression(mat, unreal.MaterialExpressionAdd, -200, 500)
    lib.connect_material_expressions(glow, "", total, "A")
    lib.connect_material_expressions(rim, "", total, "B")
    lib.connect_material_property(total, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    lib.recompile_material(mat)
    unreal.EditorAssetLibrary.save_loaded_asset(mat)
    unreal.log("Created M_RT_PBR")


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
float2 T = GetSceneTextureBufferSize(PPI_PostProcessInput0).zw;
if (PixelSize > 1.5)
{
    float2 cell = T * PixelSize;
    UV = (floor(UV / cell) + 0.5) * cell;
}
float3 col = F.Col(UV);
float depth = F.Depth(UV);
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
    float3 base = SceneTextureLookup(UV, PPI_BaseColor, false).rgb;
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
    float3 n0 = F.Nrm(UV);
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
    # Scene texture nodes make the lookups available to the custom code.
    for i, tid in enumerate(["PPI_POST_PROCESS_INPUT0", "PPI_SCENE_DEPTH", "PPI_WORLD_NORMAL", "PPI_BASE_COLOR"]):
        st = lib.create_material_expression(mat, unreal.MaterialExpressionSceneTexture, -800, 900 + i * 120)
        st.set_editor_property("scene_texture_id", getattr(unreal.SceneTextureId, tid))
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


build_pbr(style_collection())
build_glass()
build_post()
