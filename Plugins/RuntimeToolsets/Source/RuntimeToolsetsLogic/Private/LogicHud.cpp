// In-world HUD painter (enemy health bars, alert icons, floating numbers, hit flash) and feedback presets.
#include "LogicHud.h"
#include "LogicRuntime.h"
#include "RuntimeJson.h"
#include "RuntimeSceneGraph.h"
#include "RuntimeToolsetsCore.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Camera/CameraActor.h"
#include "Engine/World.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "Kismet/GameplayStatics.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"

namespace
{
	double Num(const TSharedPtr<FJsonObject>& J, const TCHAR* Field, double Default)
	{
		double V = Default;
		if (J.IsValid())
		{
			J->TryGetNumberField(Field, V);
		}
		return V;
	}

	bool Flag(const TSharedPtr<FJsonObject>& J, const TCHAR* Field, bool Default)
	{
		bool V = Default;
		if (J.IsValid())
		{
			J->TryGetBoolField(Field, V);
		}
		return V;
	}
}

const FSlateBrush* LogicRoundedBrush()
{
	static const FSlateRoundedBoxBrush Brush(FLinearColor::White, 5.f);
	return &Brush;
}

void SLogicHud::Construct(const FArguments& InArgs, TWeakPtr<FLogicWorld> InWorld)
{
	World = InWorld;
	SetCanTick(false);
	ForceVolatile(true);
}

FVector2D SLogicHud::ComputeDesiredSize(float) const
{
	return FVector2D(16.0, 16.0);
}

