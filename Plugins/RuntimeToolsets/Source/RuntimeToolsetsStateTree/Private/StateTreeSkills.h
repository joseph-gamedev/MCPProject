#pragma once

#include "CoreMinimal.h"
#include "RuntimeAgentSkill.h"
#include "StateTreeSkills.generated.h"

/** StateTree at runtime: when to use it and how to drive a tree on an actor. */
UCLASS()
class URuntimeStateTreeSkill : public URuntimeAgentSkill
{
	GENERATED_BODY()

public:
	URuntimeStateTreeSkill();
};
