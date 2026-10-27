"""Exercises every toolset's main features in a running game and saves proof (screenshots + responses).

python Scripts/proof/toolset_proof.py [port] [toolset,...]   -> Saved/ToolsetProof/{toolset}.png|.json, results.json
"""
import json
import os
import sys
import time
import traceback

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path[:0] = [os.path.join(ROOT, "Scripts")]
import unreal_kit as uk

PORT = sys.argv[1] if len(sys.argv) > 1 else "8028"
ONLY = set(sys.argv[2].split(",")) if len(sys.argv) > 2 else None
OUT = os.path.join(ROOT, "Saved", "ToolsetProof")
os.makedirs(OUT, exist_ok=True)
uk.connect(url=f"http://127.0.0.1:{PORT}/mcp")
for _ in range(60):
    try:
        uk.call("session.ping")
        break
    except Exception:
        time.sleep(3)

RESULTS = {}
if os.path.exists(os.path.join(OUT, "results.json")):
    RESULTS = json.load(open(os.path.join(OUT, "results.json"), encoding="utf-8"))


class T:
    def __init__(self, name):
        self.name, self.calls, self.proof = name, [], {}

    def c(self, _tool, **args):
        tool = _tool
        try:
            r = uk.call(tool, **args)
            self.calls.append({"tool": tool, "ok": True})
            return r
        except Exception as e:  # noqa: BLE001
            self.calls.append({"tool": tool, "ok": False, "error": str(e)[:300]})
            return None

    def keep(self, key, value):
        self.proof[key] = value

    def shot(self, cam, look, fov=65, suffix="", wait=6, show_ui=False):
        try:
            if cam is not None:   # None: whatever the game camera shows (e.g. the player's)
                uk.call("camera.set_camera", location=list(cam), look_at=list(look), fov=fov)
            time.sleep(wait)
            uk.call("capture.screenshot", format="Png", max_dim=1100, show_ui=show_ui)
            path = os.path.join(OUT, f"{self.name}{suffix}.png")
            uk.save_image(path)
            return path
        except Exception as e:  # noqa: BLE001
            self.calls.append({"tool": "(screenshot)", "ok": False, "error": str(e)[:200]})


def ref(r, *keys):
    """First id-like value in a result."""
    if r is None:
        return None
    if isinstance(r, dict):
        for k in keys + ("id", "ref", "node", "actor"):
            v = r.get(k)
            if isinstance(v, str):
                return v
            if isinstance(v, dict) and isinstance(v.get("ref") or v.get("id"), str):
                return v.get("ref") or v.get("id")
    return None


def fresh(t, terrain=True, hour=14):
    # each test starts from the same look: no leftover style, mood or exposure from the test before
    t.c("style.apply", preset="realistic")
    t.c("lighting.apply_mood", mood="Noon")
    t.c("scene.clear_all", confirm=True)
    if terrain:
        t.c("build.create_terrain", size=[40000, 40000], height=0, location=[0, 0, 0], biome="temperate", name="Ground")
    t.c("lighting.set_time_of_day", hour=hour)


# ---------------------------------------------------------------------------------------------------------------------
def t_session(t):
    t.keep("ping", t.c("session.ping"))
    t.keep("build_info", t.c("session.get_build_info"))
    t.c("session.get_capabilities")
    ts = t.c("session.list_toolsets")
    t.keep("toolsets", [x["name"] for x in (ts or {}).get("toolsets", [])])
    d = t.c("session.describe_toolset", name="actors")
    t.keep("actors_tools", [x.get("name") for x in (d or {}).get("tools", [])])
    t.c("session.call_tool", name="game.get_game_state", arguments={})
    t.keep("usage", t.c("session.get_usage_report"))


