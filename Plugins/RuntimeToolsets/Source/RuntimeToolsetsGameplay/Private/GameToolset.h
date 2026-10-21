#pragma once

#include "RuntimeToolset.h"

/** Game framework: state, players, teleport, pause and time dilation. */
class FGameToolset final : public IRuntimeToolset
{
public:
	virtual FName GetName() const override { return TEXT("game"); }
	virtual FString GetDescription() const override { return TEXT("Game state, players, teleport, pause, time dilation"); }
	virtual FString GetVersion() const override { return TEXT("1.0.0"); }
	virtual void RegisterTools(FRuntimeToolsetBuilder& B) override;
};
