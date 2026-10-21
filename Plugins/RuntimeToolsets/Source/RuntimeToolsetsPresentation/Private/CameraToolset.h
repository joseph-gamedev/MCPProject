#pragma once

#include "RuntimeToolset.h"

/** Player view control: a tool-owned camera, framing, orbiting, possession and view modes. */
class FCameraToolset final : public IRuntimeToolset
{
public:
	virtual FName GetName() const override { return TEXT("camera"); }
	virtual FString GetDescription() const override { return TEXT("Move the player view: set, frame, orbit, reset, possess, view mode"); }
	virtual FString GetVersion() const override { return TEXT("1.0.0"); }
	virtual void RegisterTools(FRuntimeToolsetBuilder& B) override;
};
