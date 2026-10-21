#pragma once

#include "RuntimeToolset.h"

/** Game logic without code: variables, event rules (click, key, button, timer, ...), on-screen UI, start/reset. */
class FLogicToolset final : public IRuntimeToolset
{
public:
	virtual FName GetName() const override { return TEXT("logic"); }
	virtual FString GetDescription() const override
	{
		return TEXT("Make the scene playable (call logic.get_guide first): variables, rules (when click/key/button/timer/start -> if -> actions), HUD text and buttons, cursor mode, start/restart");
	}
	virtual FString GetVersion() const override { return TEXT("1.0.0"); }
	virtual void RegisterTools(FRuntimeToolsetBuilder& B) override;
};
