#include "RuntimeSceneToolset.h"
#include "Helpers/SpawnHelpers.h"
#include "Helpers/ToolsCommon.h"
#include "Helpers/TreeSerializer.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Components/SceneComponent.h"
#include "GameFramework/Actor.h"

using namespace RuntimeTools;

namespace
{
	constexpr int32 MaxTreeNodes = 2000;

	/** The live scene node of an actor argument; fails the call (naming the argument) if it is not one. */
	FRuntimeSceneNode* RequireNode(AActor* Actor, const TCHAR* Arg)
	{
		FRuntimeSceneGraph* Graph = RuntimeToolCall::GetGraph();
		FRuntimeSceneNode* Node = Graph && Actor ? Graph->FindByActor(Actor) : nullptr;
		if (!Node || Node->bSoftDeleted)
		{
			RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments,
				FString::Printf(TEXT("%s: %s is not a scene node (only tool-created actors and groups are)"), Arg, Actor ? *Actor->GetName() : TEXT("null")),
				TEXT("Use scene.find_nodes to list nodes, or actors.* tools for level actors"));
			return nullptr;
		}
		return Node;
	}

	bool SceneNameMatches(const FString& Name, const FString& Filter)
	{
		if (Filter.IsEmpty())
		{
			return true;
		}
		return Filter.Contains(TEXT("*")) || Filter.Contains(TEXT("?")) ? Name.MatchesWildcard(Filter) : Name.Contains(Filter);
	}

	TArray<FString> ToStrings(const TArray<FName>& Names)
	{
		TArray<FString> Out;
		for (const FName& N : Names)
		{
			Out.Add(N.ToString());
		}
		return Out;
	}

	FString KindName(const FRuntimeSceneNode& N)
	{
		return N.Kind == ERuntimeNodeKind::Group ? TEXT("group") : TEXT("actor");
	}

	TSharedRef<FJsonObject> TreeNodeJson(FRuntimeSceneGraph& Graph, const FRuntimeSceneNode& Node, int32 Depth, const TArray<FString>& Fields, int32& Budget)
	{
		--Budget;
		TArray<FString> NodeFields = Fields;
		NodeFields.Remove(TEXT("children"));
		if (NodeFields.Num() == 0)
		{
			NodeFields = {TEXT("name"), TEXT("kind")};
		}
		TSharedRef<FJsonObject> J = Graph.NodeToJson(Node, NodeFields);
		TArray<const FRuntimeSceneNode*> Live;
		for (const FString& ChildId : Node.Children)
		{
			const FRuntimeSceneNode* Child = Graph.Find(ChildId);
			if (Child && !Child->bSoftDeleted)
			{
				Live.Add(Child);
			}
		}
		if (Live.Num() == 0)
		{
			return J;
		}
		if (Depth <= 0 || Budget <= 0)
		{
			J->SetNumberField(TEXT("child_count"), Live.Num());
			return J;
		}
		TArray<TSharedPtr<FJsonValue>> Children;
		for (const FRuntimeSceneNode* Child : Live)
		{
			if (Budget <= 0)
			{
				J->SetBoolField(TEXT("truncated"), true);
				break;
			}
			Children.Add(MakeShared<FJsonValueObject>(TreeNodeJson(Graph, *Child, Depth - 1, Fields, Budget)));
		}
		J->SetArrayField(TEXT("children"), Children);
		return J;
	}

	FTransform ParentWorld(const AActor* Actor)
	{
		const USceneComponent* Root = Actor->GetRootComponent();
		const USceneComponent* Parent = Root ? Root->GetAttachParent() : nullptr;
		return Parent ? Parent->GetSocketTransform(Root->GetAttachSocketName()) : FTransform::Identity;
	}

	bool RunTransform(const FRuntimeSceneNode& Node, const FTransform& NewWorld, const TCHAR* Verb)
	{
		return RuntimeToolCall::RunCommand(MakeUnique<FTransformCommand>(FString::Printf(TEXT("%s %s"), Verb, *Node.Name), Node.Actor.Get(), NewWorld, Node.Id));
	}

	/** Moves a group's origin while its children keep their world transforms. */
	void MovePivot(AActor* Group, const FTransform& NewWorld)
	{
		TArray<AActor*> Children;
		Group->GetAttachedActors(Children, true, false);
		TArray<FTransform> Saved;
		for (AActor* C : Children)
		{
			Saved.Add(C->GetActorTransform());
		}
		Group->SetActorTransform(NewWorld);
		for (int32 i = 0; i < Children.Num(); ++i)
		{
			Children[i]->SetActorTransform(Saved[i]);
		}
	}

	void ApplyNodeTags(FRuntimeCommandContext& Ctx, const FString& Id, const TArray<FName>& Tags)
	{
		FRuntimeSceneNode* N = Ctx.Graph ? Ctx.Graph->Find(Id) : nullptr;
		if (!N)
		{
			return;
		}
		if (AActor* A = N->Actor.Get())
		{
			for (const FName& Old : N->Tags)
			{
				A->Tags.Remove(Old);
			}
			for (const FName& T : Tags)
			{
				A->Tags.AddUnique(T);
			}
		}
		N->Tags = Tags;
	}

	TArray<AActor*> ActorsOf(FRuntimeSceneGraph& Graph, const TArray<FString>& Ids)
	{
		TArray<AActor*> Out;
		for (const FString& Id : Ids)
		{
			if (const FRuntimeSceneNode* N = Graph.Find(Id))
			{
				if (AActor* A = N->Actor.Get())
				{
					Out.Add(A);
				}
			}
		}
		return Out;
	}

	/** Shared by clear_scope / clear_all: confirm, auto-checkpoint, one composite soft-delete of the session's nodes. */
	FRuntimeClearResult ClearNodes(bool bConfirm, const TArray<FString>& RootIds, const FString& Label)
	{
		FRuntimeClearResult Out;
		FToolInvocation& Inv = *RuntimeToolCall::Current();
		if (!bConfirm)
		{
			RuntimeToolCall::Fail(ERuntimeToolError::ConfirmRequired, TEXT("this deletes nodes; pass confirm=true"), TEXT("An automatic checkpoint is created first"));
			return Out;
		}
		if (Inv.GetHistory().IsInTransaction())
		{
			RuntimeToolCall::Fail(ERuntimeToolError::WrongState, TEXT("a history transaction is open"), TEXT("Call history.end_transaction first"));
			return Out;
		}
		FRuntimeSceneGraph& Graph = *Inv.GetGraph();
		const FString Session = Inv.GetCaller().SessionId;
		TArray<FString> Ids;
		for (const FString& Root : RootIds)
		{
			for (const FString& Id : Graph.CollectSubtree(Root))
			{
				if (Graph.Find(Id)->OwnerSession == Session)
				{
					Ids.Add(Id);
				}
			}
		}
		if (Ids.Num() == 0)
		{
			return Out;
		}
		const FString CheckpointName = FString::Printf(TEXT("auto/%s_%s"), *Label, *FDateTime::UtcNow().ToString(TEXT("%H%M%S")));
		FRuntimeCommandContext Context = Inv.MakeCommandContext();
		Inv.GetHistory().Checkpoint(CheckpointName, Context);
		if (!RuntimeToolCall::RunCommand(MakeUnique<FSoftDeleteActorsCommand>(Label, ActorsOf(Graph, Ids))))
		{
			return {};
		}
		Out.Deleted = Ids.Num();
		Out.Checkpoint = CheckpointName;
		return Out;
	}
}

