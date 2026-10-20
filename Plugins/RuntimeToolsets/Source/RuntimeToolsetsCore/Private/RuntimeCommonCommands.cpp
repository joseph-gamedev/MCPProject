#include "RuntimeCommonCommands.h"
#include "RuntimeJson.h"
#include "RuntimeObjectResolver.h"
#include "RuntimePropertyPath.h"
#include "Components/SceneComponent.h"
#include "GameFramework/Actor.h"

// ---------------------------------------------------------------------------------------------------------------------
// FCreateNodeCommand
// ---------------------------------------------------------------------------------------------------------------------

FCreateNodeCommand::FCreateNodeCommand(const FString& InLabel, FSpawnFn InSpawn, const FString& InName, const FString& InParentId,
	ERuntimeNodeKind InKind, const FString& InSource, const TArray<FName>& InTags)
	: Label(InLabel), Spawn(MoveTemp(InSpawn)), Name(InName), ParentId(InParentId), Kind(InKind), Source(InSource), Tags(InTags)
{
}

bool FCreateNodeCommand::Do(FRuntimeCommandContext& Context, FString& OutError)
{
	if (!Context.Graph || !Context.World)
	{
		OutError = TEXT("no world");
		return false;
	}
	if (!ParentId.IsEmpty())
	{
		const FRuntimeSceneNode* Parent = Context.Graph->Find(ParentId);
		if (!Parent || Parent->bSoftDeleted)
		{
			OutError = FString::Printf(TEXT("parent %s not found"), *ParentId);
			return false;
		}
	}
	AActor* Actor = Spawn(Context, OutError);
	if (!Actor)
	{
		if (OutError.IsEmpty())
		{
			OutError = TEXT("spawn failed");
		}
		return false;
	}
	for (const FName& Tag : Tags)
	{
		Actor->Tags.AddUnique(Tag);
	}
	FRuntimeSceneNode& Node = Context.Graph->AddNode(Actor, Name, ParentId, Kind, Context.SessionId, Source, NodeId);
	Node.Tags = Tags;
	NodeId = Node.Id;
	FinalName = Node.Name;
	return true;
}

bool FCreateNodeCommand::Undo(FRuntimeCommandContext& Context)
{
	if (!Context.Graph || !Context.Graph->Find(NodeId))
	{
		return false;
	}
	Context.Graph->SoftDelete(NodeId);
	return true;
}

bool FCreateNodeCommand::Redo(FRuntimeCommandContext& Context)
{
	FRuntimeSceneNode* Node = Context.Graph ? Context.Graph->Find(NodeId) : nullptr;
	if (!Node || !Node->Actor.IsValid())
	{
		return false;
	}
	Context.Graph->Restore(NodeId);
	return true;
}

TSharedPtr<FJsonObject> FCreateNodeCommand::GetDelta(bool bForUndo) const
{
	TSharedRef<FJsonObject> D = MakeShared<FJsonObject>();
	D->SetStringField(bForUndo ? TEXT("removed") : TEXT("created"), NodeId);
	if (!bForUndo)
	{
		D->SetStringField(TEXT("name"), FinalName);
	}
	return D;
}

