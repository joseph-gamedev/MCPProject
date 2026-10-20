#pragma once

#include "CoreMinimal.h"
#include "RuntimeToolSchema.h"

class FToolInvocation;

using FRuntimeToolHandler = TFunction<void(FToolInvocation&)>;

/** Static description of one tool. Name + schema + result shape form the stable public surface. */
struct RUNTIMETOOLSETSCORE_API FRuntimeToolDescriptor
{
	FName Toolset;
	/** Short name inside the toolset, e.g. "spawn_actor". */
	FString Name;
	/** "<toolset>.<name>", e.g. "actors.spawn_actor". */
	FString QualifiedName;
	FString Description;
	TSharedRef<FJsonObject> InputSchema = FToolSchema().Build();

	/** Changes world or engine state. */
	bool bMutating = false;
	/** Recorded in the session's undo history. */
	bool bUndoable = false;
	/** Completes later (handler calls Defer()). */
	bool bAsync = false;
	/** Needs a game/PIE world. */
	bool bRequiresWorld = false;

	FRuntimeToolHandler Handler;

	TSharedRef<FJsonObject> ToJson(bool bIncludeSchema) const;
};

/** Fluent builder for one tool. */
class RUNTIMETOOLSETSCORE_API FRuntimeToolBuilder
{
public:
	explicit FRuntimeToolBuilder(const TSharedRef<FRuntimeToolDescriptor>& InDescriptor) : Descriptor(InDescriptor) {}

	FRuntimeToolBuilder& Args(const FToolSchema& Schema);
	/** Mutating + Undoable. */
	FRuntimeToolBuilder& Undoable();
	/** Mutating but cannot be undone (ends the undo chain). */
	FRuntimeToolBuilder& Irreversible();
	FRuntimeToolBuilder& Async();
	FRuntimeToolBuilder& RequiresWorld();
	FRuntimeToolBuilder& Handle(FRuntimeToolHandler InHandler);

private:
	TSharedRef<FRuntimeToolDescriptor> Descriptor;
};

/** Collects tools for one toolset during RegisterTools. */
class RUNTIMETOOLSETSCORE_API FRuntimeToolsetBuilder
{
public:
	explicit FRuntimeToolsetBuilder(FName InToolset) : Toolset(InToolset) {}

	FRuntimeToolBuilder Tool(const FString& Name, const FString& Description);

	const TArray<TSharedRef<FRuntimeToolDescriptor>>& GetTools() const { return Tools; }

private:
	FName Toolset;
	TArray<TSharedRef<FRuntimeToolDescriptor>> Tools;
};

/** One toolset = one class = one file pair. Register with IRuntimeToolsetsCore::RegisterToolset. */
class IRuntimeToolset
{
public:
	virtual ~IRuntimeToolset() = default;

	/** Lower-case identifier, e.g. "actors". */
	virtual FName GetName() const = 0;
	virtual FString GetDescription() const = 0;
	/** Semantic version of the toolset's public surface. */
	virtual FString GetVersion() const = 0;
	virtual void RegisterTools(FRuntimeToolsetBuilder& Builder) = 0;
};
