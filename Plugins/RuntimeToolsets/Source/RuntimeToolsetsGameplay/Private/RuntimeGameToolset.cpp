#include "RuntimeGameToolset.h"
#include "RuntimeToolHelpers.h"
#include "CollisionQueryParams.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/WorldSettings.h"

FGameToolGameState URuntimeGameToolset::GetGameState()
{
	FGameToolGameState S;
	UWorld* World = RuntimeToolCall::GetWorld();
	S.Map = World->GetMapName();
	S.TimeSeconds = static_cast<float>(RuntimeJson::Round(World->GetTimeSeconds()));
	S.RealTimeSeconds = static_cast<float>(RuntimeJson::Round(World->GetRealTimeSeconds()));
	S.bPaused = World->IsPaused();
	if (const AWorldSettings* WS = World->GetWorldSettings())
	{
		S.TimeDilation = WS->TimeDilation;
	}
	if (const AGameModeBase* GM = World->GetAuthGameMode())
	{
		S.GameMode = GM->GetClass()->GetName();
		if (GM->DefaultPawnClass)
		{
			S.DefaultPawn = GM->DefaultPawnClass->GetName();
		}
	}
	if (const AGameStateBase* GS = World->GetGameState())
	{
		S.GameState = GS->GetClass()->GetName();
		S.PlayerStates = GS->PlayerArray.Num();
	}
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		++S.Players;
	}
	return S;
}

TArray<FGameToolPlayerInfo> URuntimeGameToolset::ListPlayers()
{
	TArray<FGameToolPlayerInfo> Out;
	for (FConstPlayerControllerIterator It = RuntimeToolCall::GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		if (!PC)
		{
			continue;
		}
		FGameToolPlayerInfo& I = Out.AddDefaulted_GetRef();
		I.Controller = PC;
		I.bLocal = PC->IsLocalController();
		if (const APlayerState* PS = PC->PlayerState)
		{
			I.Name = PS->GetPlayerName();
		}
		I.Rotation = PC->GetControlRotation();
		if (APawn* Pawn = PC->GetPawn())
		{
			I.Pawn = Pawn;
			I.Location = Pawn->GetActorLocation();
		}
	}
	return Out;
}

FGameToolTeleportResult URuntimeGameToolset::Teleport(const FVector& Location, const FRotator& Rotation, AActor* Target, bool bSnapToGround)
{
	FGameToolTeleportResult Result;
	UWorld* World = RuntimeToolCall::GetWorld();
	AActor* Actor = Target;
	if (!Actor)
	{
		APlayerController* PC = World->GetFirstPlayerController();
		Actor = PC ? PC->GetPawn() : nullptr;
		if (!Actor)
		{
			RuntimeToolCall::Fail(ERuntimeToolError::WrongState, TEXT("no player pawn to teleport"),
				TEXT("Pass a target (node id, path or actor:Name), or possess a pawn first (camera.possess)"));
			return Result;
		}
	}
	FTransform T = Actor->GetActorTransform();
	FVector Dest = Location;
	if (bSnapToGround)
	{
		// Epic snap_to_ground: trace down below the moved bounds and rest their bottom on the hit.
		const FBox Box = RuntimeTools::ActorBox(Actor).ShiftBy(Location - Actor->GetActorLocation());
		FCollisionQueryParams Params(SCENE_QUERY_STAT(RuntimeToolsetsTeleportSnap), true);
		Params.AddIgnoredActor(Actor);
		TArray<AActor*> Attached;
		Actor->GetAttachedActors(Attached, true, true);
		Params.AddIgnoredActors(Attached);
		const FVector Start(Box.GetCenter().X, Box.GetCenter().Y, Box.Max.Z + 1.0);
		FHitResult Hit;
		if (World->LineTraceSingleByChannel(Hit, Start, Start - FVector(0, 0, 100000.0), ECC_Visibility, Params))
		{
			Dest.Z += Hit.ImpactPoint.Z - Box.Min.Z;
			Result.bSnappedToGround = true;
		}
	}
	T.SetLocation(Dest);
	const bool bRotate = RuntimeToolCall::Current()->Has(TEXT("rotation"));
	if (bRotate)
	{
		T.SetRotation(Rotation.Quaternion());
	}
	// Pawns: also turn the controller so the view follows (undo restores the old control rotation).
	APawn* Pawn = Cast<APawn>(Actor);
	AController* Controller = Pawn ? Pawn->GetController() : nullptr;
	const FString Ref = RuntimeToolCall::MakeReference(Actor);
	TUniquePtr<FCompositeCommand> Cmd = MakeUnique<FCompositeCommand>(TEXT("teleport"));
	Cmd->Add(MakeUnique<FTransformCommand>(TEXT("teleport"), Actor, T, Ref));
	if (Controller && bRotate)
	{
		TWeakObjectPtr<AController> Weak = Controller;
		const FRotator Old = Controller->GetControlRotation();
		const FRotator New = Rotation;
		Cmd->Add(MakeUnique<FLambdaCommand>(TEXT("control rotation"),
			[Weak, New](FRuntimeCommandContext&, FString&) { if (Weak.IsValid()) { Weak->SetControlRotation(New); } return true; },
			[Weak, Old](FRuntimeCommandContext&) { if (Weak.IsValid()) { Weak->SetControlRotation(Old); } return Weak.IsValid(); }));
	}
	if (RuntimeToolCall::RunCommand(MoveTemp(Cmd)))
	{
		Result.Teleported = Ref;
		Result.Name = Actor->GetName();
		if (const FRuntimeSceneNode* Node = RuntimeToolCall::NodeOf(Actor))
		{
			Result.Name = Node->Name;
		}
		Result.Location = Actor->GetActorLocation();
	}
	return Result;
}

