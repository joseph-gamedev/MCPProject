"""Converts downloaded Poly Haven glTF plants/rocks (Saved/FoliageSources/models) to Unreal-ready FBX + material sidecar.

    blender -b --python Scripts/foliage/convert_polyhaven.py -- [sources_dir] [out_dir]

Per model: import glTF, join all meshes (material slots kept), apply transforms, pivot at the base centre, export
SM_PH_<Name>.fbx and SM_PH_<Name>.json: {slot: {"base": png/jpg, "normal": ..., "rough": ..., "masked": bool}} so the
Unreal import builds instances of the foliage master materials (scattering needs our instancing-ready materials).
"""

import json
import os
import sys

import bpy

HERE = os.path.dirname(os.path.abspath(__file__))
ARGS = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
SRC = os.path.abspath(ARGS[0]) if ARGS else os.path.abspath(os.path.join(HERE, "..", "..", "Saved", "FoliageSources", "models"))
OUT = os.path.abspath(ARGS[1]) if len(ARGS) > 1 else os.path.abspath(os.path.join(HERE, "..", "..", "Saved", "Foliage"))
os.makedirs(OUT, exist_ok=True)


def image_from(node_input):
    """The image file feeding a Principled BSDF input (through normal-map / separate nodes)."""
    if not node_input.is_linked:
        return None
    stack = [node_input.links[0].from_node]
    seen = set()
    while stack:
        n = stack.pop()
        if n in seen:
            continue
        seen.add(n)
        if n.type == "TEX_IMAGE" and n.image:
            return bpy.path.abspath(n.image.filepath_raw or n.image.filepath)
        for i in n.inputs:
            if i.is_linked:
                stack.append(i.links[0].from_node)
    return None


def convert(model_dir, name):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    gltf = next(f for f in os.listdir(model_dir) if f.endswith(".gltf"))
    bpy.ops.import_scene.gltf(filepath=os.path.join(model_dir, gltf))
    meshes = [o for o in bpy.context.scene.objects if o.type == "MESH"]
    bpy.ops.object.select_all(action="DESELECT")
    for o in meshes:
        o.select_set(True)
    bpy.context.view_layer.objects.active = meshes[0]
    if len(meshes) > 1:
        bpy.ops.object.join()
    ob = bpy.context.view_layer.objects.active
    ob.parent = None
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    # pivot at the base centre
    xs = [v.co.x for v in ob.data.vertices]
    ys = [v.co.y for v in ob.data.vertices]
    zs = [v.co.z for v in ob.data.vertices]
    shift = ((min(xs) + max(xs)) / 2, (min(ys) + max(ys)) / 2, min(zs))
    for v in ob.data.vertices:
        v.co.x -= shift[0]
        v.co.y -= shift[1]
        v.co.z -= shift[2]
    for o in list(bpy.context.scene.objects):
        if o != ob:
            bpy.data.objects.remove(o, do_unlink=True)
    sidecar = {}
    for i, slot in enumerate(ob.material_slots):
        m = slot.material
        if not m:
            continue
        m.name = f"{name}_M{i}"
        bsdf = next((n for n in m.node_tree.nodes if n.type == "BSDF_PRINCIPLED"), None) if m.use_nodes else None
        if not bsdf:
            continue
        sidecar[m.name] = {
            "base": image_from(bsdf.inputs["Base Color"]),
            "normal": image_from(bsdf.inputs["Normal"]),
            "rough": image_from(bsdf.inputs["Roughness"]),
            "masked": bsdf.inputs["Alpha"].is_linked or getattr(m, "blend_method", "OPAQUE") in ("CLIP", "HASHED", "BLEND"),
        }
    path = os.path.join(OUT, f"SM_PH_{name}.fbx")
    bpy.ops.object.select_all(action="DESELECT")
    ob.select_set(True)
    bpy.ops.export_scene.fbx(filepath=path, use_selection=True, apply_unit_scale=True, bake_space_transform=True, axis_forward="-Y", axis_up="Z",
                             mesh_smooth_type="FACE", path_mode="STRIP")
    with open(os.path.join(OUT, f"SM_PH_{name}.json"), "w", encoding="utf-8") as f:
        json.dump(sidecar, f, indent=1)
    tris = sum(len(p.vertices) - 2 for p in ob.data.polygons)
    print(f"PH {name}: {tris} triangles, {ob.dimensions.z:.2f} m tall, slots {list(sidecar)}")


def main():
    for model in sorted(os.listdir(SRC)):
        d = os.path.join(SRC, model)
        if os.path.isdir(d) and any(f.endswith(".gltf") for f in os.listdir(d)):
            name = "".join(w.title() for w in model.split("_"))
            try:
                convert(d, name)
            except Exception as e:  # keep going: one broken model must not stop the rest
                print(f"PH FAILED {model}: {e}")
    print("DONE")


if __name__ == "__main__":
    main()
