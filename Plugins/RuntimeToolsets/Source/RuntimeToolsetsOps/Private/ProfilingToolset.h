#pragma once

#include "RuntimeToolset.h"

/** Frame timing over a window, hitches, memory, GPU info and Unreal Insights traces. */
class FProfilingToolset final : public IRuntimeToolset
{
public:
	virtual FName GetName() const override { return TEXT("profiling"); }
	virtual FString GetDescription() const override { return TEXT("Frame timing, hitches, memory, GPU info, Insights traces"); }
	virtual FString GetVersion() const override { return TEXT("1.0.0"); }
	virtual void RegisterTools(FRuntimeToolsetBuilder& B) override;
};
