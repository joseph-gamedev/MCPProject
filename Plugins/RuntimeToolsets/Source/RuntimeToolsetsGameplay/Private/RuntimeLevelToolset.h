#pragma once

#include "CoreMinimal.h"
#include "RuntimeToolCall.h"
#include "RuntimeToolsetDefinition.h"
#include "RuntimeLevelToolset.generated.h"

/** World Partition data layer runtime state. */
UENUM()
enum class ELevelToolDataLayerState : uint8
{
	/** Not loaded. */
	Unloaded,
	/** Loaded but not visible or ticking. */
	Loaded,
	/** Loaded, visible and running. */
	Activated,
};

USTRUCT()
struct FLevelToolStreamingInfo
{
	GENERATED_BODY()

	/** Short name, e.g. "Sandbox" (pass it as `level`). */
	UPROPERTY()
	FString Name;

	/** Package path, e.g. "/Game/Maps/Sandbox" (or the instance's unique package). */
	UPROPERTY()
	FString Package;

	UPROPERTY()
	bool bLoaded = false;

	UPROPERTY()
	bool bVisible = false;

	/** Requested state (streaming catches up over the next frames). */
	UPROPERTY()
	bool bShouldBeLoaded = false;

	UPROPERTY()
	bool bShouldBeVisible = false;

	/** Loaded by load_level_instance (a dynamic level instance). */
	UPROPERTY()
	bool bInstance = false;
};

USTRUCT()
struct FLevelToolLevels
{
	GENERATED_BODY()

	/** Package of the persistent level, e.g. "/Game/Maps/Main". */
	UPROPERTY()
	FString Persistent;

	/** Streaming levels and level instances, in the world's order. */
	UPROPERTY()
	TArray<FLevelToolStreamingInfo> Streaming;

	/** World Partition data layers: short name -> Unloaded / Loaded / Activated (empty without World Partition). */
	UPROPERTY()
	TMap<FString, ELevelToolDataLayerState> DataLayers;
};

USTRUCT()
struct FLevelToolLoadResult
{
	GENERATED_BODY()

	/** The streamed-in level (loaded and visible). */
	UPROPERTY()
	FLevelToolStreamingInfo Level;
};

USTRUCT()
struct FLevelToolStateChange
{
	GENERATED_BODY()

	/** Package of the streaming level. */
	UPROPERTY()
	FString Level;

	UPROPERTY()
	bool bShouldBeLoaded = false;

	UPROPERTY()
	bool bShouldBeVisible = false;
};

USTRUCT()
struct FLevelToolDataLayerChange
{
	GENERATED_BODY()

	/** Data layer short name. */
	UPROPERTY()
	FString DataLayer;

	UPROPERTY()
	ELevelToolDataLayerState State = ELevelToolDataLayerState::Unloaded;

	UPROPERTY()
	ELevelToolDataLayerState Previous = ELevelToolDataLayerState::Unloaded;
};

USTRUCT()
struct FLevelToolTravelInfo
{
	GENERATED_BODY()

	/** Map package the game travels to. */
	UPROPERTY()
	FString TravelTo;

	UPROPERTY()
	FString Note;
};

USTRUCT()
struct FLevelToolAssetInfo
{
	GENERATED_BODY()

	/** Object path, e.g. "/Game/Maps/Sandbox.Sandbox" (pass it to tools that take an asset). */
	UPROPERTY()
	FString Path;

	/** Package path, e.g. "/Game/Maps/Sandbox" (what load_level_instance / open_level take for maps). */
	UPROPERTY()
	FString Package;

	/** Asset class, e.g. "World", "StaticMesh", "InputAction". */
	UPROPERTY()
	FString Class;
};

USTRUCT()
struct FLevelToolAssetSearch
{
	GENERATED_BODY()

	/** Matches before the limit. */
	UPROPERTY()
	int32 Total = 0;

	/** Matches sorted by path, at most `limit`. */
	UPROPERTY()
	TArray<FLevelToolAssetInfo> Assets;
};

/** Completes when a streamed level instance is loaded and visible. */
UCLASS()
class URuntimeLevelToolLoadAsyncResult : public URuntimeToolAsyncResult
{
	GENERATED_BODY()

public:
	bool SetValue(const FLevelToolLoadResult& InValue)
	{
		Value = InValue;
		return Complete();
	}

	UPROPERTY()
	FLevelToolLoadResult Value;
};

/**
 * Levels and content of the running game: streaming levels, level instances, World Partition data layers, map travel and
 * a search over the cooked asset registry.
 * Workflow: find_assets (asset_class World) to discover maps -> list_levels / get_current_level -> load_level_instance
 * (streams a map in at a transform; undo unloads it) -> set_level_visibility / unload_level / set_data_layer.
 * open_level replaces the whole world and ends all undo history.
 */
UCLASS()
class URuntimeLevelToolset : public URuntimeToolsetDefinition
{
	GENERATED_BODY()

public:
	virtual FString GetToolsetVersion() const override { return TEXT("2.0.0"); }

