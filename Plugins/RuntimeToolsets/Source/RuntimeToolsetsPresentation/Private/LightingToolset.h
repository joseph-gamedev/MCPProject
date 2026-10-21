#pragma once

#include "RuntimeToolset.h"

/** Sun, sky light, height fog, post process, time of day and mood presets. */
class FLightingToolset final : public IRuntimeToolset
{
public:
	virtual FName GetName() const override { return TEXT("lighting"); }
	virtual FString GetDescription() const override { return TEXT("Sun, sky, fog, post process, time of day and mood presets"); }
	virtual FString GetVersion() const override { return TEXT("1.0.0"); }
	virtual void RegisterTools(FRuntimeToolsetBuilder& B) override;
};
