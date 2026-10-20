# RuntimeToolsets

Runtime-safe toolsets that let AI agents inspect and build inside a running Unreal Engine 5.8 game, including a packaged Development build. Epic's 5.8 MCP toolsets are editor-only; this plugin works without the editor.

The plugin only provides tools. A connector (yours, or the optional `RuntimeToolsetsMCP` adapter) owns transport, authentication and sessions.

## Toolsets (Phase 1)

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
