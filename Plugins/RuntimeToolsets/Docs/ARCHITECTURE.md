# Architecture

```
connector / RuntimeToolsetsMCP adapter
        │  ListToolsets · DescribeToolset · Invoke(tool, json, caller)   (any thread)
        ▼
RuntimeToolsetsCore ── registry ── MPSC queue ── game-thread pump (FTSTicker, frame budget)
        │                                   │
        │                         FToolInvocation (validated args, world, graph, session history)
        ▼                                   ▼
  scene graph (per world)          command history (per session): undo / redo / transactions / checkpoints
        ▲
RuntimeToolsetsCoreTools: session · actors · scene · reflection · console · log · capture · history
RuntimeToolsetsPresentation: materials · camera · ui · vfx · lighting      RuntimeToolsetsContent: layout · build
RuntimeToolsetsGameplay: game · physics · level · input      RuntimeToolsetsOps: profiling · ops
```

## Modules

| Module | Type | Depends on | Purpose |
|---|---|---|---|
| RuntimeToolsetsCore | Runtime, PostDefault | Engine, Json, JsonUtilities, DeveloperSettings | API, registry, dispatcher, schema builder and validator, command layer, scene graph, resolver, log capture, settings |
| RuntimeToolsetsCoreTools | Runtime, PostDefault | Core only (plus engine render/image modules) | The Phase 1 toolsets |
| RuntimeToolsetsGameplay | Runtime, PostDefault | Core only (plus InputCore, EnhancedInput, PhysicsCore) | game, physics, level, input |
| RuntimeToolsetsPresentation | Runtime, PostDefault | Core only (plus RenderCore, UMG, Slate, Niagara) | materials, camera, ui, vfx, lighting |
| RuntimeToolsetsContent | Runtime, PostDefault | Core only (plus GeometryCore, GeometryFramework, GeometryScriptingCore) | layout, build |
| RuntimeToolsetsOps | Runtime, PostDefault | Core only (plus RHI, RenderCore) | profiling, ops |
| RuntimeToolsetsTests | Runtime, PostEngineInit | Core only | Specs, schema snapshot test, `RuntimeToolsets.RunTests` packaged runner |
| RuntimeToolsetsMCP (separate plugin) | Runtime, PostEngineInit | Core + Epic ModelContextProtocol | Optional adapter for local testing |

**Rules:**
- Group modules depend only on Core, never on each other.
- Every Build.cs calls `RuntimeToolsetsBuildGuard.Check`, which fails the build on editor-only or Python dependencies.
- Epic's MCP plugin (NoRedist, Experimental) is used only by the adapter plugin, and all of that code is in `McpToolAdapter.cpp` and `RuntimeToolsetsMCPModule.cpp`.

## Threading

1. **Invoke.** `Invoke` parses the arguments and enqueues an `FToolCall`; a full queue (`MaxQueuedCalls`) returns `busy`.
2. **Pump.** The pump runs on the core ticker on the game thread. For each call it:
   1. dequeues the call;
   2. resolves the tool and checks it is enabled;
   3. validates the arguments against the schema and fills defaults;
   4. finds the world (test override → Game → PIE);
   5. runs the handler.
3. **Frame budget.** Synchronous calls drain until `FrameBudgetMs` is used up.
4. **Async tools.** An async tool calls `Defer()` and blocks the queue until it completes, is cancelled, or hits `DefaultTimeoutSeconds`. Calls stay strictly serial.
5. **Callbacks** always fire on the game thread.
6. **Nested calls.** `session.call_tool` runs nested tools inline (`ExecuteNested`), up to depth 4, and can wait for async tools.

Deferred tools may extend their timeout with `FToolInvocation::SetTimeout`. `session.call_tool` allows nested work up to 10 minutes, and nested tools such as `wait_for_condition` enforce their own limits.

The in-process test runner (`FRuntimeTestRunner`) lives in Core. It is shared by the `RuntimeToolsets.RunTests` console command and by `ops.run_automation_test`.

## Node sources

Every scene node stores how it was made: `group`, `shape:cube`, `class:/Script/...`, `prim:{recipe}` or `vfx:/Path`. Modules register a spawner per prefix with `RegisterSourceSpawner`. Duplicate, import and the build arrays use those spawners, so new node kinds plug in without CoreTools knowing about them.

## Command layer

- **Mutating tools** run an `IRuntimeCommand` through `FToolInvocation::RunCommand`, which applies it to the caller's `FRuntimeCommandHistory`.
- **Shared commands:** `FCreateNodeCommand`, `FSoftDeleteActorsCommand`, `FTransformCommand`, `FAttachCommand`, `FPropertyCommand`, `FCompositeCommand` and `FLambdaCommand`.
- **History:**
  - Undo and redo stacks with a size limit.
  - Nested transactions that collapse into one composite entry.
  - Named markers (checkpoints) and irreversible markers.
- **Discarding.** When an entry leaves history, `OnDiscarded` finalizes it: undone creations and applied soft-deletes destroy their actors.
- **World teardown** clears all histories without destroying anything.

## Scene graph

- **Nodes** are tool-created actors with stable ids (`n<number>`), names unique among siblings, tags, a source spec, and an owning session.
- **Hierarchy** is native actor attachment. `SyncParentFromAttachment` keeps the graph in step after any attach.
- **Groups** are `ARuntimeToolsetsGroupActor`: a non-ticking actor with a movable scene root.
- **Tags.** Each node's actor carries an `rt:<id>` tag.

## Results

- **Results are compact deltas**, not full state.
- **List tools** take `limit` (default 25) and `fields`, and report `total` and `truncated`.
- **Size cap.** Results over `MaxResultBytes` are rejected with a hint to narrow the query.
- **Usage report.** `session.get_usage_report` shows bytes per tool, so token cost can be tracked.
