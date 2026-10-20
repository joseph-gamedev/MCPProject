#pragma once

#include "RuntimeToolset.h"

/** Generic property/function access on cooked UObjects. The "multiplier" toolset. */
class FReflectionToolset final : public IRuntimeToolset
{
public:
	virtual FName GetName() const override { return TEXT("reflection"); }
	virtual FString GetDescription() const override { return TEXT("Read/write properties and call BlueprintCallable functions on any allowed object"); }
	virtual FString GetVersion() const override { return TEXT("1.0.0"); }
	virtual void RegisterTools(FRuntimeToolsetBuilder& B) override;
};