def t_build(t):
    fresh(t, terrain=False)
    t.c("build.create_terrain", size=[30000, 30000], height=500, feature_size=8000, location=[0, 0, -100], biome="temperate", seed=3, name="Hills")
    box = ref(t.c("build.create_primitive", shape="Box", size=[300, 300, 300], location=[-1200, 0, 300], material="brick", name="Box"))
    t.c("build.create_primitive", shape="Sphere", size=[250, 250, 250], location=[-600, 0, 300], material="steel", color=[0.8, 0.6, 0.2], name="Sphere")
    t.c("build.create_primitive", shape="Cylinder", size=[200, 200, 400], location=[0, 0, 350], material="wood", name="Cylinder")
    t.c("build.create_primitive", shape="Arch", size=[400, 100, 400], location=[600, 0, 350], material="stone", name="Arch")
    t.c("build.create_primitive", shape="Steps", size=[300, 400, 200], location=[1200, 0, 250], material="concrete", name="Steps")
    t.c("build.create_profile_solid", kind="Lathe", profile=[[0, 0], [80, 0], [60, 120], [90, 220], [0, 240]], location=[-1200, 800, 200], material="terracotta", name="Vase")
    t.c("build.create_profile_solid", kind="Extrude", profile=[[0, 0], [200, 0], [200, 60], [60, 60], [60, 200], [0, 200]], height=100,
        location=[-600, 800, 200], material="plaster", name="LBeam")
    asm = t.c("build.create_assembly", name="Table", location=[0, 800, 200], material="wood",
              parts=[{"shape": "box", "size": [300, 160, 12], "location": [0, 0, 90]}] +
                    [{"shape": "cylinder", "size": [12, 12, 90], "location": [x, y, 45]} for x in (-130, 130) for y in (-60, 60)])
    cyl = ref(t.c("build.create_primitive", shape="Box", size=[100, 100, 100], location=[600, 800, 250], material="glass", name="Cube"))
    if cyl:
        t.c("build.array", target=cyl, count=4, offset=[150, 0, 0])
    if box:
        t.c("build.mirror", target=box, axis="Y", point=[0, 0, 0])
        hole = ref(t.c("build.create_primitive", shape="Sphere", size=[220, 220, 220], location=[-1200, 0, 450], name="Cutter"))
        if hole:
            t.c("build.boolean_op", target=box, tool=hole, op="Subtract")
        t.c("build.set_material", targets=[box], material="brick", color=[0.7, 0.3, 0.2])
        t.keep("measure", t.c("build.measure", a=box, b="Sphere"))
    t.c("build.place_mesh", asset="/Game/RuntimeFoliage/Trees/SM_Oak_01", location=[1800, 900, 0], name="Oak")
    t.c("build.place_instances", asset="/Engine/BasicShapes/Cube", instances=[[2400 + i * 150, 900, 50, i * 20, 0.5] for i in range(6)], name="Crates")
    t.c("build.create_river", path=[[-4000, -2500, 0], [0, -2000, 0], [4000, -2600, 0]], width=800, name="River")
    t.keep("stats", t.c("build.get_build_stats"))
    t.c("build.check_overlaps")
    t.keep("materials", (t.c("build.list_materials") or {}).get("presets"))
    t.c("build.get_guide")
    t.c("build.capture_view", targets=["Table"], size=512)
    t.shot((-2800, -3200, 1600), (300, 400, 200))


def t_actors(t):
    fresh(t)
    a = ref(t.c("actors.spawn_actor", shape="cube", location=[0, 0, 100], color=[0.2, 0.6, 1.0], name="BlueCube", tags=["proof"]))
    b = ref(t.c("actors.spawn_actor", **{"class": "/Script/Engine.PointLight"}, location=[0, -300, 400], name="Lamp",
                tags=["lamp"]))
    c = ref(t.c("actors.spawn_actor", shape="sphere", location=[300, 0, 100], color=[1, 0.3, 0.2], name="RedBall"))
    if a:
        t.c("actors.set_transform", target=a, location=[0, 0, 150], rotation=[0, 45, 0], scale=[1.5, 1.5, 1.5])
        t.c("actors.add_component", target=a, **{"class": "PointLightComponent"}, name="Glow", location=[0, 0, 120])
        comps = t.c("actors.list_components", target=a)
        t.keep("components", [x.get("name") for x in (comps or {}).get("components", [])])
        if c:
            t.c("actors.attach", child=c, parent=a)
    t.keep("find", len((t.c("actors.find_actors", tag="proof") or {}).get("actors", [])))
    t.c("actors.set_visibility", target=b or "Lamp", visible=True)
    t.shot((-900, -700, 450), (100, 0, 150))
    if a:
        cm = [x for x in (t.c("actors.list_components", target=a) or {}).get("components", []) if x.get("name") == "Glow"]
        if cm:
            t.c("actors.remove_component", target=cm[0].get("ref") or cm[0].get("id"))
    t.c("actors.destroy_actor", target=b or "Lamp")


def t_scene(t):
    fresh(t)
    g = ref(t.c("scene.create_group", name="Village", location=[0, 0, 0]))
    h = ref(t.c("build.create_primitive", shape="Box", size=[400, 400, 300], location=[0, 0, 150], material="plaster", name="House", parent=g))
    r = ref(t.c("build.create_primitive", shape="Roof", size=[440, 440, 200], location=[0, 0, 400], material="roof_tile", name="Roof", parent=g))
    t.c("scene.list_tree")
    if h:
        t.c("scene.get_node", target=h)
        t.c("scene.set_node_tags", target=h, tags=["house"])
        t.c("scene.move_node", target=h, offset=[0, 0, 0])
        t.c("scene.rotate_node", target=g or h, delta=[0, 15, 0])
    if g:
        t.c("scene.save_template", target=g, name="proof_house", description="a small house")
        t.c("scene.list_templates")
        t.c("scene.place_template", name="proof_house", location=[900, 0, 0], new_name="House2")
        d = ref(t.c("scene.duplicate_node", target=g, offset=[-900, 0, 0], name="House3"))
        if d:
            t.c("scene.rename_node", target=d, name="HouseCopy")
            t.c("scene.scale_node", target=d, factor=[0.8, 0.8, 0.8])
    t.keep("find", len((t.c("scene.find_nodes", tag="house") or {}).get("nodes", [])))
    tree = t.c("scene.export_tree", root=g) if g else None
    if tree:
        t.c("scene.import_tree", tree=tree.get("tree", tree), parent=None) if False else None
    t.c("scene.delete_template", name="proof_house")
    t.shot((-200, -2200, 900), (0, 0, 200))


