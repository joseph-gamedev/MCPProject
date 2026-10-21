// Health and damage, hit zones, weapons (projectile / hitscan) and projectiles.
#include "LogicTuning.h"
#include "LogicRuntime.h"
#include "RuntimeJson.h"
#include "RuntimeSceneGraph.h"
#include "RuntimeToolHelpers.h"
#include "RuntimeToolsetsCore.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

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

	FString Str(const TSharedPtr<FJsonObject>& J, const TCHAR* Field, const FString& Default = FString())
	{
		FString V = Default;
		if (J.IsValid())
		{
			J->TryGetStringField(Field, V);
		}
		return V;
	}

	FRuntimeSceneGraph* GraphFor(UWorld* W)
	{
		return W ? IRuntimeToolsetsCore::Get().GetSceneGraph(W) : nullptr;
	}

	/** Entry time (0..1) of segment A->B into a box, or false. */
	bool SegmentBox(const FVector& A, const FVector& B, const FBox& Box, double& OutT)
	{
		double T0 = 0.0, T1 = 1.0;
		const FVector D = B - A;
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			if (FMath::Abs(D[Axis]) < 1e-9)
			{
				if (A[Axis] < Box.Min[Axis] || A[Axis] > Box.Max[Axis])
				{
					return false;
				}
				continue;
			}
			double Ta = (Box.Min[Axis] - A[Axis]) / D[Axis];
			double Tb = (Box.Max[Axis] - A[Axis]) / D[Axis];
			if (Ta > Tb)
			{
				Swap(Ta, Tb);
			}
			T0 = FMath::Max(T0, Ta);
			T1 = FMath::Min(T1, Tb);
			if (T0 > T1)
			{
				return false;
			}
		}
		OutT = T0;
		return true;
	}
}

bool FLogicWorld::IsTopDown() const
{
	return PD().IsValid() && Str(PD(), TEXT("mode")) == TEXT("topdown");
}

// ------------------------------------------------------------------ health

void FLogicWorld::RebuildHealths(bool bReset)
{
	if (bReset)
	{
		Healths.Reset();
	}
	for (const TPair<FString, TSharedPtr<FJsonObject>>& P : HealthDefs)
	{
		TArray<FString> Keys;
		if (P.Key == TEXT("player"))
		{
			Keys.Add(TEXT("player"));
		}
		else
		{
			FString Err;
			ResolveNodes({P.Key}, Keys, Err);
		}
		const double Max = FMath::Max(0.001, Num(P.Value, TEXT("max"), 10.0));
		for (const FString& K : Keys)
		{
			FHealthState* Existing = Healths.Find(K);
			if (Existing)
			{
				Existing->Def = P.Value;
				Existing->Max = Max;
				Existing->Current = FMath::Min(Existing->Current, Max);
				continue;
			}
			FHealthState H;
			H.Def = P.Value;
			H.Max = Max;
			H.Current = FMath::Clamp(Num(P.Value, TEXT("initial"), Max), 0.0, Max);
			Healths.Add(K, H);
		}
	}
}