FRuntimeNodeInfo URuntimeSceneToolset::CreateGroup(const FString& Name, const FVector& Location, const FRotator& Rotation, const FVector& Scale,
	const TArray<FString>& Tags, AActor* Parent)
{
	FString ParentId;
	if (Parent)
	{
		const FRuntimeSceneNode* P = RequireNode(Parent, TEXT("parent"));
		if (!P)
		{
			return {};
		}
		ParentId = P->Id;
	}
	if (Name.IsEmpty())
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, TEXT("name is empty"), TEXT("Name the group after what it holds, e.g. \"house\""));
		return {};
	}
	const FTransform Transform(Rotation, Location, RuntimeToolCall::Current()->Has(TEXT("scale")) ? Scale : FVector::OneVector);
	auto Spawn = [Transform, Name](FRuntimeCommandContext& Ctx, FString& OutError)
	{
		return RuntimeSpawn::SpawnFromSource(Ctx.World, TEXT("group"), Transform, Name, OutError);
	};
	TUniquePtr<FCreateNodeCommand> Cmd = MakeUnique<FCreateNodeCommand>(FString::Printf(TEXT("create group %s"), *Name), Spawn, Name, ParentId,
		ERuntimeNodeKind::Group, TEXT("group"), ToNames(Tags));
	FCreateNodeCommand* Raw = Cmd.Get();
	if (!RuntimeToolCall::RunCommand(MoveTemp(Cmd)))
	{
		return {};
	}
	return RuntimeSceneTypes::MakeNodeInfo(Raw->GetNodeId());
}

FRuntimeTreeResult URuntimeSceneToolset::ListTree(const TArray<FString>& Fields, AActor* Root, int32 Depth, int32 Limit)
{
	FRuntimeTreeResult Out;
	FRuntimeSceneGraph& Graph = *RuntimeToolCall::GetGraph();
	Depth = FMath::Clamp(Depth, 1, 32);
	int32 Budget = URuntimeToolsetsSettings::Get().ClampLimit(Limit);
	TArray<TSharedPtr<FJsonValue>> Roots;
	if (Root)
	{
		const FRuntimeSceneNode* R = RequireNode(Root, TEXT("root"));
		if (!R)
		{
			return Out;
		}
		Roots.Add(MakeShared<FJsonValueObject>(TreeNodeJson(Graph, *R, Depth, Fields, Budget)));
	}
	else
	{
		for (const FString& Id : Graph.GetRootIds())
		{
			if (Budget <= 0)
			{
				break;
			}
			Roots.Add(MakeShared<FJsonValueObject>(TreeNodeJson(Graph, *Graph.Find(Id), Depth - 1, Fields, Budget)));
		}
	}
	Out.Nodes = FRuntimeToolJson::Make(MakeShared<FJsonValueArray>(Roots));
	Out.TotalNodes = Graph.NumLiveNodes();
	Out.bTruncated = Budget <= 0;
	return Out;
}

