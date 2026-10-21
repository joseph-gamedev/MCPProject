#pragma once

#include "RuntimeToolset.h"

/** Orchestration and testing: batches of calls, waiting for conditions, in-process automation tests. */
class FOpsToolset final : public IRuntimeToolset
{
public:
	virtual FName GetName() const override { return TEXT("ops"); }
	virtual FString GetDescription() const override { return TEXT("run_batch, wait_for_condition, wait, and in-process automation tests"); }
	virtual FString GetVersion() const override { return TEXT("1.0.0"); }
	virtual void RegisterTools(FRuntimeToolsetBuilder& B) override;
};