def t_layout(t):
    fresh(t)
    ids = [ref(t.c("build.create_primitive", shape="Box", size=[120, 120, 120], location=[i * 37, i * 53, 300], material="painted_iron",
                   color=[0.2 + i * 0.07, 0.4, 0.9 - i * 0.07], name=f"Crate{i}")) for i in range(10)]
    ids = [i for i in ids if i]
    t.c("layout.arrange_in_grid", targets=ids[:6], columns=3, gap=80, origin=[-800, -400, 0])
    t.c("layout.arrange_in_row", targets=ids[6:], gap=60, start=[-800, 500, 0])
    t.c("layout.drop_to_ground", targets=ids)
    t.c("layout.align_nodes", targets=ids[6:], axis="Y", mode="center")
    t.c("layout.distribute_nodes", targets=ids[6:], axis="X")
    t.c("layout.snap_to_grid", targets=ids, size=50)
    ring = [ref(t.c("build.create_primitive", shape="Cylinder", size=[80, 80, 200], location=[0, 0, 100], material="stone", name=f"Pillar{i}")) for i in range(8)]
    ring = [i for i in ring if i]
    t.c("layout.arrange_in_circle", targets=ring, center=[900, 0, 0], radius=500, facing="Center")
    t.c("layout.drop_to_ground", targets=ring)
    t.c("layout.randomize_transform", targets=ids[:6], yaw_jitter=20, seed=2)
    t.keep("free_space", t.c("layout.find_free_space", size=[300, 300, 300], near=[0, 0, 0]))
    t.shot((0, -2600, 1800), (0, 0, 0))


def t_lighting(t):
    fresh(t)
    for i, s in enumerate(("Sphere", "Box", "Cylinder")):
        t.c("build.create_primitive", shape=s, size=[300, 300, 300], location=[i * 500 - 500, 0, 150], material="plaster", name=s)
    t.keep("env", t.c("lighting.get_environment"))
    for mood in ("Morning", "Sunset", "Night"):
        t.c("lighting.apply_mood", mood=mood)
        t.shot((0, -1800, 500), (0, 0, 150), suffix="_" + mood.lower(), wait=8)
    t.c("lighting.apply_mood", mood="Noon")
    t.c("lighting.set_sun", intensity=8, temperature=5200)
    t.c("lighting.set_sky", intensity=1.2)
    t.c("lighting.set_fog", density=0.03, color=[0.7, 0.75, 0.85])
    t.c("lighting.set_clouds", coverage=0.6)
    t.c("lighting.set_atmosphere", rayleigh_scattering_scale=0.04)
    t.c("lighting.set_post_process", saturation=1.1, vignette=0.4, bloom=0.8)
    t.c("lighting.set_time_of_day", hour=17.5)
    t.shot((0, -1800, 500), (0, 0, 150), wait=4)
    t.c("lighting.hide_environment", hidden=False)


def t_camera(t):
    fresh(t)
    a = ref(t.c("build.create_primitive", shape="Torus", size=[400, 400, 120], location=[0, 0, 200], material="steel", color=[0.9, 0.7, 0.2], name="Ring"))
    t.c("camera.set_camera", location=[-1000, -1000, 600], look_at=[0, 0, 200], fov=60)
    t.keep("camera", t.c("camera.get_camera"))
    t.c("camera.set_fov", fov=50)
    if a:
        t.c("camera.frame_nodes", targets=[a], padding=1.3)
        t.c("camera.orbit", target=a, yaw=45, pitch=-20, distance=1200)
    t.c("camera.set_fly_speed", speed=1200)
    t.c("camera.set_fly_mode", enabled=False)
    t.c("camera.set_view_mode", mode="Wireframe")
    time.sleep(2)
    uk.call("capture.screenshot", format="Png", max_dim=1100)
    uk.save_image(os.path.join(OUT, "camera_wireframe.png"))
    t.c("camera.set_view_mode", mode="Lit")
    t.shot((-900, -900, 600), (0, 0, 200))
    t.c("camera.reset_camera")


