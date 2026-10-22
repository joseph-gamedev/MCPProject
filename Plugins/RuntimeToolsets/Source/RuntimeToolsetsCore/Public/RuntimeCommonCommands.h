#pragma once

#include "CoreMinimal.h"
#include "RuntimeCommand.h"
#include "RuntimeSceneGraph.h"
#include "UObject/WeakObjectPtr.h"

class AActor;
class UObject;

/**
 * Spawns an actor and registers it as a scene-graph node.
 * Undo soft-deletes it, Redo restores the same actor with the same id. The actor is destroyed when the
 * undone entry leaves history.
 */
class RUNTIMETOOLSETSCORE_API FCreateNodeCommand : public IRuntimeCommand
{
public:
	using FSpawnFn = TFunction<AActor*(FRuntimeCommandContext&, FString& OutError)>;

	FCreateNodeCommand(const FString& InLabel, FSpawnFn InSpawn, const FString& InName, const FString& InParentId, ERuntimeNodeKind InKind,
		const FString& InSource, const TArray<FName>& InTags);

	virtual FString GetLabel() const override { return Label; }
	virtual bool Do(FRuntimeCommandContext& Context, FString& OutError) override;
	virtual bool Undo(FRuntimeCommandContext& Context) override;
	virtual bool Redo(FRuntimeCommandContext& Context) override;
	virtual TSharedPtr<FJsonObject> GetDelta(bool bForUndo) const override;
	virtual void OnDiscarded(FRuntimeCommandContext& Context, bool bWasUndone) override;

	const FString& GetNodeId() const { return NodeId; }
	/** Reuse an id (import_tree remaps, duplicate keeps fresh ids). */
	void SetForcedId(const FString& Id) { NodeId = Id; }

private:
	FString Label;
	FSpawnFn Spawn;
	FString Name;
	FString ParentId;
	ERuntimeNodeKind Kind;
	FString Source;
	TArray<FName> Tags;
	FString NodeId;
	FString FinalName;
};

/**
 * Soft-deletes actors (hidden, collision off, detached from queries). Nodes in the graph are flagged.
 * Undo restores them. When the applied entry leaves history the actors are destroyed for real.
 */
class RUNTIMETOOLSETSCORE_API FSoftDeleteActorsCommand : public IRuntimeCommand
{
public:
	FSoftDeleteActorsCommand(const FString& InLabel, const TArray<AActor*>& InActors);

	virtual FString GetLabel() const override { return Label; }
	virtual bool Do(FRuntimeCommandContext& Context, FString& OutError) override;
	virtual bool Undo(FRuntimeCommandContext& Context) override;
	virtual TSharedPtr<FJsonObject> GetDelta(bool bForUndo) const override;
	virtual void OnDiscarded(FRuntimeCommandContext& Context, bool bWasUndone) override;

private:
	struct FEntry
	{
		TWeakObjectPtr<AActor> Actor;
		FString NodeId;
		FString Ref;
		bool bWasHidden = false;
		bool bHadCollision = true;
		bool bVolumeWasEnabled = true;
	};
	FString Label;
	TArray<FEntry> Entries;
};

/** Sets an actor's world transform. Stores old/new; undo restores the old one. */
class RUNTIMETOOLSETSCORE_API FTransformCommand : public IRuntimeCommand
{
public:
	FTransformCommand(const FString& InLabel, AActor* InActor, const FTransform& InNewWorld, const FString& InRef);

	virtual FString GetLabel() const override { return Label; }
	virtual bool Do(FRuntimeCommandContext& Context, FString& OutError) override;
	virtual bool Undo(FRuntimeCommandContext& Context) override;
	virtual bool Redo(FRuntimeCommandContext& Context) override;
	virtual TSharedPtr<FJsonObject> GetDelta(bool bForUndo) const override;

private:
	bool Apply(const FTransform& T);

	FString Label;
	TWeakObjectPtr<AActor> Actor;
	FTransform OldWorld;
	FTransform NewWorld;
	FString Ref;
	uint8 OldMobility = 0;
	bool bChangedMobility = false;
};

/** Attaches (or detaches when NewParent is null) an actor. Keeps the graph in sync. */
class RUNTIMETOOLSETSCORE_API FAttachCommand : public IRuntimeCommand
{
public:
	FAttachCommand(const FString& InLabel, AActor* InChild, AActor* InNewParent, FName InSocket, bool bInKeepWorld);

	virtual FString GetLabel() const override { return Label; }
	virtual bool Do(FRuntimeCommandContext& Context, FString& OutError) override;
	virtual bool Undo(FRuntimeCommandContext& Context) override;
	virtual bool Redo(FRuntimeCommandContext& Context) override;
	virtual TSharedPtr<FJsonObject> GetDelta(bool bForUndo) const override;

private:
	bool AttachTo(FRuntimeCommandContext& Context, AActor* Parent, FName Socket, bool bKeepWorld, const FTransform* RelativeOverride, FString& OutError);

	FString Label;
	TWeakObjectPtr<AActor> Child;
	TWeakObjectPtr<AActor> NewParent;
	FName Socket;
	bool bKeepWorld = true;

	TWeakObjectPtr<AActor> OldParent;
	FName OldSocket;
	FTransform OldRelative;
	FTransform OldWorld;
	FString ChildRef;
};

/** Sets a reflected property through a path. Stores old/new JSON values. */
class RUNTIMETOOLSETSCORE_API FPropertyCommand : public IRuntimeCommand
{
public:
	FPropertyCommand(UObject* InObject, const FString& InPath, const TSharedPtr<FJsonValue>& InNewValue, const FString& InRef);

	virtual FString GetLabel() const override;
	virtual bool Do(FRuntimeCommandContext& Context, FString& OutError) override;
	virtual bool Undo(FRuntimeCommandContext& Context) override;
	virtual bool Redo(FRuntimeCommandContext& Context) override;
	virtual TSharedPtr<FJsonObject> GetDelta(bool bForUndo) const override;

private:
	TWeakObjectPtr<UObject> Object;
	FString Path;
	TSharedPtr<FJsonValue> NewValue;
	TSharedPtr<FJsonValue> OldValue;
	TSharedPtr<FJsonValue> AppliedValue;
	FString Ref;
};
