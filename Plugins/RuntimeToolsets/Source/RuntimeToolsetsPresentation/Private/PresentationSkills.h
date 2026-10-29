#pragma once

#include "CoreMinimal.h"
#include "RuntimeAgentSkill.h"
#include "PresentationSkills.generated.h"

/** Outdoor lighting rules (adapted from Epic's editor skill default_outdoor_lighting). */
UCLASS()
class URuntimeOutdoorLightingSkill : public URuntimeAgentSkill
{
	GENERATED_BODY()

public:
	URuntimeOutdoorLightingSkill();
};

/** Placing Niagara effects (adapted from Epic's Niagara placement and fundamentals skills). */
UCLASS()
class URuntimeVfxPlacementSkill : public URuntimeAgentSkill
{
	GENERATED_BODY()

public:
	URuntimeVfxPlacementSkill();
};

/** Choosing and tuning an art style. */
UCLASS()
class URuntimeArtStyleSkill : public URuntimeAgentSkill
{
	GENERATED_BODY()

public:
	URuntimeArtStyleSkill();

protected:
	virtual FString GeneratePrompt(const FString& InitialInstructions) const override;
};


/** Rebuilding anything from a reference image: a generic analyse, block out, compare and fix loop, time-boxed. */
UCLASS()
class URuntimeBuildFromImageSkill : public URuntimeAgentSkill
{
	GENERATED_BODY()

public:
	URuntimeBuildFromImageSkill();
};
