#include "GameToolset.h"
#include "RuntimeToolHelpers.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/WorldSettings.h"

using namespace RuntimeTools;

static void GetGameState(FToolInvocation& Inv)
{
	UWorld* World = Inv.GetWorld();
	TSharedRef<FJsonObject> Out = Obj();
	Out->SetStringField(TEXT("map"), World->GetMapName());
	Out->SetNumberField(TEXT("time_seconds"), RuntimeJson::Round(World->GetTimeSeconds()));
	Out->SetNumberField(TEXT("real_time_seconds"), RuntimeJson::Round(World->GetRealTimeSeconds()));
	Out->SetBoolField(TEXT("paused"), World->IsPaused());
	if (const AWorldSettings* WS = World->GetWorldSettings())
	{
		Out->SetNumberField(TEXT("time_dilation"), RuntimeJson::Round(WS->TimeDilation));
	}
	if (const AGameModeBase* GM = World->GetAuthGameMode())
	{
		Out->SetStringField(TEXT("game_mode"), GM->GetClass()->GetName());
		if (GM->DefaultPawnClass)
		{
			Out->SetStringField(TEXT("default_pawn"), GM->DefaultPawnClass->GetName());
		}
	}
	if (const AGameStateBase* GS = World->GetGameState())
	{
		Out->SetStringField(TEXT("game_state"), GS->GetClass()->GetName());
		Out->SetNumberField(TEXT("player_states"), GS->PlayerArray.Num());
	}
	int32 Players = 0;
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		++Players;
	}
	Out->SetNumberField(TEXT("players"), Players);
	Inv.Succeed(Out);
}

static void ListPlayers(FToolInvocation& Inv)
{
	TArray<TSharedPtr<FJsonValue>> Items;
	for (FConstPlayerControllerIterator It = Inv.GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		if (!PC)
		{
			continue;
		}
		TSharedRef<FJsonObject> J = Obj();
		J->SetStringField(TEXT("controller"), Ref(Inv, PC));
		J->SetBoolField(TEXT("local"), PC->IsLocalController());
		if (const APlayerState* PS = PC->PlayerState)
		{
			J->SetStringField(TEXT("name"), PS->GetPlayerName());
		}
		if (APawn* Pawn = PC->GetPawn())
		{
			J->SetStringField(TEXT("pawn"), Ref(Inv, Pawn));
			J->SetStringField(TEXT("pawn_class"), Pawn->GetClass()->GetName());
			J->SetField(TEXT("location"), RuntimeJson::Vec(Pawn->GetActorLocation()));
			J->SetField(TEXT("rotation"), RuntimeJson::Rot(PC->GetControlRotation()));
		}
		Items.Add(MakeShared<FJsonValueObject>(J));
	}
	TSharedRef<FJsonObject> Out = Obj();
	Out->SetArrayField(TEXT("players"), Items);
	Inv.Succeed(Out);
}

static void Teleport(FToolInvocation& Inv)
{
	AActor* Actor = nullptr;
	if (Inv.GetString(TEXT("target")).IsEmpty())
	{
		APlayerController* PC = Inv.GetWorld()->GetFirstPlayerController();
		Actor = PC ? PC->GetPawn() : nullptr;
		if (!Actor)
		{
			Inv.Fail(ERuntimeToolError::WrongState, TEXT("no player pawn"), TEXT("Pass a target"));
			return;
		}
	}
	else
	{
		Actor = Inv.ResolveActor(TEXT("target"));
		if (!Actor)
		{
			return;
		}
	}
	FVector Location;
	if (!Inv.GetVector(TEXT("location"), Location))
	{
		Inv.Fail(ERuntimeToolError::InvalidArguments, TEXT("location is required"));
		return;
	}
	FTransform T = Actor->GetActorTransform();
	T.SetLocation(Location);
	FRotator Rotation;
	const bool bRotate = Inv.GetRotator(TEXT("rotation"), Rotation);
	if (bRotate)
	{
		T.SetRotation(Rotation.Quaternion());
	}
	// Pawns: also turn the controller so the view follows (undo restores the old control rotation).
	APawn* Pawn = Cast<APawn>(Actor);
	AController* Controller = Pawn ? Pawn->GetController() : nullptr;
	TUniquePtr<FCompositeCommand> Cmd = MakeUnique<FCompositeCommand>(TEXT("teleport"));
	Cmd->Add(MakeUnique<FTransformCommand>(TEXT("teleport"), Actor, T, Ref(Inv, Actor)));
	if (Controller && bRotate)
	{
		TWeakObjectPtr<AController> Weak = Controller;
		const FRotator Old = Controller->GetControlRotation();
		Cmd->Add(MakeUnique<FLambdaCommand>(TEXT("control rotation"),
			[Weak, Rotation](FRuntimeCommandContext&, FString&) { if (Weak.IsValid()) { Weak->SetControlRotation(Rotation); } return true; },
			[Weak, Old](FRuntimeCommandContext&) { if (Weak.IsValid()) { Weak->SetControlRotation(Old); } return Weak.IsValid(); }));
	}
	if (Inv.RunCommand(MoveTemp(Cmd)))
	{
		TSharedRef<FJsonObject> Out = Obj();
		Out->SetStringField(TEXT("teleported"), Ref(Inv, Actor));
		Out->SetField(TEXT("location"), RuntimeJson::Vec(Actor->GetActorLocation()));
		Inv.Succeed(Out);
	}
}