FRuntimeNodeList URuntimeSceneToolset::FindNodes(const FString& Pattern, const FString& Tag, ERuntimeNodeKindFilter Kind, AActor* Under, int32 Limit)
{
	FRuntimeNodeList Out;
	FRuntimeSceneGraph& Graph = *RuntimeToolCall::GetGraph();
	const int32 MaxItems = URuntimeToolsetsSettings::Get().ClampLimit(Limit);
	TArray<FString> Scope;
	if (Under)
	{
		const FRuntimeSceneNode* R = RequireNode(Under, TEXT("under"));
		if (!R)
		{
			return Out;
		}
		Scope = Graph.CollectSubtree(R->Id);
		Scope.RemoveAt(0); // below the node, not the node itself
	}
	else
	{
		for (const FString& Id : Graph.GetRootIds())
		{
			Scope.Append(Graph.CollectSubtree(Id));
		}
	}
	const FName TagName = Tag.IsEmpty() ? NAME_None : FName(*Tag);
	for (const FString& Id : Scope)
	{
		const FRuntimeSceneNode& N = *Graph.Find(Id);
		if (!SceneNameMatches(N.Name, Pattern) || (!TagName.IsNone() && !N.Tags.Contains(TagName)))
		{
			continue;
		}
		if ((Kind == ERuntimeNodeKindFilter::Group && N.Kind != ERuntimeNodeKind::Group) || (Kind == ERuntimeNodeKindFilter::Actor && N.Kind != ERuntimeNodeKind::Actor))
		{
			continue;
		}
		++Out.Total;
		if (Out.Nodes.Num() < MaxItems)
		{
			FRuntimeNodeSummary& S = Out.Nodes.AddDefaulted_GetRef();
			S.Id = N.Id;
			S.Name = N.Name;
			S.Path = Graph.GetPath(N.Id);
			S.Kind = KindName(N);
			S.Tags = ToStrings(N.Tags);
		}
	}
	return Out;
}

FRuntimeNodeDetails URuntimeSceneToolset::GetNode(AActor* Target)
{
	FRuntimeNodeDetails Out;
	const FRuntimeSceneNode* N = RequireNode(Target, TEXT("target"));
	if (!N)
	{
		return Out;
	}
	FRuntimeSceneGraph& Graph = *RuntimeToolCall::GetGraph();
	Out.Id = N->Id;
	Out.Name = N->Name;
	Out.Path = Graph.GetPath(N->Id);
	Out.Kind = KindName(*N);
	Out.Parent = N->ParentId;
	for (const FString& C : N->Children)
	{
		const FRuntimeSceneNode* Child = Graph.Find(C);
		if (Child && !Child->bSoftDeleted)
		{
			Out.Children.Add(C);
		}
	}
	Out.Tags = ToStrings(N->Tags);
	Out.Class = Target->GetClass()->GetName();
	Out.Source = N->Source;
	Out.Location = Target->GetActorLocation();
	Out.Rotation = Target->GetActorRotation();
	Out.Scale = Target->GetActorScale3D();
	const FBox Box = ActorBox(Target);
	Out.Bounds.Center = Box.GetCenter();
	Out.Bounds.Size = Box.GetSize();
	Out.bHidden = Target->IsHidden();
	FLinearColor Color;
	if (RuntimeSpawn::GetShapeColor(Target, Color))
	{
		Out.Color = {Color.R, Color.G, Color.B};
	}
	return Out;
}

FRuntimeTransformResult URuntimeSceneToolset::MoveNode(AActor* Target, const FVector& Location, const FVector& Offset, ERuntimeNodeSpace Space)
{
	const FRuntimeSceneNode* Node = RequireNode(Target, TEXT("target"));
	if (!Node)
	{
		return {};
	}
	FToolInvocation& Inv = *RuntimeToolCall::Current();
	const bool bHasLocation = Inv.Has(TEXT("location"));
	if (bHasLocation == Inv.Has(TEXT("offset")))
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, TEXT("give exactly one of location or offset"));
		return {};
	}
	const bool bLocal = Space == ERuntimeNodeSpace::Local;
	FTransform World = Target->GetActorTransform();
	if (bHasLocation)
	{
		World.SetLocation(bLocal ? ParentWorld(Target).TransformPosition(Location) : Location);
	}
	else
	{
		World.AddToTranslation(bLocal ? Target->GetActorQuat().RotateVector(Offset) : Offset);
	}
	return RunTransform(*Node, World, TEXT("move")) ? RuntimeSceneTypes::MakeTransformResult(Target) : FRuntimeTransformResult();
}

