#include "RuntimeSceneGraph.h"
#include "RuntimeJson.h"
#include "RuntimeToolHelpers.h"
#include "RuntimeToolsetsGroupActor.h"
#include "RuntimeToolsetsLog.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

ARuntimeToolsetsGroupActor::ARuntimeToolsetsGroupActor()
{
	PrimaryActorTick.bCanEverTick = false;
	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Root->SetMobility(EComponentMobility::Movable);
	SetRootComponent(Root);
}

FString FRuntimeSceneGraph::AllocateId()
{
	FString Id;
	do
	{
		Id = FString::Printf(TEXT("n%lld"), NextId++);
	} while (Nodes.Contains(Id));
	return Id;
}

bool FRuntimeSceneGraph::IsNameTaken(const FString& ParentId, const FString& Name, const FString& IgnoreId) const
{
	for (const TPair<FString, FRuntimeSceneNode>& Pair : Nodes)
	{
		const FRuntimeSceneNode& N = Pair.Value;
		if (!N.bSoftDeleted && N.ParentId == ParentId && N.Id != IgnoreId && N.Name.Equals(Name, ESearchCase::IgnoreCase))
		{
			return true;
		}
	}
	return false;
}

FString FRuntimeSceneGraph::MakeUniqueName(const FString& ParentId, const FString& BaseName, const FString& IgnoreId) const
{
	FString Base = BaseName.IsEmpty() ? FString(TEXT("node")) : BaseName;
	Base.ReplaceInline(TEXT("/"), TEXT("_"));
	if (!IsNameTaken(ParentId, Base, IgnoreId))
	{
		return Base;
	}
	for (int32 i = 2;; ++i)
	{
		const FString Candidate = FString::Printf(TEXT("%s_%d"), *Base, i);
		if (!IsNameTaken(ParentId, Candidate, IgnoreId))
		{
			return Candidate;
		}
	}
}

void FRuntimeSceneGraph::AttachActorToParent(FRuntimeSceneNode& Node)
{
	AActor* Actor = Node.Actor.Get();
	if (!Actor)
	{
		return;
	}
	const FRuntimeSceneNode* Parent = Node.ParentId.IsEmpty() ? nullptr : Nodes.Find(Node.ParentId);
	AActor* ParentActor = Parent ? Parent->Actor.Get() : nullptr;
	if (ParentActor && ParentActor != Actor)
	{
		if (USceneComponent* Root = Actor->GetRootComponent())
		{
			Root->SetMobility(EComponentMobility::Movable);
		}
		Actor->AttachToActor(ParentActor, FAttachmentTransformRules::KeepRelativeTransform);
	}
}

FRuntimeSceneNode& FRuntimeSceneGraph::AddNode(AActor* Actor, const FString& DesiredName, const FString& ParentId, ERuntimeNodeKind Kind,
	const FString& OwnerSession, const FString& Source, const FString& ForcedId)
{
	const FString Id = (!ForcedId.IsEmpty() && !Nodes.Contains(ForcedId)) ? ForcedId : AllocateId();
	FString ValidParent = Nodes.Contains(ParentId) ? ParentId : FString();

	FRuntimeSceneNode& Node = Nodes.Add(Id);
	Node.Id = Id;
	Node.Name = MakeUniqueName(ValidParent, DesiredName, Id);
	Node.ParentId = ValidParent;
	Node.Kind = Kind;
	Node.Actor = Actor;
	Node.OwnerSession = OwnerSession;
	Node.Source = Source;

	if (!ValidParent.IsEmpty())
	{
		Nodes[ValidParent].Children.AddUnique(Id);
	}
	// Re-fetch: Add() may have reallocated, but Node is still valid as TMap::Add returns a stable reference until next add.
	FRuntimeSceneNode& Stored = Nodes[Id];
	AttachActorToParent(Stored);
	if (Actor)
	{
		Actor->Tags.AddUnique(FName(*FString::Printf(TEXT("rt:%s"), *Id)));
	}
	return Stored;
}

void FRuntimeSceneGraph::RemoveNode(const FString& Id, bool bDestroyActors)
{
	FRuntimeSceneNode* Node = Nodes.Find(Id);
	if (!Node)
	{
		return;
	}
	const TArray<FString> Children = Node->Children;
	for (const FString& Child : Children)
	{
		RemoveNode(Child, bDestroyActors);
	}
	Node = Nodes.Find(Id);
	if (!Node)
	{
		return;
	}
	if (FRuntimeSceneNode* Parent = Nodes.Find(Node->ParentId))
	{
		Parent->Children.Remove(Id);
	}
	if (bDestroyActors)
	{
		if (AActor* Actor = Node->Actor.Get())
		{
			Actor->Destroy();
		}
	}
	Nodes.Remove(Id);
}

FRuntimeSceneNode* FRuntimeSceneGraph::Find(const FString& Id)
{
	return Nodes.Find(Id);
}

const FRuntimeSceneNode* FRuntimeSceneGraph::Find(const FString& Id) const
{
	return Nodes.Find(Id);
}

