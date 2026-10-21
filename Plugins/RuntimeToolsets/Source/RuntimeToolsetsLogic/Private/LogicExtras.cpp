// Spawners and waves, loot tables, interactables, save slots.
#include "LogicRuntime.h"
#include "LogicJsonHelpers.h"
#include "RuntimeJson.h"
#include "RuntimeSceneGraph.h"
#include "RuntimeToolsetsCore.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "InputCoreTypes.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	/** Number or [min, max] -> a random value in range. */
	double Range(const TSharedPtr<FJsonObject>& J, const TCHAR* Field, double Default, FRandomStream& R)
	{
		const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
		if (J.IsValid() && J->TryGetArrayField(Field, A) && A->Num() == 2)
		{
			return R.FRandRange((*A)[0]->AsNumber(), (*A)[1]->AsNumber());
		}
		return Num(J, Field, Default);
	}

	FString SaveDir()
	{
		return FPaths::ProjectSavedDir() / TEXT("RuntimeToolsets") / TEXT("Saves");
	}
}

// ------------------------------------------------------------------ spawners

void FLogicWorld::SetSpawnerActive(const FString& Name, bool bActive, bool bReset)
{
	FSpawnerState& S = Spawners.FindOrAdd(Name);
	if (bReset)
	{
		S = FSpawnerState();
	}
	S.bActive = bActive;
	if (bActive)
	{
		const TSharedPtr<FJsonObject>* Def = SpawnerDefs.Find(Name);
		S.Next = PlayTime + (Def ? Num(*Def, TEXT("start_delay"), 0.5) : 0.5);
		if (bReset && Def && (*Def)->HasField(TEXT("waves")))
		{
			FLogicEvent E;
			E.On = TEXT("wave_start");
			E.Name = Name;
			E.Data.M->Add(TEXT("wave"), FLogicValue::MakeNumber(1));
			Queue.Add(E);
		}
	}
}

void FLogicWorld::TickSpawners(double Dt)
{
	for (TPair<FString, TSharedPtr<FJsonObject>>& P : SpawnerDefs)
	{
		FSpawnerState* SP = Spawners.Find(P.Key);
		if (!SP || !SP->bActive)
		{
			continue;
		}
		FSpawnerState& S = *SP;
		const TSharedPtr<FJsonObject>& D = P.Value;
		const FString Pool = Str(D, TEXT("pool"));
		S.Alive.RemoveAll([this](const FString& Id) { return !PoolBusy.Contains(Id); });
		const TArray<TSharedPtr<FJsonValue>>* Waves = nullptr;
		const bool bWaves = D->TryGetArrayField(TEXT("waves"), Waves) && Waves->Num() > 0;
		int32 Count = static_cast<int32>(Num(D, TEXT("total"), -1));
		int32 MaxAlive = static_cast<int32>(Num(D, TEXT("max_alive"), 5));
		TSharedPtr<FJsonObject> Wave;
		if (bWaves)
		{
			if (S.Wave >= Waves->Num())
			{
				continue;
			}
			Wave = (*Waves)[S.Wave]->AsObject();
			Count = static_cast<int32>(Num(Wave, TEXT("count"), 3));
			MaxAlive = static_cast<int32>(Num(Wave, TEXT("max_alive"), MaxAlive));
			if (S.bWaitingNextWave)
			{
				if (PlayTime < S.Next)
				{
					continue;
				}
				S.bWaitingNextWave = false;
				FLogicEvent E;
				E.On = TEXT("wave_start");
				E.Name = P.Key;
				E.Data.M->Add(TEXT("wave"), FLogicValue::MakeNumber(S.Wave + 1));
				Queue.Add(E);
			}
			if (S.SpawnedInWave >= Count && S.Alive.Num() == 0)
			{
				FLogicEvent E;
				E.On = TEXT("wave_cleared");
				E.Name = P.Key;
				E.Data.M->Add(TEXT("wave"), FLogicValue::MakeNumber(S.Wave + 1));
				Queue.Add(E);
				++S.Wave;
				S.SpawnedInWave = 0;
				if (S.Wave >= Waves->Num())
				{
					FLogicEvent Done;
					Done.On = TEXT("waves_complete");
					Done.Name = P.Key;
					Queue.Add(Done);
					S.bActive = false;
				}
				else
				{
					S.bWaitingNextWave = true;
					S.Next = PlayTime + Num(D, TEXT("wave_gap"), 3.0);
				}
				continue;
			}
		}
		if (PlayTime < S.Next || S.Alive.Num() >= MaxAlive || (Count >= 0 && (bWaves ? S.SpawnedInWave : S.SpawnedTotal) >= Count))
		{
			continue;
		}
		// Where: fixed points, or around the player.
		FVector At = FVector::ZeroVector;
		const TArray<TSharedPtr<FJsonValue>>* Points = nullptr;
		const TSharedPtr<FJsonObject>* Around = nullptr;
		if (D->TryGetArrayField(TEXT("points"), Points) && Points->Num() > 0)
		{
			RuntimeJson::ToVector((*Points)[Random.RandRange(0, Points->Num() - 1)], At);
		}
		else if (D->TryGetObjectField(TEXT("around_player"), Around))
		{
			FVector PL = FVector::ZeroVector;
			OwnerLocation(TEXT("player"), PL);
			const double R = Range(*Around, TEXT("radius"), 3000.0, Random);
			const double A = Random.FRand() * 2.0 * PI;
			At = PL + FVector(FMath::Cos(A) * R, FMath::Sin(A) * R, 0.0);
			if ((*Around)->HasField(TEXT("height")))
			{
				At.Z = Range(*Around, TEXT("height"), PL.Z, Random);
			}
		}
		FString Err;
		const FString Id = AcquireFromPool(Pool, At, Err);
		if (Id.IsEmpty())
		{
			S.Next = PlayTime + 1.0;
			continue;
		}
		FVector PL;
		FRotator Rot = FRotator::ZeroRotator;
		bool bFace = true;
		D->TryGetBoolField(TEXT("face_player"), bFace);
		if (bFace && OwnerLocation(TEXT("player"), PL))
		{
			Rot = (PL - At).Rotation();
			if (!IsFlight())
			{
				Rot.Pitch = 0.0;
			}
		}
		if (FRuntimeSceneGraph* G = IRuntimeToolsetsCore::Get().GetSceneGraph(World.Get()))
		{
			if (const FRuntimeSceneNode* N = G->Find(Id); N && N->Actor.IsValid())
			{
				N->Actor->SetActorRotation(Rot);
			}
		}
		ResetHealth(Id);
		ResetAgentAt(Id, At, Rot);
		S.Alive.Add(Id);
		++S.SpawnedInWave;
		++S.SpawnedTotal;
		S.Next = PlayTime + Range(Wave.IsValid() && Wave->HasField(TEXT("interval")) ? Wave : D, TEXT("interval"), 1.5, Random);
		FLogicEvent E;
		E.On = TEXT("spawned");
		E.Name = P.Key;
		E.Target = Id;
		E.Data.M->Add(TEXT("wave"), FLogicValue::MakeNumber(S.Wave + 1));
		Queue.Add(E);
	}
}

