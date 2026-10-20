#pragma once

#include "RuntimeToolset.h"

/** Spawn, find, move, attach, hide and inspect actors. Spawned actors become scene-graph nodes. */
class FActorsToolset final : public IRuntimeToolset
{
public:
	virtual FName GetName() const override { return TEXT("actors"); }
	virtual FString GetDescription() const override { return TEXT("Spawn, destroy, find, transform, attach and hide actors"); }
	virtual FString GetVersion() const override { return TEXT("1.0.0"); }
	virtual void RegisterTools(FRuntimeToolsetBuilder& B) override;
};
