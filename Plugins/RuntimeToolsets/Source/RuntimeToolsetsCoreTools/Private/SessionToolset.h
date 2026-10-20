#pragma once

#include "RuntimeToolset.h"

/** Discovery and health: ping, build info, capabilities, progressive toolset discovery, call_tool. Always enabled. */
class FSessionToolset final : public IRuntimeToolset
{
public:
	virtual FName GetName() const override { return TEXT("session"); }
	virtual FString GetDescription() const override { return TEXT("Health, build info, toolset discovery and call_tool"); }
	virtual FString GetVersion() const override { return TEXT("1.0.0"); }
	virtual void RegisterTools(FRuntimeToolsetBuilder& B) override;
};
