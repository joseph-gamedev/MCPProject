"""Downloads the free CC0 source assets (Poly Haven, ambientCG) used for the packaged foliage into Saved/FoliageSources.

    python Scripts/foliage/fetch_sources.py

Poly Haven models come as glTF with 2K textures; bark textures as 2K JPG (diffuse, OpenGL normal, roughness);
ambientCG Leaf001 as a 2K JPG zip (only image files are extracted). Licences: CC0 (no attribution required).
"""

import json
import os
import urllib.request
import zipfile

OUT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "Saved", "FoliageSources"))
UA = {"User-Agent": "MCPProject-foliage-fetch/1.0"}
MODELS = ["island_tree_01", "island_tree_02", "island_tree_03", "jacaranda_tree", "nettle_plant", "pachira_aquatica_01", "grass_medium_01",
          "grass_medium_02", "grass_bermuda_01", "fern_02", "dandelion_01", "celandine_01", "periwinkle_plant", "boulder_01", "rock_07"]
BARKS = ["jolcham_oak_bark_01", "bark_platanus", "bark_willow", "pine_bark", "palm_tree_bark", "eucalyptus_bark"]
# architecture: builder material preset -> Poly Haven surface texture, and street / building props
SURFACES = {"brick": "red_brick_03", "plaster": "painted_plaster_wall", "concrete": "concrete_wall_008", "limestone": "beige_wall_001",
            "sandstone": "large_sandstone_blocks", "granite": "granite_wall", "marble": "marble_01", "stone": "rustic_stone_wall",
            "roof_tile": "clay_roof_tiles", "slate_roof": "roof_slates_02", "wood": "brown_planks_03", "steel": "metal_plate",
            "painted_iron": "painted_metal_shutter", "corrugated": "corrugated_iron", "asphalt": "asphalt_02", "cobblestone": "cobblestone_floor_01",
            "paving": "concrete_tiles", "terracotta": "ceramic_roof_01"}
PROPS = ["street_lamp_01", "street_lamp_02", "fire_hydrant", "metal_trash_can", "painted_wooden_bench", "modular_street_seating", "concrete_road_barrier",
         "planter_box_01", "planter_box_02", "modular_chainlink_fence", "large_castle_door", "rollershutter_door", "rollershutter_window_01",
         "wooden_picnic_table", "outdoor_table_chair_set_01", "wooden_barrels_01", "wooden_crate_01"]


def get_json(url):
    with urllib.request.urlopen(urllib.request.Request(url, headers=UA), timeout=60) as r:
        return json.load(r)


def download(url, path):
    if os.path.exists(path) and os.path.getsize(path) > 0:
        return
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with urllib.request.urlopen(urllib.request.Request(url, headers=UA), timeout=600) as r, open(path + ".part", "wb") as f:
        while True:
            chunk = r.read(1 << 20)
            if not chunk:
                break
            f.write(chunk)
    os.replace(path + ".part", path)


def fetch_model(m, folder):
    g = get_json(f"https://api.polyhaven.com/files/{m}")["gltf"]["2k"]["gltf"]
    base = os.path.join(OUT, folder, m)
    download(g["url"], os.path.join(base, os.path.basename(g["url"])))
    for rel, info in g.get("include", {}).items():
        if ".." in rel.replace("\\", "/").split("/"):
            continue
        download(info["url"], os.path.join(base, rel))


def architecture():
    for preset, t in SURFACES.items():
        f = get_json(f"https://api.polyhaven.com/files/{t}")
        for key, name in (("Diffuse", "diff"), ("nor_gl", "nor_gl"), ("Rough", "rough"), ("AO", "ao")):
            info = f.get(key, {}).get("2k", {}).get("jpg")
            if info:
                download(info["url"], os.path.join(OUT, "surfaces", f"{preset}_{name}.jpg"))
        print("SURFACE", preset, t)
    for m in PROPS:
        fetch_model(m, "props")
        print("PROP", m)
    print("DONE architecture")


def main():
    for m in MODELS:
        g = get_json(f"https://api.polyhaven.com/files/{m}")["gltf"]["2k"]["gltf"]
        base = os.path.join(OUT, "models", m)
        download(g["url"], os.path.join(base, os.path.basename(g["url"])))
        for rel, info in g.get("include", {}).items():
            if ".." in rel.replace("\\", "/").split("/"):
                continue
            download(info["url"], os.path.join(base, rel))
        print("MODEL", m)
    for t in BARKS:
        f = get_json(f"https://api.polyhaven.com/files/{t}")
        for key, name in (("Diffuse", "diff"), ("nor_gl", "nor_gl"), ("Rough", "rough")):
            info = f[key]["2k"]["jpg"]
            download(info["url"], os.path.join(OUT, "bark", f"{t}_{name}_2k.jpg"))
        print("BARK", t)
    z = os.path.join(OUT, "leaf", "Leaf001_2K-JPG.zip")
    download("https://ambientcg.com/get?file=Leaf001_2K-JPG.zip", z)
    with zipfile.ZipFile(z) as zf:
        for name in zf.namelist():
            if name.lower().endswith((".jpg", ".png")) and "/" not in name and ".." not in name:
                zf.extract(name, os.path.join(OUT, "leaf"))
    print("LEAF Leaf001", sorted(os.listdir(os.path.join(OUT, "leaf"))))
    print("DONE")


if __name__ == "__main__":
    import sys
    architecture() if "architecture" in sys.argv else main()
