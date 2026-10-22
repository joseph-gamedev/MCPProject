"""Imports the foliage into /Game/RuntimeFoliage with scanned PBR materials, ready for scattering (instanced) and Nanite.

    UnrealEditor-Cmd.exe MCPProject.uproject -run=pythonscript -script="<repo>/Scripts/foliage/import_foliage.py" -unattended -nullrhi

Inputs (Saved/): Foliage/SM_*.fbx (Blender trees/plants), Foliage/SM_PH_*.fbx + .json (converted Poly Haven models),
Foliage/T_LeafSingle_*.png (generated leaf cards), FoliageSources/bark/*_2k.jpg (Poly Haven bark, CC0),
FoliageSources/leaf/LeafCrop_*.png (ambientCG Leaf001, CC0, one leaf cropped square). Masters:
  M_Foliage_Leaf    masked, two-sided foliage: Leaf (colour+alpha) or Opacity map, Normal, Tint, translucency, per-instance hue
  M_Foliage_Bark    world-projected scanned bark (no UVs needed): colour, normal, roughness, tile size, tint
  M_Foliage_Surface UV-mapped scanned PBR for opaque parts of scanned models (trunks, rocks): colour, normal, ARM
Every master is flagged for instanced static meshes (pcg.scatter) and works with Nanite.
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
    unreal.log(f"FOLIAGE done: {count} meshes")


if not os.environ.get("FOLIAGE_MASTERS_ONLY"):
    main()
else:  # rebuild just the master materials (instances and meshes stay)
    leaf_master(), bark_master(), surface_master()
    unreal.log("FOLIAGE masters rebuilt")
