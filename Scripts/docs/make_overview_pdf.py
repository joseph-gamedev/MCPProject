"""Builds Docs/Runtime-Toolsets-Overview.pdf: what the Runtime Toolsets plugin does and how it works at runtime.

    python Scripts/docs/make_overview_pdf.py

The toolset table is read from the plugin (Tests/Schemas and each module's compiled tool docs), so the PDF follows the
code; the prose is written here. Proof images come from Docs/Proof (Scripts/proof).
"""
import datetime
import glob
import json
import os
import re

from reportlab.graphics.shapes import Drawing, Line, Polygon, Rect, String
from reportlab.lib import colors
from reportlab.lib.enums import TA_LEFT
from reportlab.lib.pagesizes import A4
from reportlab.lib.styles import ParagraphStyle, getSampleStyleSheet
from reportlab.lib.units import mm
from reportlab.platypus import (Image, KeepTogether, ListFlowable, ListItem, PageBreak, Paragraph, SimpleDocTemplate,
                                Spacer, Table, TableStyle)

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
PLUGIN = os.path.join(ROOT, "Plugins", "RuntimeToolsets")
OUT = os.path.join(ROOT, "Docs", "Runtime-Toolsets-Overview.pdf")

INK, MUTE, LINE, ACCENT, SOFT = colors.HexColor("#1d1d1b"), colors.HexColor("#5f5f5a"), colors.HexColor("#d8d8d2"), colors.HexColor("#1f5f99"), colors.HexColor("#eef3f8")
ss = getSampleStyleSheet()
H1 = ParagraphStyle("H1", parent=ss["Heading1"], fontSize=20, leading=24, textColor=INK, spaceAfter=6)
H2 = ParagraphStyle("H2", parent=ss["Heading2"], fontSize=14, leading=18, textColor=ACCENT, spaceBefore=12, spaceAfter=4)
H3 = ParagraphStyle("H3", parent=ss["Heading3"], fontSize=11, leading=14, textColor=INK, spaceBefore=6, spaceAfter=2)
BODY = ParagraphStyle("Body", parent=ss["BodyText"], fontSize=9.5, leading=13.5, textColor=INK, alignment=TA_LEFT, spaceAfter=5)
SMALL = ParagraphStyle("Small", parent=BODY, fontSize=8, leading=10.5, textColor=MUTE)
CELL = ParagraphStyle("Cell", parent=BODY, fontSize=8, leading=10.2, spaceAfter=0)
CELLB = ParagraphStyle("CellB", parent=CELL, fontName="Helvetica-Bold")
LEAD = ParagraphStyle("Lead", parent=BODY, fontSize=11, leading=15.5)


def P(text, style=BODY):
    return Paragraph(text, style)


def bullets(items, style=BODY, numbered=False):
    return ListFlowable([ListItem(P(t, style), leftIndent=12) for t in items], bulletType="1" if numbered else "bullet",
                        start="1" if numbered else None, leftIndent=14, bulletFontSize=8 if not numbered else 9)


def table(rows, widths, header=True):
    data = [[P(c, CELLB if (header and i == 0) else CELL) for c in r] for i, r in enumerate(rows)]
    t = Table(data, colWidths=widths, repeatRows=1 if header else 0)
    t.setStyle(TableStyle([("GRID", (0, 0), (-1, -1), 0.4, LINE), ("VALIGN", (0, 0), (-1, -1), "TOP"),
                           ("BACKGROUND", (0, 0), (-1, 0), SOFT if header else colors.white),
                           ("LEFTPADDING", (0, 0), (-1, -1), 4), ("RIGHTPADDING", (0, 0), (-1, -1), 4),
                           ("TOPPADDING", (0, 0), (-1, -1), 3), ("BOTTOMPADDING", (0, 0), (-1, -1), 3)]))
    return t


# ------------------------------------------------------------------------------------------------ facts from the plugin
FALLBACK = {"build": "Builder: terrain with biomes, islands and rivers; primitives and generators (arches, roofs, steps, lattices); "
                      "assemblies of many parts; profile solids; real meshes and GPU instances; booleans, arrays, mirrors; "
                      "materials from presets; measuring and overlap checks."}