FGameToolPauseResult URuntimeGameToolset::SetPause(bool bPaused)
{
	FGameToolPauseResult Result;
	UWorld* World = RuntimeToolCall::GetWorld();
	APlayerController* PC = World->GetFirstPlayerController();
	if (!PC)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::WrongState, TEXT("pausing needs a player controller and this world has none"),
			TEXT("Use game.set_time_dilation with a tiny value to slow the world instead"));
		return Result;
	}
	const bool bWas = World->IsPaused();
	TWeakObjectPtr<APlayerController> Weak = PC;
	TSharedRef<FJsonObject> Delta = MakeShared<FJsonObject>();
	Delta->SetBoolField(TEXT("paused"), bPaused);
	auto Set = [Weak](bool bValue)
	{
		if (!Weak.IsValid())
		{
			return false;
		}
		Weak->SetPause(bValue);
		return true;
	};
	auto DoFn = [Set, bPaused](FRuntimeCommandContext&, FString& OutError)
	{
		if (!Set(bPaused))
		{
			OutError = TEXT("player controller no longer exists");
			return false;
		}
		return true;
	};
	auto UndoFn = [Set, bWas](FRuntimeCommandContext&) { return Set(bWas); };
	if (RuntimeToolCall::RunCommand(MakeUnique<FLambdaCommand>(bPaused ? TEXT("pause") : TEXT("unpause"), DoFn, UndoFn, Delta)))
	{
		Result.bPaused = bPaused;
		Result.bWorldPaused = World->IsPaused();
	}
	return Result;
}

FGameToolTimeDilationResult URuntimeGameToolset::SetTimeDilation(float Value)
{
	FGameToolTimeDilationResult Result;
	if (Value < 0.0001f || Value > 20.f)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("value %g is out of range"), Value),
			TEXT("Use 0.0001 .. 20 (1 = normal speed, 0.25 = slow motion)"));
		return Result;
	}
	AWorldSettings* WS = RuntimeToolCall::GetWorld()->GetWorldSettings();
	if (!WS)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::WrongState, TEXT("world has no world settings"));
		return Result;
	}
	const float Old = WS->TimeDilation;
	TWeakObjectPtr<AWorldSettings> Weak = WS;
	auto Set = [Weak](float V)
	{
		if (!Weak.IsValid())
		{
			return false;
		}
		Weak->SetTimeDilation(V);
		return true;
	};
	TSharedRef<FJsonObject> Delta = MakeShared<FJsonObject>();
	Delta->SetNumberField(TEXT("time_dilation"), Value);
	auto DoFn = [Set, Value](FRuntimeCommandContext&, FString& OutError)
	{
		if (!Set(Value))
		{
			OutError = TEXT("world settings no longer exist");
			return false;
		}
		return true;
	};
	auto UndoFn = [Set, Old](FRuntimeCommandContext&) { return Set(Old); };
	if (RuntimeToolCall::RunCommand(MakeUnique<FLambdaCommand>(TEXT("time dilation"), DoFn, UndoFn, Delta)))
	{
		// The engine clamps to MinGlobalTimeDilation..MaxGlobalTimeDilation.
		Result.TimeDilation = WS->TimeDilation;
		Result.Previous = Old;
	}
	return Result;
}
