#include "SRuntimeDebugPanel.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SOverlay.h"

void SRuntimeDebugPanel::Construct(const FArguments& InArgs)
{
	Position = InArgs._Position;
	Size = InArgs._Size;
	TitleHeight = InArgs._TitleHeight;
	ChildSlot
	[
		SNew(SBox)
		.WidthOverride_Lambda([this]() { return FOptionalSize(Size->X); })
		.HeightOverride_Lambda([this]() { return FOptionalSize(Size->Y); })
		[
			SNew(SOverlay)
			+ SOverlay::Slot()
			[
				InArgs._Content.Widget
			]
			// resize grip: a small corner mark
			+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Bottom).Padding(3.f)
			[
				SNew(SBox).WidthOverride(Grip - 6.f).HeightOverride(Grip - 6.f)
				[
					SNew(SImage).Image(FCoreStyle::Get().GetBrush("WhiteBrush")).ColorAndOpacity(FLinearColor(1.f, 1.f, 1.f, 0.25f))
				]
			]
		]
	];
}

SRuntimeDebugPanel::EDrag SRuntimeDebugPanel::Hit(const FGeometry& MyGeometry, const FVector2D& Screen) const
{
	const FVector2D Local = MyGeometry.AbsoluteToLocal(Screen);
	const FVector2D Local2 = MyGeometry.GetLocalSize();
	if (Local.X >= Local2.X - Grip && Local.Y >= Local2.Y - Grip)
	{
		return EDrag::Resize;
	}
	return Local.Y <= TitleHeight ? EDrag::Move : EDrag::None;
}

FReply SRuntimeDebugPanel::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return FReply::Unhandled();
	}
	Drag = Hit(MyGeometry, MouseEvent.GetScreenSpacePosition());
	if (Drag == EDrag::None)
	{
		return FReply::Handled(); // clicks on the panel never reach the game
	}
	LastScreen = MouseEvent.GetScreenSpacePosition();
	return FReply::Handled().CaptureMouse(SharedThis(this));
}

FReply SRuntimeDebugPanel::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (Drag == EDrag::None || !HasMouseCapture())
	{
		return FReply::Unhandled();
	}
	// screen pixels -> slate units (DPI scale)
	const float Scale = FMath::Max(MyGeometry.GetAccumulatedLayoutTransform().GetScale(), 0.01f);
	const FVector2D Delta = (MouseEvent.GetScreenSpacePosition() - LastScreen) / Scale;
	LastScreen = MouseEvent.GetScreenSpacePosition();
	if (Drag == EDrag::Move)
	{
		*Position += Delta;
		Position->X = FMath::Max(Position->X, -Size->X + 80.f); // keep a corner reachable
		Position->Y = FMath::Max(Position->Y, 0.f);
	}
	else
	{
		Size->X = FMath::Max(MinWidth, Size->X + Delta.X);
		Size->Y = FMath::Max(MinHeight, Size->Y + Delta.Y);
	}
	return FReply::Handled();
}

FReply SRuntimeDebugPanel::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (Drag == EDrag::None)
	{
		return FReply::Unhandled();
	}
	Drag = EDrag::None;
	return FReply::Handled().ReleaseMouseCapture();
}

FCursorReply SRuntimeDebugPanel::OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const
{
	const EDrag Over = Drag != EDrag::None ? Drag : Hit(MyGeometry, CursorEvent.GetScreenSpacePosition());
	return Over == EDrag::Resize ? FCursorReply::Cursor(EMouseCursor::ResizeSouthEast)
		: Over == EDrag::Move ? FCursorReply::Cursor(EMouseCursor::CardinalCross) : FCursorReply::Unhandled();
}