def toolsets():
    """[(name, tool count, first sentence of its description)] from the schema snapshots."""
    out = []
    for f in sorted(glob.glob(os.path.join(PLUGIN, "Tests", "Schemas", "*.json"))):
        d = json.load(open(f, encoding="utf-8"))
        name = os.path.basename(f)[:-5]
        desc = " ".join((d.get("description") or "").split()) or FALLBACK.get(name, "")
        first = re.split(r"(?<=\.)\s", desc, maxsplit=1)[0]   # the first sentence, "Name:" lead-in kept
        if len(first) > 190:
            first = first[:187].rsplit(" ", 1)[0] + " ..."
        out.append((name, len(d.get("tools", [])), first))
    return out


def modules():
    """toolset -> code module, from each module's compiled tool docs."""
    owner = {}
    for f in glob.glob(os.path.join(PLUGIN, "Source", "*", "Private", "Generated", "ToolDocs.gen.inl")):
        mod = f.split(os.sep)[-4]
        text = "".join(re.findall(r'R"RTD\((.*?)\)RTD"', open(f, encoding="utf-8").read(), re.S))
        try:
            for name in json.loads(text):
                owner[name] = mod.replace("RuntimeToolsets", "") or "Core"
        except ValueError:
            pass
    return owner


def plugin_version():
    return json.load(open(os.path.join(PLUGIN, "RuntimeToolsets.uplugin"), encoding="utf-8")).get("VersionName", "")


# ------------------------------------------------------------------------------------------------------------ drawings
def box(d, x, y, w, h, title, lines=(), fill=colors.white, stroke=ACCENT, title_size=8.5):
    d.add(Rect(x, y, w, h, rx=4, ry=4, fillColor=fill, strokeColor=stroke, strokeWidth=0.9))
    d.add(String(x + 6, y + h - 12, title, fontName="Helvetica-Bold", fontSize=title_size, fillColor=INK))
    for i, l in enumerate(lines):
        d.add(String(x + 6, y + h - 24 - i * 10, l, fontName="Helvetica", fontSize=7.2, fillColor=MUTE))


def arrow(d, x1, y1, x2, y2, label=None, lx=None, ly=None):
    d.add(Line(x1, y1, x2, y2, strokeColor=INK, strokeWidth=0.8))
    import math
    a = math.atan2(y2 - y1, x2 - x1)
    s = 5
    d.add(Polygon([x2, y2, x2 - s * math.cos(a - 0.4), y2 - s * math.sin(a - 0.4), x2 - s * math.cos(a + 0.4), y2 - s * math.sin(a + 0.4)],
                  fillColor=INK, strokeColor=INK, strokeWidth=0.5))
    if label:
        d.add(String(lx if lx is not None else (x1 + x2) / 2 + 3, ly if ly is not None else (y1 + y2) / 2 + 3, label,
                     fontName="Helvetica", fontSize=6.8, fillColor=MUTE))


