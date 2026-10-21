#pragma once

#include "CoreMinimal.h"
#include "RuntimeSceneTypes.generated.h"

class AActor;
class FToolInvocation;
struct FRuntimeSceneNode;

/** A scene-graph node (an actor or group created by the tools). */
USTRUCT()
struct FRuntimeNodeInfo
{
	GENERATED_BODY()

	/** Node id, e.g. "n12". Pass it to any tool that takes an actor or a node. */
	UPROPERTY()
	FString Id;

	/** Node name, unique among its siblings. */
	UPROPERTY()
	FString Name;

	/** Path through node names from the root, e.g. "house/door" (also accepted wherever a node is expected). */
	UPROPERTY()
	FString Path;

	/** Engine object name of the actor (for actor:Name references). */
	UPROPERTY()
	FString Actor;
};

/** World transform of an actor after a change. */
USTRUCT()
struct FRuntimeTransformResult
{
	GENERATED_BODY()

	/** The changed actor (node id or actor:Name). */
	UPROPERTY()
	FString Target;

	/** World location in cm. */
	UPROPERTY()
	FVector Location = FVector::ZeroVector;

	/** World rotation [pitch, yaw, roll] in degrees. */
	UPROPERTY()
	FRotator Rotation = FRotator::ZeroRotator;

	/** World scale. */
	UPROPERTY()
	FVector Scale = FVector::OneVector;

	/** Set when the change has a side effect worth knowing (e.g. shearing children). */
	UPROPERTY()
	FString Warning;
};

/** World-space axis-aligned box. */
USTRUCT()
struct FRuntimeBox
{
	GENERATED_BODY()

	/** Minimum corner [x, y, z] in cm. */
	UPROPERTY()
	FVector Min = FVector::ZeroVector;

	/** Maximum corner [x, y, z] in cm. */
	UPROPERTY()
	FVector Max = FVector::ZeroVector;
};

namespace RuntimeSceneTypes
{
	/** Node summary for results (id, name, path, actor name). */
	FRuntimeNodeInfo MakeNodeInfo(const FString& NodeId);

	/** Transform result for an actor. */
	FRuntimeTransformResult MakeTransformResult(const AActor* Actor);
}
