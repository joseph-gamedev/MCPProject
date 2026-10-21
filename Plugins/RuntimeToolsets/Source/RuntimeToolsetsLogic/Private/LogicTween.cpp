// Tweens (move/rotate/scale/color with easing and loops), actor pools, and the player's procedural animation.
#include "LogicTuning.h"
#include "LogicRuntime.h"
#include "RuntimeJson.h"
#include "RuntimeSceneGraph.h"
#include "RuntimeToolsetsCore.h"
#include "Components/CapsuleComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace
{
	FRuntimeSceneGraph* GraphFor(UWorld* W)
	{
		return W ? IRuntimeToolsetsCore::Get().GetSceneGraph(W) : nullptr;
	}

	AActor* NodeActor(UWorld* W, const FString& Id)
	{
		FRuntimeSceneGraph* G = GraphFor(W);
		const FRuntimeSceneNode* N = G ? G->Find(Id) : nullptr;
		return N ? N->Actor.Get() : nullptr;
	}

	void ForEachMid(AActor* A, TFunctionRef<void(UMaterialInstanceDynamic*)> Fn)
	{
		TArray<UPrimitiveComponent*> Prims;
		A->GetComponents(Prims);
		for (UPrimitiveComponent* P : Prims)
		{
			for (int32 i = 0; i < P->GetNumMaterials(); ++i)
			{
				if (UMaterialInstanceDynamic* M = Cast<UMaterialInstanceDynamic>(P->GetMaterial(i)))
				{
					Fn(M);
				}
			}
		}
	}

	void SetSubtreeVisible(UWorld* W, const FString& Id, bool bVisible, bool bCollision)
	{
		FRuntimeSceneGraph* G = GraphFor(W);
		if (!G)
		{
			return;
		}
		for (const FString& Sub : G->CollectSubtree(Id))
		{
			if (AActor* A = NodeActor(W, Sub))
			{
				A->SetActorHiddenInGame(!bVisible);
				A->SetActorEnableCollision(bCollision);
			}
		}
	}
}

const TArray<FString>& FLogicWorld::EaseNames()
{
	static const TArray<FString> Names = {TEXT("linear"), TEXT("in_quad"), TEXT("out_quad"), TEXT("in_out_quad"), TEXT("in_cubic"), TEXT("out_cubic"),
		TEXT("in_out_cubic"), TEXT("in_sine"), TEXT("out_sine"), TEXT("in_out_sine"), TEXT("in_back"), TEXT("out_back"), TEXT("in_out_back"),
		TEXT("out_elastic"), TEXT("in_bounce"), TEXT("out_bounce")};
	return Names;
}

double FLogicWorld::Ease(const FString& Name, double T)
{
	T = FMath::Clamp(T, 0.0, 1.0);
	auto OutBounce = [](double X)
	{
		const double N = 7.5625, D = 2.75;
		if (X < 1 / D) return N * X * X;
		if (X < 2 / D) { X -= 1.5 / D; return N * X * X + 0.75; }
		if (X < 2.5 / D) { X -= 2.25 / D; return N * X * X + 0.9375; }
		X -= 2.625 / D;
		return N * X * X + 0.984375;
	};
	const double C1 = 1.70158, C2 = C1 * 1.525, C3 = C1 + 1;
	if (Name == TEXT("in_quad")) return T * T;
	if (Name == TEXT("out_quad")) return 1 - (1 - T) * (1 - T);
	if (Name == TEXT("in_out_quad")) return T < 0.5 ? 2 * T * T : 1 - FMath::Pow(-2 * T + 2, 2.0) / 2;
	if (Name == TEXT("in_cubic")) return T * T * T;
	if (Name == TEXT("out_cubic")) return 1 - FMath::Pow(1 - T, 3.0);
	if (Name == TEXT("in_out_cubic")) return T < 0.5 ? 4 * T * T * T : 1 - FMath::Pow(-2 * T + 2, 3.0) / 2;
	if (Name == TEXT("in_sine")) return 1 - FMath::Cos(T * PI / 2);
	if (Name == TEXT("out_sine")) return FMath::Sin(T * PI / 2);
	if (Name == TEXT("in_out_sine")) return -(FMath::Cos(PI * T) - 1) / 2;
	if (Name == TEXT("in_back")) return C3 * T * T * T - C1 * T * T;
	if (Name == TEXT("out_back")) return 1 + C3 * FMath::Pow(T - 1, 3.0) + C1 * FMath::Pow(T - 1, 2.0);
	if (Name == TEXT("in_out_back"))
	{
		return T < 0.5 ? (FMath::Pow(2 * T, 2.0) * ((C2 + 1) * 2 * T - C2)) / 2 : (FMath::Pow(2 * T - 2, 2.0) * ((C2 + 1) * (T * 2 - 2) + C2) + 2) / 2;
	}
	if (Name == TEXT("out_elastic"))
	{
		return T == 0 || T == 1 ? T : FMath::Pow(2.0, -10 * T) * FMath::Sin((T * 10 - 0.75) * (2 * PI / 3)) + 1;
	}
	if (Name == TEXT("out_bounce")) return OutBounce(T);
	if (Name == TEXT("in_bounce")) return 1 - OutBounce(1 - T);
	return T;
}