def t_capture(t):
    fresh(t)
    t.c("build.create_primitive", shape="Dome", size=[600, 600, 300], location=[0, 0, 0], material="glass", name="Dome")
    r = t.c("capture.scene_capture", location=[-900, -900, 500], look_at=[0, 0, 100], width=640, height=360, format="Png")
    if r is not None:
        uk.save_image(os.path.join(OUT, "capture_scene_capture.png"))
    t.c("camera.set_camera", location=[0, -1200, 400], look_at=[0, 0, 100])
    time.sleep(2)
    if t.c("capture.screenshot", format="Png", max_dim=900) is not None:
        uk.save_image(os.path.join(OUT, "capture.png"))


def t_materials(t):
    fresh(t)
    m = ref(t.c("build.create_primitive", shape="Sphere", size=[300, 300, 300], location=[-400, 0, 150], material="white", name="MatBall"))
    n = ref(t.c("actors.spawn_actor", shape="cube", location=[400, 0, 150], name="MatCube"))
    if m:
        ps = t.c("materials.list_parameters", target=m)
        t.keep("params", [p.get("name") for p in (ps or {}).get("parameters", [])][:20])
        t.c("materials.set_vector", target=m, name="Color", value=[0.1, 0.8, 0.3, 1])
        t.c("materials.set_scalar", target=m, name="Roughness", value=0.15)
        t.c("materials.get_parameter", target=m, name="Roughness")
    if n:
        t.c("materials.set_material", target=n, material="/RuntimeToolsets/Materials/M_RT_PBR.M_RT_PBR")
        t.c("materials.set_vector", target=n, name="Color", value=[0.9, 0.2, 0.6, 1])
    g = ref(t.c("build.create_terrain", size=[1500, 1500], height=40, location=[0, 800, 10], biome="temperate", name="Patch"))
    if g:
        t.c("materials.set_texture", target=g, name="RockTex", texture="/RuntimeToolsets/Textures/T_RT_Granite.T_RT_Granite")
    t.c("materials.list_collection_parameters", collection="/RuntimeToolsets/Materials/MPC_RT_Style.MPC_RT_Style")
    t.shot((0, -1300, 400), (0, 0, 150))


def t_style(t):
    fresh(t)
    for i, s in enumerate(("Box", "Sphere", "Cone")):
        t.c("build.create_primitive", shape=s, size=[300, 300, 300], location=[i * 450 - 450, 0, 150], material="painted_iron",
            color=[[0.9, 0.3, 0.2], [0.2, 0.6, 0.9], [0.9, 0.8, 0.2]][i], name=s)
    pre = t.c("style.list_presets")
    t.keep("presets", pre)
    t.c("style.define_preset", name="proof_style", based_on="toon", description="proof")
    for p in ("toon", "lowpoly", "noir", "stylized", "retro"):
        if t.c("style.apply", preset=p) is not None:
            t.shot((0, -1600, 500), (0, 0, 150), suffix="_" + p, wait=6)
    t.keep("current", t.c("style.get"))
    t.c("style.apply", preset="realistic")


def t_vfx(t):
    fresh(t, hour=21)
    found = t.c("level.find_assets", asset_class="NiagaraSystem", folder_path="/Niagara", recursive=True, limit=200) or {}
    systems = [a["path"] for a in found.get("assets", [])]
    t.keep("systems", len(systems))
    pick = [s for s in systems if any(k in s.lower() for k in ("fountain", "burst", "fire", "sprite", "simple"))][:3] or systems[:3]
    ids = []
    for i, s in enumerate(pick):
        ids.append(ref(t.c("vfx.spawn_system", system=s, location=[i * 500 - 500, 0, 100], name=f"FX{i}")))
    ids = [i for i in ids if i]
    if pick:
        t.c("vfx.spawn_one_shot", system=pick[0], location=[0, 400, 100])
    t.keep("active", t.c("vfx.list_active"))
    if ids:
        t.c("vfx.get_summary", target=ids[0])
        uv = t.c("vfx.get_user_variables", target=ids[0])
        t.c("vfx.deactivate", target=ids[0])
        t.c("vfx.activate", target=ids[0], reset=True)
    time.sleep(1)
    t.shot((0, -1500, 500), (0, 0, 150), wait=2)


def t_ui(t):
    fresh(t)
    t.c("ui.create_hud_text", text="Toolset proof: UI", position=[0.04, 0.05], size=36, color=[1, 0.9, 0.3], name="Title")
    cube = ref(t.c("build.create_primitive", shape="Box", size=[200, 200, 200], location=[0, 0, 100], material="painted_iron", color=[0.3, 0.5, 0.9], name="Crate"))
    lab = ref(t.c("ui.create_label", text="Crate (label)", target=cube, offset=[0, 0, 180], name="CrateLabel"))
    if lab:
        t.c("ui.set_label", target=lab, text="Crate: 3 items", color=[0.2, 1, 0.4])
    ws = t.c("ui.list_widgets")
    t.keep("widgets", ws)
    t.c("ui.find_widget", name="Title")
    t.c("ui.show_activity", enabled=True, status="testing ui")
    t.shot((0, -900, 300), (0, 0, 120), show_ui=True)
    t.c("ui.show_activity", enabled=False)


