#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

/**
 * A floating panel: drag it by its title bar, resize it from the bottom-right corner. Position and size live in the
 * owner (they survive the panel being rebuilt, e.g. after a level change).
 */
class SRuntimeDebugPanel final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SRuntimeDebugPanel) {}
		SLATE_ARGUMENT(FVector2D*, Position)
		SLATE_ARGUMENT(FVector2D*, Size)
		SLATE_ARGUMENT(float, TitleHeight)
		SLATE_DEFAULT_SLOT(FArguments, Content)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FCursorReply OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const override;

	static constexpr float Grip = 18.f;
	static constexpr float MinWidth = 360.f;
	static constexpr float MinHeight = 160.f;

private:
	enum class EDrag : uint8 { None, Move, Resize };
	EDrag Hit(const FGeometry& MyGeometry, const FVector2D& Screen) const;

	FVector2D* Position = nullptr;
	FVector2D* Size = nullptr;
	float TitleHeight = 30.f;
	EDrag Drag = EDrag::None;
	FVector2D LastScreen = FVector2D::ZeroVector;
};
