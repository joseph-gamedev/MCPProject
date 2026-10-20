#pragma once

#include "RuntimeToolset.h"

/** Recent log lines, category subscriptions and frame/memory stats. */
class FLogToolset final : public IRuntimeToolset
{
public:
	virtual FName GetName() const override { return TEXT("log"); }
	virtual FString GetDescription() const override { return TEXT("Tail the log, subscribe to categories, frame and memory stats"); }
	virtual FString GetVersion() const override { return TEXT("1.0.0"); }
	virtual void RegisterTools(FRuntimeToolsetBuilder& B) override;
};
