"""unreal_kit: Python for the Unreal runtime toolsets (standard library only).

    import unreal_kit as uk
    uk.tools.lighting.apply_mood(mood="Noon")            # every tool of every toolset, typed and documented
    uk.batch([...])                                     # up to 100 calls in one request
    uk.parts / uk.rules / uk.ui / uk.look                 # generic helpers (examples: kit_examples/)

Connection (automatic): the relay client (UNREAL_RELAY + UNREAL_TOKEN environment variables, set by Connect.bat),
otherwise the game on this machine (http://127.0.0.1:8018/mcp, or UNREAL_LOCAL_MCP). uk.connect(url=...) overrides.

Results come back as plain JSON (dicts); a failing tool raises ToolError with the tool's message and hint.
Units: centimetres, degrees, Z up. help(uk.tools.<toolset>) lists a toolset's tools.
"""

import json
import os
import urllib.error
import urllib.request

__all__ = ["connect", "call", "batch", "place", "place_many", "save_image", "ToolError", "tools", "parts", "rules", "ui", "look"]

last_images = []  # images (base64 + mimeType) returned by the last call: screenshots, captures

USER_AGENT = "unreal-kit/1.0"  # Cloudflare blocks Python's default user agent


class ToolError(RuntimeError):
    def __init__(self, tool, error):
        self.tool = tool
        self.error = error
        msg = error.get("message") if isinstance(error, dict) else str(error)
        hint = error.get("hint") if isinstance(error, dict) else None
        super().__init__(f"{tool}: {msg}" + (f" (hint: {hint})" if hint else ""))


def _http(url, body, headers, timeout=300):
    req = urllib.request.Request(url, data=json.dumps(body).encode(), method="POST")
    req.add_header("Content-Type", "application/json")
    req.add_header("User-Agent", USER_AGENT)
    for k, v in headers.items():
        req.add_header(k, v)
    try:
        with urllib.request.urlopen(req, timeout=timeout) as r:
            return r.status, dict(r.headers), r.read().decode()
    except urllib.error.HTTPError as e:
        return e.code, dict(e.headers), e.read().decode(errors="replace")


class _Relay:
    """Relay scripting endpoint: POST /call and /batch with a bearer token (sessions handled by the relay)."""

    def __init__(self, base, token):
        self.base = base.rstrip("/")
        self.token = token

    def __call__(self, tool, arguments):
        status, _, body = _http(self.base + "/call", {"tool": tool, "arguments": arguments}, {"Authorization": "Bearer " + self.token})
        try:
            value = json.loads(body)
        except ValueError:
            value = None
        if isinstance(value, dict) and "_images" in value:  # the relay adds images next to the result
            last_images[:] = value.pop("_images")
            body = json.dumps(value.pop("_value") if "_value" in value else value)
        return _result(tool, status, body)


class _Local:
    """The game's MCP endpoint on this machine (JSON-RPC, session header)."""

    def __init__(self, url):
        self.url = url
        self.sid = None
        self.n = 0
        self._rpc("initialize", {"protocolVersion": "2025-06-18", "capabilities": {}, "clientInfo": {"name": "unreal_kit", "version": "1"}})

    def _rpc(self, method, params):
        self.n += 1
        headers = {"Accept": "application/json, text/event-stream"}
        if self.sid:
            headers["Mcp-Session-Id"] = self.sid
        status, h, body = _http(self.url, {"jsonrpc": "2.0", "id": self.n, "method": method, "params": params}, headers)
        self.sid = h.get("Mcp-Session-Id") or h.get("mcp-session-id") or self.sid
        if body.lstrip().startswith(("data:", "event:")):
            body = [l[5:] for l in body.splitlines() if l.startswith("data:")][-1]
        if status >= 400:
            raise ToolError(method, {"message": f"HTTP {status}: {body[:300]}"})
        return json.loads(body)

    def __call__(self, tool, arguments):
        msg = self._rpc("tools/call", {"name": "session_call_tool", "arguments": {"name": tool, "arguments": arguments}})
        if "error" in msg:
            raise ToolError(tool, msg["error"])
        content = msg.get("result", {}).get("content", [])
        last_images[:] = [{"data": c["data"], "mimeType": c.get("mimeType", "image/png")} for c in content if c.get("type") == "image"]
        text = next((c.get("text") for c in content if c.get("type") == "text"), "null")
        return _result(tool, 200, text)


def _result(tool, status, body):
    try:
        value = json.loads(body) if body else None
    except ValueError:
        raise ToolError(tool, {"message": f"HTTP {status}: {body[:300]}"})
    if isinstance(value, dict) and value.get("ok") is False:
        raise ToolError(tool, value.get("error"))
    if status >= 400:
        raise ToolError(tool, value if isinstance(value, dict) else {"message": str(value)})
    if isinstance(value, dict) and "returnValue" in value and not isinstance(value["returnValue"], dict):
        return value["returnValue"]
    return value


