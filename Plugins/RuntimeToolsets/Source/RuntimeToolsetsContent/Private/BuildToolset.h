#pragma once

#include "RuntimeToolset.h"

/** Primitive builder: parametric meshes, profile solids, booleans, arrays, mirror, palette materials, checks, ortho views. */
class FBuildToolset final : public IRuntimeToolset
{
public:
	virtual FName GetName() const override { return TEXT("build"); }
	virtual FString GetDescription() const override { return TEXT("Build realistic landmarks (call build.get_guide first): assemblies of 500+ parts per call, loft/revolve/sweep/extrude/raw, generators, procedural PBR materials, booleans, ortho views"); }
	virtual FString GetVersion() const override { return TEXT("1.2.0"); }
	virtual void RegisterTools(FRuntimeToolsetBuilder& B) override;
};
