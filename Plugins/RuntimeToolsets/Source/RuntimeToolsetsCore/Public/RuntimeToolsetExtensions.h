#pragma once

#include "CoreMinimal.h"

class UWorld;

/**
 * Extension points for plugins that add toolsets on top of Runtime Toolsets (for example toolsets for content packs).
 * The core plugin never names those packs: an extension registers what it brings here. Game thread.
 */
namespace RuntimeToolsetExtensions
{
	/** Adds ops recipes: the text of a JSON object {name: {about, calls:[...]}}. ops.list_recipes / ops.run_recipe use them. */
	RUNTIMETOOLSETSCORE_API void RegisterRecipes(const FString& JsonObjectText);
	RUNTIMETOOLSETSCORE_API const TArray<FString>& GetRecipes();

	/**
	 * Named asset slots the core falls back from when unset, e.g. "material.terrain", "material.pbr",
	 * "material.scanned", "folder.scanned_surfaces". Set replaces; an empty path clears.
	 */
	RUNTIMETOOLSETSCORE_API void SetAsset(FName Slot, const FString& Path);
	RUNTIMETOOLSETSCORE_API FString GetAsset(FName Slot);

	/**
	 * Something other than the level's own lights drives the lighting (a sky system): returns true with who it is and
	 * which tools to use instead. lighting.* tools that would fight it refuse with that hint.
	 */
	using FLightingOwner = TFunction<bool(UWorld* World, FString& OutWho, FString& OutUseInstead)>;
	RUNTIMETOOLSETSCORE_API void RegisterLightingOwner(FName Id, FLightingOwner Owner);
	RUNTIMETOOLSETSCORE_API void UnregisterLightingOwner(FName Id);
	RUNTIMETOOLSETSCORE_API bool FindLightingOwner(UWorld* World, FString& OutWho, FString& OutUseInstead);

	/** Base directory of the plugin that holds a code module ("" when none does). */
	RUNTIMETOOLSETSCORE_API FString PluginDirForModule(const FString& ModuleName);
}
