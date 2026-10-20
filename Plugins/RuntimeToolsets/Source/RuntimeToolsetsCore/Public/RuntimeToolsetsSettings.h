#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "RuntimeToolsetsSettings.generated.h"

/** Limits and policies for RuntimeToolsets. Defaults live in Config/DefaultGame.ini. */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Runtime Toolsets"))
class RUNTIMETOOLSETSCORE_API URuntimeToolsetsSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	URuntimeToolsetsSettings();

	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }

	static const URuntimeToolsetsSettings& Get() { return *GetDefault<URuntimeToolsetsSettings>(); }

	/** Toolsets to enable. Empty enables all registered toolsets. */
	UPROPERTY(Config, EditAnywhere, Category = "Toolsets")
	TArray<FString> EnabledToolsets;

	/** Qualified tool names (toolset.tool) to disable. */
	UPROPERTY(Config, EditAnywhere, Category = "Toolsets")
	TArray<FString> DisabledTools;

	UPROPERTY(Config, EditAnywhere, Category = "Limits")
	int32 DefaultListLimit = 25;

	UPROPERTY(Config, EditAnywhere, Category = "Limits")
	int32 MaxListLimit = 500;

	UPROPERTY(Config, EditAnywhere, Category = "Limits")
	int32 MaxHistoryPerSession = 200;

	UPROPERTY(Config, EditAnywhere, Category = "Limits")
	int32 MaxQueuedCalls = 64;

	/** Game-thread time budget per frame for draining queued calls. */
	UPROPERTY(Config, EditAnywhere, Category = "Limits")
	float FrameBudgetMs = 4.f;

	/** Timeout for deferred (async) tools. */
	UPROPERTY(Config, EditAnywhere, Category = "Limits")
	float DefaultTimeoutSeconds = 30.f;

	/** Results larger than this are rejected with a hint to narrow the query. */
	UPROPERTY(Config, EditAnywhere, Category = "Limits")
	int32 MaxResultBytes = 262144;

	UPROPERTY(Config, EditAnywhere, Category = "Limits")
	int32 LogRingLines = 4000;

	UPROPERTY(Config, EditAnywhere, Category = "Capture")
	int32 CaptureMaxDimension = 1024;

	UPROPERTY(Config, EditAnywhere, Category = "Capture")
	int32 CaptureJpegQuality = 80;

	/** Object path prefixes that reflection tools may touch. */
	UPROPERTY(Config, EditAnywhere, Category = "Reflection")
	TArray<FString> AllowedObjectRoots;

	/** Class names (or path prefixes) reflection tools must never touch. */
	UPROPERTY(Config, EditAnywhere, Category = "Reflection")
	TArray<FString> DeniedClasses;

	/** Function names call_function refuses. */
	UPROPERTY(Config, EditAnywhere, Category = "Reflection")
	TArray<FString> DeniedFunctions;

	/** Allow set_property on properties without Edit/BlueprintVisible flags. */
	UPROPERTY(Config, EditAnywhere, Category = "Reflection")
	bool bAllowWriteNonEditableProperties = false;

	/** Class path prefixes spawn_actor may load. */
	UPROPERTY(Config, EditAnywhere, Category = "Actors")
	TArray<FString> SpawnClassAllowRoots;

	/** Allow destroy_actor on actors the tools did not create (soft-deleted, undoable). */
	UPROPERTY(Config, EditAnywhere, Category = "Actors")
	bool bAllowDestroyLevelActors = false;

	/** Console commands starting with any of these are refused. */
	UPROPERTY(Config, EditAnywhere, Category = "Console")
	TArray<FString> ConsoleDenyPrefixes;

	/** When non-empty, only commands starting with one of these are allowed. */
	UPROPERTY(Config, EditAnywhere, Category = "Console")
	TArray<FString> ConsoleAllowPrefixes;

	int32 ClampLimit(int64 Requested) const
	{
		if (Requested <= 0)
		{
			return DefaultListLimit;
		}
		return static_cast<int32>(FMath::Min<int64>(Requested, MaxListLimit));
	}
};
