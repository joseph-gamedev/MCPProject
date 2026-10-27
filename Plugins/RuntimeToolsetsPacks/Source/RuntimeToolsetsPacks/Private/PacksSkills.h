#pragma once

#include "CoreMinimal.h"
#include "RuntimeAgentSkill.h"
#include "PacksSkills.generated.h"

/** Water, fluids and beaches (FluidNinja LIVE): which tool for which scene, and the gotchas. */
UCLASS()
class URuntimeWaterFluidsSkill : public URuntimeAgentSkill
{
	GENERATED_BODY()

public:
	URuntimeWaterFluidsSkill();
};

/** The project's nature and architecture packs (RuntimeFoliage, RuntimeArchitecture): real trees, plants, building modules. */
UCLASS()
class URuntimeProjectAssetsSkill : public URuntimeAgentSkill
{
	GENERATED_BODY()

public:
	URuntimeProjectAssetsSkill();
};
