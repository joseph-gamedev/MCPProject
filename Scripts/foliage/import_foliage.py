"""Imports the foliage into /Game/RuntimeFoliage with scanned PBR materials, ready for scattering (instanced) and Nanite.

    UnrealEditor-Cmd.exe MCPProject.uproject -run=pythonscript -script="<repo>/Scripts/foliage/import_foliage.py" -unattended -nullrhi

Inputs (Saved/): Foliage/SM_*.fbx (Blender trees/plants), Foliage/SM_PH_*.fbx + .json (converted Poly Haven models),
Foliage/T_LeafSingle_*.png (generated leaf cards), FoliageSources/bark/*_2k.jpg (Poly Haven bark, CC0),
FoliageSources/leaf/LeafCrop_*.png (ambientCG Leaf001, CC0, one leaf cropped square). Masters:
  M_Foliage_Leaf    masked, two-sided foliage: Leaf (colour+alpha) or Opacity map, Normal, Tint, translucency, per-instance hue
  M_Foliage_Bark    world-projected scanned bark (no UVs needed): colour, normal, roughness, tile size, tint
  M_Foliage_Surface UV-mapped scanned PBR for opaque parts of scanned models (trunks, rocks): colour, normal, ARM
Every master is flagged for instanced static meshes (pcg.scatter), works with Nanite, and moves in the wind
(MPC_Wind: WindStrength, WindSpeed, WindDirection; per instance WindHeight, WindAmount).
"""

import glob
import json
import os

import unreal

SAVED = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "Saved"))
SRC = os.path.join(SAVED, "Foliage")
BARK_SRC = os.path.join(SAVED, "FoliageSources", "bark")
LEAF_SRC = os.path.join(SAVED, "FoliageSources", "leaf")
ROOT = "/Game/RuntimeFoliage"
TEX, MAT, MESH, SCAN = ROOT + "/Textures", ROOT + "/Materials", ROOT + "/Trees", ROOT + "/Scanned"

tools = unreal.AssetToolsHelpers.get_asset_tools()
mel = unreal.MaterialEditingLibrary
eal = unreal.EditorAssetLibrary
WHITE = unreal.load_asset("/Engine/EngineResources/WhiteSquareTexture")
FLAT = unreal.load_asset("/Engine/EngineMaterials/DefaultNormal")

# Sapling bark slot -> (Poly Haven bark, tint)
BARK = {"oak": ("jolcham_oak_bark_01", (1, 1, 1)), "maple": ("bark_platanus", (1, 1, 1)), "japanese_maple": ("bark_platanus", (0.8, 0.75, 0.75)),
        "willow": ("bark_willow", (1, 1, 1)), "pine": ("pine_bark", (1, 1, 1)), "fir": ("pine_bark", (0.85, 0.8, 0.8)),
        "aspen": ("eucalyptus_bark", (1.25, 1.25, 1.2)), "birch": ("eucalyptus_bark", (1.9, 1.9, 1.85)), "bottlebrush": ("bark_platanus", (0.9, 0.85, 0.8)),
        "palm_coconut": ("palm_tree_bark", (1, 1, 1)), "palm_date": ("palm_tree_bark", (0.9, 0.85, 0.8)), "default": ("bark_platanus", (0.9, 0.85, 0.8))}


def import_file(path, dest, options=None):
    task = unreal.AssetImportTask()
    task.filename = path
    task.destination_path = dest
    task.automated = True
    task.replace_existing = True
    task.save = True
    if options:
        task.options = options
    tools.import_asset_tasks([task])
    paths = task.get_editor_property("imported_object_paths")
    return unreal.load_asset(paths[0]) if paths else None


def import_texture(path, dest, kind="color"):
    tex = import_file(path, dest)
    if not tex:
        unreal.log_error(f"FOLIAGE texture failed: {path}")
        return None
    if kind == "normal":
        tex.set_editor_property("srgb", False)
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
    elif kind == "mask":
        tex.set_editor_property("srgb", False)
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_MASKS)
    eal.save_loaded_asset(tex)
    return tex


_WHITE_MASK = []


