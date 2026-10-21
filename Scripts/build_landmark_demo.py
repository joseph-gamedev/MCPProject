"""
Builds a simplified Taj Mahal block-out over MCP with the primitive builder, then saves:
  <out>_sheet.jpg   ortho front/right/top + perspective contact sheet (build.capture_view)
  <out>_shot.jpg    framed in-game screenshot

Usage: python Scripts/build_landmark_demo.py [--url http://127.0.0.1:8018/mcp] [--out landmark]
"""

import argparse
import base64
import json
import sys
import time

from mcp_smoke_test import McpClient, PROTOCOL_VERSION


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--url", default="http://127.0.0.1:8018/mcp")
    parser.add_argument("--out", default="landmark")
    args = parser.parse_args()

    client = McpClient(args.url)
    client.request("initialize", {"protocolVersion": PROTOCOL_VERSION, "capabilities": {}, "clientInfo": {"name": "landmark-demo", "version": "1.0"}})
    client.notify("notifications/initialized")

    def call(name, arguments=None):
        payload, images, is_error = client.call("session_call_tool", {"name": name, "arguments": arguments or {}})
        if is_error or not payload.get("ok"):
            raise RuntimeError(f"{name}: {json.dumps(payload)}")
        return payload, images

    def prim(**kw):
        return call("build.create_primitive", kw)[0]["id"]

    start = time.time()
    root = call("scene.create_group", {"name": "taj_mahal", "location": [0, 0, 0]})[0]["id"]
    plinth = prim(shape="box", size=[6000, 6000, 400], material="marble", parent=root, name="plinth")
    hall = prim(shape="box", size=[3000, 3000, 2400], material="marble", parent=root, name="hall", location=[0, 0, 400])
    # Iwan: a tall pointed recess on each side of the hall (box + cylinder cutters).
    for i, (x, y, yaw) in enumerate([(-1500, 0, 0), (1500, 0, 0), (0, -1500, 90), (0, 1500, 90)]):
        cutter = prim(shape="box", size=[600, 1000, 1500], pivot="center", location=[x, y, 1150], rotation=[0, yaw, 0], name=f"iwan_cut_{i}")
        call("build.boolean_op", {"target": hall, "tool": cutter, "op": "subtract"})
    drum = prim(shape="cylinder", size=[1700, 1700, 700], material="marble", parent=root, name="drum", location=[0, 0, 2800], detail="high")
    dome = call("build.create_profile_solid", {
        "kind": "lathe", "name": "onion_dome", "parent": root, "location": [0, 0, 3500], "material": "marble", "detail": "high",
        "profile": [[0, 0], [850, 0], [1000, 450], [950, 950], [650, 1450], [250, 1850], [60, 2050], [0, 2100]]})[0]["id"]
    finial = prim(shape="cone", size=[160, 160, 500], material="gold", parent=root, name="finial", location=[0, 0, 5550])
    minaret = prim(shape="cylinder", size=[380, 380, 4200], material="marble", parent=root, name="minaret", location=[2650, 2650, 400], detail="high")
    cap = prim(shape="dome", size=[520, 520, 380], material="marble", parent=root, name="minaret_cap", location=[2650, 2650, 4600])
    chhatri = prim(shape="dome", size=[700, 700, 500], material="marble", parent=root, name="chhatri", location=[1150, 1150, 2800])
    for target in (minaret, cap, chhatri):
        call("build.array", {"target": target, "mode": "radial", "count": 4, "center": [0, 0, 0]})
    pool = prim(shape="box", size=[7000, 900, 60], material="water", parent=root, name="pool", location=[-7000, 0, 0])
    stats = call("build.get_build_stats", {"under": root})[0]
    overlaps = call("build.check_overlaps", {"under": root, "tolerance": 5})[0]
    print(f"built in {time.time() - start:.1f}s: {stats['nodes']} nodes, {stats['triangles']} triangles, "
          f"{stats['draw_calls_estimate']} draw calls, size {stats['size']}, {overlaps['overlaps']} bbox overlaps")

    call("lighting.apply_mood", {"mood": "morning"})
    payload, images = call("build.capture_view", {"under": root, "size": 512})
    with open(f"{args.out}_sheet.jpg", "wb") as f:
        f.write(base64.b64decode(images[0]["data"]))
    print("contact sheet:", payload["layout"])
    call("camera.frame_nodes", {"targets": [root], "direction": [1, 0.55, -0.3], "padding": 1.0})
    time.sleep(0.5)
    payload, images = call("capture.screenshot", {"max_dim": 1024})
    with open(f"{args.out}_shot.jpg", "wb") as f:
        f.write(base64.b64decode(images[0]["data"]))
    print(f"saved {args.out}_sheet.jpg and {args.out}_shot.jpg")
    return 0


if __name__ == "__main__":
    sys.exit(main())