int32 SLogicHud::OnPaint(const FPaintArgs& Args, const FGeometry& Geo, const FSlateRect& Culling, FSlateWindowElementList& Out, int32 Layer,
	const FWidgetStyle& Style, bool bParentEnabled) const
{
	TSharedPtr<FLogicWorld> W = World.Pin();
	UWorld* UW = W.IsValid() ? W->GetWorld() : nullptr;
	APlayerController* PC = UW ? UW->GetFirstPlayerController() : nullptr;
	if (!W.IsValid() || !PC || !W->HudDef.IsValid())
	{
		return Layer;
	}
	const TSharedPtr<FJsonObject>& Hud = W->HudDef;
	const float Scale = Geo.Scale > 0.f ? Geo.Scale : 1.f;
	const FVector2f Size = FVector2f(Geo.GetLocalSize());
	const FSlateBrush* White = FCoreStyle::Get().GetBrush("WhiteBrush");
	const FSlateBrush* Round = LogicRoundedBrush();
	TSharedRef<FSlateFontMeasure> Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	auto Project = [&](const FVector& P, FVector2f& OutPos) -> bool
	{
		FVector2D S;
		if (!UGameplayStatics::ProjectWorldToScreen(PC, P, S, false))
		{
			return false;
		}
		OutPos = FVector2f(S.X / Scale, S.Y / Scale);
		return OutPos.X > -100 && OutPos.Y > -100 && OutPos.X < Size.X + 100 && OutPos.Y < Size.Y + 100;
	};
	auto Box = [&](const FVector2f& Pos, const FVector2f& Sz, const FLinearColor& C, const FSlateBrush* B)
	{
		FSlateDrawElement::MakeBox(Out, Layer, Geo.ToPaintGeometry(Sz, FSlateLayoutTransform(Pos)), B, ESlateDrawEffect::None, C);
	};
	auto Text = [&](const FVector2f& Center, const FString& S, int32 FontSize, const FLinearColor& C)
	{
		FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Bold", FontSize);
		Font.OutlineSettings.OutlineSize = FMath::Max(1, FontSize / 10);
		Font.OutlineSettings.OutlineColor = FLinearColor(0, 0, 0, C.A);
		const FVector2f TS = FVector2f(Measure->Measure(S, Font));
		FSlateDrawElement::MakeText(Out, Layer + 1, Geo.ToPaintGeometry(TS, FSlateLayoutTransform(Center - TS * 0.5f)), S, Font, ESlateDrawEffect::None, C);
	};
	FRuntimeSceneGraph* G = IRuntimeToolsetsCore::Get().GetSceneGraph(UW);

	// Hit flash and low health pulse.
	if (Flag(Hud, TEXT("hit_flash"), true) && W->HitFlash > 0.0)
	{
		FLinearColor C = W->FlashColor;
		C.A = static_cast<float>(0.35 * W->HitFlash);
		Box(FVector2f::ZeroVector, Size, C, White);
	}
	if (const FLogicWorld::FHealthState* PH = W->Healths.Find(TEXT("player")); PH && !PH->bDead && PH->Current / PH->Max <= 0.3 && Flag(Hud, TEXT("low_health"), true))
	{
		const float A = 0.12f + 0.12f * FMath::Sin(static_cast<float>(FPlatformTime::Seconds()) * 6.f);
		const float E = 70.f;
		const FLinearColor C(0.8f, 0.0f, 0.0f, A);
		Box(FVector2f(0, 0), FVector2f(Size.X, E), C, White);
		Box(FVector2f(0, Size.Y - E), FVector2f(Size.X, E), C, White);
		Box(FVector2f(0, E), FVector2f(E, Size.Y - 2 * E), C, White);
		Box(FVector2f(Size.X - E, E), FVector2f(E, Size.Y - 2 * E), C, White);
	}

	// Health bars and alert icons over enemies.
	const bool bBars = Flag(Hud, TEXT("enemy_bars"), true);
	const bool bAlways = Flag(Hud, TEXT("always_bars"), false);
	for (const TPair<FString, FLogicWorld::FHealthState>& P : W->Healths)
	{
		if (!bBars || P.Key == TEXT("player") || P.Value.bDead || (!bAlways && P.Value.Current >= P.Value.Max))
		{
			continue;
		}
		const FRuntimeSceneNode* N = G ? G->Find(P.Key) : nullptr;
		if (!N || !N->Actor.IsValid() || N->Actor->IsHidden())
		{
			continue;
		}
		const FBox B = FLogicWorld::GameBox(N->Actor.Get());
		FVector2f S;
		if (B.IsValid && Project(FVector(B.GetCenter().X, B.GetCenter().Y, B.Max.Z + 35.0), S))
		{
			const FVector2f BarSize(62.f, 9.f);
			const float Frac = static_cast<float>(FMath::Clamp(P.Value.Current / P.Value.Max, 0.0, 1.0));
			Box(S - BarSize * 0.5f - FVector2f(2, 2), BarSize + FVector2f(4, 4), FLinearColor(0, 0, 0, 0.7f), Round);
			Box(S - BarSize * 0.5f, FVector2f(BarSize.X * Frac, BarSize.Y), FMath::Lerp(FLinearColor(0.95f, 0.1f, 0.05f), FLinearColor(0.2f, 0.9f, 0.25f), Frac), Round);
		}
	}
	if (Flag(Hud, TEXT("alert_icons"), true))
	{
		for (const TPair<FString, FLogicWorld::FBrainAgent>& P : W->Agents)
		{
			if (P.Value.Icon.IsEmpty())
			{
				continue;
			}
			const FRuntimeSceneNode* N = G ? G->Find(P.Key) : nullptr;
			const FLogicWorld::FHealthState* H = W->Healths.Find(P.Key);
			if (!N || !N->Actor.IsValid() || N->Actor->IsHidden() || (H && H->bDead))
			{
				continue;
			}
			const FBox B = FLogicWorld::GameBox(N->Actor.Get());
			FVector2f S;
			if (B.IsValid && Project(FVector(B.GetCenter().X, B.GetCenter().Y, B.Max.Z + 75.0), S))
			{
				const bool bAlert = P.Value.Icon == TEXT("!");
				const float Pulse = 1.f + 0.12f * FMath::Sin(static_cast<float>(FPlatformTime::Seconds()) * 10.f);
				Text(S, P.Value.Icon, static_cast<int32>(30 * Pulse), bAlert ? FLinearColor(1.f, 0.15f, 0.1f) : FLinearColor(1.f, 0.85f, 0.15f));
			}
		}
	}

	// Floating numbers.
	if (Flag(Hud, TEXT("damage_numbers"), true))
	{
		for (const FLogicWorld::FFloatText& F : W->FloatTexts)
		{
			FVector2f S;
			if (Project(F.World + FVector(0, 0, F.Age * 140.0), S))
			{
				FLinearColor C = F.Color;
				C.A = static_cast<float>(FMath::Clamp(1.0 - (F.Age - 0.45) / 0.45, 0.0, 1.0));
				const int32 Pop = static_cast<int32>(22 + 10 * FMath::Max(0.0, 1.0 - F.Age * 6.0));
				Text(S, F.Text, Pop, C);
			}
		}
	}
	return Layer + 2;
}

// ------------------------------------------------------------------ feedback presets

void FLogicWorld::PlayFeedback(const FString& Name, const FVector& At, const FString& Target)
{
	const TSharedPtr<FJsonObject>* Def = FeedbackDefs.Find(Name);
	if (!Def)
	{
		return;
	}
	const TArray<TSharedPtr<FJsonValue>>* Steps = nullptr;
	if (!(*Def)->TryGetArrayField(TEXT("steps"), Steps))
	{
		return;
	}
	for (const TSharedPtr<FJsonValue>& V : *Steps)
	{
		const TSharedPtr<FJsonObject> S = V->AsObject();
		if (!S.IsValid())
		{
			continue;
		}
		FPendingFeedback P;
		P.Step = S;
		P.At = At;
		P.Target = Target;
		P.Delay = Num(S, TEXT("delay"), 0.0);
		PendingFeedback.Add(P);
	}
	TickFeedback(0.0);
}

