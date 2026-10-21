#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "RuntimeAgentSkill.generated.h"

/**
 * A skill: authoritative guidance an agent loads on demand (Epic: UAgentSkill). Agents call skills.list_skills (name ->
 * description) and pull only the skills they need with skills.get_skills.
 *
 * Subclass in C++ (set Description and Instructions in the constructor) or in Blueprint (class defaults). Values live on
 * the class default object, so they survive packaging (unlike doc-comment metadata). Override GeneratePrompt to fill
 * placeholders with live data (Epic's PCG skill lists the available nodes this way).
 *
 * Writing skills (Epic's rules, editor_toolset/skills/unreal_skill_best_practices.py): give knowledge the model does not
 * already have (numbers, rules, diagnostic order, gotchas); state rules plainly; keep it durable and short.
 */
UCLASS(Abstract, Blueprintable, MinimalAPI)
class URuntimeAgentSkill : public UObject
{
	GENERATED_BODY()

public:
	/** One line: what the skill covers and when to apply it ("Apply whenever ..."). */
	UPROPERTY(EditDefaultsOnly, Category = "AgentSkill", meta = (MultiLine = true))
	FString Description;

	/** Toolsets the skill is about (shown with the skill). */
	UPROPERTY(EditDefaultsOnly, Category = "AgentSkill")
	TArray<FName> Toolsets;

	/** Skill name used by skills.get_skills; by default the class name in snake_case without "Runtime" and "Skill". */
	RUNTIMETOOLSETSCORE_API virtual FString GetSkillName() const;

	/** Final instructions (placeholders filled). */
	FString GetInstructions() const { return GeneratePrompt(Instructions); }

protected:
	/** Fills placeholders in the instructions with live data. */
	virtual FString GeneratePrompt(const FString& InitialInstructions) const { return InitialInstructions; }

	/** Detailed guidance (markdown). */
	UPROPERTY(EditDefaultsOnly, Category = "AgentSkill", meta = (MultiLine = true))
	FString Instructions;
};
