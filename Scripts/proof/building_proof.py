"""Builds every building type (unreal_kit.modern.KINDS) and screenshots each: python Scripts/proof/building_proof.py [port] -> Saved/ToolsetProof/buildings."""
import json
import math
import os
import sys
import time

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path[:0] = [os.path.join(ROOT, "Scripts")]
import unreal_kit as uk
from unreal_kit import modern

PORT = sys.argv[1] if len(sys.argv) > 1 else "8028"
OUT = os.path.join(ROOT, "Saved", "ToolsetProof", "buildings")
os.makedirs(OUT, exist_ok=True)
uk.connect(url=f"http://127.0.0.1:{PORT}/mcp")
for _ in range(80):
    try:
        uk.call("session.ping")
        break
    except Exception:
        time.sleep(3)

results = {}
for i, kind in enumerate(modern.KINDS):
    t0 = time.time()
    try:
        uk.call("style.apply", preset="realistic")
        uk.call("scene.clear_all", confirm=True)
        uk.call("build.create_terrain", size=[60000, 60000], height=0, location=[0, 0, 0], biome="temperate", name="Ground")
        uk.call("lighting.apply_mood", mood="Morning")
        r = modern.place(kind, at=(0, 0, 0), seed=7 + i)
        node = r["node"]
        b = uk.call("scene.list_tree", root=node, depth=0, fields=["bounds"])["nodes"][0]["bounds"]
        c, s = b["center"], b["size"]
        # frame the footprint and the height: a three-quarter view from the front-left, a little above
        d = max(s[0], s[1]) * 0.95 + s[2] * 0.9 + 600
        cam = [c[0] - d * 0.55, c[1] - d * 0.83, s[2] * 0.55 + d * 0.28]
        uk.call("camera.set_camera", location=cam, look_at=[c[0], c[1], s[2] * 0.4], fov=55)
        time.sleep(5)
        uk.call("capture.screenshot", format="Png", max_dim=1100)
        uk.save_image(os.path.join(OUT, f"{kind}.png"))
        results[kind] = {"ok": True, "parts": r["parts"], "size_m": [round(v / 100) for v in s], "seconds": round(time.time() - t0, 1)}
    except Exception as e:  # noqa: BLE001
        results[kind] = {"ok": False, "error": str(e)[:300]}
    print(f"{kind:<16} {results[kind]}", flush=True)
json.dump(results, open(os.path.join(OUT, "results.json"), "w"), indent=1)