def t_spline(t):
    fresh(t)
    s = t.c("spline.create_spline", points=[[-2000, -1000, 0], [-500, 800, 0], [800, -600, 0], [2000, 900, 0]], name="Path", profile={"kind": "ribbon", "width": 400}) or {}
    sid = ref(s.get("spline") if isinstance(s, dict) else None) or ref(s)
    if sid:
        smp = t.c("spline.sample_spline", spline=sid, spacing=400)
        pts = (smp or {}).get("points", [])
        t.keep("samples", len(pts))
        for p in pts[::2]:
            loc = p.get("location", p) if isinstance(p, dict) else p
            t.c("build.create_primitive", shape="Cone", size=[80, 80, 160], location=[loc[0], loc[1], 80], material="painted_iron", color=[1, 0.5, 0], name="Marker")
        if t.c("pcg.scatter", spline=sid, items=[{"template": "Marker", "weight": 1}], mode="Along", spacing=300, offset=[400, 600]) is None:
            pass
    t.shot((0, -3500, 2500), (0, 0, 0))


def t_pcg(t):
    fresh(t)
    sp = t.c("spline.create_spline", points=[[-2000, -2000, 0], [2000, -2000, 0], [2000, 2000, 0], [-2000, 2000, 0]], closed=True, name="Area") or {}
    sid = ref(sp.get("spline") if isinstance(sp, dict) else None) or ref(sp)
    if sid:
        t.keep("scatter", t.c("pcg.scatter", spline=sid, mode="Inside", spacing=500, seed=4,
                               items=[{"asset": "/Game/RuntimeFoliage/Trees/SM_Birch_01", "weight": 2, "scale": [0.8, 1.2]},
                                      {"asset": "/Game/RuntimeFoliage/Trees/SM_Pine_01", "weight": 1, "scale": [0.8, 1.1]}]))
    t.keep("graphs", t.c("pcg.list_graphs"))
    t.c("pcg.list_native_nodes", common_only=True)
    g = t.c("pcg.create_graph", name="ProofGraph")
    gp = ref(g, "graph", "path") if g else None
    if gp:
        t.c("pcg.add_node", graph=gp, native_node_type="SurfaceSampler", node_name="Sampler")
        t.c("pcg.get_graph_structure", graph=gp)
    t.c("pcg.list_graph_instances")
    t.shot((0, -4200, 2600), (0, 0, 0))


def t_physics(t):
    fresh(t)
    ids = []
    for i in range(6):
        ids.append(ref(t.c("build.create_primitive", shape="Box", size=[100, 100, 100], location=[0, 0, 100 + i * 105], material="wood", name=f"Block{i}")))
    ball = ref(t.c("build.create_primitive", shape="Sphere", size=[120, 120, 120], location=[-800, 0, 160], material="steel", name="Ball"))
    for i in ids + [ball]:
        if i:
            t.c("physics.set_simulate", target=i, enabled=True)
    if ball:
        t.c("physics.set_body", target=ball, mass_kg=80, restitution=0.4)
        t.c("physics.add_impulse", target=ball, impulse=[3000, 0, 200], velocity_change=True)
    t.keep("trace", t.c("physics.line_trace", start=[0, 0, 2000], end=[0, 0, -100]))
    t.keep("overlap", t.c("physics.overlap_test", center=[0, 0, 200], radius=300))
    t.c("physics.get_collision_channels")
    time.sleep(1.5)
    if ball:
        t.keep("ball", t.c("physics.get_physics", target=ball))
    t.shot((300, -1400, 400), (0, 0, 250), wait=1)


def t_character(t):
    fresh(t)
    t.c("build.create_primitive", shape="Steps", size=[400, 600, 300], location=[600, 0, 150], material="stone", name="Stairs")
    r = t.c("character.create", location=[0, 0, 100], yaw=0,
            controls=[{"key": "D", "action": "move", "direction": [1, 0, 0]}, {"key": "A", "action": "move", "direction": [-1, 0, 0]},
                      {"key": "SpaceBar", "action": "jump"}],
            camera={"mode": "follow", "offset": [0, -1400, 300], "lock_axes": ["z"], "location": [0, 0, 200]})
    t.keep("character", r)
    t.c("input.inject_key", key="D", event="Press", duration=0.6)
    time.sleep(1.0)
    wall = ref(t.c("build.create_primitive", shape="Box", size=[50, 400, 600], location=[-500, 0, 300], material="brick", name="ClimbWall"))
    if wall:
        t.c("character.set_climbable", targets=[wall], climbable=True)
    time.sleep(1)
    if t.c("capture.screenshot", format="Png", max_dim=1100) is not None:
        uk.save_image(os.path.join(OUT, "character.png"))


