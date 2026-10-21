#pragma once

#include "CoreMinimal.h"
#include "RuntimeGuideSkill.h"
#include "GameLogicSkill.generated.h"

/** Making a scene playable (the logic toolset's guide). */
UCLASS()
class URuntimeGameLogicSkill : public URuntimeGuideSkill
{
	GENERATED_BODY()

public:
	URuntimeGameLogicSkill()
	{
		Description = TEXT("Apply whenever making a scene playable: variables, rules, events, UI, players, combat, AI, spawners, saves.");
		Toolsets = {TEXT("logic")};
		GuideTool = TEXT("logic.get_guide");
	}
};