void FCreateNodeCommand::OnDiscarded(FRuntimeCommandContext& Context, bool bWasUndone)
{
	if (bWasUndone && Context.Graph && !Context.bWorldTearingDown)
	{
		// Only destroy if still soft-deleted (a later command may have restored it).
		if (const FRuntimeSceneNode* Node = Context.Graph->Find(NodeId); Node && Node->bSoftDeleted)
		{
			Context.Graph->RemoveNode(NodeId, true);
		}
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// FSoftDeleteActorsCommand
// ---------------------------------------------------------------------------------------------------------------------

FSoftDeleteActorsCommand::FSoftDeleteActorsCommand(const FString& InLabel, const TArray<AActor*>& InActors)
	: Label(InLabel)
{
	for (AActor* A : InActors)
	{
		if (A)
		{
			FEntry E;
			E.Actor = A;
			Entries.Add(E);
		}
	}
}

bool FSoftDeleteActorsCommand::Do(FRuntimeCommandContext& Context, FString& OutError)
{
	if (Entries.Num() == 0)
	{
		OutError = TEXT("nothing to delete");
		return false;
	}
	for (FEntry& E : Entries)
	{
		AActor* A = E.Actor.Get();
		if (!A)
		{
			continue;
		}
		const FRuntimeSceneNode* Node = Context.Graph ? Context.Graph->FindByActor(A) : nullptr;
		if (Node)
		{
			E.NodeId = Node->Id;
			E.Ref = Node->Id;
			Context.Graph->SoftDelete(Node->Id);
		}
		else
		{
			E.Ref = FString::Printf(TEXT("actor:%s"), *A->GetName());
			E.bWasHidden = A->IsHidden();
			E.bHadCollision = A->GetActorEnableCollision();
			A->SetActorHiddenInGame(true);
			A->SetActorEnableCollision(false);
		}
	}
	return true;
}

bool FSoftDeleteActorsCommand::Undo(FRuntimeCommandContext& Context)
{
	bool bAll = true;
	for (int32 i = Entries.Num() - 1; i >= 0; --i)
	{
		FEntry& E = Entries[i];
		AActor* A = E.Actor.Get();
		if (!A)
		{
			bAll = false;
			continue;
		}
		if (!E.NodeId.IsEmpty() && Context.Graph && Context.Graph->Find(E.NodeId))
		{
			Context.Graph->Restore(E.NodeId);
		}
		else
		{
			A->SetActorHiddenInGame(E.bWasHidden);
			A->SetActorEnableCollision(E.bHadCollision);
		}
	}
	return bAll;
}

TSharedPtr<FJsonObject> FSoftDeleteActorsCommand::GetDelta(bool bForUndo) const
{
	TArray<FString> Refs;
	for (const FEntry& E : Entries)
	{
		if (!E.Ref.IsEmpty())
		{
			Refs.Add(E.Ref);
		}
	}
	TSharedRef<FJsonObject> D = MakeShared<FJsonObject>();
	D->SetField(bForUndo ? TEXT("restored") : TEXT("deleted"), RuntimeJson::Strings(Refs));
	return D;
}

void FSoftDeleteActorsCommand::OnDiscarded(FRuntimeCommandContext& Context, bool bWasUndone)
{
	if (bWasUndone || Context.bWorldTearingDown)
	{
		return;
	}
	// The deletion is now permanent.
	for (FEntry& E : Entries)
	{
		if (!E.NodeId.IsEmpty() && Context.Graph)
		{
			if (const FRuntimeSceneNode* Node = Context.Graph->Find(E.NodeId); Node && Node->bSoftDeleted)
			{
				Context.Graph->RemoveNode(E.NodeId, true);
			}
		}
		else if (AActor* A = E.Actor.Get(); A && A->IsHidden())
		{
			A->Destroy();
		}
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// FTransformCommand
// ---------------------------------------------------------------------------------------------------------------------

FTransformCommand::FTransformCommand(const FString& InLabel, AActor* InActor, const FTransform& InNewWorld, const FString& InRef)
	: Label(InLabel), Actor(InActor), NewWorld(InNewWorld), Ref(InRef)
{
}

bool FTransformCommand::Apply(const FTransform& T)
{
	AActor* A = Actor.Get();
	if (!A)
	{
		return false;
	}
	A->SetActorTransform(T, false, nullptr, ETeleportType::TeleportPhysics);
	return true;
}

bool FTransformCommand::Do(FRuntimeCommandContext& Context, FString& OutError)
{
	AActor* A = Actor.Get();
	if (!A)
	{
		OutError = TEXT("actor no longer exists");
		return false;
	}
	USceneComponent* Root = A->GetRootComponent();
	if (!Root)
	{
		OutError = TEXT("actor has no root component");
		return false;
	}
	OldWorld = A->GetActorTransform();
	if (Root->Mobility != EComponentMobility::Movable)
	{
		OldMobility = static_cast<uint8>(Root->Mobility.GetValue());
		bChangedMobility = true;
		Root->SetMobility(EComponentMobility::Movable);
	}
	return Apply(NewWorld);
}

bool FTransformCommand::Undo(FRuntimeCommandContext& Context)
{
	if (!Apply(OldWorld))
	{
		return false;
	}
	if (bChangedMobility)
	{
		if (USceneComponent* Root = Actor->GetRootComponent())
		{
			Root->SetMobility(static_cast<EComponentMobility::Type>(OldMobility));
		}
	}
	return true;
}

bool FTransformCommand::Redo(FRuntimeCommandContext& Context)
{
	AActor* A = Actor.Get();
	if (!A)
	{
		return false;
	}
	if (bChangedMobility && A->GetRootComponent())
	{
		A->GetRootComponent()->SetMobility(EComponentMobility::Movable);
	}
	return Apply(NewWorld);
}

TSharedPtr<FJsonObject> FTransformCommand::GetDelta(bool bForUndo) const
{
	const FTransform& T = bForUndo ? OldWorld : NewWorld;
	TSharedRef<FJsonObject> D = MakeShared<FJsonObject>();
	D->SetStringField(TEXT("moved"), Ref);
	D->SetField(TEXT("location"), RuntimeJson::Vec(T.GetLocation()));
	D->SetField(TEXT("rotation"), RuntimeJson::Rot(T.Rotator()));
	D->SetField(TEXT("scale"), RuntimeJson::Vec(T.GetScale3D()));
	return D;
}

// ---------------------------------------------------------------------------------------------------------------------
// FAttachCommand
// ---------------------------------------------------------------------------------------------------------------------

FAttachCommand::FAttachCommand(const FString& InLabel, AActor* InChild, AActor* InNewParent, FName InSocket, bool bInKeepWorld)
	: Label(InLabel), Child(InChild), NewParent(InNewParent), Socket(InSocket), bKeepWorld(bInKeepWorld)
{
}

bool FAttachCommand::AttachTo(FRuntimeCommandContext& Context, AActor* Parent, FName InSocket, bool bInKeepWorld, const FTransform* RelativeOverride,
	FString& OutError)
{
	AActor* C = Child.Get();
	if (!C || !C->GetRootComponent())
	{
		OutError = TEXT("child actor no longer exists");
		return false;
	}
	C->GetRootComponent()->SetMobility(EComponentMobility::Movable);
	if (Parent)
	{
		if (Parent == C || Parent->IsAttachedTo(C))
		{
			OutError = TEXT("cannot attach an actor to itself or its descendant");
			return false;
		}
		const FAttachmentTransformRules Rules = bInKeepWorld ? FAttachmentTransformRules::KeepWorldTransform : FAttachmentTransformRules::KeepRelativeTransform;
		if (!C->AttachToActor(Parent, Rules, InSocket))
		{
			OutError = TEXT("attach failed (check mobility: a movable child cannot attach to a static parent)");
			return false;
		}
	}
	else
	{
		C->DetachFromActor(bInKeepWorld ? FDetachmentTransformRules::KeepWorldTransform : FDetachmentTransformRules::KeepRelativeTransform);
	}
	if (RelativeOverride)
	{
		C->GetRootComponent()->SetRelativeTransform(*RelativeOverride);
	}
	if (Context.Graph)
	{
		if (const FRuntimeSceneNode* Node = Context.Graph->FindByActor(C))
		{
			Context.Graph->SyncParentFromAttachment(Node->Id);
		}
	}
	return true;
}

bool FAttachCommand::Do(FRuntimeCommandContext& Context, FString& OutError)
{
	AActor* C = Child.Get();
	if (!C)
	{
		OutError = TEXT("child actor no longer exists");
		return false;
	}
	if (NewParent.IsExplicitlyNull() == false && !NewParent.IsValid())
	{
		OutError = TEXT("parent actor no longer exists");
		return false;
	}
	OldParent = C->GetAttachParentActor();
	OldSocket = C->GetAttachParentSocketName();
	OldWorld = C->GetActorTransform();
	OldRelative = C->GetRootComponent() ? C->GetRootComponent()->GetRelativeTransform() : FTransform::Identity;
	ChildRef = RuntimeObjectResolver::MakeReference(C, Context.Graph);
	return AttachTo(Context, NewParent.Get(), Socket, bKeepWorld, nullptr, OutError);
}

bool FAttachCommand::Undo(FRuntimeCommandContext& Context)
{
	FString Error;
	AActor* C = Child.Get();
	if (!C)
	{
		return false;
	}
	if (AActor* P = OldParent.Get())
	{
		return AttachTo(Context, P, OldSocket, false, &OldRelative, Error);
	}
	if (!AttachTo(Context, nullptr, NAME_None, true, nullptr, Error))
	{
		return false;
	}
	C->SetActorTransform(OldWorld);
	return true;
}

bool FAttachCommand::Redo(FRuntimeCommandContext& Context)
{
	FString Error;
	return AttachTo(Context, NewParent.Get(), Socket, bKeepWorld, nullptr, Error);
}

TSharedPtr<FJsonObject> FAttachCommand::GetDelta(bool bForUndo) const
{
	TSharedRef<FJsonObject> D = MakeShared<FJsonObject>();
	D->SetStringField(TEXT("reparented"), ChildRef);
	const AActor* P = bForUndo ? OldParent.Get() : NewParent.Get();
	D->SetStringField(TEXT("parent"), P ? P->GetName() : TEXT(""));
	return D;
}

// ---------------------------------------------------------------------------------------------------------------------
// FPropertyCommand
// ---------------------------------------------------------------------------------------------------------------------

FPropertyCommand::FPropertyCommand(UObject* InObject, const FString& InPath, const TSharedPtr<FJsonValue>& InNewValue, const FString& InRef)
	: Object(InObject), Path(InPath), NewValue(InNewValue), Ref(InRef)
{
}

FString FPropertyCommand::GetLabel() const
{
	return FString::Printf(TEXT("set %s.%s"), *Ref, *Path);
}

bool FPropertyCommand::Do(FRuntimeCommandContext& Context, FString& OutError)
{
	UObject* Obj = Object.Get();
	if (!Obj)
	{
		OutError = TEXT("object no longer exists");
		return false;
	}
	OldValue = RuntimePropertyPath::Get(Obj, Path, OutError);
	if (!OldValue.IsValid())
	{
		return false;
	}
	if (!RuntimePropertyPath::Set(Obj, Path, NewValue, OutError))
	{
		return false;
	}
	FString Ignored;
	AppliedValue = RuntimePropertyPath::Get(Obj, Path, Ignored);
	return true;
}

bool FPropertyCommand::Undo(FRuntimeCommandContext& Context)
{
	FString Error;
	UObject* Obj = Object.Get();
	return Obj && RuntimePropertyPath::Set(Obj, Path, OldValue, Error);
}

bool FPropertyCommand::Redo(FRuntimeCommandContext& Context)
{
	FString Error;
	UObject* Obj = Object.Get();
	return Obj && RuntimePropertyPath::Set(Obj, Path, AppliedValue.IsValid() ? AppliedValue : NewValue, Error);
}

TSharedPtr<FJsonObject> FPropertyCommand::GetDelta(bool bForUndo) const
{
	TSharedRef<FJsonObject> D = MakeShared<FJsonObject>();
	D->SetStringField(TEXT("target"), Ref);
	D->SetStringField(TEXT("property"), Path);
	D->SetField(TEXT("value"), bForUndo ? OldValue : (AppliedValue.IsValid() ? AppliedValue : NewValue));
	return D;
}
