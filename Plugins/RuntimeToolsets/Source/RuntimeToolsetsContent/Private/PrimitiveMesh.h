#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class AActor;
class UDynamicMesh;
class UWorld;

/**
 * Builder primitives are dynamic-mesh actors generated from a JSON recipe kept as the node source ("prim:{...}").
 * Booleans append to the recipe, so a mesh can always be rebuilt (undo, duplicate, import, arrays).
 *
 * Recipe: {"shape","size":[x,y,z],"detail","pivot","profile","path","height","angle","closed","thickness",
 *          "ops":[{"op","tool":{recipe},"location","rotation","scale"}],"material","color"}
 */
namespace RuntimePrimitives
{
	const TArray<FString>& ShapeNames();
	/** Palette names -> base colors (applied to the engine's basic shape material). */
	const TArray<TPair<FString, FLinearColor>>& Palette();

	/** Rebuilds Mesh from the recipe (shape + boolean ops). */
	bool BuildMesh(UDynamicMesh* Mesh, const TSharedRef<FJsonObject>& Recipe, FString& OutError);

	/** Applies the recipe's material/color to a builder actor. */
	bool ApplyMaterial(AActor* Actor, const TSharedRef<FJsonObject>& Recipe, FString& OutError);

	/** Rebuilds mesh, material and collision of an existing builder actor. */
	bool Rebuild(AActor* Actor, const TSharedRef<FJsonObject>& Recipe, FString& OutError);

	/** Source spawner for "prim:{...}" (registered with the core). */
	AActor* SpawnFromSource(UWorld* World, const FString& Source, const FTransform& Transform, const FString& Name, FString& OutError);

	FString ToSource(const TSharedRef<FJsonObject>& Recipe);
	/** Recipe of a "prim:" source, or nullptr. */
	TSharedPtr<FJsonObject> FromSource(const FString& Source);

	int32 TriangleCount(const AActor* Actor);
}