bool FLogicWorld::ValidateTween(const TSharedPtr<FJsonObject>& Spec, FString& OutError)
{
	bool bAny = false;
	for (const TCHAR* F : {TEXT("location"), TEXT("move"), TEXT("rotation"), TEXT("rotate"), TEXT("scale"), TEXT("color")})
	{
		bAny |= Spec->HasField(F);
	}
	if (!bAny)
	{
		OutError = TEXT("tween needs at least one of location, move, rotation, rotate, scale, color");
		return false;
	}
	FString Ease, Loop, Then;
	if (Spec->TryGetStringField(TEXT("ease"), Ease) && !EaseNames().Contains(Ease))
	{
		OutError = TEXT("ease must be one of ") + FString::Join(EaseNames(), TEXT(", "));
		return false;
	}
	if (Spec->TryGetStringField(TEXT("loop"), Loop) && Loop != TEXT("once") && Loop != TEXT("yoyo") && Loop != TEXT("loop") && Loop != TEXT("pingpong"))
	{
		OutError = TEXT("loop must be once, yoyo, loop or pingpong");
		return false;
	}
	if (Spec->TryGetStringField(TEXT("then"), Then) && Then != TEXT("none") && Then != TEXT("hide") && Then != TEXT("show") && Then != TEXT("release"))
	{
		OutError = TEXT("then must be none, hide, show or release");
		return false;
	}
	return true;
}

bool FLogicWorld::StartTween(const FString& NodeId, const TSharedPtr<FJsonObject>& Spec, FString& OutError)
{
	if (!ValidateTween(Spec, OutError))
	{
		return false;
	}
	AActor* A = NodeActor(World.Get(), NodeId);
	if (!A)
	{
		OutError = FString::Printf(TEXT("no node '%s' to tween"), *NodeId);
		return false;
	}
	bool bAdd = false;
	Spec->TryGetBoolField(TEXT("add"), bAdd);
	if (!bAdd)
	{
		Tweens.RemoveAll([&NodeId](const FTween& T) { return T.NodeId == NodeId; });
	}
	if (bRunning && !TweenOrigins.Contains(NodeId) && !PoolOf.Contains(NodeId))
	{
		TweenOrigins.Add(NodeId, A->GetActorTransform());
	}
	FTween T;
	T.NodeId = NodeId;
	T.Spec = Spec;
	T.Duration = FMath::Max(0.001, Spec->HasField(TEXT("duration")) ? Spec->GetNumberField(TEXT("duration")) : 0.3);
	Spec->TryGetNumberField(TEXT("delay"), T.Delay);
	Spec->TryGetStringField(TEXT("ease"), T.EaseName);
	FString Loop = TEXT("once");
	Spec->TryGetStringField(TEXT("loop"), Loop);
	double Repeat = 1;
	Spec->TryGetNumberField(TEXT("repeat"), Repeat);
	T.Cycles = Loop == TEXT("yoyo") ? FMath::Max(1, static_cast<int32>(Repeat)) * 2 : Loop == TEXT("loop") ? (Repeat < 0 ? -1 : FMath::Max(1, static_cast<int32>(Repeat)))
		: Loop == TEXT("pingpong") ? -1 : 1;
	T.bYoyo = Loop == TEXT("yoyo") || Loop == TEXT("pingpong");
	Spec->TryGetStringField(TEXT("then"), T.Then);
	Spec->TryGetStringField(TEXT("emit"), T.Emit);
	Tweens.Add(T);
	return true;
}