bool FLogicWorld::ApplyDamage(const FString& Victim, double Amount, const FString& Type, const FString& Instigator, const FVector& Push)
{
	FHealthState* H = Healths.Find(Victim);
	if (!H || H->bDead || PlayTime < H->InvulnUntil || (Victim == TEXT("player") && Invulnerable > 0.0))
	{
		return false;
	}
	const TSharedPtr<FJsonObject>* Res = nullptr;
	if (!Type.IsEmpty() && H->Def.IsValid() && H->Def->TryGetObjectField(TEXT("resistances"), Res))
	{
		double Mult = 1.0;
		if ((*Res)->TryGetNumberField(Type, Mult))
		{
			Amount *= Mult;
		}
	}
	if (Amount <= 0.0)
	{
		return false;
	}
	H->Current = FMath::Max(0.0, H->Current - Amount);
	const double Invuln = Num(H->Def, TEXT("invulnerable"), Victim == TEXT("player") ? LogicTuning::Get(TEXT("combat.player_invulnerable")) : 0.0);
	H->InvulnUntil = PlayTime + Invuln;
	if (Victim == TEXT("player"))
	{
		Invulnerable = FMath::Max(Invulnerable, Invuln);
		if (ACharacter* C = Player.Get(); C && !Push.IsNearlyZero())
		{
			C->LaunchCharacter(Push, true, true);
		}
	}
	// Feel: floating number, hit punch / screen flash, optional feedback preset.
	{
		FVector Top;
		FBox VB;
		if (VictimBox(Victim, VB))
		{
			Top = FVector(VB.GetCenter().X, VB.GetCenter().Y, VB.Max.Z + 20.0);
			FFloatText F;
			F.World = Top;
			F.Text = FString::Printf(TEXT("-%s"), *FLogicValue::MakeNumber(FMath::RoundToDouble(Amount * 10.0) / 10.0).ToText());
			F.Color = Victim == TEXT("player") ? FLinearColor(1.f, 0.25f, 0.2f) : FLinearColor(1.f, 0.95f, 0.6f);
			FloatTexts.Add(F);
		}
		if (Victim == TEXT("player"))
		{
			HitFlash = 1.0;
			FlashColor = FLinearColor(0.9f, 0.05f, 0.02f);
		}
		else if (H->Current > 0.0)
		{
			TSharedRef<FJsonObject> Punch = MakeShared<FJsonObject>();
			FVector S = FVector::OneVector;
			if (FRuntimeSceneGraph* G = GraphFor(World.Get()))
			{
				if (const FRuntimeSceneNode* N = G->Find(Victim); N && N->Actor.IsValid())
				{
					S = N->Actor->GetActorScale3D();
				}
			}
			Punch->SetField(TEXT("scale"), RuntimeJson::Vec(S * LogicTuning::Get(TEXT("combat.hit_punch_scale"))));
			Punch->SetNumberField(TEXT("duration"), LogicTuning::Get(TEXT("combat.hit_punch_time")));
			Punch->SetStringField(TEXT("loop"), TEXT("yoyo"));
			Punch->SetBoolField(TEXT("add"), true);
			FString Err;
			StartTween(Victim, Punch, Err);
		}
		FString Fb = Str(H->Def, H->Current <= 0.0 ? TEXT("death_feedback") : TEXT("hit_feedback"));
		if (!Fb.IsEmpty())
		{
			FVector At;
			OwnerLocation(Victim, At);
			PlayFeedback(Fb, At, Victim == TEXT("player") ? FString() : Victim);
		}
	}
	FLogicEvent E;
	E.On = TEXT("damaged");
	E.Target = Victim == TEXT("player") ? FString() : Victim;
	E.Name = Victim;
	E.Data.M->Add(TEXT("victim"), FLogicValue::MakeString(Victim));
	E.Data.M->Add(TEXT("amount"), FLogicValue::MakeNumber(Amount));
	E.Data.M->Add(TEXT("health"), FLogicValue::MakeNumber(H->Current));
	E.Data.M->Add(TEXT("max"), FLogicValue::MakeNumber(H->Max));
	E.Data.M->Add(TEXT("type"), FLogicValue::MakeString(Type));
	E.Data.M->Add(TEXT("instigator"), FLogicValue::MakeString(Instigator));
	FVector Loc;
	if (OwnerLocation(Victim, Loc))
	{
		E.Data.M->Add(TEXT("location"), FLogicValue::MakeList({FLogicValue::MakeNumber(Loc.X), FLogicValue::MakeNumber(Loc.Y), FLogicValue::MakeNumber(Loc.Z)}));
	}
	Queue.Add(E);
	if (H->Current <= 0.0)
	{
		H->bDead = true;
		FLogicEvent D = E;
		D.On = TEXT("died");
		Queue.Add(D);
		const FString PointsVar = Str(H->Def, TEXT("points_var"));
		if (!PointsVar.IsEmpty() && Vars.Contains(PointsVar))
		{
			SetVar(PointsVar, FLogicValue::MakeNumber(Vars[PointsVar].N + Num(H->Def, TEXT("points"), 0.0)), true);
		}
		const FString OnDeath = Str(H->Def, TEXT("on_death"), Victim == TEXT("player") ? TEXT("respawn") : TEXT("hide"));
		if (Victim == TEXT("player"))
		{
			if (OnDeath == TEXT("respawn"))
			{
				RespawnPlayer();
				H->Current = H->Max;
				H->bDead = false;
			}
			else if (OnDeath == TEXT("freeze"))
			{
				bFrozen = true;
			}
		}
		else if (OnDeath == TEXT("hide"))
		{
			HideNode(Victim, true);
		}
		if (const FString Loot = Str(H->Def, TEXT("loot")); !Loot.IsEmpty() && Victim != TEXT("player"))
		{
			FVector At;
			if (OwnerLocation(Victim, At))
			{
				DropLoot(Loot, At);
			}
		}
		else if (OnDeath == TEXT("release"))
		{
			ReleaseToPool(Victim);
		}
		else if (OnDeath == TEXT("squash"))
		{
			TSharedRef<FJsonObject> T = MakeShared<FJsonObject>();
			FVector S = FVector::OneVector;
			if (FRuntimeSceneGraph* G = GraphFor(World.Get()))
			{
				if (const FRuntimeSceneNode* N = G->Find(Victim); N && N->Actor.IsValid())
				{
					S = N->Actor->GetActorScale3D();
				}
			}
			T->SetField(TEXT("scale"), RuntimeJson::Vec(FVector(S.X * LogicTuning::Get(TEXT("combat.squash_xy")), S.Y * LogicTuning::Get(TEXT("combat.squash_xy")), S.Z * LogicTuning::Get(TEXT("combat.squash_z")))));
			T->SetNumberField(TEXT("duration"), LogicTuning::Get(TEXT("combat.squash_time")));
			T->SetStringField(TEXT("ease"), TEXT("out_back"));
			T->SetStringField(TEXT("then"), TEXT("hide"));
			FString Err;
			StartTween(Victim, T, Err);
		}
	}
	if (!bProcessing)
	{
		ProcessQueue(nullptr);
	}
	return true;
}

