#pragma once

#include "RuntimeToolset.h"

/** Console commands, console variables, scalability and game user settings. */
class FConsoleToolset final : public IRuntimeToolset
{
public:
	virtual FName GetName() const override { return TEXT("console"); }
	virtual FString GetDescription() const override { return TEXT("Console commands, cvars, scalability and user settings"); }
	virtual FString GetVersion() const override { return TEXT("1.0.0"); }
	virtual void RegisterTools(FRuntimeToolsetBuilder& B) override;
};
