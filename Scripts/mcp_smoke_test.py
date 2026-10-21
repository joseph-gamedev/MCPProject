"""
End-to-end smoke test for the RuntimeToolsets MCP adapter.

Talks MCP (Streamable HTTP) to a running game or PIE session, the same way Claude Code does:
initialize -> tools/list -> a scripted sequence of tools/call.

Usage:
    python Scripts/mcp_smoke_test.py [--url http://127.0.0.1:8018/mcp] [--save-image out.jpg]

Exit code 0 when every step passes.
"""

import argparse
import base64
import json
import sys
import urllib.request

PROTOCOL_VERSION = "2025-06-18"


class McpClient:
    def __init__(self, url):
        self.url = url
        self.session_id = None
        self.next_id = 1

    def _post(self, payload):
        data = json.dumps(payload).encode("utf-8")
        req = urllib.request.Request(self.url, data=data, method="POST")
        req.add_header("Content-Type", "application/json")
        req.add_header("Accept", "application/json, text/event-stream")
        req.add_header("MCP-Protocol-Version", PROTOCOL_VERSION)
        if self.session_id:
            req.add_header("Mcp-Session-Id", self.session_id)
        with urllib.request.urlopen(req, timeout=60) as resp:
            sid = resp.headers.get("Mcp-Session-Id")
            if sid:
                self.session_id = sid
            body = resp.read().decode("utf-8")
            ctype = resp.headers.get("Content-Type", "")
        if not body.strip():
            return None
        if "text/event-stream" in ctype:
            # Take the last JSON-RPC response carried in the SSE stream.
            result = None
            for line in body.splitlines():
                if line.startswith("data:"):
                    msg = json.loads(line[5:].strip())
                    if "id" in msg and ("result" in msg or "error" in msg):
                        result = msg
            return result
        return json.loads(body)

    def request(self, method, params=None):
        msg = {"jsonrpc": "2.0", "id": self.next_id, "method": method}
        self.next_id += 1
        if params is not None:
            msg["params"] = params
        resp = self._post(msg)
        if resp is None:
            raise RuntimeError(f"{method}: empty response")
        if "error" in resp:
            raise RuntimeError(f"{method}: {resp['error']}")
        return resp["result"]

    def notify(self, method, params=None):
        msg = {"jsonrpc": "2.0", "method": method}
        if params is not None:
            msg["params"] = params
        self._post(msg)

    def call(self, name, arguments=None):
        result = self.request("tools/call", {"name": name, "arguments": arguments or {}})
        texts = [c["text"] for c in result.get("content", []) if c.get("type") == "text"]
        images = [c for c in result.get("content", []) if c.get("type") == "image"]
        payload = json.loads(texts[0]) if texts else {}
        return payload, images, bool(result.get("isError"))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--url", default="http://127.0.0.1:8018/mcp")
    parser.add_argument("--save-image", default=None)
    args = parser.parse_args()

    client = McpClient(args.url)
    failures = []

    def step(label, fn):
        try:
            detail = fn()
            print(f"PASS {label}" + (f"  {detail}" if detail else ""))
        except Exception as exc:  # noqa: BLE001 - report every failure
            failures.append(label)
            print(f"FAIL {label}: {exc}")

    init = client.request("initialize", {
        "protocolVersion": PROTOCOL_VERSION,
        "capabilities": {},
        "clientInfo": {"name": "runtime-toolsets-smoke", "version": "1.0"},
    })
    print(f"server: {init.get('serverInfo')} protocol {init.get('protocolVersion')}")
    client.notify("notifications/initialized")

    tools = client.request("tools/list").get("tools", [])
    names = sorted(t["name"] for t in tools)
    print(f"tools/list: {len(names)} tools: {', '.join(names)}")
    # Meta mode lists session_* only; eager lists everything.
    call_direct = "actors_spawn_actor" in names

    def call(name, arguments=None):
        if call_direct or name.startswith("session_"):
            payload, images, is_error = client.call(name, arguments)
        else:
            payload, images, is_error = client.call("session_call_tool", {"name": name, "arguments": arguments or {}})
        if is_error or not payload.get("ok", False):
            raise RuntimeError(json.dumps(payload))
        return payload, images

    state = {}

    step("session_ping", lambda: call("session_ping")[0].get("frame"))
    step("session_get_build_info", lambda: call("session_get_build_info")[0].get("config"))
    step("session_list_toolsets", lambda: len(call("session_list_toolsets")[0]["toolsets"]))

    def spawn():
        payload, _ = call("actors_spawn_actor", {"shape": "cube", "name": "smoke_cube", "location": [300, 0, 150], "color": [1, 0.2, 0.1]})
        state["id"] = payload["id"]
        return payload["id"]

    step("actors_spawn_actor", spawn)
    step("actors_set_transform", lambda: call("actors_set_transform", {"target": state["id"], "location": [400, 100, 150]})[0].get("location"))
    step("scene_get_node", lambda: call("scene_get_node", {"target": state["id"]})[0].get("location"))
    step("history_undo", lambda: call("history_undo")[0].get("steps"))
    step("reflection_call_function", lambda: call("reflection_call_function", {"target": state["id"], "function": "K2_GetActorLocation"})[0].get("return"))
    step("console_get_cvar", lambda: call("console_get_cvar", {"name": "t.MaxFPS"})[0].get("value"))
    step("log_get_stat", lambda: call("log_get_stat")[0])

    # Phase 2 builder tools: color, arrange, frame, then look.
    def builder_scene():
        ids = [call("actors_spawn_actor", {"shape": "cube", "name": f"row_{i}", "location": [i * 37, 600, 50]})[0]["id"] for i in range(4)]
        state["row"] = ids
        call("layout_arrange_in_row", {"targets": ids, "start": [0, 600, 50], "gap": 40})
        call("materials_set_vector", {"target": ids[0], "name": "Color", "value": [0.1, 0.4, 1.0]})
        return ",".join(ids)

    step("builder: spawn + layout_arrange_in_row + materials_set_vector", builder_scene)
    step("camera_frame_nodes", lambda: call("camera_frame_nodes", {"targets": state["row"], "direction": [0, 1, -0.4]})[0].get("distance"))
    step("camera_get_camera", lambda: call("camera_get_camera")[0].get("tool_camera"))

    def screenshot():
        payload, images = call("capture_screenshot", {"max_dim": 512})
        if not images:
            raise RuntimeError("no image content")
        data = base64.b64decode(images[0]["data"])
        if args.save_image:
            with open(args.save_image, "wb") as f:
                f.write(data)
        return f"{payload['w']}x{payload['h']} {images[0]['mimeType']} {len(data)} bytes"

    step("capture_screenshot", screenshot)
    step("camera_reset_camera", lambda: call("camera_reset_camera")[0].get("view_target"))
    step("log_tail_log", lambda: len(call("log_tail_log", {"limit": 5})[0]["lines"]))
    step("scene_clear_all", lambda: call("scene_clear_all", {"confirm": True})[0].get("deleted"))
    step("session_get_usage_report", lambda: call("session_get_usage_report")[0].get("result_bytes_avg"))

    print(f"\n{'ALL PASSED' if not failures else 'FAILED: ' + ', '.join(failures)}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
