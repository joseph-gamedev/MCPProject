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