def architecture():
    W, H = 180 * mm, 128 * mm          # 510 x 363 pt
    d = Drawing(W, H)
    L, M, R = (0, 118), (148, 126), (306, W - 306)   # columns: x, width
    # who calls
    box(d, L[0], H - 52, L[1], 46, "AI agent", ["Claude Code or any", "MCP client"], fill=SOFT)
    box(d, L[0], H - 112, L[1], 46, "Python kit (unreal_kit)", ["batches of up to 100", "calls per request"], fill=SOFT)
    box(d, L[0], 8, L[1], 56, "Remote agent", ["game in the cloud:", "Connect.bat + AI code", "-> <relay>/s/<code>/mcp"], fill=SOFT)
    # transports
    box(d, M[0], H - 100, M[1], 64, "Local MCP endpoint", ["127.0.0.1:8018/mcp", "Epic ModelContextProtocol", "server (MCP adapter)"])
    box(d, M[0], 8, M[1], 70, "Relay (Cloudflare Worker)", ["the game long-polls it", "over outbound HTTPS:", "no open ports needed"])
    arrow(d, L[1], H - 30, M[0], H - 56)
    arrow(d, L[1], H - 90, M[0], H - 80)
    arrow(d, L[1], 36, M[0], 42)
    d.add(String(L[1] + 4, H - 24, "MCP", fontName="Helvetica", fontSize=6.8, fillColor=MUTE))
    d.add(String(L[1] + 4, 46, "HTTPS", fontName="Helvetica", fontSize=6.8, fillColor=MUTE))
    # inside the game
    gx, gw = R[0], R[1]
    bw = gw - 12
    d.add(Rect(gx - 6, 0, gw + 6, H, rx=6, ry=6, fillColor=colors.HexColor("#fafaf7"), strokeColor=LINE, strokeWidth=0.8))
    d.add(String(gx, H - 12, "The running game (editor -game or packaged)", fontName="Helvetica-Bold", fontSize=8, fillColor=INK))
    box(d, gx, H - 66, bw, 44, "MCP adapter (RuntimeToolsetsMCP)", ["lists the session_* tools; session_call_tool", "runs any toolset.tool by name"])
    box(d, gx, H - 158, bw, 80, "RuntimeToolsetsCore", ["dispatcher: queue of 64, about 4 ms of", "game-thread time per frame",
                                                         "JSON schemas and validation, results <= 256 KB", "scene graph (ids n12) and undo history",
                                                         "log capture, images, extension points"])
    box(d, gx, H - 226, bw, 56, "Toolset modules: 32 toolsets", ["CoreTools Content Presentation Gameplay", "Procedural Logic Ops AI GAS StateTree",
                                                                  "each tool = a C++ UFUNCTION"])
    box(d, gx, 8, bw, 62, "Unreal Engine world", ["actors, Lumen lighting, Niagara, PCG,", "navigation, UMG, physics ...",
                                                   "streamed by Pixel Streaming"], fill=SOFT)
    arrow(d, M[0] + M[1], H - 60, gx, H - 44)
    arrow(d, M[0] + M[1], 50, gx, H - 60)
    d.add(String(M[0] + M[1] + 6, 150, "relayed", fontName="Helvetica", fontSize=6.8, fillColor=MUTE))
    cx = gx + bw / 2
    arrow(d, cx, H - 66, cx, H - 78)
    arrow(d, cx, H - 158, cx, H - 170)
    arrow(d, cx, H - 226, cx, 70)
    return d


