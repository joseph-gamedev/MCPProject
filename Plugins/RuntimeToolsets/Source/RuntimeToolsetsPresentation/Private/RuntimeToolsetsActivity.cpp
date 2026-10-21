#include "RuntimeToolsetsActivity.h"
#include "RuntimeToolsetsCore.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/DefaultPawn.h"
#include "GameFramework/FloatingPawnMovement.h"
#include "GameFramework/PlayerController.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Styling/CoreStyle.h"

namespace
{
	constexpr double IdleAfter = 2.5;   // seconds without calls before the run counts as done
	constexpr double DoneShownFor = 5.0; // "Done" stays on screen this long
	constexpr int32 FeedSize = 5;

	/** "build.create_primitive" -> "build: create primitive" */
	FString Friendly(const FString& Tool)
	{
		FString Set, Name;
		if (!Tool.Split(TEXT("."), &Set, &Name) && !Tool.Split(TEXT("_"), &Set, &Name))
		{
			return Tool;
		}
		return Set + TEXT(": ") + Name.Replace(TEXT("_"), TEXT(" "));
	}

	FString Clock(double Seconds)
	{
		const int32 S = FMath::Max(0, FMath::FloorToInt(Seconds));
		return S >= 60 ? FString::Printf(TEXT("%dm %02ds"), S / 60, S % 60) : FString::Printf(TEXT("%ds"), S);
	}
}

FRuntimeToolsetsActivity& FRuntimeToolsetsActivity::Get()
{
	static FRuntimeToolsetsActivity Instance;
	return Instance;
}

void FRuntimeToolsetsActivity::Startup()
{
	ActivityHandle = IRuntimeToolsetsCore::Get().OnActivity().AddRaw(this, &FRuntimeToolsetsActivity::OnActivity);
	TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FRuntimeToolsetsActivity::Tick));
}

void FRuntimeToolsetsActivity::Shutdown()
{
	FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
	if (IRuntimeToolsetsCore::IsAvailable())
	{
		IRuntimeToolsetsCore::Get().OnActivity().Remove(ActivityHandle);
	}
	OverlayViewport.Reset();
	if (UGameViewportClient* VP = GEngine ? GEngine->GameViewport : nullptr; VP && Overlay)
	{
		VP->RemoveViewportWidgetContent(Overlay.ToSharedRef());
	}
	Overlay.Reset();
}

void FRuntimeToolsetsActivity::SetStatus(const FString& Text)
{
	Status = Text.Left(160);
	LastActivity = Now;
}

void FRuntimeToolsetsActivity::OnActivity(const FString& Tool, bool bStarted, bool bOk, double Seconds)
{
	// Meta tools wrap the real call; the nested call reports itself.
	const bool bMeta = Tool.EndsWith(TEXT("call_tool")) || Tool.EndsWith(TEXT("run_batch"));
	if (Now - LastActivity > IdleAfter && InFlight == 0)
	{
		// New run.
		RunStart = Now;
		Steps = 0;
		Errors = 0;
		Feed.Reset();
	}
	LastActivity = Now;
	if (bStarted)
	{
		++InFlight;
		if (!bMeta)
		{
			CurrentTool = Friendly(Tool);
		}
		return;
	}
	InFlight = FMath::Max(0, InFlight - 1);
	if (bMeta)
	{
		return;
	}
	++Steps;
	Errors += bOk ? 0 : 1;
	Feed.Insert(FString::Printf(TEXT("%s %s  %.0f ms"), bOk ? TEXT("✓") : TEXT("✗"), *Friendly(Tool), Seconds * 1000.0), 0);
	if (Feed.Num() > FeedSize)
	{
		Feed.SetNum(FeedSize);
	}
}

bool FRuntimeToolsetsActivity::Tick(float DeltaTime)
{
	Now = FPlatformTime::Seconds();
	EnsureOverlay();
	TickFlySpeed(DeltaTime);
	return true;
}

void FRuntimeToolsetsActivity::EnsureOverlay()
{
	UGameViewportClient* VP = GEngine ? GEngine->GameViewport : nullptr;
	if (!bPanelEnabled || !VP || OverlayViewport.Get() == VP || !FSlateApplication::IsInitialized())
	{
		return;
	}
	OverlayViewport = VP;
	const FSlateFontInfo Big = FCoreStyle::GetDefaultFontStyle("Bold", 15);
	const FSlateFontInfo Small = FCoreStyle::GetDefaultFontStyle("Regular", 11);
	Overlay = SNew(SBox)
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Top)
		.Padding(FMargin(16.f, 16.f))
		.Visibility(EVisibility::HitTestInvisible)
		[
			SNew(SBorder)
			.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
			.BorderBackgroundColor(FLinearColor(0.f, 0.f, 0.f, 0.55f))
			.ColorAndOpacity_Lambda([this]() { return FLinearColor(1.f, 1.f, 1.f, PanelOpacity()); })
			.Padding(FMargin(12.f, 8.f))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
					[
						SNew(STextBlock).Font(Big).Text_Lambda([this]() { return DotText(); }).ColorAndOpacity_Lambda([this]() { return DotColor(); })
					]
					+ SHorizontalBox::Slot().AutoWidth()
					[
						SNew(STextBlock).Font(Big).Text_Lambda([this]() { return HeadlineText(); })
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 2, 0, 0)
				[
					SNew(STextBlock).Font(Small).ColorAndOpacity(FLinearColor(1.f, 0.85f, 0.4f))
					.Text_Lambda([this]() { return StatusText(); })
					.Visibility_Lambda([this]() { return Status.IsEmpty() ? EVisibility::Collapsed : EVisibility::HitTestInvisible; })
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 0)
				[
					SNew(STextBlock).Font(Small).ColorAndOpacity(FLinearColor(0.8f, 0.8f, 0.8f)).Text_Lambda([this]() { return FeedText(); })
				]
			]
		];
	VP->AddViewportWidgetContent(Overlay.ToSharedRef(), 1000);
}

