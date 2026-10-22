#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class UDynamicMesh;
class UMaterialInterface;

/**
 * Terrain shape for the builder ("shape": "terrain"): a height field of layered noise with hills, flat pads and carved
 * channels (rivers always run downhill along their path; roads follow the ground smoothly). Vertex colour masks: R = wet
 * mud near rivers, G = bare earth (pads, roads). All coordinates in the recipe are local to the terrain's origin.
 */
namespace RuntimeTerrain
{
	bool IsTerrain(const TSharedRef<FJsonObject>& Recipe);

	/** Builds the terrain mesh into Mesh. */
	bool Append(UDynamicMesh* Mesh, const TSharedRef<FJsonObject>& Recipe, FString& OutError);

	/** Water surface along every river carve: [{path: [[x,y,z],...], width}] in local space; min/max ground height. */
	bool Describe(const TSharedRef<FJsonObject>& Recipe, TArray<TSharedPtr<FJsonValue>>& OutRivers, double& OutMinZ, double& OutMaxZ, FString& OutError);

	/** Ground height at a local point (after hills, pads and channels). */
	bool HeightAt(const TSharedRef<FJsonObject>& Recipe, const FVector2D& Local, double& OutZ, FString& OutError);

	/** Terrain material (M_RT_Terrain) with the recipe's biome colours; null if the plugin material is missing. */
	UMaterialInterface* MakeMaterial(const TSharedRef<FJsonObject>& Recipe, UObject* Outer);

	const TArray<FString>& BiomeNames();

	/** River water surface ("shape": "river"): a ribbon along path [[x,y,z],...] (z = water level), width (+ overlap
	 *  under the banks). UV U = metres downstream, V = 0..1 across; vertex colour R = bank foam. No collision. */
	bool IsRiver(const TSharedRef<FJsonObject>& Recipe);
	bool AppendRiver(UDynamicMesh* Mesh, const TSharedRef<FJsonObject>& Recipe, FString& OutError);

	/** Flowing water material (M_RT_River, single layer water) for the recipe's water kind and flow speed. */
	UMaterialInterface* MakeRiverMaterial(const TSharedRef<FJsonObject>& Recipe, UObject* Outer);

	const TArray<FString>& WaterNames();
}
