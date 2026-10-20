#include "SceneGraphToolset.h"
#include "Helpers/SpawnHelpers.h"
#include "Helpers/ToolsCommon.h"
#include "Helpers/TreeSerializer.h"
#include "Components/SceneComponent.h"
#include "GameFramework/Actor.h"

using namespace RuntimeTools;

static constexpr int32 MaxTreeNodes = 2000;

/** Resolves a target that must be a live scene node. */
static FRuntimeSceneNode* ResolveNode(FToolInvocation& Inv, const TCHAR* Arg)
{
	FResolvedTarget Target;
	if (!Inv.ResolveTarget(Arg, Target))
	{
		return nullptr;
	}
	if (!Target.Node || Target.Component)
	{
		Inv.Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("%s is not a scene node"), Arg), TEXT("Only tool-created nodes are in the scene graph"));
		return nullptr;
	}
	return Target.Node;
}

static bool ResolveOptionalParent(FToolInvocation& Inv, FString& OutParentId)
{
	OutParentId.Reset();
	if (Inv.GetString(TEXT("parent")).IsEmpty())
	{
		return true;
	}
	const FRuntimeSceneNode* Parent = ResolveNode(Inv, TEXT("parent"));
	if (!Parent)
	{
		return false;
	}
	OutParentId = Parent->Id;
	return true;
}

static TSharedRef<FJsonObject> NodeSummary(FToolInvocation& Inv, const FString& Id)
{
	const FRuntimeSceneNode* Node = Inv.GetGraph()->Find(Id);
	TSharedRef<FJsonObject> Out = Obj();
	Out->SetStringField(TEXT("id"), Id);
	if (Node)
	{
		Out->SetStringField(TEXT("path"), Inv.GetGraph()->GetPath(Id));
	}
	return Out;
}

// ---------------------------------------------------------------------------------------------------------------------

static void CreateGroup(FToolInvocation& Inv)
{
	FString ParentId;
	if (!ResolveOptionalParent(Inv, ParentId))
	{
		return;
	}
	const FString Name = Inv.GetString(TEXT("name"));
	const FTransform Transform = ReadTransform(Inv);
	auto Spawn = [Transform, Name](FRuntimeCommandContext& Ctx, FString& Error)
	{
		return RuntimeSpawn::SpawnFromSource(Ctx.World, TEXT("group"), Transform, Name, Error);
	};
	TUniquePtr<FCreateNodeCommand> Cmd = MakeUnique<FCreateNodeCommand>(FString::Printf(TEXT("create group %s"), *Name), Spawn, Name, ParentId,
		ERuntimeNodeKind::Group, TEXT("group"), ToNames(Inv.GetStringArray(TEXT("tags"))));
	FCreateNodeCommand* Raw = Cmd.Get();
	if (Inv.RunCommand(MoveTemp(Cmd)))
	{
		Inv.Succeed(NodeSummary(Inv, Raw->GetNodeId()));
	}
}

static TSharedRef<FJsonObject> TreeNodeJson(FRuntimeSceneGraph& Graph, const FRuntimeSceneNode& Node, int32 Depth, const TArray<FString>& Fields,
	int32& Budget)
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

