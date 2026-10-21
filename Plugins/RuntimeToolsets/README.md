# RuntimeToolsets

Runtime-safe toolsets that let AI agents inspect and build inside a running Unreal Engine 5.8 game, including a packaged Development build. Epic's 5.8 MCP toolsets are editor-only; this plugin works without the editor.

The plugin only provides tools. A connector (yours, or the optional `RuntimeToolsetsMCP` adapter) owns transport, authentication and sessions.

## Toolsets

| Toolset | Tools |
|---|---|
| `session` | ping, get_build_info, get_capabilities, list_toolsets, describe_toolset, call_tool, get_usage_report, end_session |
| `actors` | spawn_actor, destroy_actor, find_actors, set_transform, attach, set_visibility, list_components |
| `scene` | create_group, list_tree, find_nodes, get_node, move_node, rotate_node, scale_node, reparent_node, set_pivot, rename_node, set_node_tags, duplicate_node, delete_node, clear_scope, clear_all, export_tree, import_tree |
| `reflection` | list_properties, get_property, set_property, call_function, list_functions, find_class, get_subsystem |
| `console` | exec_command, get_cvar, set_cvar, set_scalability, get_user_settings |
| `log` | tail_log, subscribe_log_category, get_stat |
| `capture` | screenshot (async), capture_render_target, scene_capture |
| `history` | undo, redo, get_history, begin_transaction, end_transaction, checkpoint, rollback, set_history_limit |
| `materials` | list_parameters, set_material, set_scalar, set_vector, set_texture, set_collection_param |
| `camera` | get_camera, set_camera, set_fov, frame_nodes, orbit, reset_camera, possess, set_view_mode |
| `layout` | align_nodes, distribute_nodes, snap_to_grid, drop_to_ground, arrange_in_row, arrange_in_grid, arrange_in_circle, randomize_transform, find_free_space |

| `ui` | list_widgets, list_widget_tree, find_widget, create_widget, create_hud_text, remove_widget, set_text, set_widget_visibility, set_brush, click_button |
| `vfx` | spawn_system, list_user_parameters, set_user_variable, activate, deactivate, list_active |
| `lighting` | get_environment, set_sun, set_sky, set_fog, set_post_process, set_time_of_day, apply_mood |
| `profiling` | frame_timing, find_hitches, memory_report, gpu_stats, start_trace, stop_trace |
| `ops` | run_batch, wait_for_condition, wait, list_tests, run_automation_test, get_test_status |
| `game` | get_game_state, list_players, teleport, set_pause, set_time_dilation |
| `physics` | line_trace, overlap_test, get_physics, set_simulate, add_impulse, ragdoll |
| `level` | list_levels, load_level_instance, unload_level, set_level_visibility, set_data_layer, open_level |
| `input` | inject_key, inject_action, add_mapping_context, remove_mapping_context |

`game`, `physics`, `level` and `input` live in `RuntimeToolsetsGameplay` (needs the EnhancedInput plugin). `load_level_instance` waits asynchronously until the level is visible (or pass `blocking:true`). `open_level` needs `confirm:true` because it destroys the world and all undo history. `inject_key` reports `handled:false` when nothing in the game consumed the key.

`profiling` and `ops` live in `RuntimeToolsetsOps`.
- **`ops.run_batch`** runs up to 100 calls in one request. With `atomic:true` it is all-or-nothing: it rolls back on the first failure, and one undo reverts a successful batch.
- **`ops.wait_for_condition`** polls any tool until a field of its result matches, e.g. `{"tool":"scene.get_node","arguments":{"target":"n5"},"path":"location[2]","op":"lt","value":60}`.
- **`ops.run_automation_test`** starts tests in the running game. Poll `ops.get_test_status` for results.
- **`profiling.start_trace`** writes Unreal Insights traces to the `Saved/Profiling` folder and never overwrites existing files.

`ui`, `vfx` and `lighting` are also in `RuntimeToolsetsPresentation` (needs UMG and the Niagara plugin). Widgets are addressed as `widget:Name` or by the path `ui.find_widget` returns; `create_hud_text` needs no widget asset. VFX nodes cannot be duplicated or exported yet (their source spec is `vfx:<system>`). `apply_mood` changes sun angle/temperature, fog and global grading in one undo step; it leaves light intensities alone.

