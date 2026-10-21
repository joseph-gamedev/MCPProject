#pragma once

#include "CoreMinimal.h"
#include "RuntimeToolsetDefinition.h"
#include "RuntimeSkillsToolset.generated.h"

/** Detailed information about one skill. */
USTRUCT()
struct FRuntimeSkillDetails
{
	GENERATED_BODY()

	/** How to approach the task: rules, numbers, workflow (markdown). */
	UPROPERTY()
	FString Instructions;

	/** Toolsets the skill is about. */
	UPROPERTY()
	TArray<FString> Toolsets;
};

/**
 * Skills: authoritative guidance for kinds of work (outdoor lighting, art styles, PCG graphs, building, game logic).
 * Call list_skills when starting a task, then get_skills for the ones that apply, and follow them.
 * Projects add their own as Blueprint/C++ skills (URuntimeAgentSkill) or markdown files in Config/RuntimeToolsets/Skills
 * (front matter "description: ..." then the instructions).
 */
UCLASS()
class URuntimeSkillsToolset : public URuntimeToolsetDefinition
{
	GENERATED_BODY()

public:
	/**
	 * Lists every skill with a one-line description of when to apply it.
	 *
	 * @return Skill name -> description.
	 */
	UFUNCTION(meta = (AICallable), Category = "Skills")
	static TMap<FString, FString> ListSkills();

	/**
	 * Returns the full instructions of the given skills.
	 *
	 * @param SkillNames - Names from list_skills, e.g. ["outdoor_lighting", "art_style"].
	 * @return Skill name -> details.
	 */
	UFUNCTION(meta = (AICallable), Category = "Skills")
	static TMap<FString, FRuntimeSkillDetails> GetSkills(const TArray<FString>& SkillNames);
};