static void ListTree(FToolInvocation& Inv)
{
	FRuntimeSceneGraph& Graph = *Inv.GetGraph();
	const int32 Depth = static_cast<int32>(Inv.GetInt(TEXT("depth"), 2));
	const TArray<FString> Fields = Inv.GetFields();
	int32 Budget = Inv.GetLimit();

	TArray<TSharedPtr<FJsonValue>> Roots;
	if (!Inv.GetString(TEXT("root")).IsEmpty())
	{
		const FRuntimeSceneNode* Root = ResolveNode(Inv, TEXT("root"));
		if (!Root)
		{
			return;
		}
		Roots.Add(MakeShared<FJsonValueObject>(TreeNodeJson(Graph, *Root, Depth, Fields, Budget)));
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
	TSharedRef<FJsonObject> Out = Obj();
	Out->SetArrayField(TEXT("nodes"), Roots);
	Out->SetNumberField(TEXT("total_nodes"), Graph.NumLiveNodes());
	if (Budget <= 0)
	{
		Out->SetBoolField(TEXT("truncated"), true);
	}
	Inv.Succeed(Out);
}

static void FindNodes(FToolInvocation& Inv)
{
	FRuntimeSceneGraph& Graph = *Inv.GetGraph();
	const FString Pattern = Inv.GetString(TEXT("pattern"));
	const FString Tag = Inv.GetString(TEXT("tag"));
	const FString Kind = Inv.GetString(TEXT("kind"));
	const int32 Limit = Inv.GetLimit();
	TArray<FString> Fields = Inv.GetFields();
	if (Fields.Num() == 0)
	{
		Fields = {TEXT("name"), TEXT("path"), TEXT("kind")};
	}

	TArray<FString> Scope;
	if (!Inv.GetString(TEXT("under")).IsEmpty())
	{
		const FRuntimeSceneNode* Root = ResolveNode(Inv, TEXT("under"));
		if (!Root)
		{
			return;
		}
		Scope = Graph.CollectSubtree(Root->Id);
	}
	else
	{
		for (const FString& Id : Graph.GetRootIds())
		{
			Scope.Append(Graph.CollectSubtree(Id));
		}
	}

	TArray<TSharedPtr<FJsonValue>> Items;
	int32 Total = 0;
	for (const FString& Id : Scope)
	{
		const FRuntimeSceneNode& N = *Graph.Find(Id);
		if (!Pattern.IsEmpty() && !N.Name.MatchesWildcard(Pattern))
		{
			continue;
		}
		if (!Tag.IsEmpty() && !N.Tags.Contains(FName(*Tag)))
		{
			continue;
		}
		if (!Kind.IsEmpty() && (Kind == TEXT("group")) != (N.Kind == ERuntimeNodeKind::Group))
		{
			continue;
		}
		++Total;
		if (Items.Num() < Limit)
		{
			Items.Add(MakeShared<FJsonValueObject>(Graph.NodeToJson(N, Fields)));
		}
	}
	TSharedRef<FJsonObject> Out = Obj();
	Out->SetNumberField(TEXT("total"), Total);
	Out->SetArrayField(TEXT("nodes"), Items);
	Inv.Succeed(Out);
}

static void GetNode(FToolInvocation& Inv)
{
	const FRuntimeSceneNode* Node = ResolveNode(Inv, TEXT("target"));
	if (!Node)
	{
		return;
	}
	TArray<FString> Fields = Inv.GetFields();
	if (Fields.Num() == 0)
	{
		Fields = NodeFields();
	}
	TSharedRef<FJsonObject> Out = Inv.GetGraph()->NodeToJson(*Node, Fields);
	FLinearColor Color;
	if (RuntimeSpawn::GetShapeColor(Node->Actor.Get(), Color))
	{
		Out->SetField(TEXT("color"), RuntimeJson::Vec(FVector(Color.R, Color.G, Color.B)));
	}
	Inv.Succeed(Out);
}

// ---- transforms ----------------------------------------------------------------------------------------------------

static FTransform ParentWorld(const AActor* Actor)
{
	const USceneComponent* Root = Actor->GetRootComponent();
	const USceneComponent* Parent = Root ? Root->GetAttachParent() : nullptr;
	return Parent ? Parent->GetSocketTransform(Root->GetAttachSocketName()) : FTransform::Identity;
}

static void RunTransform(FToolInvocation& Inv, const FRuntimeSceneNode& Node, const FTransform& NewWorld, const TCHAR* Verb, TSharedPtr<FJsonObject> Extra = nullptr)
{
	TSharedPtr<FJsonObject> Delta;
	if (Inv.RunCommand(MakeUnique<FTransformCommand>(FString::Printf(TEXT("%s %s"), Verb, *Node.Name), Node.Actor.Get(), NewWorld, Node.Id), &Delta))
	{
		if (Extra.IsValid() && Delta.IsValid())
		{
			for (const FString& Key : RuntimeJson::Keys(Extra))
			{
				Delta->SetField(Key, Extra->TryGetField(Key));
			}
		}
		Inv.Succeed(Delta);
	}
}

static void MoveNode(FToolInvocation& Inv)
{
	const FRuntimeSceneNode* Node = ResolveNode(Inv, TEXT("target"));
	if (!Node)
	{
		return;
	}
	AActor* Actor = Node->Actor.Get();
	const bool bLocal = Inv.GetString(TEXT("space"), TEXT("world")) == TEXT("local");
	FTransform World = Actor->GetActorTransform();
	FVector Location, Offset;
	const bool bHasLocation = Inv.GetVector(TEXT("location"), Location);
	const bool bHasOffset = Inv.GetVector(TEXT("offset"), Offset);
	if (bHasLocation == bHasOffset)
	{
		Inv.Fail(ERuntimeToolError::InvalidArguments, TEXT("give exactly one of location or offset"));
		return;
	}
	if (bHasLocation)
	{
		// local: location relative to the parent node.
		World.SetLocation(bLocal ? ParentWorld(Actor).TransformPosition(Location) : Location);
	}
	else
	{
		// local: offset along the node's own axes.
		World.AddToTranslation(bLocal ? Actor->GetActorQuat().RotateVector(Offset) : Offset);
	}
	RunTransform(Inv, *Node, World, TEXT("move"));
}

static void RotateNode(FToolInvocation& Inv)
{
	const FRuntimeSceneNode* Node = ResolveNode(Inv, TEXT("target"));
	if (!Node)
	{
		return;
	}
	AActor* Actor = Node->Actor.Get();
	const bool bLocal = Inv.GetString(TEXT("space"), TEXT("world")) == TEXT("local");
	FTransform World = Actor->GetActorTransform();
	FRotator Rotation, Delta;
	const bool bHasRotation = Inv.GetRotator(TEXT("rotation"), Rotation);
	const bool bHasDelta = Inv.GetRotator(TEXT("delta"), Delta);
	if (bHasRotation == bHasDelta)
	{
		Inv.Fail(ERuntimeToolError::InvalidArguments, TEXT("give exactly one of rotation or delta"));
		return;
	}
	if (bHasRotation)
	{
		World.SetRotation(bLocal ? ParentWorld(Actor).GetRotation() * Rotation.Quaternion() : Rotation.Quaternion());
	}
	else
	{
		const FQuat D = Delta.Quaternion();
		World.SetRotation(bLocal ? World.GetRotation() * D : D * World.GetRotation());
	}
	RunTransform(Inv, *Node, World, TEXT("rotate"));
}

static void ScaleNode(FToolInvocation& Inv)
{
	const FRuntimeSceneNode* Node = ResolveNode(Inv, TEXT("target"));
	if (!Node)
	{
		return;
	}
	AActor* Actor = Node->Actor.Get();
	FTransform World = Actor->GetActorTransform();
	FVector Scale;
	FVector NewScale;
	if (Inv.GetVector(TEXT("scale"), Scale))
	{
		NewScale = Scale;
	}
	else if (Inv.GetVector(TEXT("factor"), Scale))
	{
		NewScale = World.GetScale3D() * Scale;
	}
	else
	{
		Inv.Fail(ERuntimeToolError::InvalidArguments, TEXT("give scale or factor"));
		return;
	}
	if (NewScale.GetMin() <= KINDA_SMALL_NUMBER && NewScale.GetMin() > -KINDA_SMALL_NUMBER)
	{
		Inv.Fail(ERuntimeToolError::InvalidArguments, TEXT("scale components must be non-zero"));
		return;
	}
	World.SetScale3D(NewScale);

	TSharedPtr<FJsonObject> Extra;
	if (!NewScale.AllComponentsEqual(KINDA_SMALL_NUMBER))
	{
		TArray<AActor*> Children;
		Actor->GetAttachedActors(Children, true, false);
		for (AActor* Child : Children)
		{
			if (Child->GetRootComponent() && !Child->GetRootComponent()->GetRelativeRotation().IsNearlyZero(0.01))
			{
				Extra = Obj();
				Extra->SetStringField(TEXT("warning"), TEXT("non-uniform scale on a parent with rotated children shears them; scale the leaves instead"));
				break;
			}
		}
	}
	RunTransform(Inv, *Node, World, TEXT("scale"), Extra);
}

static void ReparentNode(FToolInvocation& Inv)
{
	FRuntimeSceneNode* Node = ResolveNode(Inv, TEXT("target"));
	if (!Node)
	{
		return;
	}
	AActor* NewParentActor = nullptr;
	if (!Inv.GetString(TEXT("parent")).IsEmpty())
	{
		FRuntimeSceneNode* Parent = ResolveNode(Inv, TEXT("parent"));
		if (!Parent)
		{
			return;
		}
		if (Inv.GetGraph()->IsAncestorOrSelf(Node->Id, Parent->Id))
		{
			Inv.Fail(ERuntimeToolError::InvalidArguments, TEXT("cannot parent a node under itself or its descendant"));
			return;
		}
		NewParentActor = Parent->Actor.Get();
	}
	TSharedPtr<FJsonObject> Delta;
	const FString Label = FString::Printf(TEXT("reparent %s"), *Node->Name);
	const FString Id = Node->Id;
	if (Inv.RunCommand(MakeUnique<FAttachCommand>(Label, Node->Actor.Get(), NewParentActor, NAME_None, Inv.GetBool(TEXT("keep_world"), true)), &Delta))
	{
		Delta->SetStringField(TEXT("path"), Inv.GetGraph()->GetPath(Id));
		Inv.Succeed(Delta);
	}
}

/** Moves a group's origin while its children keep their world transforms. */
static void MovePivot(AActor* Group, const FTransform& NewWorld)
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

static void SetPivot(FToolInvocation& Inv)
{
	const FRuntimeSceneNode* Node = ResolveNode(Inv, TEXT("target"));
	if (!Node)
	{
		return;
	}
	if (Node->Kind != ERuntimeNodeKind::Group)
	{
		Inv.Fail(ERuntimeToolError::Unsupported, TEXT("set_pivot works on groups"), TEXT("Wrap the node in scene.create_group and reparent it"));
		return;
	}
	TWeakObjectPtr<AActor> Group = Node->Actor;
	const FTransform Old = Group->GetActorTransform();
	FTransform New = Old;
	FVector Location;
	if (!Inv.GetVector(TEXT("location"), Location))
	{
		// Default: centre of children bounds.
		FVector Origin, Extent;
		Group->GetActorBounds(false, Origin, Extent, true);
		Location = Origin;
	}
	New.SetLocation(Location);
	TSharedRef<FJsonObject> Delta = Obj();
	Delta->SetStringField(TEXT("pivot"), Node->Id);
	Delta->SetField(TEXT("location"), RuntimeJson::Vec(Location));
	auto DoFn = [Group, New](FRuntimeCommandContext&, FString& Error)
	{
		if (!Group.IsValid())
		{
			Error = TEXT("group no longer exists");
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
	if (Inv.RunCommand(MakeUnique<FLambdaCommand>(FString::Printf(TEXT("set pivot %s"), *Node->Name), DoFn, UndoFn, Delta)))
	{
		Inv.Succeed(Delta);
	}
}

// ---- naming and tags -----------------------------------------------------------------------------------------------

static void RenameNode(FToolInvocation& Inv)
{
	const FRuntimeSceneNode* Node = ResolveNode(Inv, TEXT("target"));
	if (!Node)
	{
		return;
	}
	const FString NewName = Inv.GetString(TEXT("name")).Replace(TEXT("/"), TEXT("_"));
	if (NewName.IsEmpty())
	{
		Inv.Fail(ERuntimeToolError::InvalidArguments, TEXT("name is empty"));
		return;
	}
	if (Inv.GetGraph()->IsNameTaken(Node->ParentId, NewName, Node->Id))
	{
		Inv.Fail(ERuntimeToolError::WrongState, FString::Printf(TEXT("a sibling is already named '%s'"), *NewName));
		return;
	}
	const FString Id = Node->Id;
	const FString OldName = Node->Name;
	TSharedRef<FJsonObject> Delta = Obj();
	Delta->SetStringField(TEXT("renamed"), Id);
	Delta->SetStringField(TEXT("name"), NewName);
	auto Set = [Id](FRuntimeCommandContext& Ctx, const FString& Name)
	{
		FRuntimeSceneNode* N = Ctx.Graph ? Ctx.Graph->Find(Id) : nullptr;
		if (!N)
		{
			return false;
		}
		N->Name = Ctx.Graph->IsNameTaken(N->ParentId, Name, Id) ? Ctx.Graph->MakeUniqueName(N->ParentId, Name, Id) : Name;
		return true;
	};
	auto DoFn = [Set, NewName](FRuntimeCommandContext& Ctx, FString& Error)
	{
		if (!Set(Ctx, NewName))
		{
			Error = TEXT("node no longer exists");
			return false;
		}
		return true;
	};
	auto UndoFn = [Set, OldName](FRuntimeCommandContext& Ctx) { return Set(Ctx, OldName); };
	if (Inv.RunCommand(MakeUnique<FLambdaCommand>(FString::Printf(TEXT("rename %s"), *OldName), DoFn, UndoFn, Delta)))
	{
		Delta->SetStringField(TEXT("path"), Inv.GetGraph()->GetPath(Id));
		Inv.Succeed(Delta);
	}
}

static void ApplyNodeTags(FRuntimeCommandContext& Ctx, const FString& Id, const TArray<FName>& Tags)
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

static void SetNodeTags(FToolInvocation& Inv)
{
	const FRuntimeSceneNode* Node = ResolveNode(Inv, TEXT("target"));
	if (!Node)
	{
		return;
	}
	const FString Mode = Inv.GetString(TEXT("mode"), TEXT("set"));
	const TArray<FName> Given = ToNames(Inv.GetStringArray(TEXT("tags")));
	const TArray<FName> Old = Node->Tags;
	TArray<FName> New = Mode == TEXT("set") ? Given : Old;
	if (Mode == TEXT("add"))
	{
		for (const FName& T : Given)
		{
			New.AddUnique(T);
		}
	}
	else if (Mode == TEXT("remove"))
	{
		for (const FName& T : Given)
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
	if (Inv.RunCommand(MakeUnique<FLambdaCommand>(FString::Printf(TEXT("tags %s"), *Node->Name), DoFn, UndoFn, Delta)))
	{
		Inv.Succeed(Delta);
	}
}

// ---- duplicate / delete / clear ------------------------------------------------------------------------------------

static void DuplicateNode(FToolInvocation& Inv)
{
	const FRuntimeSceneNode* Node = ResolveNode(Inv, TEXT("target"));
	if (!Node)
	{
		return;
	}
	FRuntimeSceneGraph& Graph = *Inv.GetGraph();
	const TArray<FString> Subtree = Graph.CollectSubtree(Node->Id);
	if (Subtree.Num() > MaxTreeNodes)
	{
		Inv.Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("subtree has %d nodes (max %d)"), Subtree.Num(), MaxTreeNodes));
		return;
	}
	const TSharedRef<FJsonObject> Spec = RuntimeTreeSerializer::Export(Graph, Node->Id);
	FVector Offset = FVector::ZeroVector;
	Inv.GetVector(TEXT("offset"), Offset);

	FString Error;
	TArray<FString> RootIds;
	TUniquePtr<FCompositeCommand> Cmd = RuntimeTreeSerializer::BuildImport(Graph, Spec, Node->ParentId, Inv.GetString(TEXT("name")), Offset, RootIds,
		nullptr, Error);
	if (!Cmd.IsValid())
	{
		Inv.Fail(ERuntimeToolError::InvalidArguments, Error);
		return;
	}
	Cmd->SetLabel(FString::Printf(TEXT("duplicate %s"), *Node->Name));
	if (Inv.RunCommand(MoveTemp(Cmd)))
	{
		TSharedRef<FJsonObject> Out = NodeSummary(Inv, RootIds.Num() > 0 ? RootIds[0] : FString());
		Out->SetNumberField(TEXT("nodes"), Subtree.Num());
		Inv.Succeed(Out);
	}
}

static TArray<AActor*> ActorsOf(FRuntimeSceneGraph& Graph, const TArray<FString>& Ids)
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

static void DeleteNode(FToolInvocation& Inv)
{
	const FRuntimeSceneNode* Node = ResolveNode(Inv, TEXT("target"));
	if (!Node)
	{
		return;
	}
	FRuntimeSceneGraph& Graph = *Inv.GetGraph();
	const FString Id = Node->Id;
	TUniquePtr<FCompositeCommand> Cmd = MakeUnique<FCompositeCommand>(FString::Printf(TEXT("delete %s"), *Node->Name));
	int32 Deleted = 0;
	if (Inv.GetBool(TEXT("promote_children"), false))
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
	if (Inv.RunCommand(MoveTemp(Cmd)))
	{
		TSharedRef<FJsonObject> Out = Obj();
		Out->SetStringField(TEXT("deleted"), Id);
		Out->SetNumberField(TEXT("nodes"), Deleted);
		Inv.Succeed(Out);
	}
}

/** Shared by clear_scope / clear_all: confirm, auto-checkpoint, one composite soft-delete of the session's nodes. */
static void ClearNodes(FToolInvocation& Inv, const TArray<FString>& RootIds, const FString& Label)
{
	if (!Inv.GetBool(TEXT("confirm"), false))
	{
		Inv.Fail(ERuntimeToolError::ConfirmRequired, TEXT("this deletes nodes; pass confirm=true"), TEXT("An automatic checkpoint is created first"));
		return;
	}
	if (Inv.GetHistory().IsInTransaction())
	{
		Inv.Fail(ERuntimeToolError::WrongState, TEXT("a transaction is open"), TEXT("Call history.end_transaction first"));
		return;
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
	TSharedRef<FJsonObject> Out = Obj();
	if (Ids.Num() == 0)
	{
		Out->SetNumberField(TEXT("deleted"), 0);
		Inv.Succeed(Out);
		return;
	}
	const FString CheckpointName = FString::Printf(TEXT("auto/%s_%s"), *Label, *FDateTime::UtcNow().ToString(TEXT("%H%M%S")));
	FRuntimeCommandContext Context = Inv.MakeCommandContext();
	Inv.GetHistory().Checkpoint(CheckpointName, Context);
	if (Inv.RunCommand(MakeUnique<FSoftDeleteActorsCommand>(Label, ActorsOf(Graph, Ids))))
	{
		Out->SetNumberField(TEXT("deleted"), Ids.Num());
		Out->SetStringField(TEXT("checkpoint"), CheckpointName);
		Inv.Succeed(Out);
	}
}

static void ClearScope(FToolInvocation& Inv)
{
	const FRuntimeSceneNode* Root = ResolveNode(Inv, TEXT("root"));
	if (!Root)
	{
		return;
	}
	TArray<FString> Children;
	for (const FString& C : Root->Children)
	{
		const FRuntimeSceneNode* N = Inv.GetGraph()->Find(C);
		if (N && !N->bSoftDeleted)
		{
			Children.Add(C);
		}
	}
	ClearNodes(Inv, Children, FString::Printf(TEXT("clear_scope %s"), *Root->Name));
}

static void ClearAll(FToolInvocation& Inv)
{
	ClearNodes(Inv, Inv.GetGraph()->GetRootIds(), TEXT("clear_all"));
}

// ---- export / import -----------------------------------------------------------------------------------------------

static void ExportTree(FToolInvocation& Inv)
{
	FString RootId;
	if (!Inv.GetString(TEXT("root")).IsEmpty())
	{
		const FRuntimeSceneNode* Root = ResolveNode(Inv, TEXT("root"));
		if (!Root)
		{
			return;
		}
		RootId = Root->Id;
	}
	Inv.Succeed(RuntimeTreeSerializer::Export(*Inv.GetGraph(), RootId));
}

static void ImportTree(FToolInvocation& Inv)
{
	FString ParentId;
	if (!ResolveOptionalParent(Inv, ParentId))
	{
		return;
	}
	TSharedPtr<FJsonObject> Spec = Inv.GetObject(TEXT("tree"));
	TArray<FString> RootIds;
	TMap<FString, FString> IdMap;
	FString Error;
	TUniquePtr<FCompositeCommand> Cmd = RuntimeTreeSerializer::BuildImport(*Inv.GetGraph(), Spec.ToSharedRef(), ParentId, FString(), FVector::ZeroVector,
		RootIds, &IdMap, Error);
	if (!Cmd.IsValid())
	{
		Inv.Fail(ERuntimeToolError::InvalidArguments, Error);
		return;
	}
	const int32 Count = Cmd->Num();
	if (Count > MaxTreeNodes)
	{
		Inv.Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("tree has %d nodes (max %d)"), Count, MaxTreeNodes));
		return;
	}
	Cmd->SetLabel(TEXT("import tree"));
	if (Inv.RunCommand(MoveTemp(Cmd)))
	{
		TSharedRef<FJsonObject> Out = Obj();
		Out->SetField(TEXT("roots"), RuntimeJson::Strings(RootIds));
		Out->SetNumberField(TEXT("nodes"), Count);
		if (IdMap.Num() > 0 && IdMap.Num() <= 200)
		{
			TSharedRef<FJsonObject> Map = Obj();
			for (const TPair<FString, FString>& P : IdMap)
			{
				Map->SetStringField(P.Key, P.Value);
			}
			Out->SetObjectField(TEXT("id_map"), Map);
		}
		Inv.Succeed(Out);
	}
}

// ---------------------------------------------------------------------------------------------------------------------

void FSceneGraphToolset::RegisterTools(FRuntimeToolsetBuilder& B)
{
	const TArray<FString> Fields = NodeFields();
	const auto Space = [](const TCHAR* LocalMeaning)
	{
		return FToolParam::Enum(FString::Printf(TEXT("world, or local (%s)"), LocalMeaning), {TEXT("world"), TEXT("local")}).Default(TEXT("world"));
	};

	B.Tool(TEXT("create_group"), TEXT("Create an empty group node (pivot for children). Transform is relative to parent."))
		.Args(FToolSchema()
			.Add(TEXT("name"), FToolParam::String(TEXT("Group name"), 128).Required())
			.Add(TEXT("parent"), FToolParam::Target(TEXT("Parent node")))
			.Add(TEXT("location"), FToolParam::Vec3(TEXT("[x,y,z] cm")))
			.Add(TEXT("rotation"), FToolParam::Rotator(TEXT("[pitch,yaw,roll]")))
			.Add(TEXT("scale"), FToolParam::Vec3(TEXT("[x,y,z]")))
			.Add(TEXT("tags"), FToolParam::StringArray(TEXT("Tags"), 16)))
		.Undoable().RequiresWorld()
		.Handle(&CreateGroup);

	B.Tool(TEXT("list_tree"), TEXT("Node hierarchy, depth-limited. Deeper levels show child_count."))
		.Args(FToolSchema()
			.Add(TEXT("root"), FToolParam::Target(TEXT("Start node (default: all roots)")))
			.Add(TEXT("depth"), FToolParam::Integer(TEXT("Levels to expand"), 1, 32).Default(2))
			.Add(TEXT("limit"), FToolParam::Limit())
			.Add(TEXT("fields"), FToolParam::Fields(Fields)))
		.RequiresWorld()
		.Handle(&ListTree);

	B.Tool(TEXT("find_nodes"), TEXT("Find nodes by name wildcard, tag or kind."))
		.Args(FToolSchema()
			.Add(TEXT("pattern"), FToolParam::String(TEXT("Name wildcard"), 128))
			.Add(TEXT("tag"), FToolParam::String(TEXT("Tag"), 128))
			.Add(TEXT("kind"), FToolParam::Enum(TEXT("Node kind"), {TEXT("group"), TEXT("actor")}))
			.Add(TEXT("under"), FToolParam::Target(TEXT("Search only below this node")))
			.Add(TEXT("limit"), FToolParam::Limit())
			.Add(TEXT("fields"), FToolParam::Fields(Fields)))
		.RequiresWorld()
		.Handle(&FindNodes);

	B.Tool(TEXT("get_node"), TEXT("One node with transform, bounds, tags and color."))
		.Args(FToolSchema()
			.Add(TEXT("target"), FToolParam::Target().Required())
			.Add(TEXT("fields"), FToolParam::Fields(Fields)))
		.RequiresWorld()
		.Handle(&GetNode);

	B.Tool(TEXT("move_node"), TEXT("Set location or add offset. Children follow."))
		.Args(FToolSchema()
			.Add(TEXT("target"), FToolParam::Target().Required())
			.Add(TEXT("location"), FToolParam::Vec3(TEXT("New location")))
			.Add(TEXT("offset"), FToolParam::Vec3(TEXT("Offset to add")))
			.Add(TEXT("space"), Space(TEXT("location relative to parent, offset along own axes"))))
		.Undoable().RequiresWorld()
		.Handle(&MoveNode);

	B.Tool(TEXT("rotate_node"), TEXT("Set rotation or apply a delta. Children follow."))
		.Args(FToolSchema()
			.Add(TEXT("target"), FToolParam::Target().Required())
			.Add(TEXT("rotation"), FToolParam::Rotator(TEXT("New rotation")))
			.Add(TEXT("delta"), FToolParam::Rotator(TEXT("Rotation to apply")))
			.Add(TEXT("space"), Space(TEXT("rotation relative to parent, delta about own axes"))))
		.Undoable().RequiresWorld()
		.Handle(&RotateNode);

	B.Tool(TEXT("scale_node"), TEXT("Set scale or multiply by factor. Prefer scaling leaves."))
		.Args(FToolSchema()
			.Add(TEXT("target"), FToolParam::Target().Required())
			.Add(TEXT("scale"), FToolParam::Vec3(TEXT("New world scale")))
			.Add(TEXT("factor"), FToolParam::Vec3(TEXT("Multiply current scale"))))
		.Undoable().RequiresWorld()
		.Handle(&ScaleNode);

	B.Tool(TEXT("reparent_node"), TEXT("Move a node under another node (empty parent = root)."))
		.Args(FToolSchema()
			.Add(TEXT("target"), FToolParam::Target().Required())
			.Add(TEXT("parent"), FToolParam::Target(TEXT("New parent node; empty for root")))
			.Add(TEXT("keep_world"), FToolParam::Boolean(TEXT("Keep world transform")).Default(true)))
		.Undoable().RequiresWorld()
		.Handle(&ReparentNode);

	B.Tool(TEXT("set_pivot"), TEXT("Move a group's origin; children stay in place. Default: bounds centre."))
		.Args(FToolSchema()
			.Add(TEXT("target"), FToolParam::Target(TEXT("Group node")).Required())
			.Add(TEXT("location"), FToolParam::Vec3(TEXT("New pivot (world)"))))
		.Undoable().RequiresWorld()
		.Handle(&SetPivot);

	B.Tool(TEXT("rename_node"), TEXT("Rename a node (unique among siblings). Id is unchanged."))
		.Args(FToolSchema()
			.Add(TEXT("target"), FToolParam::Target().Required())
			.Add(TEXT("name"), FToolParam::String(TEXT("New name"), 128).Required()))
		.Undoable().RequiresWorld()
		.Handle(&RenameNode);

	B.Tool(TEXT("set_node_tags"), TEXT("Set, add or remove node tags."))
		.Args(FToolSchema()
			.Add(TEXT("target"), FToolParam::Target().Required())
			.Add(TEXT("tags"), FToolParam::StringArray(TEXT("Tags"), 32).Required())
			.Add(TEXT("mode"), FToolParam::Enum(TEXT("How to apply"), {TEXT("set"), TEXT("add"), TEXT("remove")}).Default(TEXT("set"))))
		.Undoable().RequiresWorld()
		.Handle(&SetNodeTags);

	B.Tool(TEXT("duplicate_node"), TEXT("Copy a node subtree (from its spawn specs) next to it."))
		.Args(FToolSchema()
			.Add(TEXT("target"), FToolParam::Target().Required())
			.Add(TEXT("offset"), FToolParam::Vec3(TEXT("World offset for the copy")))
			.Add(TEXT("name"), FToolParam::String(TEXT("Name for the copy"), 128)))
		.Undoable().RequiresWorld()
		.Handle(&DuplicateNode);

	B.Tool(TEXT("delete_node"), TEXT("Delete a node and its subtree, or promote its children. Undoable."))
		.Args(FToolSchema()
			.Add(TEXT("target"), FToolParam::Target().Required())
			.Add(TEXT("promote_children"), FToolParam::Boolean(TEXT("Keep children, move them up")).Default(false)))
		.Undoable().RequiresWorld()
		.Handle(&DeleteNode);

	B.Tool(TEXT("clear_scope"), TEXT("Delete everything under a node (this session's nodes). Needs confirm; checkpoints first."))
		.Args(FToolSchema()
			.Add(TEXT("root"), FToolParam::Target(TEXT("Scope node")).Required())
			.Add(TEXT("confirm"), FToolParam::Confirm()))
		.Undoable().RequiresWorld()
		.Handle(&ClearScope);

	B.Tool(TEXT("clear_all"), TEXT("Delete all nodes this session created. Needs confirm; checkpoints first."))
		.Args(FToolSchema().Add(TEXT("confirm"), FToolParam::Confirm()))
		.Undoable().RequiresWorld()
		.Handle(&ClearAll);

	B.Tool(TEXT("export_tree"), TEXT("Export nodes as a JSON spec (relative transforms, sources, tags, colors)."))
		.Args(FToolSchema().Add(TEXT("root"), FToolParam::Target(TEXT("Export only this subtree"))))
		.RequiresWorld()
		.Handle(&ExportTree);

	B.Tool(TEXT("import_tree"), TEXT("Build nodes from an export_tree spec as one undo step. Returns new ids."))
		.Args(FToolSchema()
			.Add(TEXT("tree"), FToolParam::Object(TEXT("Spec: {nodes:[{name,source,location,rotation,scale,tags,color,children:[...]}]}")).Required())
			.Add(TEXT("parent"), FToolParam::Target(TEXT("Attach roots under this node"))))
		.Undoable().RequiresWorld()
		.Handle(&ImportTree);
}