FRuntimeTransformResult URuntimeSceneToolset::RotateNode(AActor* Target, const FRotator& Rotation, const FRotator& Delta, ERuntimeNodeSpace Space)
{
	const FRuntimeSceneNode* Node = RequireNode(Target, TEXT("target"));
	if (!Node)
	{
		return {};
	}
	FToolInvocation& Inv = *RuntimeToolCall::Current();
	const bool bHasRotation = Inv.Has(TEXT("rotation"));
	if (bHasRotation == Inv.Has(TEXT("delta")))
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, TEXT("give exactly one of rotation or delta"));
		return {};
	}
	const bool bLocal = Space == ERuntimeNodeSpace::Local;
	FTransform World = Target->GetActorTransform();
	if (bHasRotation)
	{
		World.SetRotation(bLocal ? ParentWorld(Target).GetRotation() * Rotation.Quaternion() : Rotation.Quaternion());
	}
	else
	{
		const FQuat D = Delta.Quaternion();
		World.SetRotation(bLocal ? World.GetRotation() * D : D * World.GetRotation());
	}
	return RunTransform(*Node, World, TEXT("rotate")) ? RuntimeSceneTypes::MakeTransformResult(Target) : FRuntimeTransformResult();
}

FRuntimeTransformResult URuntimeSceneToolset::ScaleNode(AActor* Target, const FVector& Scale, const FVector& Factor)
{
	const FRuntimeSceneNode* Node = RequireNode(Target, TEXT("target"));
	if (!Node)
	{
		return {};
	}
	FToolInvocation& Inv = *RuntimeToolCall::Current();
	FTransform World = Target->GetActorTransform();
	FVector NewScale;
	if (Inv.Has(TEXT("scale")))
	{
		NewScale = Scale;
	}
	else if (Inv.Has(TEXT("factor")))
	{
		NewScale = World.GetScale3D() * Factor;
	}
	else
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, TEXT("give scale or factor"), TEXT("e.g. {\"factor\":[2,2,2]}"));
		return {};
	}
	if (NewScale.GetAbsMin() <= KINDA_SMALL_NUMBER)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, TEXT("scale components must be non-zero"), TEXT("Hide nodes with actors.set_visibility instead of scaling to 0"));
		return {};
	}
	World.SetScale3D(NewScale);
	FString Warning;
	if (!NewScale.AllComponentsEqual(KINDA_SMALL_NUMBER))
	{
		TArray<AActor*> Children;
		Target->GetAttachedActors(Children, true, false);
		for (AActor* Child : Children)
		{
			if (Child->GetRootComponent() && !Child->GetRootComponent()->GetRelativeRotation().IsNearlyZero(0.01))
			{
				Warning = TEXT("non-uniform scale on a parent with rotated children shears them; scale the leaves instead");
				break;
			}
		}
	}
	if (!RunTransform(*Node, World, TEXT("scale")))
	{
		return {};
	}
	FRuntimeTransformResult Out = RuntimeSceneTypes::MakeTransformResult(Target);
	Out.Warning = Warning;
	return Out;
}

FRuntimeNodeInfo URuntimeSceneToolset::ReparentNode(AActor* Target, AActor* Parent, bool bKeepWorld)
{
	const FRuntimeSceneNode* Node = RequireNode(Target, TEXT("target"));
	if (!Node)
	{
		return {};
	}
	if (Parent)
	{
		const FRuntimeSceneNode* P = RequireNode(Parent, TEXT("parent"));
		if (!P)
		{
			return {};
		}
		if (RuntimeToolCall::GetGraph()->IsAncestorOrSelf(Node->Id, P->Id))
		{
			RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("cannot parent %s under itself or its descendant %s"), *Node->Name, *P->Name));
			return {};
		}
	}
	const FString Id = Node->Id;
	if (!RuntimeToolCall::RunCommand(MakeUnique<FAttachCommand>(FString::Printf(TEXT("reparent %s"), *Node->Name), Target, Parent, NAME_None, bKeepWorld)))
	{
		return {};
	}
	return RuntimeSceneTypes::MakeNodeInfo(Id);
}

FRuntimePivotResult URuntimeSceneToolset::SetPivot(AActor* Target, const FVector& Location)
{
	const FRuntimeSceneNode* Node = RequireNode(Target, TEXT("target"));
	if (!Node)
	{
		return {};
	}
	if (Node->Kind != ERuntimeNodeKind::Group)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::Unsupported, FString::Printf(TEXT("set_pivot works on groups; %s is an actor node"), *Node->Name),
			TEXT("Wrap the node in scene.create_group and reparent it"));
		return {};
	}
	TWeakObjectPtr<AActor> Group = Target;
	const FTransform Old = Target->GetActorTransform();
	FTransform New = Old;
	FVector Pivot = Location;
	if (!RuntimeToolCall::Current()->Has(TEXT("location")))
	{
		FVector Origin, Extent;
		Target->GetActorBounds(false, Origin, Extent, true);
		Pivot = Origin;
	}
	New.SetLocation(Pivot);
	FRuntimePivotResult Out;
	Out.Pivot = Node->Id;
	Out.Location = Pivot;
	TSharedRef<FJsonObject> Delta = Obj();
	Delta->SetStringField(TEXT("pivot"), Node->Id);
	Delta->SetField(TEXT("location"), RuntimeJson::Vec(Pivot));
	auto DoFn = [Group, New](FRuntimeCommandContext&, FString& OutError)
	{
		if (!Group.IsValid())
		{
			OutError = TEXT("group no longer exists");
			return false;
		}
		MovePivot(Group.Get(), New);
		return true;
	};
	auto UndoFn = [Group, Old](FRuntimeCommandContext&)
	{
		if (!Group.IsValid())
		{
			return false;
		}
		MovePivot(Group.Get(), Old);
		return true;
	};
	if (!RuntimeToolCall::RunCommand(MakeUnique<FLambdaCommand>(FString::Printf(TEXT("set pivot %s"), *Node->Name), DoFn, UndoFn, Delta)))
	{
		return {};
	}
	return Out;
}