def t_game(t):
    t.keep("state", t.c("game.get_game_state"))
    pl = t.c("game.list_players")
    t.keep("players", pl)
    t.c("game.set_time_dilation", value=0.5)
    t.c("game.set_time_dilation", value=1.0)
    t.c("game.set_pause", paused=True)
    t.c("game.set_pause", paused=False)
    t.c("game.teleport", location=[100, 0, 300], snap_to_ground=True)


def t_input(t):
    t.c("input.inject_key", key="SpaceBar", event="Tap")
    t.c("input.inject_key", key="W", event="Press", duration=0.3)
    ctx = t.c("level.find_assets", asset_class="InputMappingContext", limit=5) or {}
    ac = t.c("level.find_assets", asset_class="InputAction", limit=5) or {}
    cs, acs = [a["path"] for a in ctx.get("assets", [])], [a["path"] for a in ac.get("assets", [])]
    t.keep("contexts", cs)
    if cs:
        t.c("input.add_mapping_context", context=cs[0], priority=1)
        t.c("input.remove_mapping_context", context=cs[0])
    if acs:
        t.c("input.inject_action", action=acs[0], duration=0.2)


def t_console(t):
    t.keep("cvar", t.c("console.get_cvar", name="r.ScreenPercentage"))
    t.c("console.set_cvar", name="r.ScreenPercentage", value="100")
    t.keep("list", t.c("console.list_cvars", prefix="r.Shadow", limit=5))
    t.c("console.exec_command", command="stat unit")
    t.c("console.exec_command", command="stat none")
    t.keep("settings", t.c("console.get_user_settings"))
    t.c("console.set_scalability", overall=3)


def t_log(t):
    t.c("log.subscribe_log_category", category="LogTemp", enabled=True)
    t.keep("tail", t.c("log.tail_log", limit=5))
    t.keep("stats", t.c("log.get_stat", names=["fps", "frame_ms"]) )


def t_profiling(t):
    t.keep("timing", t.c("profiling.frame_timing", seconds=1.0))
    t.keep("gpu", t.c("profiling.gpu_stats"))
    t.keep("memory", t.c("profiling.memory_report", top_classes=5))
    t.c("profiling.find_hitches", seconds=1.0, hitch_ms=50)
    if t.c("profiling.start_trace", channels="cpu,frame") is not None:
        time.sleep(1)
        t.c("profiling.stop_trace")


def t_reflection(t):
    fresh(t)
    s = ref(t.c("actors.spawn_actor", **{"class": "/Script/Engine.PointLight"}, location=[0, 0, 300], name="ReflLamp"))
    if s:
        t.keep("props", len((t.c("reflection.list_properties", target=s, limit=40) or {}).get("properties", [])))
        t.c("reflection.get_property", target=s, path="PointLightComponent.Intensity")
        t.c("reflection.set_property", target=s, path="PointLightComponent.Intensity", value=12345)
        t.keep("after", t.c("reflection.get_property", target=s, path="PointLightComponent.Intensity"))
        t.c("reflection.set_properties", target=s, values={"PointLightComponent.AttenuationRadius": 2000})
        t.c("reflection.get_properties", target=s, properties=["PointLightComponent.AttenuationRadius"])
        t.c("reflection.list_functions", target=s, limit=10)
        t.c("reflection.call_function", target=s, function="K2_GetActorLocation")
        t.c("reflection.reset_properties", target=s, properties=["PointLightComponent.AttenuationRadius"])
    t.c("reflection.find_class", pattern="PointLight", limit=5)
    t.c("reflection.describe_enum", enum="ECollisionChannel")
    t.c("reflection.get_subsystem", kind="World", **{"class": "WorldPartitionSubsystem"})


def t_history(t):
    fresh(t)
    t.c("history.checkpoint", name="before")
    t.c("history.begin_transaction", label="three boxes")
    for i in range(3):
        t.c("build.create_primitive", shape="Box", size=[100, 100, 100], location=[i * 200, 0, 50], name=f"H{i}")
    t.c("history.end_transaction", commit=True)
    n1 = len((t.c("scene.find_nodes", pattern="H*") or {}).get("nodes", []))
    t.c("history.undo")
    n2 = len((t.c("scene.find_nodes", pattern="H*") or {}).get("nodes", []))
    t.c("history.redo")
    n3 = len((t.c("scene.find_nodes", pattern="H*") or {}).get("nodes", []))
    t.c("history.rollback", name="before")
    n4 = len((t.c("scene.find_nodes", pattern="H*") or {}).get("nodes", []))
    t.keep("counts_made_undo_redo_rollback", [n1, n2, n3, n4])
    t.keep("history", t.c("history.get_history", limit=5))
    t.c("history.set_history_limit", limit=200)


