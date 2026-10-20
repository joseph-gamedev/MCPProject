#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RuntimeToolsetsGroupActor.generated.h"

/** Empty, non-ticking actor that acts as a scene-graph group (a pivot for its children). */
UCLASS(NotBlueprintable, Transient)
class RUNTIMETOOLSETSCORE_API ARuntimeToolsetsGroupActor : public AActor
{
	GENERATED_BODY()

public:
	ARuntimeToolsetsGroupActor();
};
