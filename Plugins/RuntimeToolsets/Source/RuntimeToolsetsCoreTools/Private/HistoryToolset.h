#pragma once

#include "RuntimeToolset.h"

/** Per-session undo/redo, transactions and checkpoints. */
class FHistoryToolset final : public IRuntimeToolset
{
public:
	virtual FName GetName() const override { return TEXT("history"); }
	virtual FString GetDescription() const override { return TEXT("Undo/redo, transactions, checkpoints and rollback (per session)"); }
	virtual FString GetVersion() const override { return TEXT("1.0.0"); }
	virtual void RegisterTools(FRuntimeToolsetBuilder& B) override;
};