void FLogicWorld::Heal(const FString& Victim, double Amount)
{
	if (FHealthState* H = Healths.Find(Victim); H && !H->bDead)
	{
		H->Current = FMath::Min(H->Max, H->Current + Amount);
	}
}

bool FLogicWorld::OwnerLocation(const FString& Key, FVector& Out) const
{
	if (Key == TEXT("player"))
	{
		if (ACharacter* C = Player.Get())
		{
			Out = C->GetActorLocation();
			return true;
		}
		return false;
	}
	FRuntimeSceneGraph* G = GraphFor(World.Get());
	const FRuntimeSceneNode* N = G ? G->Find(Key) : nullptr;
	if (!N || !N->Actor.IsValid())
	{
		return false;
	}
	const FBox B = GameBox(N->Actor.Get());
	Out = B.IsValid ? B.GetCenter() : N->Actor->GetActorLocation();
	return true;
}

bool FLogicWorld::VictimBox(const FString& Key, FBox& Out) const
{
	if (Key == TEXT("player"))
	{
		ACharacter* C = Player.Get();
		if (!C)
		{
			return false;
		}
		Out = C->GetCapsuleComponent()->Bounds.GetBox();
		return true;
	}
	FRuntimeSceneGraph* G = GraphFor(World.Get());
	const FRuntimeSceneNode* N = G ? G->Find(Key) : nullptr;
	if (!N || !N->Actor.IsValid() || N->bSoftDeleted || IsHidden(Key) || N->Actor->IsHidden())
	{
		return false;
	}
	Out = GameBox(N->Actor.Get());
	return Out.IsValid != 0;
}

bool FLogicWorld::VictimMatches(const FString& Key, const FString& Filter) const
{
	if (Filter.IsEmpty() || Filter == TEXT("any"))
	{
		return true;
	}
	if (Filter == TEXT("player"))
	{
		return Key == TEXT("player");
	}
	if (Key == TEXT("player"))
	{
		return false;
	}
	return NodeMatches(Key, Filter);
}

FLogicValue FLogicWorld::HealthMap() const
{
	FLogicValue M = FLogicValue::MakeMap();
	for (const TPair<FString, FHealthState>& P : Healths)
	{
		M.M->Add(P.Key, FLogicValue::MakeNumber(P.Value.Current));
	}
	return M;
}

FLogicValue FLogicWorld::AmmoMap() const
{
	FLogicValue M = FLogicValue::MakeMap();
	for (const TPair<FString, FWeaponState>& P : WeaponStates)
	{
		M.M->Add(P.Key, FLogicValue::MakeNumber(FMath::Max(0, P.Value.Mag)));
	}
	return M;
}

