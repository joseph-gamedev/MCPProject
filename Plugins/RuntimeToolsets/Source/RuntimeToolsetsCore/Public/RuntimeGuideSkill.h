#pragma once

#include "CoreMinimal.h"
#include "RuntimeAgentSkill.h"
#include "RuntimeGuideSkill.generated.h"

/**
 * A skill whose instructions come from a toolset's get_guide tool (one source of truth for both).
 * Subclasses set GuideTool, Description and Toolsets.
 */
UCLASS(Abstract, MinimalAPI)
class URuntimeGuideSkill : public URuntimeAgentSkill
{
	GENERATED_BODY()

protected:
	/** Qualified tool returning {"guide": "..."} (e.g. "build.get_guide"). */
	FString GuideTool;

	RUNTIMETOOLSETSCORE_API virtual FString GeneratePrompt(const FString& InitialInstructions) const override;
};