void FLogicWorld::TickFeedback(double RealDt)
{
	for (int32 i = PendingFeedback.Num() - 1; i >= 0; --i)
	{
		FPendingFeedback& P = PendingFeedback[i];
		P.Delay -= RealDt;
		if (P.Delay > 0.0)
		{
			continue;
		}
		const TSharedPtr<FJsonObject> S = P.Step;
		FString Type;
		S->TryGetStringField(TEXT("type"), Type);
		const double Intensity = Num(S, TEXT("intensity"), 1.0);
		if (Type == TEXT("shake"))
		{
			ShakeTime = FMath::Max(ShakeTime, Num(S, TEXT("duration"), 0.25));
			ShakeAmp = FMath::Max(ShakeAmp, 18.0 * Intensity);
		}
		else if (Type == TEXT("flash"))
		{
			FVector C(1, 1, 1);
			RuntimeJson::ToVector(S->TryGetField(TEXT("color")), C);
			FlashColor = FLinearColor(C.X, C.Y, C.Z);
			HitFlash = FMath::Max(HitFlash, Intensity);
		}
		else if (Type == TEXT("freeze") || Type == TEXT("slowmo"))
		{
			const double Scale = Type == TEXT("freeze") ? 0.02 : Num(S, TEXT("scale"), 0.3);
			SlowmoScale = FMath::Min(SlowmoLeft > 0.0 ? SlowmoScale : 1.0, Scale);
			SlowmoLeft = FMath::Max(SlowmoLeft, Num(S, TEXT("duration"), Type == TEXT("freeze") ? 0.06 : 0.8));
			if (UWorld* W = World.Get())
			{
				UGameplayStatics::SetGlobalTimeDilation(W, static_cast<float>(SlowmoScale));
			}
		}
		else if (Type == TEXT("text"))
		{
			FFloatText F;
			F.World = P.At;
			S->TryGetStringField(TEXT("text"), F.Text);
			FVector C(1, 1, 1);
			RuntimeJson::ToVector(S->TryGetField(TEXT("color")), C);
			F.Color = FLinearColor(C.X, C.Y, C.Z);
			FloatTexts.Add(F);
		}
		else if (Type == TEXT("tween") && !P.Target.IsEmpty())
		{
			TSharedRef<FJsonObject> T = RuntimeJson::Parse(RuntimeJson::Write(S.ToSharedRef())).ToSharedRef();
			T->RemoveField(TEXT("type"));
			T->RemoveField(TEXT("delay"));
			if (!T->HasField(TEXT("add")))
			{
				T->SetBoolField(TEXT("add"), true);
			}
			FString Err;
			StartTween(P.Target, T, Err);
		}
		else if (Type == TEXT("spawn"))
		{
			FString Pool, Err;
			S->TryGetStringField(TEXT("pool"), Pool);
			const FString Id = AcquireFromPool(Pool, P.At, Err);
			if (!Id.IsEmpty())
			{
				TSharedRef<FJsonObject> T = MakeShared<FJsonObject>();
				T->SetField(TEXT("scale"), MakeShared<FJsonValueNumber>(Num(S, TEXT("scale"), 3.0)));
				T->SetNumberField(TEXT("duration"), Num(S, TEXT("duration"), 0.3));
				T->SetStringField(TEXT("then"), TEXT("release"));
				StartTween(Id, T, Err);
			}
		}
		else if (Type == TEXT("emit") && bRunning)
		{
			FLogicEvent E;
			E.On = TEXT("event");
			S->TryGetStringField(TEXT("name"), E.Name);
			E.Target = P.Target;
			Queue.Add(E);
		}
		PendingFeedback.RemoveAt(i);
	}
	// Time effects run in real time.
	if (SlowmoLeft > 0.0)
	{
		SlowmoLeft -= RealDt;
		if (SlowmoLeft <= 0.0)
		{
			SlowmoScale = 1.0;
			if (UWorld* W = World.Get())
			{
				UGameplayStatics::SetGlobalTimeDilation(W, 1.f);
			}
		}
	}
	ShakeTime = FMath::Max(0.0, ShakeTime - RealDt);
	if (ShakeTime <= 0.0)
	{
		ShakeAmp = 0.0;
	}
}

FVector FLogicWorld::ShakeOffset() const
{
	if (ShakeTime <= 0.0)
	{
		return FVector::ZeroVector;
	}
	const double T = FPlatformTime::Seconds() * 60.0;
	const double A = ShakeAmp * FMath::Clamp(ShakeTime / 0.25, 0.0, 1.0);
	return FVector(FMath::Sin(T * 1.7) * A, FMath::Cos(T * 2.3) * A, FMath::Sin(T * 2.9) * A * 0.5);
}
