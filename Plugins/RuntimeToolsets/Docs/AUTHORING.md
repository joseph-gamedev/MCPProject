# Writing a toolset

A toolset is one class in one `.h/.cpp` pair. It lives in a group module that depends only on `RuntimeToolsetsCore`. Shared helpers (`ReadTransform`, `ResolveActorList`, `ActorBox`, `StepToJson`, `Ref`) are in `RuntimeToolHelpers.h`.

```cpp
class FLightingToolset final : public IRuntimeToolset
{
public:
	virtual FName GetName() const override { return TEXT("lighting"); }      // lower-case, no underscores
	virtual FString GetDescription() const override { return TEXT("Sun, sky, fog"); }
	virtual FString GetVersion() const override { return TEXT("1.0.0"); }   // semver of the public surface
	virtual void RegisterTools(FRuntimeToolsetBuilder& B) override;
};

void FLightingToolset::RegisterTools(FRuntimeToolsetBuilder& B)
{
	B.Tool(TEXT("set_sun"), TEXT("Set sun rotation and intensity."))   // short: becomes the schema description
		.Args(FToolSchema()
			.Add(TEXT("target"), FToolParam::Target().Required())
			.Add(TEXT("rotation"), FToolParam::Rotator(TEXT("[pitch,yaw,roll]")))
			.Add(TEXT("intensity"), FToolParam::Number(TEXT("lux"), 0.0, 200000.0)))
		.Undoable().RequiresWorld()
		.Handle(&SetSun);
}
```

Register the toolset in your module's `StartupModule` with `IRuntimeToolsetsCore::Get().RegisterToolset(MakeShared<FLightingToolset>())`, and unregister it on shutdown.

## The five-step handler

```cpp
static void SetSun(FToolInvocation& Inv)
{
	// 1. validate — already done against the schema; add semantic checks here
	// 2. resolve
	AActor* Sun = Inv.ResolveActor(TEXT("target"));
	if (!Sun) return;                                  // ResolveActor already failed the call
	// 3. build a command (FPropertyCommand / FTransformCommand / FLambdaCommand / your own IRuntimeCommand)
	TUniquePtr<IRuntimeCommand> Cmd = ...;
	// 4. run it through the session history
	TSharedPtr<FJsonObject> Delta;
	if (!Inv.RunCommand(MoveTemp(Cmd), &Delta)) return;
	// 5. return a small delta
	Inv.Succeed(Delta);
}
```

## Rules

- **Every call ends exactly once,** with `Succeed` or `Fail`. An async tool calls `Defer()` first and finishes later; register `SetCancelHandler` to clean up.
- **Use the fixed error codes** in `RuntimeToolTypes.h`. Messages are short and actionable; put the fix in `Hint`.
- **Keep payloads small.** Lists honour `limit`/`fields`, return `total`, and default to a few fields.
- **Irreversible tools** call `.Irreversible()` and `Inv.RecordIrreversible(label)`.
- **Spawned actors** go through `FCreateNodeCommand` so they join the scene graph.
- **No editor-only APIs.** The build guard rejects editor modules. Also avoid `GetActorLabel`, `PostEditChangeProperty` and `WITH_EDITOR` paths.
- **Tests are required.** Each tool needs success, error and (if mutating) undo cases in a spec under `RuntimeToolsetsTests`. Then regenerate the schema snapshot and bump the toolset version.
