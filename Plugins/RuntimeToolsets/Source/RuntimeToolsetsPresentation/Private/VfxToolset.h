#pragma once

#include "RuntimeToolset.h"

/** Niagara effects as scene nodes: spawn, user parameters, activate/deactivate, list active. */
class FVfxToolset final : public IRuntimeToolset
{
public:
	virtual FName GetName() const override { return TEXT("vfx"); }
	virtual FString GetDescription() const override { return TEXT("Spawn Niagara systems, set user parameters, activate/deactivate"); }
	virtual FString GetVersion() const override { return TEXT("1.0.0"); }
	virtual void RegisterTools(FRuntimeToolsetBuilder& B) override;

	/** Source spawner for "vfx:<system path>" nodes (duplicate / import / arrays). */
	static AActor* SpawnFromSource(UWorld* World, const FString& Source, const FTransform& Transform, const FString& Name, FString& OutError);
};