def t_tags(t):
    fresh(t)
    a = ref(t.c("actors.spawn_actor", shape="cube", location=[0, 0, 100], name="Tagged"))
    lst = t.c("tags.list_tags")
    t.keep("list", lst)
    if a:
        tags = lst if isinstance(lst, list) else (lst or {}).get("tags", [])
        tag = (tags[0].get("tag") if tags and isinstance(tags[0], dict) else (tags[0] if tags else None))
        if tag:
            t.c("tags.add_actor_tags", actor=a, tags=[tag])
            t.c("tags.get_actor_tags", actor=a)
            t.c("tags.match_tags", actor=a, any=[tag])
            t.c("tags.find_actors_with_tags", any=[tag])
            t.c("tags.get_tag_info", tag_name=tag)
            t.c("tags.remove_actor_tags", actor=a, tags=[tag])


def t_level(t):
    t.keep("current", t.c("level.get_current_level"))
    t.keep("levels", t.c("level.list_levels"))
    t.keep("assets", (t.c("level.find_assets", asset_class="StaticMesh", folder_path="/Game/RuntimeFoliage", limit=5) or {}).get("assets"))
    li = t.c("level.load_level_instance", map="/Game/Maps/Sandbox", location=[0, 0, -20000], name="SandboxCopy")
    t.keep("instance", li)
    lv = t.c("level.list_levels")
    if li:
        t.c("level.set_level_visibility", level="SandboxCopy", visible=False)
        t.c("level.unload_level", level="SandboxCopy")


def t_skills(t):
    t.keep("list", t.c("skills.list_skills"))
    t.c("skills.get_skills", names=["builder"])
    t.c("skills.save_skill", name="proof_skill", description="test skill", instructions="# Proof\nUse build tools.", toolsets=["build"])
    t.c("skills.get_skills", names=["proof_skill"])
    t.c("skills.delete_skill", name="proof_skill")


def t_ops(t):
    t.keep("batch", t.c("ops.run_batch", calls=[{"tool": "session.ping", "arguments": {}}, {"tool": "game.get_game_state", "arguments": {}}]))
    t.c("ops.wait", seconds=0.2)
    t.c("ops.wait_for_condition", tool="session.ping", timeout_s=3)
    t.keep("recipes", t.c("ops.list_recipes"))
    t.keep("tests", t.c("ops.list_tests", filter="RuntimeToolsets.Core", limit=5))


def t_gis(t):
    fresh(t)
    t.c("gis.set_origin", latitude=25.2048, longitude=55.2708)
    t.keep("origin", t.c("gis.get_origin"))
    t.c("gis.create_plots", layout="Blocks", size=[30000, 24000], seed=3)
    plots = (t.c("gis.list_plots", limit=50) or {}).get("plots", [])
    t.keep("plots", len(plots))
    if plots:
        pid = plots[0].get("id")
        t.c("gis.get_plot", id=pid)
        t.c("gis.find_plot", location=[0, 0, 0])
        t.c("gis.set_plot", id=pid, zoning="green")
        t.c("gis.subdivide_plot", id=pid, parts=2)
    t.c("gis.show_plots", labels=True, streets=True)
    t.c("gis.export_geo_json", limit=3)
    t.shot((0, -26000, 26000), (0, 0, 0))


def t_ai(t):
    fresh(t)
    t.c("build.create_primitive", shape="Box", size=[600, 100, 300], location=[0, 0, 150], material="concrete", name="Obstacle")
    nav = t.c("ai.build_navigation", bounds={"min": [-4000, -4000, -200], "max": [4000, 4000, 600]})
    t.keep("nav", nav)
    t.keep("status", t.c("ai.get_navigation_status"))
    t.keep("path", t.c("ai.find_path", start=[-1500, 0, 50], end=[1500, 0, 50]))
    t.c("ai.project_to_navigation", location=[200, 300, 400])
    bot = ref(t.c("actors.spawn_actor", shape="cylinder", location=[-1500, 0, 100], color=[0.9, 0.2, 0.2], name="Bot"))
    if bot:
        ag = t.c("ai.create_agent", target=bot, settings={"max_speed": 400})
        agent = ref(ag, "agent") or bot
        t.c("ai.list_agents")
        t.c("ai.move_to", agent=agent, location=[1500, 300, 50])
        time.sleep(2)
        t.c("ai.get_brain_state", agent=agent)
        t.c("ai.get_perception", agent=agent)
        t.c("ai.report_noise", location=[0, 600, 50], loudness=1)
        trees = (t.c("ai.list_behavior_trees") or {}).get("trees", [])
        t.keep("trees", trees[:3])
        t.c("ai.stop_movement", agent=agent)
    t.shot((0, -2600, 1500), (0, 0, 0), wait=1)


