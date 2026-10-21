#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "RuntimeToolTypes.h"
#include "RuntimeToolset.h"

class AActor;
class FRuntimeSceneGraph;
class FRuntimeCommandHistory;
class FRuntimeLogCapture;
class UWorld;

/** Handle to an in-flight call returned by Invoke. */
class IToolCallHandle
{
public:
	virtual ~IToolCallHandle() = default;
	/** Requests cancellation. Queued calls complete with "cancelled"; running async tools get their cancel handler. */
	virtual void Cancel() = 0;
	virtual bool IsDone() const = 0;
	virtual FGuid GetCallId() const = 0;
};

DECLARE_DELEGATE_TwoParams(FOnToolProgress, float /*Fraction 0..1*/, const FString& /*Message*/);
DECLARE_DELEGATE_OneParam(FOnToolComplete, const FToolResult& /*Result*/);
/**
 * Builds an actor from a node source string ("shape:cube", "prim:{...}", "vfx:/Path"). Used by duplicate, import and arrays,
 * so every node kind can be recreated from its source. Spawns at Transform (relative to the future parent).
 */
using FRuntimeSourceSpawner = TFunction<AActor*(UWorld* World, const FString& Source, const FTransform& Transform, const FString& Name, FString& OutError)>;

/** Event pushed to connectors (log subscriptions, history changes). Fired on the game thread. */
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnRuntimeToolEvent, const FString& /*SessionId*/, const TSharedRef<FJsonObject>& /*Event*/);

/**
 * Public entry point of RuntimeToolsets. A connector (or the MCP adapter) needs only this interface:
 * arguments and results are plain JSON.
 *
 * Threading: ListToolsets/DescribeToolset/Invoke may be called from any thread. Calls run on the game thread,
 * one at a time, in order. Completion and progress callbacks fire on the game thread.
 */
class IRuntimeToolsetsCore;

/** Instance pointer set while the module is loaded. Safe to read from any thread. */
RUNTIMETOOLSETSCORE_API IRuntimeToolsetsCore* GetRuntimeToolsetsCoreInstance();

class IRuntimeToolsetsCore : public IModuleInterface
{
public:
	/** Thread-safe (does not touch the module manager). The module must be loaded. */
	static IRuntimeToolsetsCore& Get()
	{
		IRuntimeToolsetsCore* Instance = GetRuntimeToolsetsCoreInstance();
		check(Instance);
		return *Instance;
	}
	static bool IsAvailable() { return GetRuntimeToolsetsCoreInstance() != nullptr; }

	/** Compact JSON: {"toolsets":[{"name","description","version","tools":N}]} */
	virtual FString ListToolsets() const = 0;

	/** Compact JSON with each tool's name, description, flags and (optionally) input schema. Empty string if unknown. */
	virtual FString DescribeToolset(const FString& Name, bool bIncludeSchemas = true) const = 0;

	/**
	 * Queues a tool call. Tool is the qualified name ("actors.spawn_actor").
	 * OnComplete is always called exactly once (also for errors such as unknown_tool or busy).
	 */
	virtual TSharedRef<IToolCallHandle> Invoke(const FString& Tool, const FString& JsonArguments, const FToolCallerContext& Caller,
		FOnToolComplete OnComplete, FOnToolProgress OnProgress = FOnToolProgress()) = 0;

	virtual TSharedRef<IToolCallHandle> Invoke(const FString& Tool, const TSharedPtr<FJsonObject>& Arguments, const FToolCallerContext& Caller,
		FOnToolComplete OnComplete, FOnToolProgress OnProgress = FOnToolProgress()) = 0;

	/**
	 * Runs a synchronous tool immediately on the game thread, bypassing the queue (used by call_tool and tests).
	 * Async tools return "unsupported".
	 */
	virtual FToolResult ExecuteInline(const FString& Tool, const TSharedPtr<FJsonObject>& Arguments, const FToolCallerContext& Caller, int32 Depth) = 0;

	/**
	 * Runs a tool immediately on the game thread (bypassing the queue), including async tools.
	 * OnDone fires exactly once, possibly in a later frame. Returns a function that cancels the nested call.
	 * Used by session.call_tool; the caller must itself be deferred while the nested call is pending.
	 */
	virtual TFunction<void()> ExecuteNested(const FString& Tool, const TSharedPtr<FJsonObject>& Arguments, const FToolCallerContext& Caller, int32 Depth,
		TFunction<void(const FToolResult&)> OnDone) = 0;

	virtual FOnRuntimeToolEvent& OnEvent() = 0;
	virtual FSimpleMulticastDelegate& OnToolsetsChanged() = 0;

	virtual void RegisterToolset(const TSharedRef<IRuntimeToolset>& Toolset) = 0;
	virtual void UnregisterToolset(FName Name) = 0;

	/** All enabled tool descriptors (used by adapters to mirror the catalogue). */
	virtual TArray<TSharedRef<const FRuntimeToolDescriptor>> GetAllTools() const = 0;
	virtual TSharedPtr<const FRuntimeToolDescriptor> FindTool(const FString& QualifiedName) const = 0;
	virtual bool IsToolEnabled(const FRuntimeToolDescriptor& Tool) const = 0;

	/**
	 * Registers how to build actors for sources starting with Prefix (e.g. "prim:"); an exact source such as "group"
	 * may be registered too. Game thread.
	 */
	virtual void RegisterSourceSpawner(const FString& Prefix, FRuntimeSourceSpawner Spawner) = 0;
	virtual void UnregisterSourceSpawner(const FString& Prefix) = 0;
	virtual bool HasSourceSpawner(const FString& Source) const = 0;
	/** Spawns from a registered source; nullptr with an error when no spawner matches or spawning fails. */
	virtual AActor* SpawnFromSource(UWorld* World, const FString& Source, const FTransform& Transform, const FString& Name, FString& OutError) = 0;

	/** Drops the session's history (finalizing soft-deletes) and its log subscriptions. */
	virtual void EndSession(const FString& SessionId) = 0;

	// ---- services for toolsets (game thread) ----
	virtual UWorld* FindWorld() const = 0;
	/** Forces the world used by tools (tests). nullptr restores automatic lookup. */
	virtual void SetWorldOverride(UWorld* World) = 0;
	virtual FRuntimeSceneGraph* GetSceneGraph(UWorld* World) = 0;
	virtual FRuntimeCommandHistory& GetHistory(const FString& SessionId) = 0;
	virtual FRuntimeLogCapture& GetLogCapture() = 0;
	virtual void BroadcastEvent(const FString& SessionId, const TSharedRef<FJsonObject>& Event) = 0;
	/** Calls run since startup and the largest result in bytes (token-usage reporting). */
	virtual TSharedRef<FJsonObject> GetUsageReport() const = 0;
};