def white_mask():
    """A linear white mask texture: the default for mask-type parameters (a colour default fails to compile)."""
    if not _WHITE_MASK:
        import struct
        import zlib
        png = os.path.join(SRC, "T_WhiteMask.png")
        raw = (bytes([0]) + bytes([255]) * 4) * 4  # 4x4 8-bit greyscale, filter byte per row

        def chunk(t, d):
            return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
        with open(png, "wb") as f:
            f.write(bytes([137]) + b"PNG" + bytes([13, 10, 26, 10]) + chunk(b"IHDR", struct.pack(">IIBBBBB", 4, 4, 8, 0, 0, 0, 0))
                    + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))
        _WHITE_MASK.append(import_texture(png, TEX, "mask"))
    return _WHITE_MASK[0]


def new_material(name):
    """The master material with an empty graph: reused when it exists, so instances keep pointing at it."""
    path = f"{MAT}/{name}"
    if eal.does_asset_exist(path):
        m = unreal.load_asset(path)
        mel.delete_all_material_expressions(m)
    else:
        m = tools.create_asset(name, MAT, unreal.Material, unreal.MaterialFactoryNew())
    # scattered plants are instanced static meshes: without this flag they render with the default (grey) material
    m.set_editor_property("used_with_instanced_static_meshes", True)
    return m


def expr(m, cls, x, y, **props):
    e = mel.create_material_expression(m, cls, x, y)
    for k, v in props.items():
        e.set_editor_property(k, v)
    return e


def tex_param(m, name, x, y, default, sampler=None):
    e = expr(m, unreal.MaterialExpressionTextureSampleParameter2D, x, y, parameter_name=name, texture=default)
    if sampler is not None:
        e.set_editor_property("sampler_type", sampler)
    return e


WIND_MPC = MAT + "/MPC_Wind"


def wind_collection():
    """Global wind: WindStrength (0 calm, 0.4 light breeze, 1 windy, 3 storm), WindSpeed, SwayAmount (cm at the top
    per unit of strength), LeafFlutter (cm), Gustiness (0 steady - 1 strong gusts), WindDirection (x, y). Change it live with
    materials.set_collection_param {collection: "/Game/RuntimeFoliage/Materials/MPC_Wind", name, value}."""
    if eal.does_asset_exist(WIND_MPC):
        mpc = unreal.load_asset(WIND_MPC)
    else:
        mpc = tools.create_asset("MPC_Wind", MAT, unreal.MaterialParameterCollection, unreal.MaterialParameterCollectionFactoryNew())
    # parameters are found by id (a GUID); Python cannot set it directly, so write the struct as text
    import re
    import uuid
    old = {}
    for prm in list(mpc.get_editor_property("scalar_parameters")) + list(mpc.get_editor_property("vector_parameters")):
        t = prm.export_text()
        n, g = re.search(r'ParameterName="?([A-Za-z]+)', t), re.search(r"Id=([0-9A-Fa-f]{32})", t)
        if n and g and g.group(1) != "0" * 32:
            old[n.group(1)] = g.group(1)

    def pid(name):
        return old.get(name) or uuid.uuid4().hex.upper()
    scalars = []
    for name, value in (("WindStrength", 0.4), ("WindSpeed", 0.6), ("SwayAmount", 10.0), ("LeafFlutter", 1.0), ("Gustiness", 0.5)):
        sp = unreal.CollectionScalarParameter()
        sp.import_text(f'(DefaultValue={value},ParameterName="{name}",Id={pid(name)})')
        scalars.append(sp)
    vp = unreal.CollectionVectorParameter()
    vp.import_text(f'(DefaultValue=(R=1.0,G=0.35,B=0.0,A=0.0),ParameterName="WindDirection",Id={pid("WindDirection")})')
    mpc.set_editor_property("scalar_parameters", scalars)
    mpc.set_editor_property("vector_parameters", [vp])
    eal.save_loaded_asset(mpc)
    unreal.log("FOLIAGE wind ids " + str([q.export_text() for q in scalars + [vp]]))
    return mpc


def connect(a, a_out, b, b_in):
    mel.connect_material_expressions(a, a_out, b, b_in)