int32 FLogicWorld::CallWorldFunction(const FString& Name, TArray<FLogicValue>& A, FLogicValue& Out, FString& OutError) const
{
	auto Key = [](const FLogicValue& V) { return V.Type == FLogicValue::EType::Map && V.M->Contains(TEXT("id")) ? V.M->FindRef(TEXT("id")).ToText() : V.ToText(); };
	if (Name == TEXT("pos"))
	{
		FVector L;
		if (A.Num() != 1 || !OwnerLocation(Key(A[0]), L))
		{
			OutError = TEXT("pos(id) needs an existing node id or 'player'");
			return -1;
		}
		Out = FLogicValue::MakeList({FLogicValue::MakeNumber(L.X), FLogicValue::MakeNumber(L.Y), FLogicValue::MakeNumber(L.Z)});
		return 1;
	}
	if (Name == TEXT("hp") || Name == TEXT("alive"))
	{
		if (A.Num() != 1)
		{
			OutError = Name + TEXT("(id) takes one argument");
			return -1;
		}
		const FString K = Key(A[0]);
		const FHealthState* H = Healths.Find(K);
		if (Name == TEXT("hp"))
		{
			Out = FLogicValue::MakeNumber(H ? H->Current : 0.0);
		}
		else
		{
			FBox B;
			Out = FLogicValue::MakeBool(H ? !H->bDead && (K == TEXT("player") || VictimBox(K, B)) : VictimBox(K, B));
		}
		return 1;
	}
	if (Name == TEXT("has"))
	{
		const FLogicValue* Inv = Vars.Find(TEXT("inventory"));
		Out = FLogicValue::MakeBool(A.Num() == 1 && Inv && Inv->Type == FLogicValue::EType::Map && Inv->M->Contains(A[0].ToText())
			&& Inv->M->FindRef(A[0].ToText()).N > 0.0);
		return 1;
	}
	if (Name == TEXT("nodes"))
	{
		if (A.Num() != 1)
		{
			OutError = TEXT("nodes('tag:x') takes one argument");
			return -1;
		}
		TArray<FString> Ids;
		FString Err;
		ResolveNodes({A[0].ToText()}, Ids, Err);
		TArray<FLogicValue> Items;
		for (const FString& Id : Ids)
		{
			if (!IsHidden(Id))
			{
				Items.Add(FLogicValue::MakeString(Id));
			}
		}
		Out = FLogicValue::MakeList(MoveTemp(Items));
		return 1;
	}
	return 0;
}

// ------------------------------------------------------------------ weapons

FString FLogicWorld::EquippedWeapon(const FString& OwnerKey) const
{
	if (const FString* W = EquipDefs.Find(OwnerKey))
	{
		return *W;
	}
	FRuntimeSceneGraph* G = GraphFor(World.Get());
	const FRuntimeSceneNode* N = G && OwnerKey != TEXT("player") ? G->Find(OwnerKey) : nullptr;
	if (N)
	{
		for (const TPair<FString, FString>& P : EquipDefs)
		{
			if (P.Key != TEXT("player") && DirectMatch(*N, P.Key, *G))
			{
				return P.Value;
			}
		}
	}
	return FString();
}

void FLogicWorld::CollectOwnerActors(const FString& Owner, TArray<AActor*>& Out) const
{
	if (Owner == TEXT("player"))
	{
		if (ACharacter* C = Player.Get())
		{
			Out.Add(C);
			C->GetAttachedActors(Out, false, true);
		}
		return;
	}
	FRuntimeSceneGraph* G = GraphFor(World.Get());
	if (const FRuntimeSceneNode* N = G ? G->Find(Owner) : nullptr; N && N->Actor.IsValid())
	{
		Out.Add(N->Actor.Get());
		N->Actor->GetAttachedActors(Out, false, true);
	}
}

bool FLogicWorld::StartReload(const FString& Owner, const TSharedPtr<FJsonObject>& W, FWeaponState& S)
{
	const int32 Magazine = static_cast<int32>(Num(W, TEXT("magazine"), 0));
	if (Magazine <= 0 || S.ReloadUntil > PlayTime || S.Mag >= Magazine)
	{
		return false;
	}
	int32 Take = Magazine - FMath::Max(0, S.Mag);
	const FString AmmoVar = Str(W, TEXT("ammo_var"));
	if (!AmmoVar.IsEmpty())
	{
		const FLogicValue* Reserve = Vars.Find(AmmoVar);
		const int32 Have = Reserve ? static_cast<int32>(Reserve->N) : 0;
		Take = FMath::Min(Take, Have);
		if (Take <= 0)
		{
			return false;
		}
		SetVar(AmmoVar, FLogicValue::MakeNumber(Have - Take), true);
	}
	S.ReloadAmount = Take;
	S.ReloadUntil = PlayTime + Num(W, TEXT("reload"), 1.5);
	return true;
}

