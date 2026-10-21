#pragma once

#include "RuntimeToolset.h"

/** Traces, overlaps, simulation on/off, impulses and ragdolls. */
class FPhysicsToolset final : public IRuntimeToolset
{
public:
	virtual FName GetName() const override { return TEXT("physics"); }
	virtual FString GetDescription() const override { return TEXT("Line traces, overlaps, simulate physics, impulses, ragdoll"); }
	virtual FString GetVersion() const override { return TEXT("1.0.0"); }
	virtual void RegisterTools(FRuntimeToolsetBuilder& B) override;
};
