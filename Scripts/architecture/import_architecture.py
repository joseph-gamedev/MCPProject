"""Imports scanned architecture surfaces and props into /Game/RuntimeArchitecture.

    UnrealEditor-Cmd.exe MCPProject.uproject -run=pythonscript -script="<repo>/Scripts/architecture/import_architecture.py" -unattended -nullrhi

1. Surfaces (Saved/FoliageSources/surfaces/<preset>_{diff,nor_gl,rough,ao}.jpg, Poly Haven CC0) become textures
   /Game/RuntimeArchitecture/Surfaces/<preset>_<map>. The builder (PrimitiveMesh MakeMaterial) uses them through
   M_RT_Scanned for its material presets (brick, plaster, concrete, roof_tile ...), with no UVs needed.
2. M_RT_Scanned: world-projected scanned PBR + the builder's knobs (colour tint, per-object variation, roughness
   scale, metallic, emissive); MPC_RT_Style Pattern 0 (stylized looks) falls back to the flat Color, Tint applies.
3. Props (Saved/Architecture/SM_PH_*.fbx + .json from convert_polyhaven.py on FoliageSources/props) become
   /Game/RuntimeArchitecture/Props meshes with M_Foliage_Surface / M_Foliage_Leaf instances (wind off).
"""

import glob
import json
import os
import sys

import unreal

HERE = os.path.dirname(os.path.abspath(__file__))
os.environ["FOLIAGE_LIBRARY_ONLY"] = "1"
sys.path.insert(0, os.path.join(HERE, "..", "foliage"))
import import_foliage as F  # noqa: E402

SAVED = os.path.abspath(os.path.join(HERE, "..", "..", "Saved"))
SURF_SRC = os.path.join(SAVED, "FoliageSources", "surfaces")
PROP_SRC = os.path.join(SAVED, "Architecture")
ROOT = "/Game/RuntimeArchitecture"
SURF, MAT, PROPS = ROOT + "/Surfaces", ROOT + "/Materials", ROOT + "/Props"
mel, eal, tools = F.mel, F.eal, F.tools