bool FLogicWorld::FireWeapon(const FString& Owner, const FString& WeaponName, const FVector* AimPointIn, FString& OutError)
{
	const FString Name = WeaponName.IsEmpty() ? EquippedWeapon(Owner) : WeaponName;
	const TSharedPtr<FJsonObject>* WP = WeaponDefs.Find(Name);
	if (!WP)
	{
		OutError = Name.IsEmpty() ? FString::Printf(TEXT("'%s' has no weapon (logic.equip)"), *Owner) : FString::Printf(TEXT("no weapon '%s'"), *Name);
		return false;
	}
	const TSharedPtr<FJsonObject>& W = *WP;
	if (const FHealthState* H = Healths.Find(Owner); H && H->bDead)
	{
		return false;
	}
	FVector From;
	if (!OwnerLocation(Owner, From))
	{
		OutError = FString::Printf(TEXT("no shooter '%s'"), *Owner);
		return false;
	}
	const FString StateKey = (WeaponName.IsEmpty() || WeaponName == EquippedWeapon(Owner)) ? Owner : Owner + TEXT("#") + Name;
	FWeaponState& S = WeaponStates.FindOrAdd(StateKey);
	const int32 Magazine = static_cast<int32>(Num(W, TEXT("magazine"), 0));
	if (S.Mag < 0)
	{
		S.Mag = Magazine;
	}
	if (PlayTime < S.ReloadUntil || PlayTime < S.NextShot || S.BurstLeft > 0)
	{
		return false;
	}
	if (Magazine > 0 && S.Mag <= 0)
	{
		StartReload(StateKey, W, S);
		return false;
	}
	FVector Aim;
	if (AimPointIn)
	{
		Aim = *AimPointIn;
	}
	else if (Owner == TEXT("player"))
	{
		Aim = IsTopDown() ? AimPoint : IsFlight() && Player.IsValid() ? From + Player->GetActorForwardVector() * 8000.0 : From + FVector(Facing * 1000.0, 0, 0);
	}
	else
	{
		FRuntimeSceneGraph* G = GraphFor(World.Get());
		const FRuntimeSceneNode* N = G ? G->Find(Owner) : nullptr;
		Aim = From + (N && N->Actor.IsValid() ? N->Actor->GetActorForwardVector() : FVector::ForwardVector) * 1000.0;
	}
	if (IsTopDown())
	{
		Aim.Z = From.Z; // shots travel level with the shooter in top-down games
	}
	FVector Dir = (Aim - From).GetSafeNormal();
	if (Dir.IsNearlyZero())
	{
		Dir = FVector::ForwardVector;
	}
	const double Rate = FMath::Max(0.01, Num(W, TEXT("rate"), 4.0));
	S.NextShot = PlayTime + 1.0 / Rate;
	const FVector Muzzle = From + Dir * Num(W, TEXT("muzzle"), 60.0) + FVector(0, 0, Num(W, TEXT("muzzle_height"), 0.0));
	SpawnShots(Owner, W, Muzzle, Dir);
	if (Owner == TEXT("player"))
	{
		Recoil = 1.0;
		HearNoise(From, Num(W, TEXT("noise"), 1600.0));
	}
	else if (!IsTweening(Owner))
	{
		FVector Back = -Dir * LogicTuning::Get(TEXT("combat.fire_kick"));
		TSharedRef<FJsonObject> Kick = MakeShared<FJsonObject>();
		Kick->SetField(TEXT("move"), RuntimeJson::Vec(Back));
		Kick->SetNumberField(TEXT("duration"), 0.05);
		Kick->SetStringField(TEXT("loop"), TEXT("yoyo"));
		Kick->SetBoolField(TEXT("add"), true);
		FString Err;
		StartTween(Owner, Kick, Err);
	}
	if (const FString Fb = Str(W, TEXT("feedback")); !Fb.IsEmpty())
	{
		PlayFeedback(Fb, Muzzle, Owner == TEXT("player") ? FString() : Owner);
	}
	const int32 Burst = static_cast<int32>(Num(W, TEXT("burst"), 1));
	if (Burst > 1)
	{
		S.BurstLeft = Burst - 1;
		S.NextBurst = PlayTime + Num(W, TEXT("burst_gap"), 0.08);
		S.BurstAim = Aim;
	}
	if (Magazine > 0)
	{
		S.Mag = FMath::Max(0, S.Mag - 1);
		bool bAuto = true;
		W->TryGetBoolField(TEXT("auto_reload"), bAuto);
		if (S.Mag == 0 && bAuto)
		{
			StartReload(StateKey, W, S);
		}
	}
	FLogicEvent E;
	E.On = TEXT("fired");
	E.Name = Name;
	E.Target = Owner == TEXT("player") ? FString() : Owner;
	E.Data.M->Add(TEXT("shooter"), FLogicValue::MakeString(Owner));
	E.Data.M->Add(TEXT("weapon"), FLogicValue::MakeString(Name));
	Queue.Add(E);
	if (!bProcessing)
	{
		ProcessQueue(nullptr);
	}
	return true;
}