void FLogicWorld::ActivateTween(FTween& T, AActor* A)
{
	const TSharedPtr<FJsonObject>& S = T.Spec;
	T.bStarted = true;
	T.FromLoc = A->GetActorLocation();
	T.FromRot = A->GetActorRotation();
	T.FromScale = A->GetActorScale3D();
	FVector V;
	FRotator R;
	if (RuntimeJson::ToVector(S->TryGetField(TEXT("location")), V)) { T.bLoc = true; T.ToLoc = V; }
	if (RuntimeJson::ToVector(S->TryGetField(TEXT("move")), V)) { T.bLoc = true; T.ToLoc = T.FromLoc + V; }
	if (RuntimeJson::ToRotator(S->TryGetField(TEXT("rotation")), R)) { T.bRot = true; T.ToRot = R; }
	if (RuntimeJson::ToRotator(S->TryGetField(TEXT("rotate")), R)) { T.bRot = true; T.ToRot = T.FromRot + R; }
	if (const TSharedPtr<FJsonValue> Sc = S->TryGetField(TEXT("scale")))
	{
		T.bScale = true;
		T.ToScale = Sc->Type == EJson::Number ? FVector(Sc->AsNumber()) : (RuntimeJson::ToVector(Sc, V) ? V : T.FromScale);
	}
	if (RuntimeJson::ToVector(S->TryGetField(TEXT("color")), V))
	{
		T.bColor = true;
		T.ToColor = FLinearColor(V.X, V.Y, V.Z);
		T.FromColor = T.ToColor;
		ForEachMid(A, [&T](UMaterialInstanceDynamic* M) { T.FromColor = M->K2_GetVectorParameterValue(TEXT("Color")); });
	}
}

void FLogicWorld::ApplyTween(const FTween& T, AActor* A, double Alpha)
{
	if (T.bLoc || T.bRot)
	{
		A->SetActorLocationAndRotation(T.bLoc ? FMath::Lerp(T.FromLoc, T.ToLoc, Alpha) : A->GetActorLocation(),
			T.bRot ? FRotator(FMath::Lerp(T.FromRot.Pitch, T.ToRot.Pitch, Alpha), FMath::Lerp(T.FromRot.Yaw, T.ToRot.Yaw, Alpha),
				FMath::Lerp(T.FromRot.Roll, T.ToRot.Roll, Alpha)) : A->GetActorRotation());
	}
	if (T.bScale)
	{
		A->SetActorScale3D(FMath::Lerp(T.FromScale, T.ToScale, Alpha));
	}
	if (T.bColor)
	{
		const FLinearColor C = FMath::Lerp(T.FromColor, T.ToColor, static_cast<float>(Alpha));
		ForEachMid(A, [&C](UMaterialInstanceDynamic* M) { M->SetVectorParameterValue(TEXT("Color"), C); });
	}
}

void FLogicWorld::TickTweens(double Dt)
{
	TArray<FTween> Finished;
	for (int32 i = Tweens.Num() - 1; i >= 0; --i)
	{
		FTween& T = Tweens[i];
		AActor* A = NodeActor(World.Get(), T.NodeId);
		if (!A)
		{
			Tweens.RemoveAt(i);
			continue;
		}
		T.Elapsed += Dt;
		if (T.Elapsed < T.Delay)
		{
			continue;
		}
		if (!T.bStarted)
		{
			ActivateTween(T, A);
		}
		double Local = T.Elapsed - T.Delay;
		bool bDone = false;
		while (Local >= T.Duration)
		{
			// a cycle finished
			if (T.Cycles > 0 && --T.Cycles == 0)
			{
				bDone = true;
				break;
			}
			Local -= T.Duration;
			T.Elapsed -= T.Duration;
			if (T.bYoyo)
			{
				T.bForward = !T.bForward;
			}
		}
		const double E = Ease(T.EaseName, bDone ? 1.0 : Local / T.Duration);
		const double Alpha = bDone ? (T.bForward ? 1.0 : 0.0) : (T.bForward ? E : 1.0 - E);
		ApplyTween(T, A, T.bYoyo && bDone ? (T.bForward ? 1.0 : 0.0) : Alpha);
		if (bDone)
		{
			Finished.Add(T);
			Tweens.RemoveAt(i);
		}
	}
	for (const FTween& T : Finished)
	{
		if (T.Then == TEXT("hide"))
		{
			HideNode(T.NodeId, true);
		}
		else if (T.Then == TEXT("show"))
		{
			HideNode(T.NodeId, false);
		}
		else if (T.Then == TEXT("release"))
		{
			ReleaseToPool(T.NodeId);
		}
		if (!T.Emit.IsEmpty() && bRunning)
		{
			FLogicEvent E;
			E.On = TEXT("event");
			E.Name = T.Emit;
			E.Target = T.NodeId;
			E.Data.M->Add(TEXT("target"), FLogicValue::MakeString(T.NodeId));
			Queue.Add(MoveTemp(E));
		}
	}
	if (Queue.Num() > 0 && !bProcessing && bRunning)
	{
		ProcessQueue(nullptr);
	}
}

