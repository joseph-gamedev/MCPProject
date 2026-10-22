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
 * (front matter "description: ..." then the instructions). Agents save what they learn with save_skill.
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
	 * Returns the full instructions of the given skills. Skills can be long (game_logic is the whole logic guide): load
	 * one or two per call.
	 *
	 * @param SkillNames - Names from list_skills, e.g. ["outdoor_lighting", "art_style"].
	 * @param Names - Same as skill_names (either works).
	 * @return Skill name -> details. Fails if no name is given or a skill does not exist (the error lists the skills).
	 */
	UFUNCTION(meta = (AICallable, AutoCreateRefTerm = "SkillNames,Names"), Category = "Skills")
	static TMap<FString, FRuntimeSkillDetails> GetSkills(const TArray<FString>& SkillNames, const TArray<FString>& Names);

	/**
	 * Saves a skill for future agents (a markdown file in Config/RuntimeToolsets/Skills; saving the same name updates it).
	 * Do this after finishing a task that needed several fixes: write the working recipe (order of steps, exact argument
	 * shapes that worked, numbers, gotchas and how you verified it), not a story of the session.
	 *
	 * @param Name - snake_case name, e.g. "breakout_game".
	 * @param Description - One line starting "Apply when ...", shown by list_skills so agents know when to load it.
	 * @param Instructions - The recipe in markdown.
	 * @param Toolsets - Toolsets it uses, e.g. ["logic", "build"].
	 * @return Path of the saved file. Fails if the name is not snake_case, belongs to a built-in skill, or the text is empty.
	 */
	UFUNCTION(meta = (AICallable), Category = "Skills")
	static FString SaveSkill(const FString& Name, const FString& Description, const FString& Instructions, const TArray<FString>& Toolsets);

	/**
	 * Deletes a skill saved with save_skill (built-in skills cannot be deleted).
	 *
	 * @param Name - The skill name.
	 */
	UFUNCTION(meta = (AICallable, Irreversible), Category = "Skills")
	static void DeleteSkill(const FString& Name);
};
