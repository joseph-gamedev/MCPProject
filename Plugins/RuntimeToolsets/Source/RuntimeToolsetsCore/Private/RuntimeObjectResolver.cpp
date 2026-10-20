#include "RuntimeObjectResolver.h"
#include "RuntimeSceneGraph.h"
#include "EngineUtils.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/GameUserSettings.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "Subsystems/EngineSubsystem.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "Subsystems/WorldSubsystem.h"
#include "UObject/UObjectIterator.h"

namespace RuntimeObjectResolver
{
	static UClass* FindClassByName(const FString& Name)
	{
		if (Name.StartsWith(TEXT("/")))
		{
			return FindObject<UClass>(nullptr, *Name);
		}
		if (UClass* C = FindFirstObject<UClass>(*Name, EFindFirstObjectOptions::NativeFirst | EFindFirstObjectOptions::EnsureIfAmbiguous))
		{
			return C;
		}
		// Allow omitting the U/A prefix.
		for (TObjectIterator<UClass> It; It; ++It)
		{
			if (It->GetName().Equals(Name, ESearchCase::IgnoreCase))
			{
				return *It;
			}
		}
		return nullptr;
	}

	UObject* ResolveSubsystem(const FString& Kind, const FString& ClassName, UWorld* World, FString& OutMessage)
	{
		UClass* Class = FindClassByName(ClassName);
		if (!Class)
		{
			OutMessage = FString::Printf(TEXT("subsystem class '%s' not found"), *ClassName);
			return nullptr;
		}
		if (Kind == TEXT("engine"))
		{
			return (GEngine && Class->IsChildOf(UEngineSubsystem::StaticClass())) ? GEngine->GetEngineSubsystemBase(Class) : nullptr;
		}
		if (Kind == TEXT("game_instance"))
		{
			UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
			return (GI && Class->IsChildOf(UGameInstanceSubsystem::StaticClass())) ? GI->GetSubsystemBase(Class) : nullptr;
		}
		if (Kind == TEXT("world"))
		{
			return (World && Class->IsChildOf(UWorldSubsystem::StaticClass())) ? World->GetSubsystemBase(Class) : nullptr;
		}
		if (Kind == TEXT("local_player"))
		{
			APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
			ULocalPlayer* LP = PC ? PC->GetLocalPlayer() : nullptr;
			return (LP && Class->IsChildOf(ULocalPlayerSubsystem::StaticClass())) ? LP->GetSubsystemBase(Class) : nullptr;
		}
		OutMessage = FString::Printf(TEXT("unknown subsystem kind '%s' (engine, game_instance, world, local_player)"), *Kind);
		return nullptr;
	}

	static UObject* ResolveShortcut(const FString& Name, UWorld* World)
	{
		if (Name == TEXT("game_user_settings"))
		{
			return GEngine ? GEngine->GetGameUserSettings() : nullptr;
		}
		if (!World)
		{
			return nullptr;
		}
		APlayerController* PC = World->GetFirstPlayerController();
		if (Name == TEXT("player_controller")) return PC;
		if (Name == TEXT("pawn")) return PC ? PC->GetPawn() : nullptr;
		if (Name == TEXT("camera_manager")) return PC ? PC->PlayerCameraManager.Get() : nullptr;
		if (Name == TEXT("game_mode")) return World->GetAuthGameMode();
		if (Name == TEXT("game_state")) return World->GetGameState();
		if (Name == TEXT("world_settings")) return World->GetWorldSettings();
		if (Name == TEXT("game_instance")) return World->GetGameInstance();
		if (Name == TEXT("world")) return World;
		return nullptr;
	}

	static bool IsShortcut(const FString& Name)
	{
		static const TCHAR* Names[] = {TEXT("player_controller"), TEXT("pawn"), TEXT("camera_manager"), TEXT("game_mode"), TEXT("game_state"),
			TEXT("world_settings"), TEXT("game_instance"), TEXT("world"), TEXT("game_user_settings")};
		for (const TCHAR* N : Names)
		{
			if (Name == N)
			{
				return true;
			}
		}
		return false;
	}