// ------------------------------------------------------------------ loot

void FLogicWorld::DropLoot(const FString& Name, const FVector& At)
{
	const TSharedPtr<FJsonObject>* D = LootDefs.Find(Name);
	const TArray<TSharedPtr<FJsonValue>>* Entries = nullptr;
	if (!D || !(*D)->TryGetArrayField(TEXT("entries"), Entries) || Entries->Num() == 0)
	{
		return;
	}
	double Total = 0.0;
	for (const TSharedPtr<FJsonValue>& V : *Entries)
	{
		Total += FMath::Max(0.0, Num(V->AsObject(), TEXT("weight"), 1.0));
	}
	const int32 Count = FMath::Max(0, FMath::RoundToInt(Range(*D, TEXT("count"), 1.0, Random)));
	const double Scatter = Num(*D, TEXT("scatter"), 120.0);
	for (int32 i = 0; i < Count && Total > 0.0; ++i)
	{
		// Weighted pick: walk the cumulative weights.
		double Roll = Random.FRand() * Total;
		TSharedPtr<FJsonObject> Pick;
		for (const TSharedPtr<FJsonValue>& V : *Entries)
		{
			Roll -= FMath::Max(0.0, Num(V->AsObject(), TEXT("weight"), 1.0));
			if (Roll <= 0.0)
			{
				Pick = V->AsObject();
				break;
			}
		}
		const FString Pool = Str(Pick, TEXT("pool"));
		if (Pool.IsEmpty() || Pool == TEXT("none"))
		{
			continue;
		}
		FString Err;
		const FVector P = At + FVector(Random.FRandRange(-Scatter, Scatter), Random.FRandRange(-Scatter, Scatter), 0.0);
		AcquireFromPool(Pool, P, Err);
	}
}

// ------------------------------------------------------------------ interactables

