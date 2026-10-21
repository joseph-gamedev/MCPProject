#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class AActor;
class UWorld;
class UPCGComponent;

/** PCG helpers shared by the pcg toolset. */
namespace RuntimePcg
{
	/** Runs a PCG component's graph to completion on this thread (PCG normally spreads work over frames). */
	bool Run(UWorld* World, UPCGComponent* Component, FString& OutError);
	/** Recreates a scatter node from its recipe (source prefix "scatter:"). */
	AActor* SpawnScatterFromSource(UWorld* World, const FString& Source, const FTransform& Transform, const FString& Name, FString& OutError);
	/** Counts from the last generation of a scatter node. */
	TSharedPtr<FJsonObject> ScatterStats(const AActor* Scatter);
	/** Recreates a graph instance volume (source prefix "pcginst:"). */
	AActor* SpawnInstanceFromSource(UWorld* World, const FString& Source, const FTransform& Transform, const FString& Name, FString& OutError);
	/** JSON converters for PCG types (attribute selectors). */
	void RegisterConverters();
	void UnregisterConverters();
}