FRuntimeSceneNode* FRuntimeSceneGraph::FindByActor(const AActor* Actor)
{
	if (!Actor)
	{
		return nullptr;
	}
	for (TPair<FString, FRuntimeSceneNode>& Pair : Nodes)
	{
		if (Pair.Value.Actor.Get() == Actor)
		{
			return &Pair.Value;
		}
	}
	return nullptr;
}

FRuntimeSceneNode* FRuntimeSceneGraph::FindByPath(const FString& Path)
{
	TArray<FString> Parts;
	Path.ParseIntoArray(Parts, TEXT("/"), true);
	if (Parts.Num() == 0)
	{
		return nullptr;
	}
	FString ParentId;
	FRuntimeSceneNode* Current = nullptr;
	for (const FString& Part : Parts)
	{
		Current = nullptr;
		for (TPair<FString, FRuntimeSceneNode>& Pair : Nodes)
		{
			FRuntimeSceneNode& N = Pair.Value;
			if (!N.bSoftDeleted && N.ParentId == ParentId && N.Name.Equals(Part, ESearchCase::IgnoreCase))
			{
				Current = &N;
				break;
			}
		}
		if (!Current)
		{
			return nullptr;
		}
		ParentId = Current->Id;
	}
	return Current;
}

FString FRuntimeSceneGraph::GetPath(const FString& Id) const
{
	TArray<FString> Parts;
	const FRuntimeSceneNode* Node = Nodes.Find(Id);
	int32 Guard = 0;
	while (Node && Guard++ < 256)
	{
		Parts.Insert(Node->Name, 0);
		Node = Node->ParentId.IsEmpty() ? nullptr : Nodes.Find(Node->ParentId);
	}
	return FString::Join(Parts, TEXT("/"));
}

void FRuntimeSceneGraph::SyncParentFromAttachment(const FString& Id)
{
	FRuntimeSceneNode* Node = Nodes.Find(Id);
	if (!Node)
	{
		return;
	}
	AActor* Actor = Node->Actor.Get();
	AActor* AttachParent = Actor ? Actor->GetAttachParentActor() : nullptr;
	FString NewParentId;
	if (FRuntimeSceneNode* ParentNode = FindByActor(AttachParent))
	{
		NewParentId = ParentNode->Id;
	}
	Node = Nodes.Find(Id);
	if (Node->ParentId == NewParentId)
	{
		return;
	}
	if (FRuntimeSceneNode* OldParent = Nodes.Find(Node->ParentId))
	{
		OldParent->Children.Remove(Id);
	}
	Node->ParentId = NewParentId;
	if (FRuntimeSceneNode* NewParent = Nodes.Find(NewParentId))
	{
		NewParent->Children.AddUnique(Id);
	}
	if (IsNameTaken(NewParentId, Node->Name, Id))
	{
		Node->Name = MakeUniqueName(NewParentId, Node->Name, Id);
	}
}

bool FRuntimeSceneGraph::IsAncestorOrSelf(const FString& PotentialAncestor, const FString& Id) const
{
	const FRuntimeSceneNode* Node = Nodes.Find(Id);
	int32 Guard = 0;
	while (Node && Guard++ < 256)
	{
		if (Node->Id == PotentialAncestor)
		{
			return true;
		}
		Node = Node->ParentId.IsEmpty() ? nullptr : Nodes.Find(Node->ParentId);
	}
	return false;
}

void FRuntimeSceneGraph::SoftDelete(const FString& Id)
{
	FRuntimeSceneNode* Node = Nodes.Find(Id);
	if (!Node || Node->bSoftDeleted)
	{
		return;
	}
	Node->bSoftDeleted = true;
	if (AActor* Actor = Node->Actor.Get())
	{
		Node->bHiddenBeforeDelete = Actor->IsHidden();
		Node->bCollisionBeforeDelete = Actor->GetActorEnableCollision();
		Actor->SetActorHiddenInGame(true);
		Actor->SetActorEnableCollision(false);
		// Volumes are always hidden in game, so hiding does not stop a post-process volume: switch it off too, or a
		// deleted volume keeps grading the image (and lighting tools would find and reuse it until it is destroyed).
		if (APostProcessVolume* Volume = Cast<APostProcessVolume>(Actor))
		{
			Node->bVolumeEnabledBeforeDelete = Volume->bEnabled;
			Volume->bEnabled = false;
		}
	}
}

void FRuntimeSceneGraph::Restore(const FString& Id)
{
	FRuntimeSceneNode* Node = Nodes.Find(Id);
	if (!Node || !Node->bSoftDeleted)
	{
		return;
	}
	Node->bSoftDeleted = false;
	if (AActor* Actor = Node->Actor.Get())
	{
		Actor->SetActorHiddenInGame(Node->bHiddenBeforeDelete);
		Actor->SetActorEnableCollision(Node->bCollisionBeforeDelete);
		if (APostProcessVolume* Volume = Cast<APostProcessVolume>(Actor))
		{
			Volume->bEnabled = Node->bVolumeEnabledBeforeDelete;
		}
	}
	// A sibling may have taken the name meanwhile.
	if (IsNameTaken(Node->ParentId, Node->Name, Id))
	{
		Node->Name = MakeUniqueName(Node->ParentId, Node->Name, Id);
	}
}

