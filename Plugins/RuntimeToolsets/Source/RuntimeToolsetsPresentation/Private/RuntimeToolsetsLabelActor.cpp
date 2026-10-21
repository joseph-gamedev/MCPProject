#include "RuntimeToolsetsLabelActor.h"
#include "RuntimeToolsetsHudWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/TextBlock.h"
#include "Components/WidgetComponent.h"
#include "Engine/World.h"

ARuntimeToolsetsLabelActor::ARuntimeToolsetsLabelActor()
{
	PrimaryActorTick.bCanEverTick = false;
	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Root->SetMobility(EComponentMobility::Movable);
	SetRootComponent(Root);
	Widget = CreateDefaultSubobject<UWidgetComponent>(TEXT("Label"));
	Widget->SetupAttachment(Root);
	Widget->SetWidgetSpace(EWidgetSpace::Screen); // always faces the camera, constant pixel size
	Widget->SetDrawAtDesiredSize(true);
	Widget->SetPivot(FVector2D(0.5f, 1.0f)); // text sits just above the anchor point
	Widget->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

bool ARuntimeToolsetsLabelActor::SetLabel(const FString& Text, float Size, const FLinearColor& Color, bool bBackground)
{
	LabelText = Text;
	UWorld* World = GetWorld();
	if (!World || !World->GetGameInstance())
	{
		return false;
	}
	URuntimeToolsetsHudWidget* W = CreateWidget<URuntimeToolsetsHudWidget>(World->GetGameInstance(), URuntimeToolsetsHudWidget::StaticClass());
	if (!W || !W->WidgetTree)
	{
		return false;
	}
	UTextBlock* TextBlock = W->WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Text"));
	TextBlock->SetText(FText::FromString(Text));
	TextBlock->SetFontSize(Size);
	TextBlock->SetColorAndOpacity(FSlateColor(Color));
	TextBlock->SetJustification(ETextJustify::Center);
	if (bBackground)
	{
		UBorder* Border = W->WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Background"));
		Border->SetBrushColor(FLinearColor(0.f, 0.f, 0.f, 0.55f));
		Border->SetPadding(FMargin(Size * 0.5f, Size * 0.2f));
		Border->SetContent(TextBlock);
		W->WidgetTree->RootWidget = Border;
	}
	else
	{
		W->WidgetTree->RootWidget = TextBlock;
	}
	Widget->SetWidget(W);
	return true;
}