void FLogicWorld::TickInteract()
{
	CurrentInteract.Reset();
	ACharacter* C = Player.Get();
	if (!C || InteractDefs.Num() == 0 || bFrozen)
	{
		return;
	}
	const FVector PL = C->GetActorLocation();
	double Best = TNumericLimits<double>::Max();
	TSharedPtr<FJsonObject> BestDef;
	for (const TPair<FString, TSharedPtr<FJsonObject>>& P : InteractDefs)
	{
		TArray<FString> Ids;
		FString Err;
		ResolveNodes({P.Key}, Ids, Err);
		for (const FString& Id : Ids)
		{
			FBox B;
			if (InteractUsed.Contains(Id) || !VictimBox(Id, B) || PlayTime < InteractCooldown.FindRef(Id))
			{
				continue;
			}
			const double D = FVector::Dist2D(B.GetCenter(), PL);
			if (D > Num(P.Value, TEXT("radius"), 250.0) || D >= Best)
			{
				continue;
			}
			const FString Requires = Str(P.Value, TEXT("requires"));
			if (!Requires.IsEmpty())
			{
				FFrame Frame;
				Frame.Locals.Add(TEXT("self"), FLogicValue::MakeString(Id));
				FLogicScope Scope = MakeScope(Frame);
				if (!LogicExpr::Evaluate(Requires, Scope).Truthy() && !P.Value->HasField(TEXT("locked_prompt")))
				{
					continue;
				}
			}
			Best = D;
			CurrentInteract = Id;
			BestDef = P.Value;
		}
	}
	if (CurrentInteract.IsEmpty())
	{
		return;
	}
	UWorld* W = World.Get();
	APlayerController* PC = W ? W->GetFirstPlayerController() : nullptr;
	const FKey Key(*Str(BestDef, TEXT("key"), TEXT("E")));
	if (PC && Key.IsValid() && PC->WasInputKeyJustPressed(Key))
	{
		bool bOk = true;
		const FString Requires = Str(BestDef, TEXT("requires"));
		if (!Requires.IsEmpty())
		{
			FFrame Frame;
			Frame.Locals.Add(TEXT("self"), FLogicValue::MakeString(CurrentInteract));
			FLogicScope Scope = MakeScope(Frame);
			bOk = LogicExpr::Evaluate(Requires, Scope).Truthy();
		}
		FLogicEvent E;
		E.On = bOk ? TEXT("interact") : TEXT("interact_denied");
		E.Target = CurrentInteract;
		if (FRuntimeSceneGraph* G = IRuntimeToolsetsCore::Get().GetSceneGraph(W))
		{
			if (const FRuntimeSceneNode* N = G->Find(CurrentInteract))
			{
				E.Name = N->Name;
			}
		}
		E.Data.M->Add(TEXT("data"), FindNodeData(CurrentInteract));
		Fire(MoveTemp(E));
		if (bOk)
		{
			bool bOnce = false;
			BestDef->TryGetBoolField(TEXT("once"), bOnce);
			if (bOnce)
			{
				InteractUsed.Add(CurrentInteract);
			}
			InteractCooldown.Add(CurrentInteract, PlayTime + Num(BestDef, TEXT("cooldown"), 0.3));
		}
	}
}

// ------------------------------------------------------------------ saves

bool FLogicWorld::SaveSlot(const FString& Slot, const TArray<FString>& Names, FString& OutError) const
{
	for (const TCHAR C : Slot)
	{
		if (!FChar::IsAlnum(C) && C != TEXT('_') && C != TEXT('-'))
		{
			OutError = TEXT("slot names use letters, digits, _ and -");
			return false;
		}
	}
	TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
	for (const TPair<FString, FLogicValue>& P : Vars)
	{
		if (Names.Num() == 0 || Names.Contains(P.Key))
		{
			J->SetField(P.Key, P.Value.ToJson());
		}
	}
	IFileManager::Get().MakeDirectory(*SaveDir(), true);
	if (!FFileHelper::SaveStringToFile(RuntimeJson::Write(J), *(SaveDir() / Slot + TEXT(".json"))))
	{
		OutError = TEXT("could not write the save file");
		return false;
	}
	return true;
}

bool FLogicWorld::LoadSlot(const FString& Slot, FString& OutError)
{
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *(SaveDir() / Slot + TEXT(".json"))))
	{
		OutError = TEXT("no save in this slot yet");
		return false;
	}
	const TSharedPtr<FJsonObject> J = RuntimeJson::Parse(Text);
	if (!J.IsValid())
	{
		OutError = TEXT("the save file is damaged");
		return false;
	}
	for (const TPair<FString, TSharedPtr<FJsonValue>>& P : J->Values)
	{
		if (Vars.Contains(P.Key))
		{
			SetVar(P.Key, FLogicValue::FromJson(P.Value), true);
		}
	}
	return true;
}

void FLogicWorld::ResetExtras()
{
	Spawners.Reset();
	for (const TPair<FString, TSharedPtr<FJsonObject>>& P : SpawnerDefs)
	{
		bool bAuto = true;
		P.Value->TryGetBoolField(TEXT("autostart"), bAuto);
		SetSpawnerActive(P.Key, bAuto, true);
	}
	InteractUsed.Reset();
	InteractCooldown.Reset();
	CurrentInteract.Reset();
	DashReadyAt = 0.0;
	DashUntil = 0.0;
	GridT = 1.0;
	FlightSpeed = 0.0;
	FlightVelocity = FVector::ZeroVector;
	FlightThrottle = 0.6;
}
