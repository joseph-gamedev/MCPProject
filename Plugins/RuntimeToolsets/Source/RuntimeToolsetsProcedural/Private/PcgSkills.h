#pragma once

#include "CoreMinimal.h"
#include "RuntimeAgentSkill.h"
#include "PcgSkills.generated.h"

/** PCG graph generation (adapted from Epic's PCGGraphGeneration skill), with live node and graph lists. */
UCLASS()
class URuntimePcgGraphGenerationSkill : public URuntimeAgentSkill
{
	GENERATED_BODY()

public:
	URuntimePcgGraphGenerationSkill();

protected:
	virtual FString GeneratePrompt(const FString& InitialInstructions) const override;
};