bool FLogicWorld::IsTweening(const FString& NodeId) const
{
	return Tweens.ContainsByPredicate([&NodeId](const FTween& T) { return T.NodeId == NodeId && T.bStarted; });
}

void FLogicWorld::ResetTweens()
{
	Tweens.Reset();
	for (const TPair<FString, FTransform>& P : TweenOrigins)
	{
		if (AActor* A = NodeActor(World.Get(), P.Key))
		{
			A->SetActorTransform(P.Value);
		}
	}
	TweenOrigins.Reset();
}

// ------------------------------------------------------------------ pools

bool FLogicWorld::EnsurePool(const FString& Name, FString& OutError)
{
	const TSharedPtr<FJsonObject>* Def = Pools.Find(Name);
	if (!Def)
	{
		OutError = FString::Printf(TEXT("no pool '%s' (logic.define_pool)"), *Name);
		return false;
	}
	FString Template;
	(*Def)->TryGetStringField(TEXT("template"), Template);
	const int32 Size = FMath::Clamp(static_cast<int32>((*Def)->GetNumberField(TEXT("size"))), 1, 500);
	TArray<FString>& Members = PoolMembers.FindOrAdd(Name);
	Members.RemoveAll([this](const FString& Id) { return NodeActor(World.Get(), Id) == nullptr; });
	if (!NodeActor(World.Get(), Template))
	{
		OutError = FString::Printf(TEXT("pool '%s': template node is gone"), *Name);
		return false;
	}
	SetSubtreeVisible(World.Get(), Template, false, false);
	FToolCallerContext Caller;
	Caller.SessionId = FLogicManager::Session();
	Caller.ClientName = TEXT("logic");
	while (Members.Num() < Size)
	{
		TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("target"), Template);
		Args->SetStringField(TEXT("name"), FString::Printf(TEXT("%s_%d"), *Name, Members.Num()));
		const FToolResult R = IRuntimeToolsetsCore::Get().ExecuteInline(TEXT("scene.duplicate_node"), Args, Caller, 1);
		FString Id;
		if (!R.IsOk() || !R.Payload.IsValid() || !R.Payload->TryGetStringField(TEXT("id"), Id))
		{
			OutError = FString::Printf(TEXT("pool '%s': could not copy the template: %s"), *Name, *R.Message);
			return false;
		}
		SetSubtreeVisible(World.Get(), Id, false, false);
		Members.Add(Id);
		PoolOf.Add(Id, Name);
	}
	return true;
}

