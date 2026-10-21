#pragma once

#include "RuntimeToolset.h"

/** Game logic without code: variables, event rules (click, key, button, timer, ...), on-screen UI, start/reset. */
class FLogicToolset final : public IRuntimeToolset
{
public:
	virtual FName GetName() const override { return TEXT("logic"); }
	virtual FString GetDescription() const override
	{
		return TEXT("Make the scene playable (call logic.get_guide first): variables, rules (when click/key/button/timer/touch/start -> if -> actions), HUD, buttons, platformer or top-down player, health, damage zones, weapons, tweens, pools, start/restart");
	}
	virtual FString GetVersion() const override { return TEXT("1.2.0"); }
	virtual void RegisterTools(FRuntimeToolsetBuilder& B) override;
};