# ------------------------------------------------------------------------------------------------------------- content
def story():
    s = []
    ts = toolsets()
    owner = modules()
    n_tools = sum(t[1] for t in ts)
    today = datetime.date.today().isoformat()
    s += [P("Runtime Toolsets", H1),
          P(f"What it does and how it works at runtime &nbsp;·&nbsp; plugin {plugin_version()} &nbsp;·&nbsp; Unreal Engine 5.8 &nbsp;·&nbsp; {today}", SMALL),
          Spacer(1, 8),
          P(f"Runtime Toolsets lets an AI agent build and run a game <b>while the game is running</b>. The plugin turns "
            f"the live Unreal world into <b>{len(ts)} toolsets of {n_tools} tools</b> that an agent calls with plain JSON: "
            "build terrain and buildings, light the scene, place effects and UI, add physics, AI and game rules, then "
            "check the result with screenshots. Nothing needs the Unreal Editor: the same tools work in a packaged game, "
            "including one streamed from the cloud.", LEAD)]

    s += [P("1. What it does", H2),
          P("An agent works the way a person would at a live editor, but through tools. A typical session:"),
          bullets(["<b>Look:</b> session.list_toolsets and skills.get_skills tell the agent what exists and how to use it; "
                   "capture.screenshot shows the result.",
                   "<b>Build:</b> build.create_terrain, build.create_primitive / create_assembly (any shape, real materials), "
                   "gis plots, PCG scatter of trees, splines for roads and rivers; the Python kit adds whole buildings "
                   "(27 types: apartment to petrol station) and districts.",
                   "<b>Dress:</b> lighting moods and time of day, art styles (realistic, toon, lowpoly, noir, retro, stylized), "
                   "materials, Niagara effects, HUD text and world labels.",
                   "<b>Make it playable:</b> character.create or logic.spawn_player, logic rules (when touch, add score), AI agents "
                   "on a navmesh, Gameplay Ability System effects, StateTrees, physics.",
                   "<b>Change and undo:</b> every change is a scene-graph node or command, so history.undo, checkpoints and "
                   "rollback work across all toolsets."]),
          P("<b>Toolsets</b> (read from the plugin; module = the code module that registers it):", H3)]
    rows = [["Toolset", "Tools", "Module", "What it covers"]]
    for name, n, first in ts:
        rows.append([f"<b>{name}</b>", str(n), owner.get(name, "Core"), first])
    s += [table(rows, [22 * mm, 11 * mm, 24 * mm, 123 * mm])]

    s += [PageBreak(), P("2. How it fits together", H2),
          P("Three ways in, one engine inside. Agents speak the Model Context Protocol (MCP) to the game, either directly "
            "on this machine or through a small relay when the game runs somewhere nothing can connect to (a cloud GPU "
            "streaming the game). Every request ends in the same dispatcher on the game thread."),
          architecture(),
          Spacer(1, 6),
          table([["Way in", "Address", "When"],
                 ["Local MCP", "http://127.0.0.1:8018/mcp (port: -RuntimeToolsetsPort=N)", "Agent and game on one machine; editor -game or packaged."],
                 ["Relay", "https://&lt;relay&gt;/s/&lt;AI code&gt;/mcp", "Game in the cloud: the game long-polls the relay over outbound HTTPS and shows a short AI code; Connect.bat links the agent once. Nothing is installed."],
                 ["Python kit", "unreal_kit.zip next to the game (uk.connect())", "Scripts that build much at once: batches of up to 100 calls per request; reconnects by itself when the game reloads a level."]],
                [24 * mm, 62 * mm, 94 * mm])]

    s += [P("3. How a tool call runs at runtime", H2),
          P("From the agent's request to the change on screen, in order:"),
          bullets(["<b>Request.</b> The agent calls an MCP tool. In the default Meta mode the game lists only the session_* tools; "
                   "the agent discovers toolsets with session.describe_toolset and runs any tool through session_call_tool "
                   "{name: \"build.create_primitive\", arguments: {...}}. This keeps the agent's tool list short.",
                   "<b>Queue.</b> The call joins the dispatcher's queue (at most 64 waiting). All engine work happens on the game "
                   "thread, so the dispatcher drains the queue in slices of about 4 ms per frame: the game keeps rendering "
                   "and streaming at full rate while an agent builds.",
                   "<b>Validate.</b> Arguments are checked against the tool's JSON schema (generated from the C++ UFUNCTION "
                   "signature and its doc comment). A wrong type or a missing argument comes back as an error with a hint, "
                   "and nothing changes.",
                   "<b>Run.</b> The tool runs as a command: it spawns or edits actors, components and materials through the "
                   "normal engine APIs. Changes that last (spawn, move, material, light) are recorded in the session's history "
                   "with how to undo them; new objects become scene-graph nodes with stable ids (n12) and paths (house/door).",
                   "<b>Answer.</b> The result is JSON (at most 256 KB, or the call fails with a hint to narrow it). Capture tools "
                   "attach an image. Slow tools (navigation build, tests, waits) finish asynchronously, within 30 s by default.",
                   "<b>See it.</b> The world updates in the next frame; the player, or a Pixel Streaming viewer, sees it at once."],
                  numbered=True)]

    s += [P("4. The scene graph, undo and safety", H2),
          bullets(["<b>Nodes.</b> Everything the tools create is a node: an actor plus its source recipe (prim:, mesh:, inst:, vfx: ...). "
                   "From the recipe a node can be rebuilt: undo/redo, duplicate, templates (scene.save_template) and export/import of whole trees.",
                   "<b>Soft delete.</b> Deleting hides the actor and keeps it, so undo restores it exactly; scene.clear_all clears the "
                   "session's nodes in one undoable step.",
                   "<b>History.</b> history.undo / redo, transactions (several calls = one step), named checkpoints and rollback; "
                   "200 steps per session by default.",
                   "<b>Guard rails (Config/DefaultGame.ini).</b> Reflection may only touch allowed object roots and never denied "
                   "classes or functions; console commands can be deny- or allow-listed; level actors the tools did not make "
                   "cannot be destroyed unless allowed; toolsets and single tools can be switched off."])]

    s += [P("5. Building it into a game", H2),
          bullets(["<b>Plugins.</b> RuntimeToolsets (the toolsets, no external content needed) and RuntimeToolsetsMCP (the MCP "
                   "adapter and relay client). Both load in Development builds; Shipping and Test builds leave them out.",
                   "<b>Content.</b> The core plugin carries its own materials (/RuntimeToolsets). Project assets it can use, such as "
                   "scanned surfaces or a rippled-sand texture, are named in the project config as asset slots "
                   "(AssetSlots in DefaultGame.ini), so the plugin itself depends on nothing.",
                   "<b>Build and test.</b> Build the editor target, then run the automation tests (RuntimeToolsets.*: 223 tests, "
                   "including schema snapshots that flag any change to a tool's public surface).",
                   "<b>Package.</b> A normal BuildCookRun (Win64 Development, Pixel Streaming on). The relay URL and token are written into "
                   "the config only while packaging and never committed. The Python kit and its examples are copied next to the exe."])]

    s += [P("6. Extending it", H2),
          P("A new toolset is a C++ class deriving from URuntimeToolsetDefinition whose static UFUNCTIONs carry "
            "meta=(AICallable): the doc comment becomes the tool's description, the parameters its JSON schema, and the "
            "module registers the class at startup. Extension plugins add more without touching the core:"),
          bullets(["<b>Recipes:</b> RegisterRecipes adds ready-made scenes to ops.list_recipes / ops.run_recipe.",
                   "<b>Asset slots:</b> SetAsset points the core at an extension's materials (weather-aware ground, scanned surfaces) "
                   "or a project's third-person character.",
                   "<b>Lighting owner:</b> RegisterLightingOwner tells the lighting tools that a sky system drives the light, so they "
                   "refuse with a pointer to its tools instead of fighting it.",
                   "<b>Skills:</b> any URuntimeAgentSkill subclass is found by itself and served by skills.get_skills.",
                   "Toolsets for bought content packs (FluidNinja fluids, Ultra Dynamic Sky, Road Creator Pro roads) were built "
                   "this way in a separate plugin; they are kept out of this project for now."])]

    s += [P("7. Known limits", H2),
          bullets(["Development and editor builds only: the plugins are excluded from Shipping and Test configurations.",
                   "session.call_tool cannot call itself; the Python kit already runs every call through it.",
                   "Results over 256 KB fail: scene.get_node on a very large building (hundreds of parts) does; use "
                   "scene.list_tree with fields [\"bounds\"].",
                   "Screen UI (HUD text, logic HUDs) is not a scene node, so scene.clear_all leaves it on screen.",
                   "The retro style keeps its pixel look without colour posterising (that step flattened the image)."])]

    s += [P("8. Proof", H2),
          P("Every toolset was exercised in a running game (447 of 448 calls passed; the one refusal is the expected "
            "self-call) and all 27 building types were built in the packaged game. Screenshots and the saved responses "
            "are in <b>Docs/Proof</b>; Scripts/proof regenerates them. A few of them:")]
    picks = [("toolsets/build.jpg", "build: terrain, primitives, assembly, mesh, river"), ("toolsets/gis.jpg", "gis: generated plots and streets"),
             ("toolsets/pcg.jpg", "pcg: a forest scattered inside a spline"), ("toolsets/style_toon.jpg", "style: toon"),
             ("toolsets/lighting_sunset.jpg", "lighting: sunset mood"), ("buildings/office_tower.jpg", "kit: office tower")]
    cells = []
    for path, cap in picks:
        p = os.path.join(ROOT, "Docs", "Proof", path)
        if os.path.exists(p):
            cells.append([Image(p, width=86 * mm, height=48.4 * mm), P(cap, SMALL)])
    if cells:
        grid = [[cells[i], cells[i + 1] if i + 1 < len(cells) else ""] for i in range(0, len(cells), 2)]
        t = Table(grid, colWidths=[90 * mm, 90 * mm])
        t.setStyle(TableStyle([("VALIGN", (0, 0), (-1, -1), "TOP"), ("BOTTOMPADDING", (0, 0), (-1, -1), 6)]))
        s += [t]
    return s


def footer(canvas, doc):
    canvas.saveState()
    canvas.setFont("Helvetica", 7.5)
    canvas.setFillColor(MUTE)
    canvas.drawString(15 * mm, 10 * mm, "Runtime Toolsets - what it does and how it works at runtime")
    canvas.drawRightString(A4[0] - 15 * mm, 10 * mm, f"page {doc.page}")
    canvas.restoreState()


def main():
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    doc = SimpleDocTemplate(OUT, pagesize=A4, leftMargin=15 * mm, rightMargin=15 * mm, topMargin=15 * mm, bottomMargin=16 * mm,
                            title="Runtime Toolsets: what it does and how it works at runtime", author="MCPProject")
    doc.build(story(), onFirstPage=footer, onLaterPages=footer)
    print("written", OUT, os.path.getsize(OUT) // 1024, "KB")


if __name__ == "__main__":
    main()