TArray<FString> FRuntimeSceneGraph::CollectSubtree(const FString& Id, bool bIncludeSoftDeleted) const
{
	TArray<FString> Out;
	const FRuntimeSceneNode* Node = Nodes.Find(Id);
	if (!Node || (!bIncludeSoftDeleted && Node->bSoftDeleted))
	{
		return Out;
	}
	Out.Add(Id);
	for (const FString& Child : Node->Children)
	{
		Out.Append(CollectSubtree(Child, bIncludeSoftDeleted));
	}
	return Out;
}

TArray<FString> FRuntimeSceneGraph::GetRootIds(bool bIncludeSoftDeleted) const
{
	TArray<FString> Out;
	for (const TPair<FString, FRuntimeSceneNode>& Pair : Nodes)
	{
		if (Pair.Value.ParentId.IsEmpty() && (bIncludeSoftDeleted || !Pair.Value.bSoftDeleted))
		{
			Out.Add(Pair.Key);
		}
	}
	// Stable order: numeric id.
	Out.Sort([](const FString& A, const FString& B) { return FCString::Atoi64(*A + 1) < FCString::Atoi64(*B + 1); });
	return Out;
}

int32 FRuntimeSceneGraph::NumLiveNodes() const
{
	int32 Count = 0;
	for (const TPair<FString, FRuntimeSceneNode>& Pair : Nodes)
	{
		Count += Pair.Value.bSoftDeleted ? 0 : 1;
	}
	return Count;
}

TSharedRef<FJsonObject> FRuntimeSceneGraph::NodeToJson(const FRuntimeSceneNode& Node, const TArray<FString>& Fields) const
{
	TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
	J->SetStringField(TEXT("id"), Node.Id);
	if (RuntimeJson::Wants(Fields, TEXT("name")))
	{
		J->SetStringField(TEXT("name"), Node.Name);
	}
	if (RuntimeJson::Wants(Fields, TEXT("path")))
	{
		J->SetStringField(TEXT("path"), GetPath(Node.Id));
	}
	if (RuntimeJson::Wants(Fields, TEXT("kind")))
	{
		J->SetStringField(TEXT("kind"), Node.Kind == ERuntimeNodeKind::Group ? TEXT("group") : TEXT("actor"));
	}
	if (RuntimeJson::Wants(Fields, TEXT("parent")) && !Node.ParentId.IsEmpty())
	{
		J->SetStringField(TEXT("parent"), Node.ParentId);
	}
	if (RuntimeJson::Wants(Fields, TEXT("children")) && Node.Children.Num() > 0)
	{
		J->SetField(TEXT("children"), RuntimeJson::Strings(Node.Children));
	}
	if (RuntimeJson::Wants(Fields, TEXT("tags")) && Node.Tags.Num() > 0)
	{
		J->SetField(TEXT("tags"), RuntimeJson::Names(Node.Tags));
	}
	if (RuntimeJson::Wants(Fields, TEXT("source")) && !Node.Source.IsEmpty())
	{
		J->SetStringField(TEXT("source"), Node.Source);
	}
	if (Node.bSoftDeleted)
	{
		J->SetBoolField(TEXT("deleted"), true);
	}
	const AActor* Actor = Node.Actor.Get();
	if (!Actor)
	{
		J->SetBoolField(TEXT("stale"), true);
		return J;
	}
	if (RuntimeJson::Wants(Fields, TEXT("class")))
	{
		J->SetStringField(TEXT("class"), Actor->GetClass()->GetName());
	}
	if (RuntimeJson::Wants(Fields, TEXT("location")))
	{
		J->SetField(TEXT("location"), RuntimeJson::Vec(Actor->GetActorLocation()));
	}
	if (RuntimeJson::Wants(Fields, TEXT("rotation")))
	{
		J->SetField(TEXT("rotation"), RuntimeJson::Rot(Actor->GetActorRotation()));
	}
	if (RuntimeJson::Wants(Fields, TEXT("scale")))
	{
		J->SetField(TEXT("scale"), RuntimeJson::Vec(Actor->GetActorScale3D()));
	}
	if (Fields.Contains(TEXT("bounds")))
	{
		const FBox Box = RuntimeTools::ActorBox(Actor); // includes child nodes
		TSharedRef<FJsonObject> B = MakeShared<FJsonObject>();
		B->SetField(TEXT("center"), RuntimeJson::Vec(Box.GetCenter()));
		B->SetField(TEXT("size"), RuntimeJson::Vec(Box.GetSize()));
		J->SetObjectField(TEXT("bounds"), B);
	}
	if (Actor->IsHidden() && !Node.bSoftDeleted)
	{
		J->SetBoolField(TEXT("hidden"), true);
	}
	return J;
}