FString FLogicWorld::AcquireFromPool(const FString& Name, const FVector& At, FString& OutError)
{
	if (!EnsurePool(Name, OutError))
	{
		return FString();
	}
	TArray<FString>& Members = PoolMembers[Name];
	FString Pick;
	for (const FString& Id : Members)
	{
		if (!PoolBusy.Contains(Id))
		{
			Pick = Id;
			break;
		}
	}
	if (Pick.IsEmpty())
	{
		// All in use: recycle the one taken longest ago.
		for (const FString& Id : PoolOrder)
		{
			if (PoolOf.FindRef(Id) == Name)
			{
				Pick = Id;
				break;
			}
		}
	}
	if (Pick.IsEmpty())
	{
		OutError = TEXT("pool is empty");
		return FString();
	}
	Tweens.RemoveAll([&Pick](const FTween& T) { return T.NodeId == Pick; });
	PoolOrder.Remove(Pick);
	PoolOrder.Add(Pick);
	PoolBusy.Add(Pick);
	if (AActor* A = NodeActor(World.Get(), Pick))
	{
		if (AActor* Tpl = NodeActor(World.Get(), Pools[Name]->GetStringField(TEXT("template"))))
		{
			A->SetActorRotation(Tpl->GetActorRotation());
			A->SetActorScale3D(Tpl->GetActorScale3D());
		}
		A->SetActorLocation(At);
	}
	SetSubtreeVisible(World.Get(), Pick, true, false);
	return Pick;
}

void FLogicWorld::ReleaseToPool(const FString& NodeId)
{
	if (!PoolOf.Contains(NodeId))
	{
		HideNode(NodeId, true);
		return;
	}
	PoolBusy.Remove(NodeId);
	PoolOrder.Remove(NodeId);
	Tweens.RemoveAll([&NodeId](const FTween& T) { return T.NodeId == NodeId; });
	SetSubtreeVisible(World.Get(), NodeId, false, false);
	Touching.Remove(NodeId);
}

void FLogicWorld::ResetPools()
{
	for (const FString& Id : PoolBusy.Array())
	{
		ReleaseToPool(Id);
	}
	PoolBusy.Reset();
	PoolOrder.Reset();
}

// ------------------------------------------------------------------ player animation