def scanned_master():
    name = "M_RT_Scanned"
    path = f"{MAT}/{name}"
    if eal.does_asset_exist(path):
        m = unreal.load_asset(path)
        mel.delete_all_material_expressions(m)
    else:
        m = tools.create_asset(name, MAT, unreal.Material, unreal.MaterialFactoryNew())
    for usage in ("used_with_instanced_static_meshes", "used_with_spline_meshes", "used_with_niagara_mesh_particles"):
        m.set_editor_property(usage, True)
    m.set_editor_property("tangent_space_normal", False)  # world-aligned normals are world space
    e = F.expr
    tile = e(m, unreal.MaterialExpressionScalarParameter, -1500, 400, parameter_name="ScanTile", default_value=200.0)
    tile3 = e(m, unreal.MaterialExpressionAppendVector, -1300, 400)
    tile2 = e(m, unreal.MaterialExpressionAppendVector, -1400, 450)
    F.connect(tile, "", tile2, "A")
    F.connect(tile, "", tile2, "B")
    F.connect(tile2, "", tile3, "A")
    F.connect(tile, "", tile3, "B")
    col_t = e(m, unreal.MaterialExpressionTextureObjectParameter, -1500, 0, parameter_name="ScanColor", texture=F.WHITE)
    nor_t = e(m, unreal.MaterialExpressionTextureObjectParameter, -1500, 600, parameter_name="ScanNormal", texture=F.FLAT)
    rou_t = e(m, unreal.MaterialExpressionTextureObjectParameter, -1500, 900, parameter_name="ScanRough", texture=F.WHITE)
    ao_t = e(m, unreal.MaterialExpressionTextureObjectParameter, -1500, 1200, parameter_name="ScanAO", texture=F.WHITE)
    wat = "/Engine/Functions/Engine_MaterialFunctions01/Texturing/WorldAlignedTexture"
    col = F.world_aligned(m, wat, col_t, tile3, -1100, 0)
    nor = F.world_aligned(m, "/Engine/Functions/Engine_MaterialFunctions01/Texturing/WorldAlignedNormal", nor_t, tile3, -1100, 600)
    rou = F.world_aligned(m, wat, rou_t, tile3, -1100, 900)
    ao = F.world_aligned(m, wat, ao_t, tile3, -1100, 1200)
    # per-object variation: brightness/warmth jitter from the object's position (neighbouring buildings differ)
    obj = e(m, unreal.MaterialExpressionObjectPositionWS, -1500, -400)
    oxy = e(m, unreal.MaterialExpressionComponentMask, -1350, -400, r=True, g=True, b=False, a=False)
    F.connect(obj, "", oxy, "")
    h = F.math2(m, unreal.MaterialExpressionDotProduct, oxy, e(m, unreal.MaterialExpressionConstant2Vector, -1350, -300, r=0.0173, g=0.0291), -1200, -400)
    hs = e(m, unreal.MaterialExpressionSine, -1050, -400)
    F.connect(h, "", hs, "")
    hr = e(m, unreal.MaterialExpressionFrac, -900, -400)
    F.connect(F.math2(m, unreal.MaterialExpressionMultiply, hs, e(m, unreal.MaterialExpressionConstant, -1050, -300, r=4375.85), -950, -350), "", hr, "")
    var = e(m, unreal.MaterialExpressionScalarParameter, -900, -250, parameter_name="Variation", default_value=0.15)
    centered = F.math2(m, unreal.MaterialExpressionSubtract, hr, e(m, unreal.MaterialExpressionConstant, -800, -300, r=0.5), -750, -400)
    jitter = F.math2(m, unreal.MaterialExpressionAdd, e(m, unreal.MaterialExpressionConstant, -650, -450, r=1.0),
                     F.math2(m, unreal.MaterialExpressionMultiply, centered, var, -650, -350), -500, -400)
    def rgb(node, x, y):  # vector parameters are float4; the textures are float3
        mask = e(m, unreal.MaterialExpressionComponentMask, x, y, r=True, g=True, b=True, a=False)
        F.connect(node, "", mask, "")
        return mask
    tint = rgb(e(m, unreal.MaterialExpressionVectorParameter, -1000, 200, parameter_name="ScanTint", default_value=unreal.LinearColor(1, 1, 1, 1)), -850, 200)
    scan = F.math2(m, unreal.MaterialExpressionMultiply, F.math2(m, unreal.MaterialExpressionMultiply, col, tint, -700, 50, a_out="XYZ Texture"),
                   jitter, -500, 0)
    # stylized looks (MPC_RT_Style Pattern 0) use the flat colour; Tint from the style always applies
    style = unreal.load_asset("/RuntimeToolsets/Materials/MPC_RT_Style.MPC_RT_Style")
    pattern = e(m, unreal.MaterialExpressionCollectionParameter, -700, -150, collection=style, parameter_name="Pattern")
    style_tint = rgb(e(m, unreal.MaterialExpressionCollectionParameter, -850, -250, collection=style, parameter_name="Tint"), -700, -250)
    flat = rgb(e(m, unreal.MaterialExpressionVectorParameter, -850, 300, parameter_name="Color", default_value=unreal.LinearColor(0.8, 0.8, 0.8, 1)), -700, 300)
    base = e(m, unreal.MaterialExpressionLinearInterpolate, -350, 0)
    F.connect(flat, "", base, "A")
    F.connect(scan, "", base, "B")
    F.connect(pattern, "", base, "Alpha")
    tinted = F.math2(m, unreal.MaterialExpressionMultiply, base, style_tint, -200, 0)
    mel.connect_material_property(tinted, "", unreal.MaterialProperty.MP_BASE_COLOR)
    mel.connect_material_property(nor, "XYZ Texture", unreal.MaterialProperty.MP_NORMAL)
    rmask = e(m, unreal.MaterialExpressionComponentMask, -800, 900, r=True, g=False, b=False, a=False)
    F.connect(rou, "XYZ Texture", rmask, "")
    rscale = e(m, unreal.MaterialExpressionScalarParameter, -800, 1000, parameter_name="RoughnessScale", default_value=1.0)
    rs = e(m, unreal.MaterialExpressionSaturate, -450, 900)
    F.connect(F.math2(m, unreal.MaterialExpressionMultiply, rmask, rscale, -600, 900), "", rs, "")
    mel.connect_material_property(rs, "", unreal.MaterialProperty.MP_ROUGHNESS)
    amask = e(m, unreal.MaterialExpressionComponentMask, -800, 1200, r=True, g=False, b=False, a=False)
    F.connect(ao, "XYZ Texture", amask, "")
    mel.connect_material_property(amask, "", unreal.MaterialProperty.MP_AMBIENT_OCCLUSION)
    mel.connect_material_property(e(m, unreal.MaterialExpressionScalarParameter, -400, 1100, parameter_name="Metallic", default_value=0.0), "",
                                  unreal.MaterialProperty.MP_METALLIC)
    glow = F.math2(m, unreal.MaterialExpressionMultiply,
                   rgb(e(m, unreal.MaterialExpressionVectorParameter, -650, 1400, parameter_name="Emissive", default_value=unreal.LinearColor(0, 0, 0, 1)), -500, 1400),
                   e(m, unreal.MaterialExpressionScalarParameter, -500, 1500, parameter_name="EmissiveStrength", default_value=0.0), -300, 1400)
    mel.connect_material_property(glow, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel.recompile_material(m)
    eal.save_loaded_asset(m)
    unreal.log("ARCH M_RT_Scanned built")
    return m


def surfaces():
    count = 0
    for jpg in sorted(glob.glob(os.path.join(SURF_SRC, "*.jpg"))):
        stem = os.path.splitext(os.path.basename(jpg))[0]
        kind = "normal" if stem.endswith("_nor_gl") else "mask" if stem.endswith(("_rough", "_ao")) else "color"
        if F.import_texture(jpg, SURF, kind):
            count += 1
    unreal.log(f"ARCH surfaces imported: {count} textures")


def props():
    surface = unreal.load_asset(F.MAT + "/M_Foliage_Surface")
    leaf = unreal.load_asset(F.MAT + "/M_Foliage_Leaf")
    count = 0
    for fbx in sorted(glob.glob(os.path.join(PROP_SRC, "SM_PH_*.fbx"))):
        name = os.path.splitext(os.path.basename(fbx))[0]
        mesh = F.import_file(fbx, PROPS, F.mesh_options())
        if not mesh:
            unreal.log_error(f"ARCH import failed: {fbx}")
            continue
        with open(os.path.splitext(fbx)[0] + ".json", encoding="utf-8") as f:
            side = json.load(f)
        for i, slot in enumerate(mesh.get_editor_property("static_materials")):
            info = side.get(str(slot.get_editor_property("material_slot_name")), {})
            folder = f"{ROOT}/PropTextures/{name[6:]}"

            def tex(key, kind):
                p = info.get(key)
                return F.import_texture(p, folder, kind) if p and os.path.exists(p) else None
            base, nor = tex("base", "color"), tex("normal", "normal")
            path = f"{MAT}/MI_{name[3:]}_{i}"
            if eal.does_asset_exist(path):
                eal.delete_asset(path)
            mi = tools.create_asset(f"MI_{name[3:]}_{i}", MAT, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
            mel.set_material_instance_parent(mi, leaf if info.get("masked") else surface)
            for k, v in ({"Leaf": base, "Normal": nor} if info.get("masked") else {"BaseColor": base, "Normal": nor, "ARM": tex("rough", "mask")}).items():
                if v:
                    mel.set_material_instance_texture_parameter_value(mi, k, v)
            mel.set_material_instance_scalar_parameter_value(mi, "WindAmount", 0.0)  # props do not sway
            eal.save_loaded_asset(mi)
            mesh.set_material(i, mi)
        F.finish_mesh(mesh, foliage=False)
        count += 1
        unreal.log(f"ARCH prop {mesh.get_path_name()}")
    unreal.log(f"ARCH props imported: {count}")


MODULE_SRC = os.path.join(SAVED, "Architecture", "Modules")
MODULES = ROOT + "/Modules"
# Arch_<kind> slot -> (scanned surface or None, flat colour, roughness, metallic)
ARCH_KINDS = {"wood": ("wood", (0.25, 0.15, 0.08), 0.7, 0.0), "stone": ("limestone", (0.62, 0.58, 0.5), 0.8, 0.0), "marble": ("marble", (0.85, 0.84, 0.8), 0.3, 0.0),
              "brick": ("brick", (0.4, 0.17, 0.11), 0.85, 0.0), "roof_tile": ("roof_tile", (0.45, 0.2, 0.12), 0.75, 0.0),
              "metal_dark": (None, (0.03, 0.03, 0.035), 0.45, 0.6), "paint_white": (None, (0.82, 0.81, 0.78), 0.45, 0.0),
              "paint_dark": (None, (0.05, 0.08, 0.07), 0.45, 0.2), "glass": (None, (0.015, 0.02, 0.025), 0.04, 0.0)}


def module_materials():
    scanned = unreal.load_asset(MAT + "/M_RT_Scanned")
    pbr = unreal.load_asset("/RuntimeToolsets/Materials/M_RT_PBR")
    glass = unreal.load_asset("/RuntimeToolsets/Materials/M_RT_Glass")
    out = {}
    for kind, (surface, color, rough, metal) in ARCH_KINDS.items():
        name = f"MI_Arch_{kind}"
        path = f"{MAT}/{name}"
        mi = unreal.load_asset(path) if eal.does_asset_exist(path) else tools.create_asset(name, MAT, unreal.MaterialInstanceConstant,
                                                                                         unreal.MaterialInstanceConstantFactoryNew())
        lc = unreal.LinearColor(color[0], color[1], color[2], 1)
        if surface and eal.does_asset_exist(f"{SURF}/{surface}_diff"):
            mel.set_material_instance_parent(mi, scanned)
            for param, suffix in (("ScanColor", "diff"), ("ScanNormal", "nor_gl"), ("ScanRough", "rough"), ("ScanAO", "ao")):
                if eal.does_asset_exist(f"{SURF}/{surface}_{suffix}"):
                    mel.set_material_instance_texture_parameter_value(mi, param, unreal.load_asset(f"{SURF}/{surface}_{suffix}"))
            mel.set_material_instance_scalar_parameter_value(mi, "ScanTile", 120.0)
            mel.set_material_instance_vector_parameter_value(mi, "Color", lc)
        else:  # paint, metal and window glass (opaque, dark and mirror-smooth: reads as glass on a facade in daylight)
            mel.set_material_instance_parent(mi, pbr)
            mel.set_material_instance_vector_parameter_value(mi, "Color", lc)
            mel.set_material_instance_scalar_parameter_value(mi, "Roughness", rough)
            mel.set_material_instance_scalar_parameter_value(mi, "Metallic", metal)
            mel.set_material_instance_scalar_parameter_value(mi, "Style", 0.0)
        eal.save_loaded_asset(mi)
        out[kind] = mi
    return out


def modules():
    mats = module_materials()
    current = {os.path.splitext(os.path.basename(f))[0] for f in glob.glob(os.path.join(MODULE_SRC, "SM_*.fbx"))}
    for path in eal.list_assets(MODULES, recursive=False, include_folder=False) if eal.does_directory_exist(MODULES) else []:
        if path.rsplit("/", 1)[-1].split(".")[0] not in current:  # pieces that were removed from the generator
            eal.delete_asset(path)
    count = 0
    for fbx in sorted(glob.glob(os.path.join(MODULE_SRC, "SM_*.fbx"))):
        mesh = F.import_file(fbx, MODULES, F.mesh_options())
        if not mesh:
            unreal.log_error(f"ARCH module import failed: {fbx}")
            continue
        for i, slot in enumerate(mesh.get_editor_property("static_materials")):
            kind = str(slot.get_editor_property("material_slot_name"))[len("Arch_"):]
            if kind in mats:
                mesh.set_material(i, mats[kind])
        F.finish_mesh(mesh, foliage=False)
        count += 1
    unreal.log(f"ARCH modules imported: {count}")


scanned_master()
if os.environ.get("ARCH_MODULES_ONLY"):
    modules()
elif not os.environ.get("ARCH_MASTER_ONLY"):
    surfaces()
    props()
    modules()
unreal.log("ARCH done")
