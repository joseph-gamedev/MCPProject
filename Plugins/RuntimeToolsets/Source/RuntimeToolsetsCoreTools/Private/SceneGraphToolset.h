#pragma once

#include "RuntimeToolset.h"

/** Hierarchy of tool-created nodes: groups, transforms, reparenting, duplicate, delete, export/import. */
class FSceneGraphToolset final : public IRuntimeToolset
{
public:
	virtual FName GetName() const override { return TEXT("scene"); }
	virtual FString GetDescription() const override { return TEXT("Scene graph of tool-created nodes: groups, hierarchy, transforms, export/import"); }
	virtual FString GetVersion() const override { return TEXT("1.0.0"); }
	virtual void RegisterTools(FRuntimeToolsetBuilder& B) override;
};
