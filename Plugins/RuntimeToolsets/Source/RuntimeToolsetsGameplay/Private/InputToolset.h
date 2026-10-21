#pragma once

#include "RuntimeToolset.h"

/** Drive the local player: inject keys and Enhanced Input actions, add/remove mapping contexts. */
class FInputToolset final : public IRuntimeToolset
{
public:
	virtual FName GetName() const override { return TEXT("input"); }
	virtual FString GetDescription() const override { return TEXT("Inject keys and Enhanced Input actions; add/remove mapping contexts"); }
	virtual FString GetVersion() const override { return TEXT("1.0.0"); }
	virtual void RegisterTools(FRuntimeToolsetBuilder& B) override;
};
