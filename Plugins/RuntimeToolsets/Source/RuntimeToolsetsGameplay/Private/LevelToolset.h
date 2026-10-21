#pragma once

#include "RuntimeToolset.h"

/** Levels: list, stream level instances in and out, visibility, data layers, open another map. */
class FLevelToolset final : public IRuntimeToolset
{
public:
	virtual FName GetName() const override { return TEXT("level"); }
	virtual FString GetDescription() const override { return TEXT("List levels, load/unload level instances, visibility, data layers, open map"); }
	virtual FString GetVersion() const override { return TEXT("1.0.0"); }
	virtual void RegisterTools(FRuntimeToolsetBuilder& B) override;
};
