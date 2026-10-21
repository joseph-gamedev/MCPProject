#pragma once

#include "RuntimeToolset.h"

/** Arrange many objects at once: align, distribute, snap, drop to ground, rows/grids/circles, jitter, free space. */
class FLayoutToolset final : public IRuntimeToolset
{
public:
	virtual FName GetName() const override { return TEXT("layout"); }
	virtual FString GetDescription() const override { return TEXT("Align, distribute, snap, drop to ground, arrange and find free space"); }
	virtual FString GetVersion() const override { return TEXT("1.0.0"); }
	virtual void RegisterTools(FRuntimeToolsetBuilder& B) override;
};