	static AActor* FindActorByName(const FString& Pattern, UWorld* World, ERuntimeToolError& OutError, FString& OutMessage)
	{
		if (!World)
		{
			OutError = ERuntimeToolError::NoWorld;
			OutMessage = TEXT("no game world");
			return nullptr;
		}
		const bool bWildcard = Pattern.Contains(TEXT("*")) || Pattern.Contains(TEXT("?"));
		TArray<AActor*> Matches;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			const FString Name = It->GetName();
			if (bWildcard ? Name.MatchesWildcard(Pattern) : Name.Equals(Pattern, ESearchCase::IgnoreCase))
			{
				Matches.Add(*It);
				if (!bWildcard)
				{
					break;
				}
			}
		}
		if (Matches.Num() == 1)
		{
			return Matches[0];
		}
		if (Matches.Num() == 0)
		{
			OutError = ERuntimeToolError::NotFound;
			OutMessage = FString::Printf(TEXT("no actor named '%s'"), *Pattern);
			return nullptr;
		}
		OutError = ERuntimeToolError::AmbiguousTarget;
		OutMessage = FString::Printf(TEXT("'%s' matches %d actors (e.g. %s, %s)"), *Pattern, Matches.Num(), *Matches[0]->GetName(), *Matches[1]->GetName());
		return nullptr;
	}

	bool Resolve(const FString& InTarget, UWorld* World, FRuntimeSceneGraph* Graph, FResolvedTarget& Out, ERuntimeToolError& OutError, FString& OutMessage)
	{
		Out = FResolvedTarget();
		FString Target = InTarget.TrimStartAndEnd();
		if (Target.IsEmpty())
		{
			OutError = ERuntimeToolError::InvalidArguments;
			OutMessage = TEXT("empty target");
			return false;
		}

		FString ComponentName;
		{
			int32 Hash = INDEX_NONE;
			if (Target.FindLastChar(TEXT('#'), Hash))
			{
				ComponentName = Target.Mid(Hash + 1);
				Target = Target.Left(Hash);
			}
		}

		UObject* Object = nullptr;
		FRuntimeSceneNode* Node = nullptr;

		if (Target.StartsWith(TEXT("actor:")))
		{
			Object = FindActorByName(Target.Mid(6), World, OutError, OutMessage);
			if (!Object)
			{
				return false;
			}
		}
		else if (Target.StartsWith(TEXT("class:")))
		{
			UClass* Class = FindClassByName(Target.Mid(6));
			if (!Class)
			{
				OutError = ERuntimeToolError::NotFound;
				OutMessage = FString::Printf(TEXT("class '%s' not found"), *Target.Mid(6));
				return false;
			}
			Object = Class->GetDefaultObject();
		}
		else if (Target.StartsWith(TEXT("subsystem:")))
		{
			FString Kind, ClassName;
			if (!Target.Mid(10).Split(TEXT("/"), &Kind, &ClassName))
			{
				OutError = ERuntimeToolError::InvalidArguments;
				OutMessage = TEXT("use subsystem:<kind>/<Class>");
				return false;
			}
			Object = ResolveSubsystem(Kind, ClassName, World, OutMessage);
			if (!Object)
			{
				OutError = ERuntimeToolError::NotFound;
				if (OutMessage.IsEmpty())
				{
					OutMessage = FString::Printf(TEXT("subsystem %s/%s not available"), *Kind, *ClassName);
				}
				return false;
			}
		}
		else if (Target.StartsWith(TEXT("/")))
		{
			Object = StaticFindObject(UObject::StaticClass(), nullptr, *Target);
			if (!Object)
			{
				OutError = ERuntimeToolError::NotFound;
				OutMessage = FString::Printf(TEXT("object '%s' not found (it may not be loaded or cooked)"), *Target);
				return false;
			}
		}
		else if (IsShortcut(Target))
		{
			Object = ResolveShortcut(Target, World);
			if (!Object)
			{
				OutError = World ? ERuntimeToolError::NotFound : ERuntimeToolError::NoWorld;
				OutMessage = FString::Printf(TEXT("'%s' is not available"), *Target);
				return false;
			}
		}
		else
		{
			// Node id, then graph path, then actor name.
			if (Graph)
			{
				if (Target.Len() > 1 && Target[0] == TEXT('n') && Target.Mid(1).IsNumeric())
				{
					Node = Graph->Find(Target);
				}
				if (!Node)
				{
					Node = Graph->FindByPath(Target);
				}
			}
			if (Node)
			{
				if (Node->bSoftDeleted)
				{
					OutError = ERuntimeToolError::NotFound;
					OutMessage = FString::Printf(TEXT("node %s is deleted (undo to restore)"), *Node->Id);
					return false;
				}
				Object = Node->Actor.Get();
				if (!Object)
				{
					OutError = ERuntimeToolError::NotFound;
					OutMessage = FString::Printf(TEXT("node %s has no live actor"), *Node->Id);
					return false;
				}
			}
			else if (!Target.Contains(TEXT("/")))
			{
				Object = FindActorByName(Target, World, OutError, OutMessage);
				if (!Object)
				{
					if (OutError == ERuntimeToolError::NotFound)
					{
						OutMessage = FString::Printf(TEXT("'%s' is not a node id, node path or actor name"), *Target);
					}
					return false;
				}
			}
			else
			{
				OutError = ERuntimeToolError::NotFound;
				OutMessage = FString::Printf(TEXT("no node at path '%s'"), *Target);
				return false;
			}
		}

		Out.Object = Object;
		Out.Actor = Cast<AActor>(Object);
		Out.Node = Node;
		if (!Out.Node && Out.Actor && Graph)
		{
			Out.Node = Graph->FindByActor(Out.Actor);
		}

		if (!ComponentName.IsEmpty())
		{
			if (!Out.Actor)
			{
				OutError = ERuntimeToolError::InvalidArguments;
				OutMessage = TEXT("#component requires an actor target");
				return false;
			}
			UActorComponent* Found = nullptr;
			for (UActorComponent* C : Out.Actor->GetComponents())
			{
				if (C && C->GetName().Equals(ComponentName, ESearchCase::IgnoreCase))
				{
					Found = C;
					break;
				}
			}
			if (!Found)
			{
				TArray<FString> Names;
				for (UActorComponent* C : Out.Actor->GetComponents())
				{
					if (C)
					{
						Names.Add(C->GetName());
					}
				}
				OutError = ERuntimeToolError::NotFound;
				OutMessage = FString::Printf(TEXT("component '%s' not on %s (has: %s)"), *ComponentName, *Out.Actor->GetName(), *FString::Join(Names, TEXT(", ")));
				return false;
			}
			Out.Component = Found;
			Out.Object = Found;
		}
		return true;
	}

	AActor* ResolveActor(const FString& Target, UWorld* World, FRuntimeSceneGraph* Graph, ERuntimeToolError& OutError, FString& OutMessage)
	{
		FResolvedTarget R;
		if (!Resolve(Target, World, Graph, R, OutError, OutMessage))
		{
			return nullptr;
		}
		if (!R.Actor || R.Component)
		{
			OutError = ERuntimeToolError::InvalidArguments;
			OutMessage = FString::Printf(TEXT("'%s' is not an actor"), *Target);
			return nullptr;
		}
		return R.Actor;
	}

	FString MakeReference(const UObject* Object, FRuntimeSceneGraph* Graph)
	{
		if (!Object)
		{
			return FString();
		}
		if (const AActor* Actor = Cast<AActor>(Object))
		{
			if (Graph)
			{
				if (const FRuntimeSceneNode* Node = Graph->FindByActor(Actor))
				{
					return Node->Id;
				}
			}
			return FString::Printf(TEXT("actor:%s"), *Actor->GetName());
		}
		if (const UActorComponent* Comp = Cast<UActorComponent>(Object))
		{
			return MakeReference(Comp->GetOwner(), Graph) + TEXT("#") + Comp->GetName();
		}
		return Object->GetPathName();
	}
}
