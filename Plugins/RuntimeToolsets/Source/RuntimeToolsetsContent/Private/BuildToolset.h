#pragma once

#include "RuntimeToolset.h"

/** Primitive builder: parametric meshes, profile solids, booleans, arrays, mirror, palette materials, checks, ortho views. */
class FBuildToolset final : public IRuntimeToolset
{
public:
	virtual FName GetName() const override { return TEXT("build"); }
	virtual FString GetDescription() const override { return TEXT("Block out landmarks: primitives, lathe/extrude/sweep, booleans, arrays, mirror, ortho views"); }
	virtual FString GetVersion() const override { return TEXT("1.0.0"); }
	virtual void RegisterTools(FRuntimeToolsetBuilder& B) override;
};
