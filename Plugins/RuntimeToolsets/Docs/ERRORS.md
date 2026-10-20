# Error codes

Every failed call returns `{"ok":false,"error":{"code":"<code>","message":"...","hint":"..."}}`.

| Code | Meaning | Typical fix |
|---|---|---|
| `invalid_arguments` | Schema or semantic check failed (message names the field, e.g. `$.location[2]`) | Fix the argument; see `session.describe_toolset` |
| `unknown_tool` | No tool with that name | Use `session.list_toolsets` / `describe_toolset`; names are `toolset.tool` |
| `toolset_disabled` | Disabled in project settings | Enable in Runtime Toolsets settings |
| `not_found` | Target, property, function, cvar or checkpoint does not exist (or node is deleted) | Find it first (`scene.find_nodes`, `actors.find_actors`, `reflection.list_*`) |
| `ambiguous_target` | A name matched several actors | Use a node id, a node path or a full object path |
| `not_allowed` | Blocked by policy (deny-lists, allow-roots, read-only property, level actor deletion) | Use an allowed alternative or change project settings |
| `no_world` | No game or PIE world is running | Start the game |
| `wrong_state` | Valid request that cannot run now (open transaction, name taken, command failed) | Follow the hint |
| `confirm_required` | Destructive tool called without `confirm:true` | Repeat with `confirm:true` |
| `nothing_to_undo` | Undo/redo stack empty | — |
| `unsupported` | Not possible in this build or on this target (no viewport, async tool nested inline) | Use the suggested alternative |
| `busy` | Call queue full | Retry after pending calls finish |
| `timeout` | Async tool exceeded its timeout | Retry; raise `DefaultTimeoutSeconds` if needed |
| `cancelled` | Cancelled by the caller or at shutdown | — |
| `internal` | Bug: a tool did not produce a result | Report it with the log |