`materials` and `camera` live in the `RuntimeToolsetsPresentation` module; `layout` lives in `RuntimeToolsetsContent`. Debug view modes (unlit, wireframe…) are only available in cooked builds cooked with `r.ForceDebugViewModes=1`; otherwise `set_view_mode` returns `unsupported` and only `lit` works.

Each toolset's exact names and schemas are in `Tests/Schemas/<toolset>.json`, which is guarded by a snapshot test.

## Using it from C++ (connector API)

```cpp
#include "RuntimeToolsetsCore.h"

IRuntimeToolsetsCore& Core = IRuntimeToolsetsCore::Get();
FString Catalogue = Core.ListToolsets();                 // compact JSON
FString Actors    = Core.DescribeToolset(TEXT("actors")); // tools + input schemas

FToolCallerContext Caller;
Caller.SessionId = TEXT("user-42");                      // own undo history per session
Core.Invoke(TEXT("actors.spawn_actor"), TEXT(R"({"shape":"cube","location":[0,0,100]})"), Caller,
    FOnToolComplete::CreateLambda([](const FToolResult& R) { /* R.ToJsonString(), R.Attachments */ }));
```

- `Invoke` can be called from any thread. Calls run on the game thread one at a time, in order. Callbacks fire on the game thread.
- Arguments and results are plain JSON. Results look like `{"ok":true,...delta}` or `{"ok":false,"error":{"code","message","hint"}}`.
- Images (`capture.*`) are returned in `FToolResult::Attachments`.
- `OnEvent()` pushes log-subscription lines to connectors.
- Tool names are `toolset.tool`; `toolset_tool` is also accepted.

## Target syntax

Tools that act on something take a `target`:
- **Scene nodes:** `n12` (node id), `house/door` (node path).
- **Actors:** `actor:PointLight_0` (actor name).
- **Objects:** a full path such as `/Game/Map.Map:PersistentLevel.X`.
- **Static functions:** `class:/Script/Engine.KismetSystemLibrary` (the class default object).
- **Subsystems:** `subsystem:world/MySubsystem`.
- **Shortcuts:** `pawn`, `player_controller`, `game_mode`, `game_state`, `world_settings`, `game_user_settings`.
- **Components:** append `#ComponentName` to any of the above.

## Rules

- Mutating tools are undoable by default and history is kept per session. Irreversible tools (`exec_command`, non-pure `call_function`) are marked `undoable:false`; undo skips them and reports them.
- Deletes are soft (hidden, collision off). The actor is destroyed only when the entry leaves history.
- `clear_scope` and `clear_all` need `confirm:true`, create a checkpoint first, and only touch the caller's nodes.
- Policies are in **Project Settings → Plugins → Runtime Toolsets** (`DefaultGame.ini`):
  - Reflection allow-roots and deny-lists.
  - Spawn class roots.
  - Console deny prefixes, plus an optional allow-list.
  - Limits.
- Development builds only. The modules are denied in Shipping and Test.

## Packaging

- Always cook `/Engine/BasicShapes` (shapes and BasicShapeMaterial).
- Reflection only reaches classes and assets that were cooked.

## Tests

- **Editor:** `UnrealEditor-Cmd MCPProject.uproject -ExecCmds="Automation RunTests RuntimeToolsets,Quit" -unattended -nullrhi -TestExit="Automation Test Queue Empty"`
- **Packaged game:** `MCPProject.exe -ExecCmds="RuntimeToolsets.RunTests RuntimeToolsets. quit"`. The log ends with `RuntimeToolsets tests: N passed, M failed`.
- **Update schema snapshots** after an intentional API change: add `RuntimeToolsets.UpdateSchemaSnapshots 1` before `Automation RunTests`. Then review the diff and bump the toolset version.

See `Docs/ARCHITECTURE.md`, `Docs/AUTHORING.md` and `Docs/ERRORS.md`.
