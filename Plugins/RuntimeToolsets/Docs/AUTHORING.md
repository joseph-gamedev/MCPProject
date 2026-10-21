# Writing a toolset

Toolsets follow the model of Epic's editor toolsets (`Engine/Plugins/Experimental/ToolsetRegistry`,
`Engine/Plugins/Experimental/Toolsets/*`): a class whose static UFUNCTIONs are the tools. Epic's are editor-only; ours
run in game builds too. Read the matching Epic toolset before designing a new one (see `EPIC_TOOLSETS_COMPARISON.md`).

```cpp
/**
 * Sun, sky and fog. Workflow: get_environment -> set_sun -> screenshot (see the outdoor_lighting skill).
 */
UCLASS()
class URuntimeLightingToolset : public URuntimeToolsetDefinition      // toolset name "lighting"
{
	GENERATED_BODY()

public:
	virtual FString GetToolsetVersion() const override { return TEXT("2.0.0"); }

	/**
	 * Sets the sun's direction and brightness. Undoable.
	 *
	 * @param Sun - Directional light; the level's sun when omitted.
	 * @param Rotation - [pitch, yaw, roll]: pitch 0 = horizon, -90 = overhead.
	 * @param Intensity - Lux, e.g. 100000 for direct sun.
	 * @return The sun after the change.
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld, AutoCreateRefTerm = "Rotation"), Category = "Lighting")
	static FSunInfo SetSun(const FRotator& Rotation, AActor* Sun = nullptr, float Intensity = -1.f);
};
```

Register it in the module's `StartupModule`:

```cpp
#include "Generated/ToolDocs.gen.inl"   // docs for packaged builds (generated)

RuntimeToolReflection::RegisterCompiledDocs(GRuntimeToolDocs, UE_ARRAY_COUNT(GRuntimeToolDocs));
Registered.Add(RuntimeToolReflection::RegisterToolsetClass(URuntimeLightingToolset::StaticClass()));
```

and unregister with `IRuntimeToolsetsCore::Get().UnregisterToolset(Toolset->GetName())` on shutdown.

## What is generated

| From | Becomes |
|---|---|
| class name `URuntime<Name>Toolset` | toolset name (snake_case) |
| function name | tool name (`SetSun` -> `lighting.set_sun`) |
| parameters | input schema: snake_case names (`bClosed` -> `closed`), enums as strings, structs as objects, `FVector` as `[x,y,z]`, object pointers as references (node id, path, `actor:Name`, object/asset path), `FRuntimeToolJson` as any JSON |
| C++ default values, `AutoCreateRefTerm` | optional parameters (with `default` in the schema) |
| return value and out parameters | output schema; results are `{"returnValue": ...}` |
| doc comment | description; `@param Name - text` documents arguments, `@return` the result |
| `Undoable` / `Irreversible` / `RequiresWorld` metadata | flags; `Irreversible` adds Epic's "only after explicit permission" sentence |
| returning a `URuntimeToolAsyncResult` subclass | async tool (completes on `SetValue` / `SetError`) |

Doc comments are editor-only metadata. `RuntimeToolsets.UpdateSchemaSnapshots 1` (with the snapshot test) writes them to
each module's `Private/Generated/ToolDocs.gen.inl`, compiled into packaged builds; the test fails when they are stale.

## Inside a tool

```cpp
FSunInfo URuntimeLightingToolset::SetSun(const FRotator& Rotation, AActor* Sun, float Intensity)
{
	if (!Sun)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::NotFound, TEXT("no directional light"), TEXT("Spawn one: actors.spawn_actor class /Script/Engine.DirectionalLight"));
		return {};                               // the value is ignored after Fail
	}
	if (!RuntimeToolCall::RunCommand(MakeUnique<FPropertyCommand>(...)))   // undo history; fails the call on error
	{
		return {};
	}
	return Info(Sun);
}
```

`RuntimeToolCall` also gives `GetWorld()`, `GetGraph()`, `NodeOf(Actor)`, `MakeReference(Object)`, `Resolve(Ref, Class)`,
`SetTimeout`, `ReportProgress`.

## Rules (Epic's conventions)

- **Document like Epic does:** what it does and when to use it, every parameter with an example value, the result shape
  and sort order, and when it fails. The class comment gives the workflow (call order).
- **Errors are actionable:** fixed codes from `RuntimeToolTypes.h`; list the candidates on NotFound; name the expected
  type on mismatches; put the fix in the hint.
- **Results are documented USTRUCTs.** Return scene nodes with their `Id` so the agent can pass them straight back.
- **Small payloads:** lists take a `limit` and are sorted.
- **Spawned actors** go through `FCreateNodeCommand` (or a source spawner) so they join the scene graph and undo.
- **No editor-only APIs** (the build guard rejects editor modules); avoid `WITH_EDITOR` paths.
- **Tests are required:** success, failure and (if mutating) undo cases per tool, in a spec under `RuntimeToolsetsTests`;
  then update the snapshots and bump the toolset version.
- **Custom JSON for a struct type:** `RuntimeToolReflection::RegisterStructConverter` (Epic: `FToolsetJsonConverter`).

## Skills

Guidance for a kind of task (rules, numbers, workflow) is a skill, not tool text: subclass `URuntimeAgentSkill` (C++ or
Blueprint) and set `Description` ("Apply whenever ..."), `Instructions` and `Toolsets`; override `GeneratePrompt` to fill
placeholders with live data. Projects can also drop markdown skills in `Config/RuntimeToolsets/Skills/*.md`
(front matter `description: ...`). Agents use `skills.list_skills` and `skills.get_skills`. A test checks that every
`toolset.tool` a skill mentions exists.

## Hand-built tools

`IRuntimeToolset` with `FRuntimeToolsetBuilder` (`B.Tool(...).Args(...).Returns(...).Handle(...)`) still works and is
used for the framework's own session and history tools.