FRuntimeNodeInfo URuntimeSceneToolset::RenameNode(AActor* Target, const FString& Name)
{
	const FRuntimeSceneNode* Node = RequireNode(Target, TEXT("target"));
	if (!Node)
	{
		return {};
	}
	const FString NewName = Name.Replace(TEXT("/"), TEXT("_"));
	if (NewName.IsEmpty())
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, TEXT("name is empty"));
		return {};
	}
	FRuntimeSceneGraph* Graph = RuntimeToolCall::GetGraph();
	if (Graph->IsNameTaken(Node->ParentId, NewName, Node->Id))
	{
		RuntimeToolCall::Fail(ERuntimeToolError::WrongState, FString::Printf(TEXT("a sibling is already named '%s'"), *NewName),
			FString::Printf(TEXT("Pick another name, e.g. '%s'"), *Graph->MakeUniqueName(Node->ParentId, NewName, Node->Id)));
		return {};
	}
	const FString Id = Node->Id;
	const FString OldName = Node->Name;
	TSharedRef<FJsonObject> Delta = Obj();
	Delta->SetStringField(TEXT("renamed"), Id);
	Delta->SetStringField(TEXT("name"), NewName);
	auto Set = [Id](FRuntimeCommandContext& Ctx, const FString& N)
	{
		FRuntimeSceneNode* Found = Ctx.Graph ? Ctx.Graph->Find(Id) : nullptr;
		if (!Found)
		{
			return false;
		}
		Found->Name = Ctx.Graph->IsNameTaken(Found->ParentId, N, Id) ? Ctx.Graph->MakeUniqueName(Found->ParentId, N, Id) : N;
		return true;
	};
	auto DoFn = [Set, NewName](FRuntimeCommandContext& Ctx, FString& OutError)
	{
		if (!Set(Ctx, NewName))
		{
			OutError = TEXT("node no longer exists");
			return false;
		}
		return true;
	};
	auto UndoFn = [Set, OldName](FRuntimeCommandContext& Ctx) { return Set(Ctx, OldName); };
	if (!RuntimeToolCall::RunCommand(MakeUnique<FLambdaCommand>(FString::Printf(TEXT("rename %s"), *OldName), DoFn, UndoFn, Delta)))
	{
		return {};
	}
	return RuntimeSceneTypes::MakeNodeInfo(Id);
}

FRuntimeTagsResult URuntimeSceneToolset::SetNodeTags(AActor* Target, const TArray<FString>& Tags, ERuntimeTagMode Mode)
{
	const FRuntimeSceneNode* Node = RequireNode(Target, TEXT("target"));
	if (!Node)
	{
		return {};
	}
	const TArray<FName> Given = ToNames(Tags);
	const TArray<FName> Old = Node->Tags;
	TArray<FName> New = Mode == ERuntimeTagMode::Set ? Given : Old;
	for (const FName& T : Given)
	{
		if (Mode == ERuntimeTagMode::Add)
		{
			New.AddUnique(T);
		}
		else if (Mode == ERuntimeTagMode::Remove)
		{
			New.Remove(T);
		}
	}
	const FString Id = Node->Id;
	TSharedRef<FJsonObject> Delta = Obj();
	Delta->SetStringField(TEXT("target"), Id);
	Delta->SetField(TEXT("tags"), RuntimeJson::Names(New));
	auto DoFn = [Id, New](FRuntimeCommandContext& Ctx, FString&)
	{
		ApplyNodeTags(Ctx, Id, New);
		return true;
	};
	auto UndoFn = [Id, Old](FRuntimeCommandContext& Ctx)
	{
		ApplyNodeTags(Ctx, Id, Old);
		return true;
	};
	if (!RuntimeToolCall::RunCommand(MakeUnique<FLambdaCommand>(FString::Printf(TEXT("tags %s"), *Node->Name), DoFn, UndoFn, Delta)))
	{
		return {};
	}
	FRuntimeTagsResult Out;
	Out.Target = Id;
	Out.Tags = ToStrings(New);
	return Out;
}

