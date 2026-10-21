#pragma once

#include "CoreMinimal.h"
#include "RuntimeGuideSkill.h"
#include "BuilderSkill.generated.h"

/** Building objects and scenes from parts (the build toolset's guide). */
UCLASS()
class URuntimeBuilderSkill : public URuntimeGuideSkill
{
	GENERATED_BODY()

public:
	URuntimeBuilderSkill()
	{
		Description = TEXT("Apply whenever building objects, buildings, landmarks or scenes from parts: real-scale dimensions, generators, materials.");
		Toolsets = {TEXT("build"), TEXT("layout"), TEXT("scene")};
		GuideTool = TEXT("build.get_guide");
	}
};
