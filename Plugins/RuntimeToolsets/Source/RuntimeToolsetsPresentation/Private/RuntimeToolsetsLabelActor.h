#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RuntimeToolsetsLabelActor.generated.h"

class UWidgetComponent;

/** A name tag in the world: screen-facing UMG text drawn at the actor's location (ui.create_label). */
UCLASS(NotBlueprintable, Transient)
class ARuntimeToolsetsLabelActor : public AActor
{
	GENERATED_BODY()

public:
	ARuntimeToolsetsLabelActor();

	/** Builds (or rebuilds) the label widget. Returns false when no widget can be created (no game instance). */
	bool SetLabel(const FString& Text, float Size, const FLinearColor& Color, bool bBackground);

	FString GetLabelText() const { return LabelText; }

private:
	UPROPERTY()
	TObjectPtr<UWidgetComponent> Widget;

	FString LabelText;
};