static void SetPause(FToolInvocation& Inv)
{
	APlayerController* PC = Inv.GetWorld()->GetFirstPlayerController();
	if (!PC)
	{
		Inv.Fail(ERuntimeToolError::WrongState, TEXT("pausing needs a player controller"));
		return;
	}
	const bool bPaused = Inv.GetBool(TEXT("paused"), true);
	const bool bWas = Inv.GetWorld()->IsPaused();
	TWeakObjectPtr<APlayerController> Weak = PC;
	TSharedRef<FJsonObject> Delta = Obj();
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
	auto DoFn = [Set, bPaused](FRuntimeCommandContext&, FString& Error)
	{
		if (!Set(bPaused))
		{
			Error = TEXT("player controller no longer exists");
			return false;
		}
		return true;
	};
	auto UndoFn = [Set, bWas](FRuntimeCommandContext&) { return Set(bWas); };
	if (Inv.RunCommand(MakeUnique<FLambdaCommand>(bPaused ? TEXT("pause") : TEXT("unpause"), DoFn, UndoFn, Delta)))
	{
		Delta->SetBoolField(TEXT("world_paused"), Inv.GetWorld()->IsPaused());
		Inv.Succeed(Delta);
	}
}

static void SetTimeDilation(FToolInvocation& Inv)
{
	AWorldSettings* WS = Inv.GetWorld()->GetWorldSettings();
	if (!WS)
	{
		Inv.Fail(ERuntimeToolError::WrongState, TEXT("world has no world settings"));
		return;
	}
	const float New = static_cast<float>(Inv.GetNumber(TEXT("value"), 1.0));
	const float Old = WS->TimeDilation;
	TWeakObjectPtr<AWorldSettings> Weak = WS;
	TSharedRef<FJsonObject> Delta = Obj();
	auto Set = [Weak](float Value)
	{
		if (!Weak.IsValid())
		{
			return false;
		}
		Weak->SetTimeDilation(Value);
		return true;
	};
	auto DoFn = [Set, New, Weak, Delta](FRuntimeCommandContext&, FString& Error)
	{
		if (!Set(New))
		{
			Error = TEXT("world settings no longer exist");
			return false;
		}
		// The engine clamps to MinGlobalTimeDilation..MaxGlobalTimeDilation.
		Delta->SetNumberField(TEXT("time_dilation"), RuntimeJson::Round(Weak->TimeDilation));
		return true;
	};
	auto UndoFn = [Set, Old](FRuntimeCommandContext&) { return Set(Old); };
	if (Inv.RunCommand(MakeUnique<FLambdaCommand>(TEXT("time dilation"), DoFn, UndoFn, Delta)))
	{
		Delta->SetNumberField(TEXT("previous"), RuntimeJson::Round(Old));
		Inv.Succeed(Delta);
	}
}

void FGameToolset::RegisterTools(FRuntimeToolsetBuilder& B)
{
	B.Tool(TEXT("get_game_state"), TEXT("Map, game mode, time, pause, time dilation, player count."))
		.RequiresWorld()
		.Handle(&GetGameState);

	B.Tool(TEXT("list_players"), TEXT("Player controllers with their pawns and locations."))
		.RequiresWorld()
		.Handle(&ListPlayers);

	B.Tool(TEXT("teleport"), TEXT("Move an actor (default: the player pawn) to a location, optionally turning it."))
		.Args(FToolSchema()
			.Add(TEXT("target"), FToolParam::Target(TEXT("Actor or pawn (default: player pawn)")))
			.Add(TEXT("location"), FToolParam::Vec3(TEXT("[x,y,z] cm")).Required())
			.Add(TEXT("rotation"), FToolParam::Rotator(TEXT("[pitch,yaw,roll]"))))
		.Undoable().RequiresWorld()
		.Handle(&Teleport);

	B.Tool(TEXT("set_pause"), TEXT("Pause or unpause the game. Tools keep working while paused."))
		.Args(FToolSchema().Add(TEXT("paused"), FToolParam::Boolean(TEXT("Paused")).Required()))
		.Undoable().RequiresWorld()
		.Handle(&SetPause);

	B.Tool(TEXT("set_time_dilation"), TEXT("Slow down or speed up game time (1 = normal)."))
		.Args(FToolSchema().Add(TEXT("value"), FToolParam::Number(TEXT("Time scale"), 0.0001, 20.0).Required()))
		.Undoable().RequiresWorld()
		.Handle(&SetTimeDilation);
}
