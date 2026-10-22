#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagAssetInterface.h"
#include "GameplayTagContainer.h"
#include "RuntimeToolsetDefinition.h"
#include "RuntimeTagsToolset.generated.h"

/** Gameplay tags on any actor (added by tags.add_actor_tags). Seen by everything that reads IGameplayTagAssetInterface. */
UCLASS(ClassGroup = (RuntimeToolsets))
class URuntimeTagsComponent : public UActorComponent, public IGameplayTagAssetInterface
{
	GENERATED_BODY()

public:
	virtual void GetOwnedGameplayTags(FGameplayTagContainer& TagContainer) const override { TagContainer.AppendTags(Tags); }

	UPROPERTY()
	FGameplayTagContainer Tags;
};

USTRUCT()
struct FRuntimeTagInfo
{
	GENERATED_BODY()

	UPROPERTY()
	FString Name;

	/** The tag is registered (only registered tags can be used). */
	UPROPERTY()
	bool bExists = false;

	/** Parent tags from the root down ("State" for "State.Stunned"). */
	UPROPERTY()
	TArray<FString> Parents;

	/** Direct children. */
	UPROPERTY()
	TArray<FString> Children;
};

USTRUCT()
struct FRuntimeTagQueryResult
{
	GENERATED_BODY()

	UPROPERTY()
	bool bMatches = false;

	/** Tags from `all` the actor lacks. */
	UPROPERTY()
	TArray<FString> MissingAll;

	/** Tags from `none` the actor has. */
	UPROPERTY()
	TArray<FString> PresentNone;

	/** The actor's tags. */
	UPROPERTY()
	TArray<FString> Tags;
};

USTRUCT()
struct FRuntimeTaggedActor
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<AActor> Actor = nullptr;

	UPROPERTY()
	TArray<FString> Tags;
};

/**
 * Gameplay Tags at runtime: browse the tag table (Epic: list_tags, get_tag_info), give any actor tags, and query them.
 * Tags are hierarchical ("State.Stunned" matches "State"). Only registered tags can be used: the plugin registers a starter
 * set (State, Team, Damage, Ability, Event, Cue, AI, Status); projects add theirs in Config/Tags or project settings.
 * Logic rules read them with has_tag(target, 'State.Stunned'); GAS abilities and effects use the same tags.
 */
UCLASS()
class URuntimeTagsToolset : public URuntimeToolsetDefinition
{
	GENERATED_BODY()

public:
	virtual FString GetToolsetVersion() const override { return TEXT("1.0.0"); }

	/**
	 * Lists registered gameplay tags.
	 *
	 * @param ParentTag - Only tags under this one, e.g. "State" (empty = all).
	 * @return Tag names, sorted.
	 */
	UFUNCTION(meta = (AICallable), Category = "Tags")
	static TArray<FString> ListTags(const FString& ParentTag = TEXT(""));

	/**
	 * Describes one tag: whether it exists, its parents and children.
	 *
	 * @param TagName - e.g. "Damage.Fire".
	 */
	UFUNCTION(meta = (AICallable), Category = "Tags")
	static FRuntimeTagInfo GetTagInfo(const FString& TagName);

	/**
	 * The gameplay tags an actor has (from its own tags, tag components and ability system).
	 *
	 * @param Actor - Actor to read.
	 * @return Tag names, sorted.
	 */
	UFUNCTION(meta = (AICallable, RequiresWorld), Category = "Tags")
	static TArray<FString> GetActorTags(AActor* Actor);

	/**
	 * Adds gameplay tags to an actor. Undoable.
	 *
	 * @param Actor - Actor to tag.
	 * @param Tags - Registered tag names, e.g. ["State.Stunned", "Team.Enemy"].
	 * @return The actor's tags after the change. Fails if a tag is not registered (lists close matches).
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld), Category = "Tags")
	static TArray<FString> AddActorTags(AActor* Actor, const TArray<FString>& Tags);

	/**
	 * Removes gameplay tags added with add_actor_tags. Undoable.
	 *
	 * @param Actor - Actor to untag.
	 * @param Tags - Tag names.
	 * @return The actor's tags after the change.
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld), Category = "Tags")
	static TArray<FString> RemoveActorTags(AActor* Actor, const TArray<FString>& Tags);

	/**
	 * Tests an actor's tags: it must have every tag in `all`, at least one of `any` (if given) and none of `none`.
	 * Parent tags match children ("State" matches "State.Stunned").
	 *
	 * @param Actor - Actor to test.
	 * @param All - Required tags.
	 * @param Any - At least one of these.
	 * @param None - Forbidden tags.
	 */
	UFUNCTION(meta = (AICallable, RequiresWorld, AutoCreateRefTerm = "All,Any,None"), Category = "Tags")
	static FRuntimeTagQueryResult MatchTags(AActor* Actor, const TArray<FString>& All, const TArray<FString>& Any, const TArray<FString>& None);

	/**
	 * Finds actors whose tags match (same rules as match_tags).
	 *
	 * @param All - Required tags.
	 * @param Any - At least one of these.
	 * @param None - Forbidden tags.
	 * @param Limit - Maximum results.
	 * @return Matching actors with their tags, sorted by name.
	 */
	UFUNCTION(meta = (AICallable, RequiresWorld, AutoCreateRefTerm = "All,Any,None"), Category = "Tags")
	static TArray<FRuntimeTaggedActor> FindActorsWithTags(const TArray<FString>& All, const TArray<FString>& Any, const TArray<FString>& None, int32 Limit = 100);
};

namespace RuntimeTags
{
	/** Every gameplay tag an actor owns (actor interface, components, ability system). */
	FGameplayTagContainer TagsOf(const AActor* Actor);
	/** Registered tag or invalid. */
	FGameplayTag Find(const FString& Name);
}