float FRuntimeToolsetsActivity::PanelOpacity() const
{
	if (!bPanelEnabled || Steps == 0 && InFlight == 0 && Status.IsEmpty())
	{
		return 0.f;
	}
	const double Idle = Now - LastActivity;
	if (InFlight > 0 || Idle < IdleAfter + DoneShownFor)
	{
		return 1.f;
	}
	return static_cast<float>(FMath::Clamp(1.0 - (Idle - IdleAfter - DoneShownFor), 0.0, 1.0));
}

FText FRuntimeToolsetsActivity::DotText() const
{
	return FText::FromString(InFlight > 0 || Now - LastActivity < IdleAfter ? TEXT("●") : TEXT("✓"));
}

FSlateColor FRuntimeToolsetsActivity::DotColor() const
{
	if (InFlight > 0 || Now - LastActivity < IdleAfter)
	{
		const float Pulse = 0.5f + 0.5f * FMath::Sin(static_cast<float>(Now) * 6.f);
		return FLinearColor(0.2f, 0.8f + 0.2f * Pulse, 1.f, 0.4f + 0.6f * Pulse);
	}
	return FLinearColor(0.3f, 1.f, 0.4f);
}

FText FRuntimeToolsetsActivity::HeadlineText() const
{
	const bool bWorking = InFlight > 0 || Now - LastActivity < IdleAfter;
	const FString Err = Errors > 0 ? FString::Printf(TEXT(", %d failed"), Errors) : FString();
	if (bWorking)
	{
		return FText::FromString(FString::Printf(TEXT("AI working — %s   (step %d%s, %s)"), *CurrentTool, Steps + 1, *Err, *Clock(Now - RunStart)));
	}
	return FText::FromString(FString::Printf(TEXT("AI done — %d steps%s in %s"), Steps, *Err, *Clock(LastActivity - RunStart)));
}

FText FRuntimeToolsetsActivity::StatusText() const
{
	return FText::FromString(Status);
}

FText FRuntimeToolsetsActivity::FeedText() const
{
	return FText::FromString(FString::Join(Feed, TEXT("\n")));
}

void FRuntimeToolsetsActivity::TickFlySpeed(float DeltaTime)
{
	UWorld* World = IRuntimeToolsetsCore::IsAvailable() ? IRuntimeToolsetsCore::Get().FindWorld() : nullptr;
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	ADefaultPawn* Pawn = PC ? Cast<ADefaultPawn>(PC->GetPawn()) : nullptr;
	UFloatingPawnMovement* Move = Pawn ? Cast<UFloatingPawnMovement>(Pawn->GetMovementComponent()) : nullptr;
	if (!Move)
	{
		return;
	}
	double Target = Fly.FixedSpeed * Fly.Multiplier;
	if (Fly.bAuto)
	{
		// Distance to the nearest surface in six directions: fly fast in open space, slow near the build.
		const FVector Eye = Pawn->GetPawnViewLocation();
		const FRotator View = PC->GetControlRotation();
		const FVector Dirs[] = {View.Vector(), -View.Vector(), FRotationMatrix(View).GetUnitAxis(EAxis::Y), -FRotationMatrix(View).GetUnitAxis(EAxis::Y),
			FVector::UpVector, -FVector::UpVector};
		FCollisionQueryParams Params(SCENE_QUERY_STAT(RuntimeToolsetsFlySpeed), false, Pawn);
		double Nearest = 200000.0;
		for (const FVector& Dir : Dirs)
		{
			FHitResult Hit;
			if (World->LineTraceSingleByChannel(Hit, Eye, Eye + Dir * Nearest, ECC_Visibility, Params))
			{
				Nearest = FMath::Min(Nearest, static_cast<double>(Hit.Distance));
			}
		}
		Target = FMath::Clamp(Nearest * 1.5, Fly.MinSpeed, Fly.MaxSpeed) * Fly.Multiplier;
	}
	CurrentSpeed = CurrentSpeed <= 0.0 ? Target : FMath::FInterpTo(CurrentSpeed, Target, DeltaTime, 4.0);
	Move->MaxSpeed = static_cast<float>(CurrentSpeed);
	Move->Acceleration = static_cast<float>(CurrentSpeed * 4.0);
	Move->Deceleration = static_cast<float>(CurrentSpeed * 8.0);
}
