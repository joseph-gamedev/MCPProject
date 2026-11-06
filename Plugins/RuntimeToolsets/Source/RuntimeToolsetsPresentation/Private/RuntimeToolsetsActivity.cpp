#include "RuntimeToolsetsActivity.h"
#include "RuntimeToolsetsCore.h"
#include "RuntimeSceneGraph.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/Pawn.h"
#include "RuntimeToolHelpers.h"
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
#include "Widgets/Notifications/SProgressBar.h"
#include "Styling/CoreStyle.h"

namespace
{
	constexpr double IdleAfter = 2.5;   // seconds without calls before "designing the next stage" shows
	constexpr double DesignFor = 300.0; // an agent that has been quiet this long is done
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
	Placeholder.Clear();
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
	if (Text != Status)
	{
		StatusSince = Now;
		bStageLanded = false;
	}
	Status = Text.Left(160);
	LastActivity = Now;
}

bool FRuntimeToolsetsActivity::IsActive() const
{
	return InFlight > 0 || (Now - LastActivity < DesignFor && !Status.StartsWith(TEXT("Done")));
}

TOptional<float> FRuntimeToolsetsActivity::Progress() const
{
	if (Status.StartsWith(TEXT("Done")))
	{
		return 1.f;
	}
	// "Stage 2 of 4": two stages' worth of the bar, the current one creeping while it is written
	int32 Stage = 0, Stages = 0;
	FString Lower = Status.ToLower();
	int32 Of = Lower.Find(TEXT(" of "));
	if (Of != INDEX_NONE)
	{
		int32 i = Of - 1;
		while (i >= 0 && FChar::IsDigit(Lower[i])) --i;
		Stage = FCString::Atoi(*Lower.Mid(i + 1, Of - i - 1));
		Stages = FCString::Atoi(*Lower.Mid(Of + 4));
	}
	const double Since = FMath::Max(0.0, Now - StatusSince);
	const float Stage01 = bStageLanded ? 1.f : static_cast<float>(0.92 * (1.0 - FMath::Exp(-Since / 45.0)));
	if (Stages > 0 && Stage >= 1 && Stage <= Stages)
	{
		return FMath::Clamp((Stage - 1 + Stage01) / Stages, 0.f, 1.f);
	}
	return Status.IsEmpty() && Steps == 0 ? TOptional<float>() : TOptional<float>(Stage01);
}

