#pragma once

#include "CoreMinimal.h"

class AActor;
class UClass;
class UWorld;

/** Spawning used by actors.spawn_actor, scene.create_group, duplicate_node and import_tree. */
namespace RuntimeSpawn
{
	/** Shapes available from /Engine/BasicShapes (100 cm at scale 1). */
	const TArray<FString>& ShapeNames();

	/** Loads a spawnable actor class, enforcing SpawnClassAllowRoots. OutNotAllowed tells policy failures from missing classes. */
	UClass* LoadSpawnClass(const FString& ClassPath, FString& OutError, bool* OutNotAllowed = nullptr);

	/**
	 * Spawns from a source string: "shape:cube", "class:/Script/Engine.PointLight", "group".
	 * The actor is movable and does not tick unless its class requires it.
	 */
	AActor* SpawnFromSource(UWorld* World, const FString& Source, const FTransform& Transform, const FString& BaseName, FString& OutError);

	/** Sets the "Color" parameter of the BasicShapeMaterial (creates a dynamic instance once). */
	bool SetShapeColor(AActor* Actor, const FLinearColor& Color);

	/** Reads the color set by SetShapeColor, if any. */
	bool GetShapeColor(AActor* Actor, FLinearColor& OutColor);
}