def math2(m, cls, a, b, x, y, a_out="", b_out=""):
    e = expr(m, cls, x, y)
    connect(a, a_out, e, "A")
    connect(b, b_out, e, "B")
    return e


def add_wind(m, flutter):
    """World position offset: whole-plant sway that grows with height above the plant's pivot (squared, so trunks
    stay planted), a per-plant phase, plus fast leaf flutter on leaf materials. Driven by MPC_Wind."""
    mpc = wind_collection()
    x0, y0 = -2200, 1500

    def cp(name, y):
        return expr(m, unreal.MaterialExpressionCollectionParameter, x0, y0 + y, collection=mpc, parameter_name=name)  # name -> id lookup on edit
    strength, speed, direction = cp("WindStrength", 0), cp("WindSpeed", 100), cp("WindDirection", 200)
    sway_amount, flutter_amount, gustiness = cp("SwayAmount", -300), cp("LeafFlutter", -200), cp("Gustiness", -100)
    amount = expr(m, unreal.MaterialExpressionScalarParameter, x0, y0 + 300, parameter_name="WindAmount", default_value=1.0)
    height = expr(m, unreal.MaterialExpressionScalarParameter, x0, y0 + 400, parameter_name="WindHeight", default_value=1500.0)
    wp = expr(m, unreal.MaterialExpressionWorldPosition, x0, y0 + 500)
    op = expr(m, unreal.MaterialExpressionObjectPositionWS, x0, y0 + 600)
    time = expr(m, unreal.MaterialExpressionTime, x0, y0 + 700)
    # height factor: ((z - pivot z) / WindHeight)^2
    rel = math2(m, unreal.MaterialExpressionSubtract, wp, op, x0 + 250, y0 + 550)
    relz = expr(m, unreal.MaterialExpressionComponentMask, x0 + 450, y0 + 550, r=False, g=False, b=True, a=False)
    connect(rel, "", relz, "")
    hf = math2(m, unreal.MaterialExpressionDivide, relz, height, x0 + 650, y0 + 500)
    hf_sat = expr(m, unreal.MaterialExpressionSaturate, x0 + 850, y0 + 500)
    connect(hf, "", hf_sat, "")
    hf2 = math2(m, unreal.MaterialExpressionMultiply, hf_sat, hf_sat, x0 + 1000, y0 + 500)
    # phase per plant from its pivot, slow sway with a faster second harmonic (gusts)
    opxy = expr(m, unreal.MaterialExpressionComponentMask, x0 + 250, y0 + 650, r=True, g=True, b=False, a=False)
    connect(op, "", opxy, "")
    phase_dot = math2(m, unreal.MaterialExpressionDotProduct, opxy, expr(m, unreal.MaterialExpressionConstant2Vector, x0 + 250, y0 + 750, r=0.0011, g=0.0017),
                      x0 + 450, y0 + 650)
    t_speed = math2(m, unreal.MaterialExpressionMultiply, time, speed, x0 + 250, y0 + 850)
    arg1 = math2(m, unreal.MaterialExpressionAdd, math2(m, unreal.MaterialExpressionMultiply, t_speed,
                                                          expr(m, unreal.MaterialExpressionConstant, x0 + 300, y0 + 950, r=0.9), x0 + 450, y0 + 850),
                 phase_dot, x0 + 650, y0 + 750)
    s1 = expr(m, unreal.MaterialExpressionSine, x0 + 850, y0 + 750)
    connect(arg1, "", s1, "")
    arg2 = math2(m, unreal.MaterialExpressionMultiply, arg1, expr(m, unreal.MaterialExpressionConstant, x0 + 650, y0 + 900, r=2.7), x0 + 850, y0 + 850)
    s2 = expr(m, unreal.MaterialExpressionSine, x0 + 1000, y0 + 850)
    connect(arg2, "", s2, "")
    s2w = math2(m, unreal.MaterialExpressionMultiply, s2, expr(m, unreal.MaterialExpressionConstant, x0 + 1000, y0 + 950, r=0.35), x0 + 1150, y0 + 850)
    # lean downwind a little (bias 0.6) so gusts push rather than wobble symmetrically
    bias = expr(m, unreal.MaterialExpressionConstant, x0 + 1150, y0 + 700, r=0.6)
    sway = math2(m, unreal.MaterialExpressionAdd, math2(m, unreal.MaterialExpressionAdd, s1, s2w, x0 + 1300, y0 + 780), bias, x0 + 1450, y0 + 760)
    # gusts: a slow swell (period ~40 s, offset per plant) scales everything between 1 - G and 1 + G
    gust_arg = math2(m, unreal.MaterialExpressionAdd, math2(m, unreal.MaterialExpressionMultiply, t_speed,
                                                              expr(m, unreal.MaterialExpressionConstant, x0 + 300, y0 - 450, r=0.16), x0 + 450, y0 - 450),
                     math2(m, unreal.MaterialExpressionMultiply, phase_dot, expr(m, unreal.MaterialExpressionConstant, x0 + 300, y0 - 550, r=0.4),
                           x0 + 450, y0 - 550), x0 + 650, y0 - 500)
    gs = expr(m, unreal.MaterialExpressionSine, x0 + 850, y0 - 500)
    connect(gust_arg, "", gs, "")
    gust = math2(m, unreal.MaterialExpressionAdd, expr(m, unreal.MaterialExpressionConstant, x0 + 850, y0 - 600, r=1.0),
                 math2(m, unreal.MaterialExpressionMultiply, gs, gustiness, x0 + 1000, y0 - 500), x0 + 1150, y0 - 550)
    amp = math2(m, unreal.MaterialExpressionMultiply, sway_amount, gust, x0 + 1300, y0 + 600)
    k = math2(m, unreal.MaterialExpressionMultiply, math2(m, unreal.MaterialExpressionMultiply, strength, amount, x0 + 1300, y0 + 300), amp,
              x0 + 1450, y0 + 350)
    k2 = math2(m, unreal.MaterialExpressionMultiply, k, hf2, x0 + 1600, y0 + 450)
    k3 = math2(m, unreal.MaterialExpressionMultiply, k2, sway, x0 + 1750, y0 + 550)
    dir3 = expr(m, unreal.MaterialExpressionComponentMask, x0 + 1600, y0 + 200, r=True, g=True, b=True, a=False)
    connect(direction, "", dir3, "")
    wpo = math2(m, unreal.MaterialExpressionMultiply, dir3, k3, x0 + 1900, y0 + 300)
    if flutter:
        # leaf flutter: fast, small, varies across the crown (world position), mostly vertical
        wsum = expr(m, unreal.MaterialExpressionComponentMask, x0 + 250, y0 + 1050, r=True, g=True, b=True, a=False)
        connect(wp, "", wsum, "")
        wdot = math2(m, unreal.MaterialExpressionDotProduct, wsum, expr(m, unreal.MaterialExpressionConstant3Vector, x0 + 250, y0 + 1150,
                                                                         constant=unreal.LinearColor(0.031, 0.027, 0.043, 0)), x0 + 450, y0 + 1050)
        ft = math2(m, unreal.MaterialExpressionMultiply, t_speed, expr(m, unreal.MaterialExpressionConstant, x0 + 450, y0 + 1250, r=4.5), x0 + 650, y0 + 1150)
        fs = expr(m, unreal.MaterialExpressionSine, x0 + 850, y0 + 1100)
        connect(math2(m, unreal.MaterialExpressionAdd, ft, wdot, x0 + 750, y0 + 1100), "", fs, "")
        famp = math2(m, unreal.MaterialExpressionMultiply, math2(m, unreal.MaterialExpressionMultiply, strength, amount, x0 + 900, y0 + 1250),
                     math2(m, unreal.MaterialExpressionMultiply, flutter_amount, gust, x0 + 900, y0 + 1350), x0 + 1050, y0 + 1250)
        fl = math2(m, unreal.MaterialExpressionMultiply, fs, famp, x0 + 1200, y0 + 1150)
        fdir = expr(m, unreal.MaterialExpressionConstant3Vector, x0 + 1200, y0 + 1300, constant=unreal.LinearColor(0.3, 0.3, 1.0, 0))
        fvec = math2(m, unreal.MaterialExpressionMultiply, fdir, fl, x0 + 1400, y0 + 1200)
        # flutter fades out at the plant's base (hf, not squared: leaves low on a bush still move)
        fvec2 = math2(m, unreal.MaterialExpressionMultiply, fvec, hf_sat, x0 + 1600, y0 + 1200)
        wpo = math2(m, unreal.MaterialExpressionAdd, wpo, fvec2, x0 + 2050, y0 + 600)
    mel.connect_material_property(wpo, "", unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    try:  # lets Nanite / culling account for the movement
        m.set_editor_property("max_world_position_offset_displacement", 120.0)
    except Exception:
        pass


def leaf_master():
    m = new_material("M_Foliage_Leaf")
    m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
    m.set_editor_property("two_sided", True)
    m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_TWO_SIDED_FOLIAGE)
    m.set_editor_property("opacity_mask_clip_value", 0.35)
    leaf = tex_param(m, "Leaf", -900, 0, WHITE)
    opac = tex_param(m, "Opacity", -900, 250, white_mask(), unreal.MaterialSamplerType.SAMPLERTYPE_MASKS)
    normal = tex_param(m, "Normal", -900, 500, FLAT, unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
    use_op = expr(m, unreal.MaterialExpressionScalarParameter, -900, 750, parameter_name="UseOpacityMap", default_value=0.0)
    tint = expr(m, unreal.MaterialExpressionVectorParameter, -900, 850, parameter_name="Tint", default_value=unreal.LinearColor(1, 1, 1, 1))
    rough = expr(m, unreal.MaterialExpressionScalarParameter, -900, 1000, parameter_name="Roughness", default_value=0.55)
    col = expr(m, unreal.MaterialExpressionMultiply, -600, 0)
    mel.connect_material_expressions(leaf, "RGB", col, "A")
    mel.connect_material_expressions(tint, "", col, "B")
    # per-instance hue variation: a forest is never one flat green
    rnd = expr(m, unreal.MaterialExpressionPerInstanceRandom, -900, 1150)
    lo = expr(m, unreal.MaterialExpressionConstant3Vector, -900, 1250, constant=unreal.LinearColor(0.82, 0.92, 1.0, 1))
    hi = expr(m, unreal.MaterialExpressionConstant3Vector, -900, 1350, constant=unreal.LinearColor(1.12, 1.05, 0.82, 1))
    var = expr(m, unreal.MaterialExpressionLinearInterpolate, -600, 1250)
    mel.connect_material_expressions(lo, "", var, "A")
    mel.connect_material_expressions(hi, "", var, "B")
    mel.connect_material_expressions(rnd, "", var, "Alpha")
    base = expr(m, unreal.MaterialExpressionMultiply, -350, 100)
    mel.connect_material_expressions(col, "", base, "A")
    mel.connect_material_expressions(var, "", base, "B")
    mel.connect_material_property(base, "", unreal.MaterialProperty.MP_BASE_COLOR)
    mask = expr(m, unreal.MaterialExpressionLinearInterpolate, -600, 300)
    mel.connect_material_expressions(leaf, "A", mask, "A")
    mel.connect_material_expressions(opac, "RGB", mask, "B")
    mel.connect_material_expressions(use_op, "", mask, "Alpha")
    mel.connect_material_property(mask, "", unreal.MaterialProperty.MP_OPACITY_MASK)
    mel.connect_material_property(normal, "RGB", unreal.MaterialProperty.MP_NORMAL)
    mel.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    sss = expr(m, unreal.MaterialExpressionMultiply, -350, 400)
    mel.connect_material_expressions(base, "", sss, "A")
    k = expr(m, unreal.MaterialExpressionConstant, -500, 450, r=0.75)
    mel.connect_material_expressions(k, "", sss, "B")
    mel.connect_material_property(sss, "", unreal.MaterialProperty.MP_SUBSURFACE_COLOR)
    add_wind(m, flutter=True)
    mel.recompile_material(m)
    eal.save_loaded_asset(m)
    return m


def world_aligned(m, fn_path, tex_obj, size, x, y):
    call = expr(m, unreal.MaterialExpressionMaterialFunctionCall, x, y)
    call.set_editor_property("material_function", unreal.load_asset(fn_path))
    mel.connect_material_expressions(tex_obj, "", call, "TextureObject")
    mel.connect_material_expressions(size, "", call, "TextureSize")
    return call


def bark_master():
    """Scanned bark projected in world space (Sapling meshes have no useful UVs)."""
    m = new_material("M_Foliage_Bark")
    m.set_editor_property("tangent_space_normal", False)  # WorldAlignedNormal outputs world-space normals
    size = expr(m, unreal.MaterialExpressionVectorParameter, -1200, 400, parameter_name="TileSize", default_value=unreal.LinearColor(120, 120, 240, 1))
    size3 = expr(m, unreal.MaterialExpressionComponentMask, -1000, 400, r=True, g=True, b=True, a=False)
    mel.connect_material_expressions(size, "", size3, "")
    col_t = expr(m, unreal.MaterialExpressionTextureObjectParameter, -1200, 0, parameter_name="BarkColor", texture=WHITE)
    nor_t = expr(m, unreal.MaterialExpressionTextureObjectParameter, -1200, 600, parameter_name="BarkNormal", texture=FLAT)
    rou_t = expr(m, unreal.MaterialExpressionTextureObjectParameter, -1200, 900, parameter_name="BarkRough", texture=WHITE)
    col = world_aligned(m, "/Engine/Functions/Engine_MaterialFunctions01/Texturing/WorldAlignedTexture", col_t, size3, -800, 0)
    nor = world_aligned(m, "/Engine/Functions/Engine_MaterialFunctions01/Texturing/WorldAlignedNormal", nor_t, size3, -800, 600)
    rou = world_aligned(m, "/Engine/Functions/Engine_MaterialFunctions01/Texturing/WorldAlignedTexture", rou_t, size3, -800, 900)
    tint = expr(m, unreal.MaterialExpressionVectorParameter, -800, 300, parameter_name="Tint", default_value=unreal.LinearColor(1, 1, 1, 1))
    base = expr(m, unreal.MaterialExpressionMultiply, -450, 100)
    mel.connect_material_expressions(col, "XYZ Texture", base, "A")
    mel.connect_material_expressions(tint, "", base, "B")
    mel.connect_material_property(base, "", unreal.MaterialProperty.MP_BASE_COLOR)
    mel.connect_material_property(nor, "XYZ Texture", unreal.MaterialProperty.MP_NORMAL)
    rmask = expr(m, unreal.MaterialExpressionComponentMask, -450, 900, r=True, g=False, b=False, a=False)
    mel.connect_material_expressions(rou, "XYZ Texture", rmask, "")
    mel.connect_material_property(rmask, "", unreal.MaterialProperty.MP_ROUGHNESS)
    add_wind(m, flutter=False)
    mel.recompile_material(m)
    eal.save_loaded_asset(m)
    return m


def surface_master():
    """UV-mapped scanned PBR (Poly Haven trunks, rocks): colour, normal, ARM (AO, roughness, metal)."""
    m = new_material("M_Foliage_Surface")
    col = tex_param(m, "BaseColor", -800, 0, WHITE)
    nor = tex_param(m, "Normal", -800, 300, FLAT, unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
    arm = tex_param(m, "ARM", -800, 600, white_mask(), unreal.MaterialSamplerType.SAMPLERTYPE_MASKS)
    tint = expr(m, unreal.MaterialExpressionVectorParameter, -800, 900, parameter_name="Tint", default_value=unreal.LinearColor(1, 1, 1, 1))
    base = expr(m, unreal.MaterialExpressionMultiply, -450, 0)
    mel.connect_material_expressions(col, "RGB", base, "A")
    mel.connect_material_expressions(tint, "", base, "B")
    mel.connect_material_property(base, "", unreal.MaterialProperty.MP_BASE_COLOR)
    mel.connect_material_property(nor, "RGB", unreal.MaterialProperty.MP_NORMAL)
    mel.connect_material_property(arm, "R", unreal.MaterialProperty.MP_AMBIENT_OCCLUSION)
    mel.connect_material_property(arm, "G", unreal.MaterialProperty.MP_ROUGHNESS)
    add_wind(m, flutter=False)  # WindAmount 0 on rocks
    mel.recompile_material(m)
    eal.save_loaded_asset(m)
    return m


def instance(name, parent, scalars=None, vectors=None, textures=None):
    path = f"{MAT}/{name}"
    if eal.does_asset_exist(path):
        mi = unreal.load_asset(path)
        mel.clear_all_material_instance_parameters(mi)
    else:
        mi = tools.create_asset(name, MAT, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    mel.set_material_instance_parent(mi, parent)
    for k, v in (scalars or {}).items():
        mel.set_material_instance_scalar_parameter_value(mi, k, v)
    for k, v in (vectors or {}).items():
        mel.set_material_instance_vector_parameter_value(mi, k, unreal.LinearColor(v[0], v[1], v[2], 1))
    for k, v in (textures or {}).items():
        if v:
            mel.set_material_instance_texture_parameter_value(mi, k, v)
    eal.save_loaded_asset(mi)
    return mi


def wind_settings():
    """WindHeight (cm the sway is measured against) and WindAmount per material instance, by plant type."""
    small = ("grass", "fern", "wildflower", "flower", "dandelion", "celandine", "periwinkle", "nettle")
    for path in eal.list_assets(MAT, recursive=False, include_folder=False):
        mi = unreal.load_asset(path)
        if not isinstance(mi, unreal.MaterialInstanceConstant):
            continue
        name = mi.get_name().lower()
        if any(w in name for w in ("rock", "boulder")):
            height, amount = 1000.0, 0.0
        elif any(w in name for w in small):
            height, amount = 70.0, 1.0
        elif any(w in name for w in ("bush", "boxwood", "pachira")):
            height, amount = 250.0, 0.8
        elif "palm" in name:
            height, amount = 1100.0, 1.3
        else:
            height, amount = 1500.0, 1.0
        mel.set_material_instance_scalar_parameter_value(mi, "WindHeight", height)
        mel.set_material_instance_scalar_parameter_value(mi, "WindAmount", amount)
        eal.save_loaded_asset(mi)
    unreal.log("FOLIAGE wind settings applied")


def mesh_options():
    opts = unreal.FbxImportUI()
    opts.set_editor_property("import_mesh", True)
    opts.set_editor_property("import_materials", False)
    opts.set_editor_property("import_textures", False)
    opts.set_editor_property("import_as_skeletal", False)
    sm = opts.get_editor_property("static_mesh_import_data")
    sm.set_editor_property("combine_meshes", True)
    sm.set_editor_property("generate_lightmap_u_vs", False)
    sm.set_editor_property("auto_generate_collision", False)
    sm.set_editor_property("build_nanite", True)
    return opts


def finish_mesh(mesh, foliage=True):
    nanite = mesh.get_editor_property("nanite_settings")
    nanite.set_editor_property("enabled", True)
    if foliage:  # keep the crown's area at a distance (plain Nanite thins leaf cards away)
        nanite.set_editor_property("shape_preservation", unreal.NaniteShapePreservation.PRESERVE_AREA)
    mesh.set_editor_property("nanite_settings", nanite)
    eal.save_loaded_asset(mesh)


def main():
    if eal.does_directory_exist(ROOT + "/Test"):
        eal.delete_directory(ROOT + "/Test")
    leaf, bark, surface = leaf_master(), bark_master(), surface_master()

    # ---- leaves: scanned leaf for broad-leaved trees and bushes, generated cards for needles, fronds and grass
    scanned = {}
    for key, kind in (("Color", "color"), ("Opacity", "mask"), ("NormalGL", "normal")):
        p = os.path.join(LEAF_SRC, f"LeafCrop_{key}.png")
        scanned[key] = import_texture(p, TEX + "/ScannedLeaf", kind) if os.path.exists(p) else None
    leaf_mis = {}
    for png in glob.glob(os.path.join(SRC, "T_LeafSingle_*.png")):
        kind = os.path.splitext(os.path.basename(png))[0][len("T_LeafSingle_"):]
        if kind in ("broad", "small") and scanned["Color"]:
            tint = (1, 1, 1) if kind == "broad" else (1.15, 1.2, 0.9)
            leaf_mis[kind] = instance(f"MI_Leaves_{kind}", leaf, scalars={"UseOpacityMap": 1.0, "Roughness": 0.5}, vectors={"Tint": tint},
                                      textures={"Leaf": scanned["Color"], "Opacity": scanned["Opacity"], "Normal": scanned["NormalGL"]})
            continue
        tex = import_texture(png, TEX)
        leaf_mis[kind] = instance(f"MI_Leaves_{kind}", leaf, textures={"Leaf": tex})

    # ---- bark: scanned, world-projected
    bark_tex, bark_mis = {}, {}

    def bark_mi(kind):
        if kind in bark_mis:
            return bark_mis[kind]
        ph, tint = BARK.get(kind, BARK["default"])
        if ph not in bark_tex:
            bark_tex[ph] = {}
            for param, suffix, t in (("BarkColor", "diff", "color"), ("BarkNormal", "nor_gl", "normal"), ("BarkRough", "rough", "mask")):
                p = os.path.join(BARK_SRC, f"{ph}_{suffix}_2k.jpg")
                if os.path.exists(p):
                    bark_tex[ph][param] = import_texture(p, TEX + "/Bark", t)
        bark_mis[kind] = instance(f"MI_Bark_{kind}", bark, vectors={"Tint": tint}, textures=bark_tex[ph])
        return bark_mis[kind]

    count = 0
    for fbx in sorted(glob.glob(os.path.join(SRC, "SM_*.fbx"))):
        name = os.path.splitext(os.path.basename(fbx))[0]
        scanned_model = name.startswith("SM_PH_")
        mesh = import_file(fbx, SCAN if scanned_model else MESH, mesh_options())
        if not mesh:
            unreal.log_error(f"FOLIAGE import failed: {fbx}")
            continue
        mats = mesh.get_editor_property("static_materials")
        side = {}
        if scanned_model:
            with open(os.path.splitext(fbx)[0] + ".json", encoding="utf-8") as f:
                side = json.load(f)
        is_foliage = not any(w in name.lower() for w in ("rock", "boulder"))
        for i, slot in enumerate(mats):
            slot_name = str(slot.get_editor_property("material_slot_name"))
            if scanned_model:
                info = side.get(slot_name, {})
                folder = f"{TEX}/Scanned/{name[6:]}"

                def tex_of(key, kind):
                    p = info.get(key)
                    return import_texture(p, folder, kind) if p and os.path.exists(p) else None
                base, nor = tex_of("base", "color"), tex_of("normal", "normal")
                if info.get("masked"):
                    mi = instance(f"MI_{name[3:]}_{i}", leaf, textures={"Leaf": base, "Normal": nor})
                else:
                    mi = instance(f"MI_{name[3:]}_{i}", surface, textures={"BaseColor": base, "Normal": nor, "ARM": tex_of("rough", "mask")})
            elif slot_name.startswith("Leaves_"):
                mi = leaf_mis.get(slot_name[len("Leaves_"):])
            else:
                mi = bark_mi(slot_name.split("_", 1)[1] if "_" in slot_name else "default")
            if mi:
                mesh.set_material(i, mi)
        finish_mesh(mesh, foliage=is_foliage)
        count += 1
        unreal.log(f"FOLIAGE imported {mesh.get_path_name()} slots={[str(s.get_editor_property('material_slot_name')) for s in mats]}")
    wind_settings()
    unreal.log(f"FOLIAGE done: {count} meshes")


if not os.environ.get("FOLIAGE_MASTERS_ONLY"):
    main()
else:  # rebuild just the master materials (instances and meshes stay)
    leaf_master(), bark_master(), surface_master()
    wind_settings()
    unreal.log("FOLIAGE masters rebuilt")
