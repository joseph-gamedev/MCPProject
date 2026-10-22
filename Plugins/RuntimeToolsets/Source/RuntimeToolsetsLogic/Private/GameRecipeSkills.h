#pragma once

#include "CoreMinimal.h"
#include "RuntimeAgentSkill.h"
#include "GameRecipeSkills.generated.h"

/** Fixed-screen arcade shooters (Galaga, Space Invaders, Galaxian): playfield, sprites, formations, dives, waves. */
UCLASS()
class URuntimeArcadeShooterSkill : public URuntimeAgentSkill
{
	GENERATED_BODY()

public:
	URuntimeArcadeShooterSkill();
};

/** Good-looking HUDs, menus, inventories and shops with the layout UI (logic.define_ui). */
UCLASS()
class URuntimeGameUiSkill : public URuntimeAgentSkill
{
	GENERATED_BODY()

public:
	URuntimeGameUiSkill();
};

/** Believable, photo-like scenes: real scale, physical light, materials, detail, weathering and a screenshot review loop. */
UCLASS()
class URuntimeRealisticSceneSkill : public URuntimeAgentSkill
{
	GENERATED_BODY()

public:
	URuntimeRealisticSceneSkill();
};

/** The unreal_kit Python package: every tool as a function plus generators for worlds, games, UI, combat and looks. */
UCLASS()
class URuntimePythonKitSkill : public URuntimeAgentSkill
{
	GENERATED_BODY()

public:
	URuntimePythonKitSkill();
};
