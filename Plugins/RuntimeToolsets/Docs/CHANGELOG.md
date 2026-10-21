# Changelog

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