FRuntimeDuplicateResult URuntimeSceneToolset::DuplicateNode(AActor* Target, const FVector& Offset, const FString& Name)
{
	const FRuntimeSceneNode* Node = RequireNode(Target, TEXT("target"));
	if (!Node)
	{
		return {};
	}
	FRuntimeSceneGraph& Graph = *RuntimeToolCall::GetGraph();
	const TArray<FString> Subtree = Graph.CollectSubtree(Node->Id);
	if (Subtree.Num() > MaxTreeNodes)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("subtree has %d nodes (max %d)"), Subtree.Num(), MaxTreeNodes),
			TEXT("Duplicate smaller subtrees"));
		return {};
	}
	const TSharedRef<FJsonObject> Spec = RuntimeTreeSerializer::Export(Graph, Node->Id);
	FString Err;
	TArray<FString> RootIds;
	TUniquePtr<FCompositeCommand> Cmd = RuntimeTreeSerializer::BuildImport(Graph, Spec, Node->ParentId, Name, Offset, RootIds, nullptr, Err);
	if (!Cmd.IsValid())
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, Err);
		return {};
	}
	Cmd->SetLabel(FString::Printf(TEXT("duplicate %s"), *Node->Name));
	if (!RuntimeToolCall::RunCommand(MoveTemp(Cmd)))
	{
		return {};
	}
	const FRuntimeNodeInfo Info = RuntimeSceneTypes::MakeNodeInfo(RootIds.Num() > 0 ? RootIds[0] : FString());
	FRuntimeDuplicateResult Out;
	Out.Id = Info.Id;
	Out.Name = Info.Name;
	Out.Path = Info.Path;
	Out.Nodes = Subtree.Num();
	return Out;
}

FRuntimeDeleteNodeResult URuntimeSceneToolset::DeleteNode(AActor* Target, bool bPromoteChildren)
{
	const FRuntimeSceneNode* Node = RequireNode(Target, TEXT("target"));
	if (!Node)
	{
		return {};
	}
	FRuntimeSceneGraph& Graph = *RuntimeToolCall::GetGraph();
	const FString Id = Node->Id;
	TUniquePtr<FCompositeCommand> Cmd = MakeUnique<FCompositeCommand>(FString::Printf(TEXT("delete %s"), *Node->Name));
	int32 Deleted = 0;
	if (bPromoteChildren)
	{
		const FRuntimeSceneNode* Parent = Graph.Find(Node->ParentId);
		AActor* ParentActor = Parent ? Parent->Actor.Get() : nullptr;
		for (const FString& ChildId : Node->Children)
		{
			const FRuntimeSceneNode* Child = Graph.Find(ChildId);
			if (Child && !Child->bSoftDeleted && Child->Actor.IsValid())
			{
				Cmd->Add(MakeUnique<FAttachCommand>(TEXT("promote"), Child->Actor.Get(), ParentActor, NAME_None, true));
			}
		}
		Cmd->Add(MakeUnique<FSoftDeleteActorsCommand>(TEXT("delete"), ActorsOf(Graph, {Id})));
		Deleted = 1;
	}
	else
	{
		const TArray<FString> Subtree = Graph.CollectSubtree(Id);
		Cmd->Add(MakeUnique<FSoftDeleteActorsCommand>(TEXT("delete"), ActorsOf(Graph, Subtree)));
		Deleted = Subtree.Num();
	}
	if (!RuntimeToolCall::RunCommand(MoveTemp(Cmd)))
	{
		return {};
	}
	FRuntimeDeleteNodeResult Out;
	Out.Deleted = Id;
	Out.Nodes = Deleted;
	return Out;
}

FRuntimeClearResult URuntimeSceneToolset::ClearScope(AActor* Root, bool bConfirm)
{
	const FRuntimeSceneNode* R = RequireNode(Root, TEXT("root"));
	if (!R)
	{
		return {};
	}
	TArray<FString> Children;
	for (const FString& C : R->Children)
	{
		const FRuntimeSceneNode* N = RuntimeToolCall::GetGraph()->Find(C);
		if (N && !N->bSoftDeleted)
		{
			Children.Add(C);
		}
	}
	return ClearNodes(bConfirm, Children, FString::Printf(TEXT("clear_scope %s"), *R->Name));
}

FRuntimeClearResult URuntimeSceneToolset::ClearAll(bool bConfirm)
{
	return ClearNodes(bConfirm, RuntimeToolCall::GetGraph()->GetRootIds(), TEXT("clear_all"));
}

FRuntimeToolJson URuntimeSceneToolset::ExportTree(AActor* Root)
{
	FString RootId;
	if (Root)
	{
		const FRuntimeSceneNode* R = RequireNode(Root, TEXT("root"));
		if (!R)
		{
			return {};
		}
		RootId = R->Id;
	}
	return FRuntimeToolJson::Make(RuntimeTreeSerializer::Export(*RuntimeToolCall::GetGraph(), RootId));
}

