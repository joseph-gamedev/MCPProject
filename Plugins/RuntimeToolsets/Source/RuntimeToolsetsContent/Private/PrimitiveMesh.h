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
 * Recipe: {"shape","size":[x,y,z],"detail","pivot","bevel","profile","path","height","angle","closed","thickness",
 *          generator params ("count","taper","beam","levels","rows","cols","opening","arched","margin"),
 *          "ops":[{"op","tool":{recipe},"location","rotation","scale"}],
 *          "material","color","roughness","metallic","emissive","emissive_strength","opacity","variation"}
 */
namespace RuntimePrimitives
{
	const TArray<FString>& ShapeNames();
	/** Physically based material presets (marble, copper, glass, ...). */
	const TArray<FString>& PaletteNames();
	/** Procedural surface styles of the PBR material (brick, blocks, marble, ...). */
	const TArray<FString>& StyleNames();
	/** Recipe fields that describe a material (per part in an assembly). */
	const TArray<FString>& MaterialFieldNames();

	/** Stats of the most recent assembly build (game thread). */
	struct FAssemblyStats
	{
		int32 Parts = 0;
		int32 Triangles = 0;
		int32 Materials = 0;
		int32 CoplanarSeparated = 0;
	};
	const FAssemblyStats& LastAssemblyStats();

	/** Rebuilds Mesh from the recipe (shape + boolean ops). */
	bool BuildMesh(UDynamicMesh* Mesh, const TSharedRef<FJsonObject>& Recipe, FString& OutError);

	/** Checks the recipe's material fields (every part of an assembly) without touching an actor. */
	bool ValidateMaterials(const TSharedRef<FJsonObject>& Recipe, FString& OutError);

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
