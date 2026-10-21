#pragma once

#include "RuntimeToolset.h"

/** Live UMG control: list/find widgets, HUD text without assets, set text/visibility/images, click buttons. */
class FUiToolset final : public IRuntimeToolset
{
public:
	virtual FName GetName() const override { return TEXT("ui"); }
	virtual FString GetDescription() const override { return TEXT("List, find, create and change on-screen UMG widgets"); }
	virtual FString GetVersion() const override { return TEXT("1.2.0"); }
	virtual void RegisterTools(FRuntimeToolsetBuilder& B) override;

	/** Source spawner for "label:{text,size,color,background}" nodes. */
	static AActor* SpawnLabelFromSource(UWorld* World, const FString& Source, const FTransform& Transform, const FString& Name, FString& OutError);
};