void FLogicWorld::SpawnShots(const FString& Owner, const TSharedPtr<FJsonObject>& W, const FVector& Muzzle, const FVector& Dir)
{
	const int32 Count = FMath::Clamp(static_cast<int32>(Num(W, TEXT("per_shot"), 1)), 1, 64);
	const double Spread = Num(W, TEXT("spread"), 0.0);
	const bool bFan = Str(W, TEXT("spread_mode"), TEXT("random")) == TEXT("fan");
	FVector Axis = FVector::CrossProduct(Dir, FVector::UpVector).GetSafeNormal();
	if (IsTopDown() || Owner != TEXT("player") || Axis.IsNearlyZero())
	{
		Axis = FVector::UpVector;
	}
	const FString Mode = Str(W, TEXT("mode"), TEXT("projectile"));
	const FString Pool = Str(W, TEXT("pool"));
	TArray<AActor*> Ignore;
	CollectOwnerActors(Owner, Ignore);
	for (int32 i = 0; i < Count; ++i)
	{
		double Angle = 0.0;
		if (Spread > 0.0)
		{
			Angle = bFan && Count > 1 ? FMath::Lerp(-Spread, Spread, static_cast<double>(i) / (Count - 1)) : Random.FRandRange(-Spread, Spread);
		}
		const FVector D = Dir.RotateAngleAxis(Angle, Axis);
		if (Mode == TEXT("hitscan"))
		{
			const double Range = Num(W, TEXT("range"), 3000.0);
			FVector End = Muzzle + D * Range;
			UWorld* Wd = World.Get();
			if (Wd)
			{
				FCollisionQueryParams Q(SCENE_QUERY_STAT(LogicHitscan), false);
				Q.AddIgnoredActors(Ignore);
				if (ACharacter* C = Player.Get())
				{
					Q.AddIgnoredActor(C);
				}
				FHitResult Hit;
				if (Wd->LineTraceSingleByChannel(Hit, Muzzle, End, ECC_Visibility, Q))
				{
					End = Hit.ImpactPoint;
				}
			}
			FString Best;
			double BestT = 2.0;
			for (const TPair<FString, FHealthState>& H : Healths)
			{
				FBox B;
				double T;
				if (H.Key != Owner && !H.Value.bDead && VictimMatches(H.Key, Str(W, TEXT("hits"))) && VictimBox(H.Key, B) && SegmentBox(Muzzle, End, B, T) && T < BestT)
				{
					BestT = T;
					Best = H.Key;
				}
			}
			const FVector Impact = Best.IsEmpty() ? End : Muzzle + (End - Muzzle) * BestT;
			if (!Best.IsEmpty())
			{
				ApplyDamage(Best, Num(W, TEXT("damage"), 1.0), Str(W, TEXT("type")), Owner, D * Num(W, TEXT("knockback"), 0.0));
			}
			if (!Pool.IsEmpty())
			{
				FString Err;
				const FString Tracer = AcquireFromPool(Pool, Muzzle, Err);
				if (!Tracer.IsEmpty())
				{
					TSharedRef<FJsonObject> T = MakeShared<FJsonObject>();
					T->SetField(TEXT("location"), RuntimeJson::Vec(Impact));
					T->SetNumberField(TEXT("duration"), 0.06);
					T->SetStringField(TEXT("ease"), TEXT("linear"));
					T->SetStringField(TEXT("then"), TEXT("release"));
					StartTween(Tracer, T, Err);
				}
			}
			continue;
		}
		FProjectile P;
		P.Pos = Muzzle;
		P.Dir = D;
		P.Speed = Num(W, TEXT("speed"), 2500.0);
		P.Life = Num(W, TEXT("lifetime"), 1.5);
		P.Damage = Num(W, TEXT("damage"), 1.0);
		P.Knockback = Num(W, TEXT("knockback"), 0.0);
		P.Radius = Num(W, TEXT("radius"), 12.0);
		P.Type = Str(W, TEXT("type"));
		P.Owner = Owner;
		P.Hits = Str(W, TEXT("hits"));
		// Shots inherit the shooter's flight speed; homing missiles lock the best target in front.
		if (Owner == TEXT("player") && IsFlight())
		{
			P.Speed += FlightSpeed;
		}
		else if (const FBrainAgent* Ag = Agents.Find(Owner))
		{
			P.Speed += Ag->Speed;
		}
		P.Homing = Num(W, TEXT("homing"), 0.0);
		if (P.Homing > 0.0)
		{
			P.Target = LockTarget(Owner, Muzzle, D, Num(W, TEXT("lock_range"), 12000.0), Num(W, TEXT("lock_angle"), 25.0), P.Hits);
		}
		if (!Pool.IsEmpty())
		{
			FString Err;
			P.Node = AcquireFromPool(Pool, Muzzle, Err);
			FRuntimeSceneGraph* G = GraphFor(World.Get());
			if (const FRuntimeSceneNode* N = G ? G->Find(P.Node) : nullptr; N && N->Actor.IsValid())
			{
				N->Actor->SetActorRotation(D.Rotation());
			}
		}
		Projectiles.Add(P);
	}
}