FRuntimeImportResult URuntimeSceneToolset::ImportTree(const FRuntimeToolJson& Tree, AActor* Parent)
{
	FString ParentId;
	if (Parent)
	{
		const FRuntimeSceneNode* P = RequireNode(Parent, TEXT("parent"));
		if (!P)
		{
			return {};
		}
		ParentId = P->Id;
	}
	const TSharedPtr<FJsonObject> Spec = Tree.AsObject();
	if (!Spec.IsValid())
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, TEXT("tree must be an object {nodes:[...]}"), TEXT("Pass the result of scene.export_tree"));
		return {};
	}
	TArray<FString> RootIds;
	TMap<FString, FString> IdMap;
	FString Err;
	TUniquePtr<FCompositeCommand> Cmd = RuntimeTreeSerializer::BuildImport(*RuntimeToolCall::GetGraph(), Spec.ToSharedRef(), ParentId, FString(),
		FVector::ZeroVector, RootIds, &IdMap, Err);
	if (!Cmd.IsValid())
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, Err, TEXT("Nodes need a source like \"shape:cube\" or \"group\" (see scene.export_tree)"));
		return {};
	}
	const int32 Count = Cmd->Num();
	if (Count > MaxTreeNodes)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("tree has %d nodes (max %d)"), Count, MaxTreeNodes));
		return {};
	}
	Cmd->SetLabel(TEXT("import tree"));
	if (!RuntimeToolCall::RunCommand(MoveTemp(Cmd)))
	{
		return {};
	}
	FRuntimeImportResult Out;
	Out.Roots = RootIds;
	Out.Nodes = Count;
	if (IdMap.Num() <= 200)
	{
		Out.IdMap = IdMap;
	}
	return Out;
}

// ---- templates ------------------------------------------------------------------------------------------------------------

namespace
{
	FString UserTemplateDir() { return FPaths::ProjectSavedDir() / TEXT("RuntimeToolsets/Templates"); }
	FString BuiltinTemplateDir() { return FPaths::ProjectContentDir() / TEXT("RuntimeTemplates"); }

	bool ValidTemplateName(const FString& Name)
	{
		if (Name.IsEmpty() || Name.Len() > 64)
		{
			return false;
		}
		for (const TCHAR C : Name)
		{
			if (!FChar::IsAlnum(C) && C != TEXT('_') && C != TEXT('-'))
			{
				return false;
			}
		}
		return true;
	}

	/** Saved templates win over built-in ones with the same name. */
	TSharedPtr<FJsonObject> LoadTemplate(const FString& Name)
	{
		for (const FString& Dir : {UserTemplateDir(), BuiltinTemplateDir()})
		{
			FString Text;
			if (FFileHelper::LoadFileToString(Text, *(Dir / (Name + TEXT(".json")))))
			{
				if (TSharedPtr<FJsonObject> J = RuntimeJson::Parse(Text))
				{
					return J;
				}
			}
		}
		return nullptr;
	}

	int32 CountTreeNodes(const TArray<TSharedPtr<FJsonValue>>& Nodes)
	{
		int32 N = 0;
		for (const TSharedPtr<FJsonValue>& V : Nodes)
		{
			const TSharedPtr<FJsonObject>* O = nullptr;
			if (V.IsValid() && V->TryGetObject(O))
			{
				const TArray<TSharedPtr<FJsonValue>>* Kids = nullptr;
				N += 1 + ((*O)->TryGetArrayField(TEXT("children"), Kids) ? CountTreeNodes(*Kids) : 0);
			}
		}
		return N;
	}

	FVector ReadVec(const TSharedPtr<FJsonObject>& O, const TCHAR* Field, const FVector& Default)
	{
		const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
		return O->TryGetArrayField(Field, A) && A->Num() == 3 ? FVector((*A)[0]->AsNumber(), (*A)[1]->AsNumber(), (*A)[2]->AsNumber()) : Default;
	}

	TArray<TSharedPtr<FJsonValue>> VecJson(const FVector& V)
	{
		return {MakeShared<FJsonValueNumber>(V.X), MakeShared<FJsonValueNumber>(V.Y), MakeShared<FJsonValueNumber>(V.Z)};
	}
}

FRuntimeToolJson URuntimeSceneToolset::SaveTemplate(AActor* Target, const FString& Name, const FString& Description)
{
	const FRuntimeSceneNode* Node = RequireNode(Target, TEXT("target"));
	if (!Node)
	{
		return {};
	}
	if (!ValidTemplateName(Name))
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("invalid template name '%s'"), *Name),
			TEXT("1-64 letters, digits, _ or -, e.g. corner_cafe"));
		return {};
	}
	TSharedRef<FJsonObject> Tree = RuntimeTreeSerializer::Export(*RuntimeToolCall::GetGraph(), Node->Id);
	const int32 Count = CountTreeNodes(Tree->GetArrayField(TEXT("nodes")));
	if (Count > MaxTreeNodes)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("%d nodes (max %d)"), Count, MaxTreeNodes));
		return {};
	}
	TSharedRef<FJsonObject> File = MakeShared<FJsonObject>();
	File->SetStringField(TEXT("description"), Description);
	File->SetNumberField(TEXT("nodes"), Count);
	File->SetObjectField(TEXT("tree"), Tree);
	const FString Path = UserTemplateDir() / (Name + TEXT(".json"));
	if (!FFileHelper::SaveStringToFile(RuntimeJson::Write(File), *Path))
	{
		RuntimeToolCall::Fail(ERuntimeToolError::Internal, FString::Printf(TEXT("could not write %s"), *Path));
		return {};
	}
	TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
	Out->SetStringField(TEXT("name"), Name);
	Out->SetNumberField(TEXT("nodes"), Count);
	Out->SetStringField(TEXT("file"), FPaths::ConvertRelativePathToFull(Path));
	return FRuntimeToolJson::Make(Out);
}