void FRuntimeToolsetsActivity::OnActivity(const FString& Tool, bool bStarted, bool bOk, double Seconds)
{
	// Meta tools wrap the real call; the nested call reports itself.
	const bool bMeta = Tool.EndsWith(TEXT("call_tool")) || Tool.EndsWith(TEXT("run_batch"));
	if ((Now - LastActivity > DesignFor || Status.StartsWith(TEXT("Done"))) && InFlight == 0 && bStarted)
	{
		// New run.
		bBuiltThisRun = false;
		bCameraMovedThisRun = false;
		UE_LOG(LogTemp, Display, TEXT("BuildTiming: run started (%s)"), *Tool);
		if (Status.StartsWith(TEXT("Done")))
		{
			Status.Reset();
		}
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
	if (bOk && Tool.StartsWith(TEXT("camera.")))
	{
		bCameraMovedThisRun = true;
		FrameAt = -1.0;
	}
	if (bOk && (Tool.StartsWith(TEXT("build")) || Tool.StartsWith(TEXT("scene")) || Tool.EndsWith(TEXT("run_batch"))) && !Status.IsEmpty())
	{
		bStageLanded = true;
	}
	if (bOk && (Tool.StartsWith(TEXT("build.create")) || Tool.StartsWith(TEXT("build.place")) || Tool.StartsWith(TEXT("scene.place_template")) ||
		Tool.StartsWith(TEXT("scene.import_tree")) || Tool.StartsWith(TEXT("actors.spawn"))))
	{
		if (!bBuiltThisRun)
		{
			UE_LOG(LogTemp, Display, TEXT("BuildTiming: first piece built after %.1f s (%s)"), Now - RunStart, *Tool);
			if (!bCameraMovedThisRun)
			{
				FrameAt = Now + 0.2; // the player sees it even if the agent does not move the view
			}
		}
		bBuiltThisRun = true; // the first real piece is on screen
	}
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
	if (FrameAt > 0.0 && Now >= FrameAt)
	{
		FrameAt = -1.0;
		FrameBuild();
	}
	// in the scene: a blueprint grid only while an agent works on an EMPTY scene (never over something already built,
	// e.g. the screenshots an agent takes after "Done")
	bool bSceneEmpty = true;
	if (IRuntimeToolsetsCore::IsAvailable())
	{
		IRuntimeToolsetsCore& Core = IRuntimeToolsetsCore::Get();
		if (UWorld* World = Core.FindWorld())
		{
			const FRuntimeSceneGraph* Graph = Core.GetSceneGraph(World);
			bSceneEmpty = !Graph || Graph->NumLiveNodes() == 0;
		}
	}
	Placeholder.Update(bPanelEnabled && Steps > 0 && IsActive() && !bBuiltThisRun && bSceneEmpty, Now - RunStart, Now);
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
	const FSlateFontInfo Big = FCoreStyle::GetDefaultFontStyle("Bold", 11);
	const FSlateFontInfo Small = FCoreStyle::GetDefaultFontStyle("Regular", 9);
	Overlay = SNew(SBox)
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Bottom)
		.Padding(FMargin(16.f, 0.f, 16.f, 18.f))
		.Visibility(EVisibility::HitTestInvisible)
		[
			SNew(SBorder)
			.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
			.BorderBackgroundColor(FLinearColor(0.f, 0.f, 0.f, 0.55f))
			.ColorAndOpacity_Lambda([this]() { return FLinearColor(1.f, 1.f, 1.f, PanelOpacity()); })
			.Padding(FMargin(10.f, 5.f))
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
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 2)
				[
					SNew(SBox).WidthOverride(300.f).HeightOverride(4.f)
					.Visibility_Lambda([this]() { return Progress().IsSet() ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
					[
						SNew(SProgressBar)
						.Percent_Lambda([this]() { return Progress(); })
						.FillColorAndOpacity(FLinearColor(0.35f, 0.75f, 1.f))
					]
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
	if (IsActive())
	{
		return 1.f;
	}
	// done: stays a few seconds, then fades
	const double Since = Now - LastActivity;
	return static_cast<float>(FMath::Clamp(1.0 - (Since - DoneShownFor) / 2.0, 0.0, 1.0));
}

FText FRuntimeToolsetsActivity::DotText() const
{
	return FText::FromString(IsActive() ? TEXT("●") : TEXT("✓"));
}

FSlateColor FRuntimeToolsetsActivity::DotColor() const
{
	if (IsActive())
	{
		const float Pulse = 0.5f + 0.5f * FMath::Sin(static_cast<float>(Now) * 6.f);
		return FLinearColor(0.2f, 0.8f + 0.2f * Pulse, 1.f, 0.4f + 0.6f * Pulse);
	}
	return FLinearColor(0.3f, 1.f, 0.4f);
}

FText FRuntimeToolsetsActivity::HeadlineText() const
{
	const FString Err = Errors > 0 ? FString::Printf(TEXT(", %d failed"), Errors) : FString();
	if (InFlight > 0 || Now - LastActivity < IdleAfter)
	{
		return FText::FromString(FString::Printf(TEXT("Building — %s   (%s)"), *CurrentTool, *Clock(Now - RunStart)));
	}
	if (IsActive())
	{
		// between calls the agent is writing the next stage: say so, with a live timer
		return FText::FromString(FString::Printf(TEXT("Claude is designing the next stage…  %s   (total %s)"), *Clock(Now - LastActivity), *Clock(Now - RunStart)));
	}
	return FText::FromString(FString::Printf(TEXT("Done — %d steps%s in %s"), Steps, *Err, *Clock(LastActivity - RunStart)));
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

void FRuntimeToolsetsActivity::FrameBuild()
{
	// Everything tools have built, seen from above at 30 degrees along the player's heading (the player keeps control).
	IRuntimeToolsetsCore& Core = IRuntimeToolsetsCore::Get();
	UWorld* World = Core.FindWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	const FRuntimeSceneGraph* Graph = World ? Core.GetSceneGraph(World) : nullptr;
	if (!PC || !Graph)
	{
		return;
	}
	FBox Box(ForceInit);
	for (const TPair<FString, FRuntimeSceneNode>& N : Graph->GetNodes())
	{
		if (!N.Value.bSoftDeleted && N.Value.Actor.IsValid())
		{
			Box += RuntimeTools::ActorBox(N.Value.Actor.Get());
		}
	}
	if (!Box.IsValid)
	{
		return;
	}
	const double Fov = PC->PlayerCameraManager ? PC->PlayerCameraManager->GetFOVAngle() : 90.0;
	const double Radius = FMath::Max(Box.GetExtent().Size(), 200.0);
	const double Distance = Radius / FMath::Sin(FMath::DegreesToRadians(FMath::Clamp(Fov, 30.0, 120.0) * 0.5)) * 0.85;
	const FVector Dir = FRotator(-30.0, PC->GetControlRotation().Yaw, 0.0).Vector();
	FVector Eye = Box.GetCenter() - Dir * Distance;
	Eye.Z = FMath::Max(Eye.Z, Box.Max.Z * 0.5 + Box.Min.Z * 0.5 + 300.0);
	if (APawn* Pawn = PC->GetPawn())
	{
		Pawn->SetActorLocation(Eye, false, nullptr, ETeleportType::TeleportPhysics);
	}
	PC->SetControlRotation((Box.GetCenter() - Eye).Rotation());
	UE_LOG(LogTemp, Display, TEXT("BuildTiming: view moved to show the build"));
}