void FLogicWorld::TickWeapons()
{
	for (TPair<FString, FWeaponState>& P : WeaponStates)
	{
		FWeaponState& S = P.Value;
		FString OwnerKey = P.Key, Name;
		if (!P.Key.Split(TEXT("#"), &OwnerKey, &Name))
		{
			Name = EquippedWeapon(P.Key);
		}
		const TSharedPtr<FJsonObject>* W = WeaponDefs.Find(Name);
		if (S.ReloadUntil > 0.0 && PlayTime >= S.ReloadUntil)
		{
			S.ReloadUntil = -1.0;
			S.Mag = FMath::Max(0, S.Mag) + S.ReloadAmount;
			S.ReloadAmount = 0;
			FLogicEvent E;
			E.On = TEXT("reloaded");
			E.Name = Name;
			E.Target = OwnerKey == TEXT("player") ? FString() : OwnerKey;
			E.Data.M->Add(TEXT("shooter"), FLogicValue::MakeString(OwnerKey));
			Queue.Add(E);
		}
		if (S.BurstLeft > 0 && PlayTime >= S.NextBurst && W)
		{
			FVector From;
			if (OwnerLocation(OwnerKey, From))
			{
				FVector Aim = S.BurstAim;
				if (IsTopDown())
				{
					Aim.Z = From.Z;
				}
				const FVector Dir = (Aim - From).GetSafeNormal();
				SpawnShots(OwnerKey, *W, From + Dir * Num(*W, TEXT("muzzle"), 60.0), Dir.IsNearlyZero() ? FVector::ForwardVector : Dir);
			}
			--S.BurstLeft;
			S.NextBurst = PlayTime + Num(*W, TEXT("burst_gap"), 0.08);
		}
	}
}

void FLogicWorld::TickProjectiles(double Dt)
{
	UWorld* W = World.Get();
	FRuntimeSceneGraph* G = GraphFor(W);
	for (int32 i = Projectiles.Num() - 1; i >= 0; --i)
	{
		FProjectile& P = Projectiles[i];
		if (P.Homing > 0.0 && !P.Target.IsEmpty())
		{
			FBox TB;
			const FHealthState* TH = Healths.Find(P.Target);
			if (TH && !TH->bDead && VictimBox(P.Target, TB))
			{
				const FVector Want = (TB.GetCenter() - P.Pos).GetSafeNormal();
				const double Angle = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(P.Dir, Want), -1.0, 1.0)));
				const double Max = P.Homing * Dt;
				P.Dir = Angle <= Max ? Want : FQuat::Slerp(FQuat::Identity, FQuat::FindBetweenNormals(P.Dir, Want), Max / Angle).RotateVector(P.Dir).GetSafeNormal();
			}
			if (const FRuntimeSceneNode* PN = G ? G->Find(P.Node) : nullptr; PN && PN->Actor.IsValid())
			{
				PN->Actor->SetActorRotation(P.Dir.Rotation());
			}
		}
		const FVector Old = P.Pos;
		const FVector New = Old + P.Dir * P.Speed * Dt;
		P.Life -= Dt;
		double WallT = 2.0;
		if (W)
		{
			FCollisionQueryParams Q(SCENE_QUERY_STAT(LogicProjectile), false);
			TArray<AActor*> Ignore;
			CollectOwnerActors(P.Owner, Ignore);
			Q.AddIgnoredActors(Ignore);
			if (ACharacter* C = Player.Get())
			{
				Q.AddIgnoredActor(C);
			}
			if (const FRuntimeSceneNode* N = G ? G->Find(P.Node) : nullptr; N && N->Actor.IsValid())
			{
				Q.AddIgnoredActor(N->Actor.Get());
			}
			FHitResult Hit;
			if (W->LineTraceSingleByChannel(Hit, Old, New, ECC_Visibility, Q))
			{
				WallT = Hit.Time;
			}
		}
		FString Victim;
		double VictimT = 2.0;
		for (const TPair<FString, FHealthState>& H : Healths)
		{
			FBox B;
			double T;
			if (H.Key != P.Owner && !H.Value.bDead && VictimMatches(H.Key, P.Hits) && VictimBox(H.Key, B) && SegmentBox(Old, New, B.ExpandBy(P.Radius), T) && T < VictimT)
			{
				VictimT = T;
				Victim = H.Key;
			}
		}
		bool bEnd = P.Life <= 0.0;
		if (!Victim.IsEmpty() && VictimT <= WallT)
		{
			const FProjectile Copy = P;
			Projectiles.RemoveAt(i);
			ApplyDamage(Victim, Copy.Damage, Copy.Type, Copy.Owner, Copy.Dir * Copy.Knockback);
			if (!Copy.Node.IsEmpty())
			{
				ReleaseToPool(Copy.Node);
			}
			continue;
		}
		if (WallT <= 1.0)
		{
			bEnd = true;
		}
		if (bEnd)
		{
			if (!P.Node.IsEmpty())
			{
				ReleaseToPool(P.Node);
			}
			Projectiles.RemoveAt(i);
			continue;
		}
		P.Pos = New;
		if (const FRuntimeSceneNode* N = G ? G->Find(P.Node) : nullptr; N && N->Actor.IsValid())
		{
			N->Actor->SetActorLocation(New);
		}
	}
}

