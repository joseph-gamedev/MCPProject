# Changelog

## 0.13.0 — 2026-10-22

- **Real Unreal gameplay systems** (Epic's AI/StateTree/GAS/GameplayTags toolsets only inspect editor data; these drive the systems at runtime):
  - `ai`: navmesh built at runtime over tool-made worlds (`build_navigation`), `find_path`, AI agents for any object (`create_agent`: an AI-controlled character carries it), `move_to`, AI perception (sight, hearing, `report_noise`, `make_perceivable`), behavior trees and blackboards.
  - `tags`: gameplay tags on any actor, queries, 64 starter tags (State, Team, Damage, Ability, Event, Cue, AI, Status, Noise).
  - `gas`: Gameplay Ability System on any actor (generic attribute set with damage meta-attribute), effects defined at runtime, abilities, gameplay events, cues, loose tags.
  - `statetree`: run cooked StateTrees on actors, send events, read active states, describe trees.
- **Logic on real AI:** `define_brain backend:"ai"` walks the navmesh and senses with AI perception; player shots are AI noise events; `has_tag(target, tag)` and `add_tags` / `remove_tags` actions. The stealth example uses it by default (`--native` for the old steering).
- **Builder collision:** simple shapes per part (physics and navigation), complex triangles for traces.
- **Skills:** ai_navigation_and_perception, gameplay_ability_system, state_tree.
- **Other:** `-RuntimeToolsetsPort=N` command-line override; project config needs `[/Script/NavigationSystem.RecastNavMesh] RuntimeGeneration=Dynamic`.
- **Tests:** 215 automation tests.

## 0.12.0 — 2026-10-22

- **Rewritten after Epic's editor toolsets** (`Engine/Plugins/Experimental/ToolsetRegistry` and `Toolsets/*`):
  - every toolset is a class whose static `UFUNCTION(meta=(AICallable))` functions are the tools;
  - input and output schemas and descriptions are generated from signatures and doc comments; results are `{"returnValue": ...}`;
  - docs are compiled into packaged builds; async results; pluggable struct converters.
- **Skills** (Epic's AgentSkill model): `skills.list_skills` / `skills.get_skills`; outdoor_lighting, vfx_placement, art_style, pcg_graph_generation, builder, game_logic; markdown skills in `Config/RuntimeToolsets/Skills`.
- **New toolsets:** `pcg` (graphs node by node like Epic's PCG toolset, instances, params, data view, scatter), `spline`, `style`.
- **Epic capabilities added:** batch property get/set/reset, describe_enum, actor search by bounds and channel, asset search, sky atmosphere and volumetric clouds, exposure modes, Niagara summaries and one-shots, widget-tree editing, collision channels, test filters and per-test results, batch result references, snap to ground.
- **Error handling:** candidates on NotFound, expected types on mismatches, a failure test for every tool.
- **Tests:** 203 automation tests; MCP smoke test passes.

## 0.11.0 — 2026-10-21

- **AI brains:**
  - data-driven state machines: move modes, facing, firing, icons, expression transitions;
  - vision cones (blocked by walls) drawn in-world, hearing gunshots, wall-avoiding steering;
  - `state` events.
- **Game HUD:**
  - `set_hud`: enemy health bars, alert icons, damage numbers, hit flash, low-health pulse;
  - UI `bar` kind with a ghost bar; rounded panels; text shadows; edge anchoring; crosshair cursor.
- **Feedback presets:** shake, flash, freeze, slow motion, text, tween, spawn and emit; per-weapon and per-health hooks. Time effects run in real time and slow the game step.
- **Hero animation:** the hero holds the weapon and aims with recoil.
- **Hit feel:** hit punch and enemy firing kick.
- **Rule and UI cleanup:**
  - `define_rules`/`define_ui` take `replace`;
  - `create_assembly` accepts opacity/emissive/variation defaults;
  - new `reloading` variable, and the player's magazine starts full.
- **Tests:** 142 automation tests.

## 0.10.0 — 2026-10-21

- **Combat in `logic`:**
  - top-down player mode (WASD, mouse/stick aim, overhead camera);
  - `define_health`: resistances, invulnerability, death handling, points;
  - `define_hitzone`: on-enter or interval damage, knockback;
  - `define_weapon`: projectile/hitscan, rate, auto, burst, magazine/reload/reserve ammo, spread (random/fan), knockback, hit filter, automatic bullet pool;
  - `equip` and `damage` tools; `fire`/`damage`/`heal` actions; `damaged`/`died`/`fired`/`reloaded` events;
  - the `health` and `ammo` expression variables, plus the world functions `pos`, `hp`, `alive`, `nodes` and `dist`.
- **`Scripts/examples/topdown_shooter.py`.**
- **Tests:** 140 automation tests.

## 0.9.0 — 2026-10-21

- **Platformer support in `logic`:**
  - `spawn_player`: a side-scroll character with run/sprint/variable-height jump, a follow camera, and procedural walk/jump/squash animation;
  - `touch` (from above, from below or from the side) and `fall` events;
  - `set_motion`: patrol, path, bob, spin, waddle;
  - `set_solid`;
  - `hide`/`show`/`player` actions and a `player` expression variable;
  - invulnerability after a hit, and a re-touch cooldown.
- **Tweens:** the `logic.tween` tool and `tween` action, with 16 easings, yoyo/loop/pingpong, delay, repeat, then hide/show/release, an end event, and colour tweens.
- **Actor pools:** `logic.define_pool` with `spawn`/`release` actions; when full, the oldest copy is recycled.
- **`restart` can keep variables** (`"keep":[...]`).
- **Fixes:**
  - touch direction is now readable as `event.data.from_above` (stomps and head bumps never fired before);
  - stomp detection uses the previous frame's feet height;
  - motion blur is off project-wide.
- **`Scripts/examples/platformer.py`.**
- **Tests:** 137 automation tests.

## 0.8.0 — 2026-10-21

- **New `logic` toolset** (module `RuntimeToolsetsLogic`): 17 tools to make a scene playable without code.
  - **Variables**, an expression language and **rules** (event → condition → actions).
  - **Events:** click, key, button, timer, var_changed, custom and tick.
  - **UI:** on-screen text and buttons; cursor mode and camera lock.
  - **Game control:** start/restart (objects created during play are cleaned up), simulated input, time stepping, state and event log, undo, and export/import.
- **`Scripts/examples/tictactoe.py`:** a complete tic-tac-toe built only from tool calls, with an optional computer opponent.
- **`Scripts/examples/city.py`:** a district of 64 buildings of 8 different types.
- **Tests:** 133 automation tests, including full tic-tac-toe games.

## 0.7.0 — 2026-10-21

Builder quality: ideas ported from the StreamPixelMCP analysis (no code copied).

- **`build.create_assembly`:** up to 512 parts per call, merged into one object with per-part materials and one undo step. Assemblies are cached between the validation build and the spawn build.
- **New part shapes:**
  - `loft` (rings with equal point counts);
  - `revolve` (`segments` and `start_angle`);
  - 3D `extrude` (an outline pushed along a `direction`);
  - `raw` triangles.
- **Coplanar repair:** where faces of later parts overlap earlier faces in the same plane and facing the same way, they are nudged 3 mm per layer, so touching parts don't flicker.
- **Procedural PBR material** (`M_RT_PBR`): world-projected brick, stone blocks, marble, plaster, concrete, roof tiles, planks, brushed metal and ground patterns, with derivative bump relief. New presets `roof_tile` and `cobblestone`. Per-part `style`, `tile` and `relief`.
- **`Scripts/rtbuild.py`** generator helpers and **`Scripts/examples/eiffel_and_pyramid.py`** (2,020 parts in 1.4 s).
- **`build.get_guide`** rewritten around generator scripts and true-scale detail.
- **Renderer** pinned to Lumen GI and reflections, virtual shadow maps, mesh distance fields and TSR.
- **Earlier in this version:**
  - fly camera with distance-based speed (`camera.set_fly_mode`, `camera.set_fly_speed`);
  - an optional activity panel (`ui.show_activity`);
  - the `set_post_process` volume reuse fix.
- **Tests:** 126 automation tests.

## 0.6.1 — 2026-10-21

- **New `ui.create_label` / `ui.set_label`** (ui 1.1.0): world-anchored name tags. They are child nodes of their target, so they follow it, and they undo, duplicate and export like any other node.
- **Prompt-driven run:** `Scripts/prompt_demo.ps1` gives a plain-English request to a separate Claude Code session connected only to the game's MCP server. The Eiffel Tower + Great Pyramid example ran in 90 s.
- **Tests:** 122 automation tests.

## 0.6.0 — 2026-10-21

Phase 3, primitive builder.

- **New toolset `build` 1.0.0** (10 tools) in `RuntimeToolsetsContent`, built on Geometry Script dynamic meshes:
  - Recipe-based primitives and profile solids.
  - Booleans, radial/linear/grid arrays, mirror.
  - Palette materials.
  - measure, check_overlaps, get_build_stats.
  - An ortho contact-sheet `capture_view`.
- **Core:**
  - Source-spawner registry (`RegisterSourceSpawner`, `SpawnFromSource`, `HasSourceSpawner`).
  - Shared image encoding (`RuntimeImage.h`).
  - `RuntimeActorUtils::CopyMaterials`.
- **Fixes:**
  - VFX nodes can now be duplicated and exported/imported.
  - Bounds (`ActorBox`, node `bounds`) include visible attached children, so framing a group works.
- **Tests:** 121 automation tests, passing in the editor and in the packaged Development build.
- **New script:** `Scripts/build_landmark_demo.py`.

## 0.5.0 — 2026-10-21

Phase 2, ops tools. This completes Phase 2.

- **New module:** `RuntimeToolsetsOps`.
- **New toolsets:**
  - `profiling` 1.0.0 (6 tools): frame timing, hitches, memory, GPU, and Insights traces.
  - `ops` 1.0.0 (6 tools): `run_batch` (optionally atomic), `wait_for_condition`, `wait`, and in-process automation tests.
- **Core:**
  - `FRuntimeTestRunner` moved into Core and now reports status.
  - Added `FToolInvocation::SetTimeout`.
  - `call_tool` allows long-running nested tools.
- **Fixes:**
  - The test runner reports only errors in failure details.
  - Trace files are never overwritten.
- **Tests:** 112 automation tests, passing in the editor and in the packaged Development build.
- **Smoke test:** now covers waiting for a falling cube, an atomic batch with undo, frame timing, memory, and an in-game test run over MCP.

## 0.4.0 — 2026-10-21

Phase 2, presentation tools.

- **New toolsets in `RuntimeToolsetsPresentation`:**
  - `ui` 1.0.0 (10 tools): asset-free `create_hud_text`, and `widget:Name` targets.
  - `vfx` 1.0.0 (6 tools): Niagara systems spawned as scene nodes.
  - `lighting` 1.0.0 (7 tools): sun, sky, fog, post process, time of day, and `apply_mood` presets.
- **Test harness:** `SetupLiveWorld()` runs tests against the real game world. It rolls back afterwards, and the UI specs use it.
- **Cooking:** `/Niagara/DefaultAssets` is always cooked in the host project.
- **Tests:** 102 automation tests, passing in the editor and in the packaged Development build.
- **Smoke test:** now covers a sunset mood, a VFX spawn, HUD text, and a screenshot that includes the UI.

## 0.3.0 — 2026-10-21

Phase 2, game runtime tools.

- **New module:** `RuntimeToolsetsGameplay`. It depends on Core plus the EnhancedInput plugin.
- **New toolsets:**
  - `game` 1.0.0 (5 tools)
  - `physics` 1.0.0 (6 tools)
  - `level` 1.0.0 (6 tools)
  - `input` 1.0.0 (4 tools)
- **Async tools:** `load_level_instance`, `inject_key` (tap/hold) and `inject_action` (hold) complete asynchronously. They also work when called through `session.call_tool`.
- **Tests:** 94 automation tests, passing in the editor and in the packaged Development build.
- **Smoke test:** now covers physics, input injection and async level streaming.

## 0.2.0 — 2026-10-21

Phase 2, builder tools.

- **New modules:** `RuntimeToolsetsPresentation` and `RuntimeToolsetsContent`. Both depend only on Core.
- **New toolsets:**
  - `materials` 1.0.0 (6 tools)
  - `camera` 1.0.0 (8 tools)
  - `layout` 1.0.0 (9 tools)
- **Undo:** every mutating tool is undoable. Layout operations are a single undo step.
- **Core:** shared helpers moved to the public header `RuntimeToolHelpers.h`.
- **Tests:** 82 automation tests, passing in the editor and in the packaged Development build.
- **Smoke test:** now covers layout, materials and camera framing.

## 0.1.0 — 2026-10-20

Phase 1.

- **Core:**
  - Plain-JSON API (`ListToolsets`, `DescribeToolset`, `Invoke`, `ExecuteNested`, `OnEvent`).
  - Game-thread dispatcher with an MPSC queue, a frame budget, async tools, timeouts and cancellation.
  - Schema builder and validator.
  - Per-session command history with transactions, checkpoints and irreversible markers.
  - Per-world scene graph and target resolver.
  - Log capture, settings and a usage report.
- **Toolsets, all at 1.0.0:**
  - session (8 tools)
  - actors (7)
  - scene (17)
  - reflection (7)
  - console (5)
  - log (3)
  - capture (3)
  - history (8)
- **Tests:** 64 automation tests, including schema snapshots, plus the `RuntimeToolsets.RunTests` runner for packaged builds.
- **Optional `RuntimeToolsetsMCP` adapter:**
  - Exposes the tools through Epic's ModelContextProtocol server on `127.0.0.1:8018/mcp`.
  - `Meta` exposure mode (default) or `Eager`.
