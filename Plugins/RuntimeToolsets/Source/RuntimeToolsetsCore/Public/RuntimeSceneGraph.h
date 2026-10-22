#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "UObject/WeakObjectPtr.h"

class AActor;
class UWorld;

enum class ERuntimeNodeKind : uint8
{
	Group,
	Actor,
};

/** A tool-created object in the scene graph. Hierarchy mirrors native actor attachment. */
struct FRuntimeSceneNode
{
	/** Stable id, "n<number>". Survives renames, reparenting and undo/redo. */
	FString Id;
	/** Unique among siblings. */
	FString Name;
	/** Empty for roots. */
	FString ParentId;
	TArray<FString> Children;
	TArray<FName> Tags;
	ERuntimeNodeKind Kind = ERuntimeNodeKind::Actor;
	TWeakObjectPtr<AActor> Actor;
	/** Session that created the node. clear_* only touches nodes owned by the caller's session. */
	FString OwnerSession;
	/** What created it ("shape:cube", "class:/Script/Engine.PointLight", "group"). Used by export/duplicate. */
	FString Source;
	bool bSoftDeleted = false;
	/** Collision state captured when soft-deleted. */
	bool bCollisionBeforeDelete = true;
	bool bHiddenBeforeDelete = false;
	/** Post-process volumes ignore hidden-in-game, so soft-delete disables them; this is their state before. */
	bool bVolumeEnabledBeforeDelete = true;
};

/** Registry of tool-created nodes for one world. Game thread only. */
class RUNTIMETOOLSETSCORE_API FRuntimeSceneGraph
{
public:
	explicit FRuntimeSceneGraph(UWorld* InWorld) : World(InWorld) {}

	UWorld* GetWorld() const { return World.Get(); }

	/** Reserves a fresh node id (used to wire parents before nodes exist, e.g. duplicate/import). */
	FString ReserveId() { return AllocateId(); }

	/**
	 * Registers an actor as a node and attaches it to the parent node's actor.
	 * @param ForcedId  Reuse a previous id (redo/import); empty allocates a new one.
	 */
	FRuntimeSceneNode& AddNode(AActor* Actor, const FString& DesiredName, const FString& ParentId, ERuntimeNodeKind Kind,
		const FString& OwnerSession, const FString& Source, const FString& ForcedId = FString());

	/** Removes the node record (and its children's records). Destroys actors when bDestroyActors. */
	void RemoveNode(const FString& Id, bool bDestroyActors);

	FRuntimeSceneNode* Find(const FString& Id);
	const FRuntimeSceneNode* Find(const FString& Id) const;
	FRuntimeSceneNode* FindByActor(const AActor* Actor);
	/** "a/b/c" -> node. Soft-deleted nodes are ignored. */
	FRuntimeSceneNode* FindByPath(const FString& Path);

	FString GetPath(const FString& Id) const;
	/** Name made unique among the children of ParentId (appends _2, _3...). */
	FString MakeUniqueName(const FString& ParentId, const FString& BaseName, const FString& IgnoreId = FString()) const;
	bool IsNameTaken(const FString& ParentId, const FString& Name, const FString& IgnoreId = FString()) const;

	/** Updates graph parent records from the actor's actual attach parent. Call after any attach/detach. */
	void SyncParentFromAttachment(const FString& Id);

	/** True if PotentialAncestor is Id or one of its ancestors. */
	bool IsAncestorOrSelf(const FString& PotentialAncestor, const FString& Id) const;

	/** Hides the actor, disables collision and marks the node deleted (record kept for undo). */
	void SoftDelete(const FString& Id);
	void Restore(const FString& Id);

	/** Ids of Id and all descendants (pre-order). */
	TArray<FString> CollectSubtree(const FString& Id, bool bIncludeSoftDeleted = false) const;
	TArray<FString> GetRootIds(bool bIncludeSoftDeleted = false) const;
	int32 NumLiveNodes() const;

	const TMap<FString, FRuntimeSceneNode>& GetNodes() const { return Nodes; }

	/** Compact JSON for a node. Fields filter: id,name,path,kind,parent,children,tags,location,rotation,scale,bounds,class,source. */
	TSharedRef<FJsonObject> NodeToJson(const FRuntimeSceneNode& Node, const TArray<FString>& Fields) const;

private:
	FString AllocateId();
	void AttachActorToParent(FRuntimeSceneNode& Node);

	TWeakObjectPtr<UWorld> World;
	TMap<FString, FRuntimeSceneNode> Nodes;
	int64 NextId = 1;
};
