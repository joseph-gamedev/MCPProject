#pragma once

#include "RuntimeToolset.h"

/** Material slots and parameters on live meshes, plus material parameter collections. */
class FMaterialsToolset final : public IRuntimeToolset
{
public:
	virtual FName GetName() const override { return TEXT("materials"); }
	virtual FString GetDescription() const override { return TEXT("Inspect and change materials and material parameters at runtime"); }
	virtual FString GetVersion() const override { return TEXT("1.0.0"); }
	virtual void RegisterTools(FRuntimeToolsetBuilder& B) override;
};