void FLogicWorld::TickPlayerAnim(double Dt)
{
	ACharacter* C = Player.Get();
	AActor* Body = PlayerBody.Get();
	if (!C || !Body || Dt <= 0.0)
	{
		return;
	}
	if (IsFlight())
	{
		Invulnerable = FMath::Max(0.0, Invulnerable - Dt);
		const bool bShowJet = Invulnerable <= 0.0 || FMath::Fmod(Invulnerable, 0.16) < 0.08;
		TArray<AActor*> Parts = {Body};
		Body->GetAttachedActors(Parts, false, true);
		for (AActor* P : Parts)
		{
			P->SetActorHiddenInGame(!bShowJet);
		}
		return;
	}
	const double Half = C->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const double S = Half / 90.0;
	const FVector Vel = C->GetVelocity();
	const double Speed = Vel.Size2D();
	const bool bGround = C->GetCharacterMovement()->IsMovingOnGround();
	double Amp = 0.0;
	if (bGround && Speed > 30.0 && !bFrozen)
	{
		AnimPhase += Dt * Speed * 0.02;
		Amp = FMath::Min(1.0, Speed / LogicTuning::Get(TEXT("anim.run_speed_ref")));
	}
	if (bGround && !bWasGrounded)
	{
		Squash = 1.0; // landing
	}
	else if (!bGround && bWasGrounded && Vel.Z > 100.0)
	{
		Squash = -0.8; // take-off stretch
	}
	bWasGrounded = bGround;
	Squash = FMath::FInterpTo(Squash, 0.0, Dt, 9.0);
	const double Bob = bGround ? FMath::Abs(FMath::Sin(AnimPhase)) * 5.0 * S * Amp : 0.0;
	const double Lean = bGround ? -LogicTuning::Get(TEXT("anim.lean")) * Amp : -4.0;
	Body->SetActorRelativeLocation(FVector(0, 0, -Half + Bob));
	Body->SetActorRelativeRotation(FRotator(Lean, 0, 0));
	Body->SetActorRelativeScale3D(FVector(1.0 + 0.12 * Squash, 1.0 + 0.12 * Squash, 1.0 - 0.2 * Squash));
	const double Swing = FMath::Sin(AnimPhase);
	const bool bArmed = IsTopDown() && !EquippedWeapon(TEXT("player")).IsEmpty();
	if (bArmed && GunNode.IsEmpty() && PlayerBodyNodes.Num() >= 5)
	{
		// A blaster in the right hand, along the arm (the arm points down -Z at rest).
		auto P = [S](const TCHAR* Shape, FVector Size, FVector Loc, FVector Color, const TCHAR* Material)
		{
			TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
			J->SetStringField(TEXT("shape"), Shape);
			J->SetField(TEXT("size"), RuntimeJson::Vec(Size * S));
			J->SetField(TEXT("location"), RuntimeJson::Vec(Loc * S));
			J->SetField(TEXT("color"), RuntimeJson::Vec(Color));
			J->SetStringField(TEXT("material"), Material);
			return MakeShared<FJsonValueObject>(J);
		};
		TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("name"), TEXT("PlayerGun"));
		Args->SetStringField(TEXT("parent"), PlayerBodyNodes.Last());
		Args->SetField(TEXT("location"), RuntimeJson::Vec(FVector::ZeroVector));
		Args->SetBoolField(TEXT("repair_coplanar"), false);
		Args->SetArrayField(TEXT("parts"), {
			P(TEXT("box"), FVector(16, 12, 48), FVector(4, 0, -100), FVector(0.12, 0.13, 0.15), TEXT("steel")),
			P(TEXT("box"), FVector(10, 14, 20), FVector(14, 0, -72), FVector(0.05, 0.05, 0.06), TEXT("black")),
			P(TEXT("cylinder"), FVector(9, 9, 26), FVector(2, 0, -126), FVector(0.3, 0.32, 0.35), TEXT("steel")),
			P(TEXT("box"), FVector(6, 16, 8), FVector(4, 0, -110), FVector(0.2, 0.7, 1.0), TEXT("light")),
		});
		FToolCallerContext Caller;
		Caller.SessionId = FLogicManager::Session();
		const FToolResult R = IRuntimeToolsetsCore::Get().ExecuteInline(TEXT("build.create_assembly"), Args, Caller, 1);
		FString Id;
		if (R.IsOk() && R.Payload->TryGetStringField(TEXT("id"), Id))
		{
			GunNode = Id;
			PlayerBodyNodes.Add(Id);
			if (FRuntimeSceneGraph* G = GraphFor(World.Get()))
			{
				if (const FRuntimeSceneNode* N = G->Find(Id); N && N->Actor.IsValid())
				{
					N->Actor->Tags.Add(TEXT("logic_fx"));
					N->Actor->SetActorEnableCollision(false);
					N->Actor->SetActorRelativeRotation(FRotator::ZeroRotator);
				}
			}
		}
	}
	for (FLimb& L : Limbs)
	{
		AActor* A = L.Actor.Get();
		if (!A)
		{
			continue;
		}
		double Target = 0.0;
		switch (L.Kind)
		{
		case 0: Target = bGround ? Swing * LogicTuning::Get(TEXT("anim.leg_swing")) * Amp : 38.0; break;    // left leg
		case 1: Target = bGround ? -Swing * LogicTuning::Get(TEXT("anim.leg_swing")) * Amp : -22.0; break;  // right leg
		case 2: Target = bArmed ? 72.0 - 10.0 * Recoil : (bGround ? -Swing * LogicTuning::Get(TEXT("anim.arm_swing")) * Amp : 155.0); break; // left arm (supports the gun / raised in the air)
		case 3: Target = bArmed ? 90.0 - LogicTuning::Get(TEXT("anim.recoil")) * Recoil : (bGround ? Swing * LogicTuning::Get(TEXT("anim.arm_swing")) * Amp : 25.0); break;   // right arm (aims / swings)
		}
		L.Pitch = FMath::FInterpTo(L.Pitch, Target, Dt, bArmed && L.Kind >= 2 ? 30.0 : 18.0);
		// Arms holding the gun turn slightly inward so both hands meet in front.
		const double Inward = bArmed ? (L.Kind == 2 ? 18.0 : L.Kind == 3 ? -6.0 : 0.0) : 0.0;
		A->SetActorRelativeRotation(FRotator(L.Pitch, Inward, 0));
	}
	// Blink while invulnerable after a hit.
	if (Invulnerable > 0.0)
	{
		Invulnerable = FMath::Max(0.0, Invulnerable - Dt);
		const bool bShow = Invulnerable <= 0.0 || FMath::Fmod(Invulnerable, 0.16) < 0.08;
		TArray<AActor*> Parts = {Body};
		Body->GetAttachedActors(Parts, false, true);
		for (AActor* P : Parts)
		{
			P->SetActorHiddenInGame(!bShow);
		}
	}
}
