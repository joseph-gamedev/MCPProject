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

import gzip
import http.client
import json
import os
import threading
import urllib.error
import urllib.request
from concurrent.futures import ThreadPoolExecutor
from urllib.parse import urlsplit

__all__ = ["connect", "call", "batch", "place", "place_many", "save_image", "ToolError", "tools", "parts", "rules", "ui", "look", "assets", "district", "gis"]

last_images = []  # images (base64 + mimeType) returned by the last call: screenshots, captures

USER_AGENT = "unreal-kit/1.0"  # Cloudflare blocks Python's default user agent


class ToolError(RuntimeError):
    def __init__(self, tool, error):
        self.tool = tool
        self.error = error
        msg = error.get("message") if isinstance(error, dict) else str(error)
        hint = error.get("hint") if isinstance(error, dict) else None
        super().__init__(f"{tool}: {msg}" + (f" (hint: {hint})" if hint else ""))


_conns = threading.local()  # one kept-alive connection per thread and host: no TLS handshake per request (~0.5 s far away)


def _http(url, body, headers, timeout=300, compress=False):
    data = json.dumps(body, separators=(",", ":")).encode()
    hdrs = {"Content-Type": "application/json", "User-Agent": USER_AGENT, **headers}
    if compress and len(data) > 32768:  # the relay unpacks it: big part lists shrink about 8x
        data = gzip.compress(data, 5)
        hdrs["Content-Encoding"] = "gzip"
    u = urlsplit(url)
    key = (u.scheme, u.netloc)
    pool = getattr(_conns, "pool", None)
    if pool is None:
        pool = _conns.pool = {}
    path = u.path + ("?" + u.query if u.query else "")
    for attempt in range(2):
        conn = pool.get(key)
        if conn is None:
            cls = http.client.HTTPSConnection if u.scheme == "https" else http.client.HTTPConnection
            conn = pool[key] = cls(u.netloc, timeout=timeout)
        try:
            conn.request("POST", path, body=data, headers=hdrs)
            r = conn.getresponse()
            text = r.read().decode(errors="replace")
            if r.getheader("Connection", "").lower() == "close":
                conn.close()
                pool.pop(key, None)
            return r.status, {k: v for k, v in r.getheaders()}, text
        except (http.client.HTTPException, ConnectionError, OSError):
            conn.close()
            pool.pop(key, None)
            if attempt:
                raise


class _Relay:
    """Relay scripting endpoint: POST /call and /batch with a bearer token (sessions handled by the relay)."""

    def __init__(self, base, token):
        self.base = base.rstrip("/")
        self.token = token

    def __call__(self, tool, arguments):
        status, _, body = _http(self.base + "/call", {"tool": tool, "arguments": arguments}, {"Authorization": "Bearer " + self.token}, compress=True)
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
        self._initialize()

    def _initialize(self):
        self.sid = None
        self._rpc("initialize", {"protocolVersion": "2025-06-18", "capabilities": {}, "clientInfo": {"name": "unreal_kit", "version": "1"}})

    def _rpc(self, method, params, retry=True):
        try:
            return self._rpc_once(method, params)
        except ToolError as e:
            # the game restarts its MCP server when a level loads (or the game restarts): start a new session and retry once
            if retry and method != "initialize" and "Unknown session id" in str(e):
                self._initialize()
                return self._rpc_once(method, params)
            raise

    def _rpc_once(self, method, params):
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


def call(tool, /, **arguments):
    """Runs one tool by its qualified name, e.g. call("build.create_primitive", shape="box").
    (tool is positional-only, so tools with their own "tool" argument, e.g. build.boolean_op, work too.)"""
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


_max_parts = None  # parts per assembly: 800 on plugin 0.14+, 500 on older builds (checked once)


def place_many(jobs, group=None, parts_per_request=12000, in_flight=3):
    """jobs = [(name, parts, location)]: many objects, sent in big requests, several in flight at once. Returns one node id
    per job (the object, or its group when it is bigger than one assembly).
    Every part is checked first, so a typo fails before anything is built. A whole building is usually one object."""
    global _max_parts
    for name, parts, _ in jobs:
        check_parts(parts, name)
    if group:  # by node id: a name can match a group hidden by an earlier clear
        g = call("scene.create_group", name=group)
        group = g.get("id") or g.get("path") or group

    owner, job_group = [], {}

    def plan(limit):
        # objects bigger than one assembly get their own group: all such groups in one request, by node id
        big = [name for name, parts, _ in jobs if len(parts) > limit]
        gid = {}
        for b in range(0, len(big), 100):  # a batch takes at most 100 calls
            part = big[b:b + 100]
            res = call("ops.run_batch", calls=[{"tool": "scene.create_group", "arguments": dict({"name": n}, **({"parent": group} if group else {}))}
                                               for n in part])
            for n, r in zip(part, res.get("results", [])):
                if not r.get("ok", True):
                    raise ToolError("scene.create_group", r.get("error", r))
                gid[n] = r.get("id") or r.get("path") or n
        calls, sizes = [], []
        owner.clear()
        for j, (name, parts, at) in enumerate(jobs):
            chunks = [parts[i:i + limit] for i in range(0, len(parts), limit)] or [[]]
            parent = gid.get(name, group) if len(chunks) > 1 else group
            for k, chunk in enumerate(chunks):
                args = {"name": name if len(chunks) == 1 else f"{name}_{k}", "parts": chunk, "location": [round(float(v), 2) for v in at]}
                if parent:
                    args["parent"] = parent
                calls.append({"tool": "build.create_assembly", "arguments": args})
                sizes.append(len(chunk))
                owner.append(j)
            job_group[j] = gid.get(name) if len(chunks) > 1 else None
        requests, cur, n = [], [], 0
        for c, sz in zip(calls, sizes):  # pack whole assemblies into requests of about parts_per_request parts
            if cur and (n + sz > parts_per_request or len(cur) >= 100):
                requests.append(cur)
                cur, n = [], 0
            cur.append(c)
            n += sz
        if cur:
            requests.append(cur)
        return requests

    def send(req):
        return call("ops.run_batch", calls=req)

    def check(results):
        ids = []
        for res in results:
            for r in res.get("results", []):
                if not r.get("ok", True):
                    raise ToolError("build.create_assembly", r.get("error", r))
                ids.append(r.get("id"))
        out = [job_group.get(j) for j in range(len(jobs))]
        for k, nid in enumerate(ids):                         # single-assembly jobs: the assembly itself
            j = owner[k]
            if out[j] is None:
                out[j] = nid
        return out

    if _max_parts is None:  # plugin 0.14+ takes 8192 parts per assembly; older builds 512
        try:
            v = tuple(int(x) for x in str(call("session.get_build_info").get("plugin_version", "0")).split(".")[:2])
        except (ToolError, ValueError):
            v = (0, 0)
        _max_parts = 800 if v >= (0, 14) else 500  # past ~1000 parts an assembly builds slower per part
    requests, done = plan(_max_parts), []
    with ThreadPoolExecutor(max_workers=max(1, in_flight)) as pool:
        done += list(pool.map(send, requests))
    return check(done)


from . import tools  # noqa: E402  (generated: one function per tool)
from . import assets, look, parts, rules, ui  # noqa: E402,F401
from .district import district  # noqa: E402,F401  (one call: a whole styled district)
from . import gis  # noqa: E402,F401  (land plots: random, GeoJSON, GeoRegions; buildings on plots)  (generic helpers; content examples are in kit_examples/)
