#pragma once

#include "CoreMinimal.h"

class AActor;
class UWorld;
class USplineComponent;

/** Spline nodes: a spline component plus an optional mesh swept along it (source prefix "spline:"). */
namespace RuntimeSplines
{
	/** Recreates a spline node from its recipe (undo/redo, duplicate, import). */
	AActor* SpawnFromSource(UWorld* World, const FString& Source, const FTransform& Transform, const FString& Name, FString& OutError);
	/** The spline of a spline node's actor (null if none). */
	USplineComponent* FindSpline(const AActor* Actor);
}
