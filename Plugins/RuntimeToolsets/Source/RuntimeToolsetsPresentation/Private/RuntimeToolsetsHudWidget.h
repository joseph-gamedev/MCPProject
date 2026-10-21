#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "RuntimeToolsetsHudWidget.generated.h"

/** Concrete, asset-free user widget. ui.create_hud_text fills its tree (canvas + text) at runtime. */
UCLASS(NotBlueprintable, Transient)
class URuntimeToolsetsHudWidget : public UUserWidget
{
	GENERATED_BODY()
};