_transport = None


def _local_up(url):
    import socket
    from urllib.parse import urlparse
    u = urlparse(url)
    try:
        socket.create_connection((u.hostname, u.port or 80), timeout=0.5).close()
        return True
    except OSError:
        return False


def connect(url=None, relay=None, token=None):
    """Chooses the connection: url=<local MCP URL>, or relay=<https://.../s/CODE> + token. Default: the game on this
    machine when it is running (fastest), otherwise the relay from the environment (UNREAL_RELAY + UNREAL_TOKEN)."""
    global _transport
    local = url or os.environ.get("UNREAL_LOCAL_MCP", "http://127.0.0.1:8018/mcp")
    if not relay and (url or _local_up(local)):
        _transport = _Local(local)
        return _transport
    relay = relay or os.environ.get("UNREAL_RELAY")
    if relay:
        _transport = _Relay(relay, token or os.environ.get("UNREAL_TOKEN", ""))
    else:
        _transport = _Local(url or os.environ.get("UNREAL_LOCAL_MCP", "http://127.0.0.1:8018/mcp"))
    return _transport


def call(tool, **arguments):
    """Runs one tool by its qualified name, e.g. call("build.create_primitive", shape="box")."""
    last_images.clear()
    return (_transport or connect())(tool, arguments)


def save_image(path, index=0):
    """Writes an image from the last call (capture.screenshot, capture.scene_capture, build.capture_view) to a file."""
    import base64
    if not last_images:
        raise ToolError("save_image", {"message": "the last call returned no image"})
    with open(path, "wb") as f:
        f.write(base64.b64decode(last_images[index]["data"]))
    return path


def batch(calls, atomic=False):
    """Up to 100 calls [{"tool": ..., "arguments": {...}}] in one request; "$N.id" refers to call N's result."""
    out = []
    for i in range(0, len(calls), 100):
        out.append(call("ops.run_batch", calls=calls[i:i + 100], atomic=atomic))
    return out[0] if len(out) == 1 else out


def place(name, parts, at=(0, 0, 0), group=None):
    """Builds parts (dicts) as one object (more objects above 500 parts) at a location; returns node ids."""
    return place_many([(name, parts, at)], group=group)


def check_parts(parts, where="parts"):
    """Raises ToolError for unknown materials or shapes before anything is built (no half-built scenes)."""
    from .parts import PRESETS
    shapes = {"box", "sphere", "cylinder", "cone", "pyramid", "torus", "wedge", "capsule", "plane", "dome", "tube", "arch", "roof", "lattice",
              "steps", "tiers", "column", "openings", "revolve", "extrude", "sweep", "loft", "raw"}
    for i, p in enumerate(parts):
        m = p.get("material")
        if m is not None and m not in PRESETS:
            raise ToolError("check_parts", {"message": f"{where}[{i}]: material '{m}' is not a preset",
                                            "hint": "Use a preset (unreal_kit.parts.PRESETS) or M('<palette name>') / mat('<palette name>')"})
        if p.get("shape") not in shapes:
            raise ToolError("check_parts", {"message": f"{where}[{i}]: unknown shape '{p.get('shape')}'", "hint": ", ".join(sorted(shapes))})


def place_many(jobs, group=None, per_request=16):
    """jobs = [(name, parts, location)]: many objects, sent several per request. Returns created node ids.
    Every part is checked first, so a typo fails before anything is built."""
    for name, parts, _ in jobs:
        check_parts(parts, name)
    if group:
        try:
            group = call("scene.create_group", name=group).get("path", group)
        except ToolError:
            pass
    calls = []
    for name, parts, at in jobs:
        chunks = [parts[i:i + 500] for i in range(0, len(parts), 500)] or [[]]
        parent = group
        if len(chunks) > 1:  # big objects: chunks under one group with the object's name
            g = {"name": name}
            if group:
                g["parent"] = group
            parent = call("scene.create_group", **g).get("path", name)
        for k, chunk in enumerate(chunks):
            args = {"name": name if len(chunks) == 1 else f"{name}_{k}", "parts": chunk, "location": [round(float(v), 2) for v in at]}
            if parent:
                args["parent"] = parent
            calls.append({"tool": "build.create_assembly", "arguments": args})
    ids = []
    for i in range(0, len(calls), per_request):
        res = call("ops.run_batch", calls=calls[i:i + per_request])
        for r in res.get("results", []):
            if not r.get("ok", True):
                raise ToolError("build.create_assembly", r.get("error", r))
            ids.append(r.get("id"))
    return ids


from . import tools  # noqa: E402  (generated: one function per tool)
from . import look, parts, rules, ui  # noqa: E402,F401  (generic helpers; content examples are in kit_examples/)