void FLogicWorld::TickHitZones()
{
	for (const TPair<FString, TSharedPtr<FJsonObject>>& Z : HitZoneDefs)
	{
		TArray<FString> Zones;
		FString Err;
		ResolveNodes({Z.Key}, Zones, Err);
		const FString Affects = Str(Z.Value, TEXT("affects"), TEXT("any"));
		const bool bStay = Str(Z.Value, TEXT("mode"), TEXT("enter")) == TEXT("stay");
		const double Interval = FMath::Max(0.05, Num(Z.Value, TEXT("interval"), 0.5));
		for (const FString& ZoneId : Zones)
		{
			FBox ZB;
			if (!VictimBox(ZoneId, ZB))
			{
				ZoneTimes.Remove(ZoneId);
				continue;
			}
			TMap<FString, double>& Times = ZoneTimes.FindOrAdd(ZoneId);
			for (const TPair<FString, FHealthState>& H : Healths)
			{
				FBox VB;
				if (H.Key == ZoneId || H.Value.bDead || !VictimMatches(H.Key, Affects) || !VictimBox(H.Key, VB) || !VB.Intersect(ZB))
				{
					Times.Remove(H.Key);
					continue;
				}
				const double* Next = Times.Find(H.Key);
				if (Next && (!bStay || PlayTime < *Next))
				{
					continue;
				}
				Times.Add(H.Key, PlayTime + Interval);
				const double Lo = Num(Z.Value, TEXT("damage"), 1.0);
				const double Amount = Lo + Random.FRand() * FMath::Max(0.0, Num(Z.Value, TEXT("damage_max"), Lo) - Lo);
				FVector Push = (VB.GetCenter() - ZB.GetCenter()).GetSafeNormal2D() * Num(Z.Value, TEXT("knockback"), 0.0);
				if (!Push.IsNearlyZero())
				{
					Push.Z = Num(Z.Value, TEXT("knockback"), 0.0) * 0.4;
				}
				ApplyDamage(H.Key, Amount, Str(Z.Value, TEXT("type")), ZoneId, Push);
			}
		}
	}
}

void FLogicWorld::TickCombat(double Dt)
{
	if (HealthDefs.Num() == 0 && WeaponDefs.Num() == 0 && HitZoneDefs.Num() == 0)
	{
		return;
	}
	TickWeapons();
	TickProjectiles(Dt);
	TickHitZones();
}

void FLogicWorld::ResetCombat()
{
	for (const FProjectile& P : Projectiles)
	{
		if (!P.Node.IsEmpty())
		{
			ReleaseToPool(P.Node);
		}
	}
	Projectiles.Reset();
	WeaponStates.Reset();
	ZoneTimes.Reset();
	RebuildHealths(true);
	// The player's magazine starts full, so HUD counters are right before the first shot.
	if (const TSharedPtr<FJsonObject>* W = WeaponDefs.Find(EquippedWeapon(TEXT("player"))))
	{
		WeaponStates.FindOrAdd(TEXT("player")).Mag = static_cast<int32>(Num(*W, TEXT("magazine"), 0));
	}
}
