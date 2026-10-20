#pragma once

#include "RuntimeToolset.h"

/** Screenshots, render-target readback and off-screen scene captures, returned as image attachments. */
class FCaptureToolset final : public IRuntimeToolset
{
public:
	virtual FName GetName() const override { return TEXT("capture"); }
	virtual FString GetDescription() const override { return TEXT("Screenshots, render targets and scene captures as JPEG/PNG"); }
	virtual FString GetVersion() const override { return TEXT("1.0.0"); }
	virtual void RegisterTools(FRuntimeToolsetBuilder& B) override;
};