def t_gas(t):
    fresh(t)
    a = ref(t.c("actors.spawn_actor", shape="cylinder", location=[0, 0, 100], name="Hero"))
    if a:
        t.c("gas.add_ability_system", target=a, attributes={"Health": 200, "MaxHealth": 200})
        t.c("gas.define_effect", name="Burn", duration_policy="HasDuration", duration=5, period=1,
            modifiers=[{"attribute": "Health", "op": "add", "magnitude": -10}], granted_tags=["Status.Burning"] if False else None)
        t.c("gas.apply_effect", target=a, effect="Burn")
        time.sleep(1.2)
        t.keep("attrs", t.c("gas.get_attribute_values", target=a))
        t.keep("effects", t.c("gas.get_active_effects", target=a))
        t.c("gas.set_attribute_base", target=a, attribute="Shield", value=50)
        t.c("gas.get_active_tags", target=a)
        t.c("gas.remove_effect", target=a, effect="Burn")
        t.c("gas.get_granted_abilities", target=a)
    t.c("gas.list_effects")
    t.c("gas.list_ability_classes")
    t.c("gas.find_attribute_set_classes")
    t.c("gas.list_cues")


def t_statetree(t):
    lst = t.c("statetree.list_state_trees")
    t.keep("trees", lst)
    trees = (lst or {}).get("state_trees", (lst or {}).get("trees", [])) if isinstance(lst, dict) else []
    if trees:
        p = trees[0].get("path") if isinstance(trees[0], dict) else trees[0]
        t.c("statetree.describe_state_tree", asset=p)


def t_logic(t):
    fresh(t)
    t.c("logic.get_guide")
    t.c("logic.define_vars", vars={"score": 0, "coins": 3})
    coins = [ref(t.c("build.create_primitive", shape="Cylinder", size=[80, 80, 15], location=[i * 300 + 300, 0, 120], rotation=[90, 0, 0],
                     material="steel", color=[1, 0.8, 0.1], name=f"Coin{i}", tags=["coin"])) for i in range(3)]
    t.c("logic.tween", targets=["tag:coin"], rotate=[0, 360, 0], duration=2, loop="Loop")
    t.c("logic.define_ui", items=[{"name": "hud", "kind": "panel", "anchor": "top-left",
                                   "children": [{"kind": "text", "text": "Score {score}  Coins left {coins}", "weight": "bold"}]}])
    t.c("logic.define_rules", rules=[{"name": "collect", "on": "touch", "target": "tag:coin",
                                      "actions": [{"set": "score", "value": "score + 10"}, {"set": "coins", "value": "coins - 1"}, {"hide": "event.target"}]}])
    t.c("logic.spawn_player", location=[0, 0, 100])
    t.c("logic.start")
    t.c("logic.set_var", name="score", value=5)
    t.keep("eval", t.c("logic.eval", expr="score + coins"))
    t.keep("state", t.c("logic.get_state", log_limit=5))
    t.c("logic.list_rules")
    exp = t.c("logic.export")
    t.keep("export_size", len(json.dumps(exp)) if exp else 0)
    t.shot(None, None, wait=3, show_ui=True)   # the running game: player camera and the HUD
    t.c("logic.stop")
    t.c("logic.remove_player")


ALL = [("session", t_session), ("build", t_build), ("actors", t_actors), ("scene", t_scene), ("layout", t_layout), ("lighting", t_lighting),
       ("camera", t_camera), ("capture", t_capture), ("materials", t_materials), ("style", t_style), ("vfx", t_vfx), ("ui", t_ui),
       ("spline", t_spline), ("pcg", t_pcg), ("physics", t_physics), ("character", t_character), ("game", t_game), ("input", t_input),
       ("console", t_console), ("log", t_log), ("profiling", t_profiling), ("reflection", t_reflection), ("history", t_history), ("tags", t_tags),
       ("level", t_level), ("skills", t_skills), ("ops", t_ops), ("gis", t_gis), ("ai", t_ai), ("gas", t_gas), ("statetree", t_statetree),
       ("logic", t_logic)]

for name, fn in ALL:
    if ONLY and name not in ONLY:
        continue
    t = T(name)
    try:
        fn(t)
    except Exception:  # noqa: BLE001
        t.calls.append({"tool": "(harness)", "ok": False, "error": traceback.format_exc()[-400:]})
    ok = sum(c["ok"] for c in t.calls)
    RESULTS[name] = {"passed": ok, "total": len(t.calls), "failures": [c for c in t.calls if not c["ok"]]}
    with open(os.path.join(OUT, f"{name}.json"), "w", encoding="utf-8") as f:
        json.dump({"calls": t.calls, "proof": t.proof}, f, indent=1, default=str)
    print(f"{name:<11} {ok}/{len(t.calls)}", flush=True)
    with open(os.path.join(OUT, "results.json"), "w", encoding="utf-8") as f:
        json.dump(RESULTS, f, indent=1)
try:
    uk.call("camera.reset_camera")
except Exception:
    pass
