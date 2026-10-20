#pragma once

#include "CoreMinimal.h"
#include "RuntimeToolTypes.h"

class UObject;
class AActor;
class UActorComponent;
class UWorld;
class FRuntimeSceneGraph;
struct FRuntimeSceneNode;

/** What a target string resolved to. */
struct FResolvedTarget
{
	UObject* Object = nullptr;
	AActor* Actor = nullptr;
	UActorComponent* Component = nullptr;
	FRuntimeSceneNode* Node = nullptr;

	bool IsValid() const { return Object != nullptr; }
};

/**
 * Turns target strings into objects. Accepted forms:
 *   n12                      scene-graph node id
 *   group/child              scene-graph path
 *   actor:Name               actor by object name (wildcards allowed if unique)
 *   /Game/Map.Map:Persistent.Actor or /Script/Engine.Default__Class   full object path
 *   class:/Script/Engine.KismetSystemLibrary   class default object
 *   subsystem:<engine|game_instance|world|local_player>/<ClassName>
 *   pawn, player_controller, game_mode, game_state, world_settings, game_instance, game_user_settings
 *   <any of the above>#ComponentName   a component on the resolved actor
 */
namespace RuntimeObjectResolver
{
	RUNTIMETOOLSETSCORE_API bool Resolve(const FString& Target, UWorld* World, FRuntimeSceneGraph* Graph, FResolvedTarget& Out,
		ERuntimeToolError& OutError, FString& OutMessage);

	/** Resolves and requires an actor. */
	RUNTIMETOOLSETSCORE_API AActor* ResolveActor(const FString& Target, UWorld* World, FRuntimeSceneGraph* Graph, ERuntimeToolError& OutError,
		FString& OutMessage);

	RUNTIMETOOLSETSCORE_API UObject* ResolveSubsystem(const FString& Kind, const FString& ClassName, UWorld* World, FString& OutMessage);

	/** Short stable reference for results: node id if any, else actor:Name, else path name. */
	RUNTIMETOOLSETSCORE_API FString MakeReference(const UObject* Object, FRuntimeSceneGraph* Graph);
}
