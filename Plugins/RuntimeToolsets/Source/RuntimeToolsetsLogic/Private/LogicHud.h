#pragma once

#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"

class FLogicWorld;

/** Paints world-anchored HUD elements over the viewport: enemy health bars, alert icons, floating numbers, hit flash. */
class SLogicHud : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SLogicHud) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, TWeakPtr<FLogicWorld> InWorld);
	virtual FVector2D ComputeDesiredSize(float) const override;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	TWeakPtr<FLogicWorld> World;
};

/** Rounded white brush (tinted per use) for HUD panels and bars. */
const struct FSlateBrush* LogicRoundedBrush();