	/**
	 * Lists the persistent level, streaming levels and level instances (loaded/visible state) and data layer states.
	 *
	 * @return {persistent, streaming:[{name, package, loaded, visible, should_be_loaded, should_be_visible, instance}], data_layers:{name: state}}.
	 */
	UFUNCTION(meta = (AICallable, RequiresWorld), Category = "Level")
	static FLevelToolLevels ListLevels();

	/**
	 * Returns the package path of the loaded map (the persistent level), e.g. "/Game/Maps/Main".
	 */
	UFUNCTION(meta = (AICallable, RequiresWorld), Category = "Level")
	static FString GetCurrentLevel();

	/**
	 * Searches the cooked asset registry for assets (maps, meshes, materials, input actions, ...). Only assets packaged
	 * with the build are found. Per-actor packages (__ExternalActors__) are skipped.
	 *
	 * @param FolderPath - Folder to search, e.g. "/Game/Maps"; empty = /Game and every plugin content root.
	 * @param Name - Only assets whose name contains this text (case-insensitive), e.g. "Sandbox".
	 * @param AssetClass - Only assets of this class or a subclass, e.g. "World" for maps, "StaticMesh", "InputAction".
	 * @param bRecursive - Also search sub-folders.
	 * @param Limit - Maximum results (0 = default list limit).
	 * @return Total matches and the assets {path, package, class}, sorted by path.
	 * Fails if folder_path does not start with "/" or the class is unknown.
	 */
	UFUNCTION(meta = (AICallable), Category = "Level|Assets")
	static FLevelToolAssetSearch FindAssets(const FString& FolderPath = TEXT(""), const FString& Name = TEXT(""), UClass* AssetClass = nullptr,
		bool bRecursive = true, int32 Limit = 0);

	/**
	 * Streams a map in as a level instance at a transform and completes when it is loaded and visible. Undoable (undo
	 * unloads it).
	 *
	 * @param Map - Map package, e.g. "/Game/Maps/Sandbox" (see find_assets with asset_class World).
	 * @param Location - Offset of the instance [x,y,z] in cm, e.g. [0,0,-5000].
	 * @param Rotation - Rotation [pitch,yaw,roll] of the instance.
	 * @param Name - Optional unique instance name.
	 * @param bBlocking - Finish now with a full streaming flush (frame hitch) instead of waiting over the next frames.
	 * @return {level:{name, package, loaded, visible, ...}} once visible.
	 * Fails if the path is not a map package or the map is not in the build.
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld, AutoCreateRefTerm = "Location,Rotation"), Category = "Level")
	static URuntimeLevelToolLoadAsyncResult* LoadLevelInstance(const FString& Map, const FVector& Location, const FRotator& Rotation,
		const FString& Name = TEXT(""), bool bBlocking = false);

	/**
	 * Unloads a streaming level or level instance. Undoable (undo loads it back).
	 *
	 * @param Level - Short name or package from list_levels, e.g. "Sandbox".
	 * @param bBlocking - Flush streaming now instead of over the next frames.
	 * @return The level package and its requested state.
	 * Fails if no streaming level matches (the known ones are listed).
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld), Category = "Level")
	static FLevelToolStateChange UnloadLevel(const FString& Level, bool bBlocking = false);

	/**
	 * Shows or hides a streaming level (showing also loads it). Undoable.
	 *
	 * @param Level - Short name or package from list_levels, e.g. "Sandbox".
	 * @param bVisible - true to show, false to hide (it stays loaded).
	 * @param bBlocking - Flush streaming now instead of over the next frames.
	 * @return The level package and its requested state.
	 * Fails if no streaming level matches (the known ones are listed).
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld), Category = "Level")
	static FLevelToolStateChange SetLevelVisibility(const FString& Level, bool bVisible, bool bBlocking = false);

	/**
	 * Sets a World Partition data layer to Unloaded, Loaded or Activated. Undoable.
	 *
	 * @param Name - Data layer short name from list_levels, e.g. "Night".
	 * @param State - Unloaded, Loaded or Activated.
	 * @param bRecursive - Apply to child layers too.
	 * @return The layer, its new and previous state.
	 * Fails if the world has no data layers (not World Partition) or no layer matches (the known ones are listed).
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld), Category = "Level")
	static FLevelToolDataLayerChange SetDataLayer(const FString& Name, ELevelToolDataLayerState State, bool bRecursive = false);

	/**
	 * Travels to another map. This destroys the current world, its scene graph and all undo history; travel happens next
	 * frame (poll session.ping until the new map reports).
	 *
	 * @param Map - Map package, e.g. "/Game/Maps/Sandbox".
	 * @param Options - URL options, e.g. "?game=/Script/Engine.GameModeBase".
	 * @param bConfirm - Must be true: acknowledges that the world and its history are lost.
	 * @return The destination package and a note.
	 * Fails without confirm, if the path is not a map package or the map is not in the build.
	 */
	UFUNCTION(meta = (AICallable, Irreversible, RequiresWorld), Category = "Level")
	static FLevelToolTravelInfo OpenLevel(const FString& Map, const FString& Options = TEXT(""), bool bConfirm = false);
};