FRuntimeImportResult URuntimeSceneToolset::PlaceTemplate(const FString& Name, const FVector& Location, float Yaw, const FString& NewName, AActor* Parent)
{
	FString ParentId;
	if (Parent)
	{
		const FRuntimeSceneNode* P = RequireNode(Parent, TEXT("parent"));
		if (!P)
		{
			return {};
		}
		ParentId = P->Id;
	}
	const TSharedPtr<FJsonObject> File = ValidTemplateName(Name) ? LoadTemplate(Name) : nullptr;
	const TSharedPtr<FJsonObject>* TreePtr = nullptr;
	if (!File.IsValid() || !File->TryGetObjectField(TEXT("tree"), TreePtr))
	{
		RuntimeToolCall::Fail(ERuntimeToolError::NotFound, FString::Printf(TEXT("no template '%s'"), *Name), TEXT("scene.list_templates shows them"));
		return {};
	}
	// Move the roots so the first one lands at Location, turned by Yaw around it.
	TSharedRef<FJsonObject> Tree = RuntimeJson::Parse(RuntimeJson::Write(TreePtr->ToSharedRef())).ToSharedRef();
	const TArray<TSharedPtr<FJsonValue>>& Roots = Tree->GetArrayField(TEXT("nodes"));
	FVector First = FVector::ZeroVector;
	const FQuat Turn(FRotator(0.f, Yaw, 0.f));
	for (int32 i = 0; i < Roots.Num(); ++i)
	{
		const TSharedPtr<FJsonObject> R = Roots[i]->AsObject();
		const FVector Loc = ReadVec(R, TEXT("location"), FVector::ZeroVector);
		if (i == 0)
		{
			First = Loc;
		}
		R->SetArrayField(TEXT("location"), VecJson(Location + Turn.RotateVector(Loc - First)));
		const FVector Rot = ReadVec(R, TEXT("rotation"), FVector::ZeroVector);
		R->SetArrayField(TEXT("rotation"), VecJson(FVector(Rot.X, Rot.Y + Yaw, Rot.Z)));
	}
	TArray<FString> RootIds;
	FString Err;
	TUniquePtr<FCompositeCommand> Cmd = RuntimeTreeSerializer::BuildImport(*RuntimeToolCall::GetGraph(), Tree, ParentId, NewName, FVector::ZeroVector, RootIds,
		nullptr, Err);
	if (!Cmd.IsValid())
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("template '%s' is broken: %s"), *Name, *Err));
		return {};
	}
	const int32 Count = Cmd->Num();
	Cmd->SetLabel(FString::Printf(TEXT("place template %s"), *Name));
	if (!RuntimeToolCall::RunCommand(MoveTemp(Cmd)))
	{
		return {};
	}
	FRuntimeImportResult Out;
	Out.Roots = RootIds;
	Out.Nodes = Count;
	return Out;
}

FRuntimeToolJson URuntimeSceneToolset::ListTemplates()
{
	TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
	for (const bool bBuiltin : {true, false})
	{
		const FString Dir = bBuiltin ? BuiltinTemplateDir() : UserTemplateDir();
		TArray<FString> Files;
		IFileManager::Get().FindFiles(Files, *(Dir / TEXT("*.json")), true, false);
		for (const FString& F : Files)
		{
			FString Text;
			const TSharedPtr<FJsonObject> J = FFileHelper::LoadFileToString(Text, *(Dir / F)) ? RuntimeJson::Parse(Text) : nullptr;
			if (!J.IsValid())
			{
				continue;
			}
			TSharedRef<FJsonObject> Info = MakeShared<FJsonObject>();
			FString Description;
			J->TryGetStringField(TEXT("description"), Description);
			Info->SetStringField(TEXT("description"), Description);
			double Nodes = 0;
			J->TryGetNumberField(TEXT("nodes"), Nodes);
			Info->SetNumberField(TEXT("nodes"), Nodes);
			Info->SetBoolField(TEXT("builtin"), bBuiltin);
			Out->SetObjectField(FPaths::GetBaseFilename(F), Info);  // saved ones replace built-in ones of the same name
		}
	}
	return FRuntimeToolJson::Make(Out);
}

FRuntimeToolJson URuntimeSceneToolset::DeleteTemplate(const FString& Name)
{
	const FString Path = UserTemplateDir() / (Name + TEXT(".json"));
	if (!ValidTemplateName(Name) || !FPaths::FileExists(Path))
	{
		RuntimeToolCall::Fail(ERuntimeToolError::NotFound, FString::Printf(TEXT("no saved template '%s'"), *Name),
			TEXT("Built-in templates cannot be deleted; scene.list_templates shows them"));
		return {};
	}
	IFileManager::Get().Delete(*Path);
	TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
	Out->SetStringField(TEXT("deleted"), Name);
	return FRuntimeToolJson::Make(Out);
}
