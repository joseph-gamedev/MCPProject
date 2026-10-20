#include "ActorsToolset.h"
#include "Helpers/SpawnHelpers.h"
#include "Helpers/ToolsCommon.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

using namespace RuntimeTools;

/** Resolves an optional "parent" arg to a scene node id. Returns false (and fails the call) on error. */
static bool ResolveParentNode(FToolInvocation& Inv, const TCHAR* Arg, FString& OutParentId)
{
	OutParentId.Reset();
	if (!Inv.Has(Arg) || Inv.GetString(Arg).IsEmpty())
	{
		return true;
	}
	FResolvedTarget Target;
	if (!Inv.ResolveTarget(Arg, Target))
	{
		return false;
	}
	if (!Target.Node)
	{
		Inv.Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("%s must be a scene node (created by the tools)"), Arg),
			TEXT("Use scene.create_group to make a parent"));
		return false;
	}
	OutParentId = Target.Node->Id;
	return true;
}

static void SpawnActor(FToolInvocation& Inv)
{
	const bool bHasShape = Inv.Has(TEXT("shape"));
	const bool bHasClass = Inv.Has(TEXT("class"));
	if (bHasShape == bHasClass)
	{
		Inv.Fail(ERuntimeToolError::InvalidArguments, TEXT("give exactly one of shape or class"));
		return;
	}
	const FString Source = bHasShape ? TEXT("shape:") + Inv.GetString(TEXT("shape")) : TEXT("class:") + Inv.GetString(TEXT("class"));
	if (bHasClass)
	{
		FString ClassError;
		bool bNotAllowed = false;
		if (!RuntimeSpawn::LoadSpawnClass(Inv.GetString(TEXT("class")), ClassError, &bNotAllowed))
		{
			Inv.Fail(bNotAllowed ? ERuntimeToolError::NotAllowed : ERuntimeToolError::NotFound, ClassError,
				TEXT("Use reflection.find_class with base=Actor"));
			return;
		}
	}

	FString ParentId;
	if (!ResolveParentNode(Inv, TEXT("parent"), ParentId))
	{
		return;
	}

	const FTransform Transform = ReadTransform(Inv);
	const FString Name = Inv.GetString(TEXT("name"), bHasShape ? Inv.GetString(TEXT("shape")) : FPaths::GetExtension(Inv.GetString(TEXT("class"))));
	FLinearColor Color;
	bool bColor = false;
	if (FVector C; Inv.GetVector(TEXT("color"), C))
	{
		Color = FLinearColor(C.X, C.Y, C.Z);
		bColor = true;
	}

	auto Spawn = [Source, Transform, Name, Color, bColor](FRuntimeCommandContext& Ctx, FString& Error) -> AActor*
	{
		AActor* Actor = RuntimeSpawn::SpawnFromSource(Ctx.World, Source, Transform, Name, Error);
		if (Actor && bColor)
		{
			RuntimeSpawn::SetShapeColor(Actor, Color);
		}
		return Actor;
	};
	TUniquePtr<FCreateNodeCommand> Cmd = MakeUnique<FCreateNodeCommand>(FString::Printf(TEXT("spawn %s"), *Name), Spawn, Name, ParentId,
		ERuntimeNodeKind::Actor, Source, ToNames(Inv.GetStringArray(TEXT("tags"))));
	FCreateNodeCommand* Raw = Cmd.Get();
	if (!Inv.RunCommand(MoveTemp(Cmd)))
	{
		return;
	}
	const FRuntimeSceneNode* Node = Inv.GetGraph()->Find(Raw->GetNodeId());
	TSharedRef<FJsonObject> Out = Obj();
	Out->SetStringField(TEXT("id"), Raw->GetNodeId());
	if (Node)
	{
		Out->SetStringField(TEXT("path"), Inv.GetGraph()->GetPath(Node->Id));
		if (AActor* A = Node->Actor.Get())
		{
			Out->SetStringField(TEXT("actor"), A->GetName());
		}
	}
	Inv.Succeed(Out);
}

static void DestroyActor(FToolInvocation& Inv)
{
	FResolvedTarget Target;
	if (!Inv.ResolveTarget(TEXT("target"), Target))
	{
		return;
	}
	if (!Target.Actor || Target.Component)
	{
		Inv.Fail(ERuntimeToolError::InvalidArguments, TEXT("target is not an actor"));
		return;
	}
	TArray<AActor*> Actors;
	if (Target.Node)
	{
		for (const FString& Id : Inv.GetGraph()->CollectSubtree(Target.Node->Id))
		{
			if (AActor* A = Inv.GetGraph()->Find(Id)->Actor.Get())
			{
				Actors.Add(A);
			}
		}
	}
	else
	{
		if (!URuntimeToolsetsSettings::Get().bAllowDestroyLevelActors)
		{
			Inv.Fail(ERuntimeToolError::NotAllowed, TEXT("destroying level actors is disabled"), TEXT("Enable bAllowDestroyLevelActors, or use set_visibility"));
			return;
		}
		Actors.Add(Target.Actor);
	}
	TSharedPtr<FJsonObject> Delta;
	if (Inv.RunCommand(MakeUnique<FSoftDeleteActorsCommand>(FString::Printf(TEXT("destroy %s"), *Target.Actor->GetName()), Actors), &Delta))
	{
		Inv.Succeed(Delta);
	}
}

static void FindActors(FToolInvocation& Inv)
{
	const FString NamePattern = Inv.GetString(TEXT("name"));
	const FString ClassName = Inv.GetString(TEXT("class"));
	const FName Tag = Inv.Has(TEXT("tag")) ? FName(*Inv.GetString(TEXT("tag"))) : NAME_None;
	const int32 Limit = Inv.GetLimit();
	const TArray<FString> Fields = Inv.GetFields();
	const bool bIncludeHidden = Inv.GetBool(TEXT("include_hidden"), false);

	TArray<TSharedPtr<FJsonValue>> Items;
	int32 Total = 0;
	for (TActorIterator<AActor> It(Inv.GetWorld()); It; ++It)
	{
		AActor* A = *It;
		if (!NamePattern.IsEmpty() && !A->GetName().MatchesWildcard(NamePattern))
		{
			continue;
		}
		if (!ClassName.IsEmpty())
		{
			bool bMatch = false;
			for (const UClass* C = A->GetClass(); C && !bMatch; C = C->GetSuperClass())
			{
				bMatch = C->GetName().MatchesWildcard(ClassName);
			}
			if (!bMatch)
			{
				continue;
			}
		}
		if (!Tag.IsNone() && !A->ActorHasTag(Tag))
		{
			continue;
		}
		const FRuntimeSceneNode* Node = Inv.GetGraph() ? Inv.GetGraph()->FindByActor(A) : nullptr;
		if (Node && Node->bSoftDeleted)
		{
			continue;
		}
		if (!bIncludeHidden && A->IsHidden() && !Node)
		{
			continue;
		}
		++Total;
		if (Items.Num() >= Limit)
		{
			continue;
		}
		TSharedRef<FJsonObject> J = Obj();
		J->SetStringField(TEXT("ref"), Node ? Node->Id : FString::Printf(TEXT("actor:%s"), *A->GetName()));
		if (RuntimeJson::Wants(Fields, TEXT("name")))
		{
			J->SetStringField(TEXT("name"), A->GetName());
		}
		if (RuntimeJson::Wants(Fields, TEXT("class")))
		{
			J->SetStringField(TEXT("class"), A->GetClass()->GetName());
		}
		if (RuntimeJson::Wants(Fields, TEXT("location")))
		{
			J->SetField(TEXT("location"), RuntimeJson::Vec(A->GetActorLocation()));
		}
		if (Fields.Contains(TEXT("rotation")))
		{
			J->SetField(TEXT("rotation"), RuntimeJson::Rot(A->GetActorRotation()));
		}
		if (Fields.Contains(TEXT("tags")) && A->Tags.Num() > 0)
		{
			J->SetField(TEXT("tags"), RuntimeJson::Names(A->Tags));
		}
		if (A->IsHidden())
		{
			J->SetBoolField(TEXT("hidden"), true);
		}
		Items.Add(MakeShared<FJsonValueObject>(J));
	}
	TSharedRef<FJsonObject> Out = Obj();
	Out->SetNumberField(TEXT("total"), Total);
	Out->SetArrayField(TEXT("actors"), Items);
	if (Total > Items.Num())
	{
		Out->SetBoolField(TEXT("truncated"), true);
	}
	Inv.Succeed(Out);
}

static void SetTransform(FToolInvocation& Inv)
{
	AActor* Actor = Inv.ResolveActor(TEXT("target"));
	if (!Actor)
	{
		return;
	}
	USceneComponent* Root = Actor->GetRootComponent();
	if (!Root)
	{
		Inv.Fail(ERuntimeToolError::Unsupported, TEXT("actor has no root component"));
		return;
	}
	const bool bRelative = Inv.GetString(TEXT("space"), TEXT("world")) == TEXT("relative");
	FTransform NewWorld;
	if (bRelative)
	{
		const FTransform Rel = ReadTransform(Inv, Root->GetRelativeTransform());
		const USceneComponent* Parent = Root->GetAttachParent();
		NewWorld = Parent ? Rel * Parent->GetSocketTransform(Root->GetAttachSocketName()) : Rel;
	}
	else
	{
		NewWorld = ReadTransform(Inv, Actor->GetActorTransform());
	}
	TSharedPtr<FJsonObject> Delta;
	const FString Ref = RuntimeObjectResolver::MakeReference(Actor, Inv.GetGraph());
	if (Inv.RunCommand(MakeUnique<FTransformCommand>(FString::Printf(TEXT("transform %s"), *Ref), Actor, NewWorld, Ref), &Delta))
	{
		Inv.Succeed(Delta);
	}
}

static void Attach(FToolInvocation& Inv)
{
	AActor* Child = Inv.ResolveActor(TEXT("child"));
	if (!Child)
	{
		return;
	}
	AActor* Parent = nullptr;
	if (!Inv.GetString(TEXT("parent")).IsEmpty())
	{
		Parent = Inv.ResolveActor(TEXT("parent"));
		if (!Parent)
		{
			return;
		}
	}
	const FName Socket = Inv.Has(TEXT("socket")) ? FName(*Inv.GetString(TEXT("socket"))) : NAME_None;
	TSharedPtr<FJsonObject> Delta;
	const FString Label = Parent ? FString::Printf(TEXT("attach %s to %s"), *Child->GetName(), *Parent->GetName()) : FString::Printf(TEXT("detach %s"), *Child->GetName());
	if (Inv.RunCommand(MakeUnique<FAttachCommand>(Label, Child, Parent, Socket, Inv.GetBool(TEXT("keep_world"), true)), &Delta))
	{
		Inv.Succeed(Delta);
	}
}

static void SetVisibility(FToolInvocation& Inv)
{
	AActor* Actor = Inv.ResolveActor(TEXT("target"));
	if (!Actor)
	{
		return;
	}
	const bool bVisible = Inv.GetBool(TEXT("visible"), true);
	TArray<AActor*> Actors = {Actor};
	if (Inv.GetBool(TEXT("propagate"), true))
	{
		Actor->GetAttachedActors(Actors, false, true);
	}
	TArray<TWeakObjectPtr<AActor>> Weak;
	TArray<bool> OldHidden;
	for (AActor* A : Actors)
	{
		Weak.Add(A);
		OldHidden.Add(A->IsHidden());
	}
	const FString Ref = RuntimeObjectResolver::MakeReference(Actor, Inv.GetGraph());
	TSharedRef<FJsonObject> Delta = Obj();
	Delta->SetStringField(TEXT("target"), Ref);
	Delta->SetBoolField(TEXT("visible"), bVisible);
	Delta->SetNumberField(TEXT("actors"), Actors.Num());

	auto DoFn = [Weak, bVisible](FRuntimeCommandContext&, FString&)
	{
		for (const TWeakObjectPtr<AActor>& A : Weak)
		{
			if (A.IsValid())
			{
				A->SetActorHiddenInGame(!bVisible);
			}
		}
		return true;
	};
	auto UndoFn = [Weak, OldHidden](FRuntimeCommandContext&)
	{
		bool bAll = true;
		for (int32 i = 0; i < Weak.Num(); ++i)
		{
			if (Weak[i].IsValid())
			{
				Weak[i]->SetActorHiddenInGame(OldHidden[i]);
			}
			else
			{
				bAll = false;
			}
		}
		return bAll;
	};
	if (Inv.RunCommand(MakeUnique<FLambdaCommand>(FString::Printf(TEXT("%s %s"), bVisible ? TEXT("show") : TEXT("hide"), *Ref), DoFn, UndoFn, Delta)))
	{
		Inv.Succeed(Delta);
	}
}

static void ListComponents(FToolInvocation& Inv)
{
	AActor* Actor = Inv.ResolveActor(TEXT("target"));
	if (!Actor)
	{
		return;
	}
	const int32 Limit = Inv.GetLimit();
	TArray<TSharedPtr<FJsonValue>> Items;
	int32 Total = 0;
	for (UActorComponent* C : Actor->GetComponents())
	{
		if (!C)
		{
			continue;
		}
		++Total;
		if (Items.Num() >= Limit)
		{
			continue;
		}
		TSharedRef<FJsonObject> J = Obj();
		J->SetStringField(TEXT("name"), C->GetName());
		J->SetStringField(TEXT("class"), C->GetClass()->GetName());
		if (const USceneComponent* S = Cast<USceneComponent>(C))
		{
			if (S == Actor->GetRootComponent())
			{
				J->SetBoolField(TEXT("root"), true);
			}
			else if (S->GetAttachParent())
			{
				J->SetStringField(TEXT("parent"), S->GetAttachParent()->GetName());
			}
			if (!S->IsVisible())
			{
				J->SetBoolField(TEXT("visible"), false);
			}
		}
		Items.Add(MakeShared<FJsonValueObject>(J));
	}
	TSharedRef<FJsonObject> Out = Obj();
	Out->SetStringField(TEXT("actor"), RuntimeObjectResolver::MakeReference(Actor, Inv.GetGraph()));
	Out->SetNumberField(TEXT("total"), Total);
	Out->SetArrayField(TEXT("components"), Items);
	Inv.Succeed(Out);
}

void FActorsToolset::RegisterTools(FRuntimeToolsetBuilder& B)
{
	B.Tool(TEXT("spawn_actor"), TEXT("Spawn a basic shape or actor class; returns node id. With parent, transform is relative to it."))
		.Args(FToolSchema()
			.Add(TEXT("shape"), FToolParam::Enum(TEXT("Basic shape (100 cm)"), RuntimeSpawn::ShapeNames()))
			.Add(TEXT("class"), FToolParam::String(TEXT("Actor class path, e.g. /Script/Engine.PointLight or /Game/BP_X"), 512))
			.Add(TEXT("name"), FToolParam::String(TEXT("Node name (unique among siblings)"), 128))
			.Add(TEXT("parent"), FToolParam::Target(TEXT("Parent node (id or path)")))
			.Add(TEXT("location"), FToolParam::Vec3(TEXT("[x,y,z] cm")))
			.Add(TEXT("rotation"), FToolParam::Rotator(TEXT("[pitch,yaw,roll] deg")))
			.Add(TEXT("scale"), FToolParam::Vec3(TEXT("[x,y,z] scale")))
			.Add(TEXT("color"), FToolParam::Vec3(TEXT("Shape color [r,g,b] 0-1")))
			.Add(TEXT("tags"), FToolParam::StringArray(TEXT("Tags"), 16)))
		.Undoable().RequiresWorld()
		.Handle(&SpawnActor);

	B.Tool(TEXT("destroy_actor"), TEXT("Delete an actor (and its node subtree). Undoable."))
		.Args(FToolSchema().Add(TEXT("target"), FToolParam::Target().Required()))
		.Undoable().RequiresWorld()
		.Handle(&DestroyActor);

	B.Tool(TEXT("find_actors"), TEXT("Find actors by name wildcard, class (incl. parents) and tag."))
		.Args(FToolSchema()
			.Add(TEXT("name"), FToolParam::String(TEXT("Name wildcard, e.g. Light*"), 256))
			.Add(TEXT("class"), FToolParam::String(TEXT("Class name wildcard, e.g. *Light*"), 256))
			.Add(TEXT("tag"), FToolParam::String(TEXT("Actor tag"), 128))
			.Add(TEXT("include_hidden"), FToolParam::Boolean(TEXT("Include hidden level actors")).Default(false))
			.Add(TEXT("limit"), FToolParam::Limit())
			.Add(TEXT("fields"), FToolParam::Fields({TEXT("name"), TEXT("class"), TEXT("location"), TEXT("rotation"), TEXT("tags")})))
		.RequiresWorld()
		.Handle(&FindActors);

	B.Tool(TEXT("set_transform"), TEXT("Set location/rotation/scale (missing parts unchanged). Children follow."))
		.Args(FToolSchema()
			.Add(TEXT("target"), FToolParam::Target().Required())
			.Add(TEXT("location"), FToolParam::Vec3(TEXT("[x,y,z] cm")))
			.Add(TEXT("rotation"), FToolParam::Rotator(TEXT("[pitch,yaw,roll] deg")))
			.Add(TEXT("scale"), FToolParam::Vec3(TEXT("[x,y,z]")))
			.Add(TEXT("space"), FToolParam::Enum(TEXT("Coordinate space"), {TEXT("world"), TEXT("relative")}).Default(TEXT("world"))))
		.Undoable().RequiresWorld()
		.Handle(&SetTransform);

	B.Tool(TEXT("attach"), TEXT("Attach child actor to parent actor (empty parent = detach)."))
		.Args(FToolSchema()
			.Add(TEXT("child"), FToolParam::Target().Required())
			.Add(TEXT("parent"), FToolParam::Target(TEXT("Parent target; empty to detach")))
			.Add(TEXT("socket"), FToolParam::String(TEXT("Socket name"), 128))
			.Add(TEXT("keep_world"), FToolParam::Boolean(TEXT("Keep world transform")).Default(true)))
		.Undoable().RequiresWorld()
		.Handle(&Attach);

	B.Tool(TEXT("set_visibility"), TEXT("Show or hide an actor (and attached actors)."))
		.Args(FToolSchema()
			.Add(TEXT("target"), FToolParam::Target().Required())
			.Add(TEXT("visible"), FToolParam::Boolean(TEXT("Visible")).Required())
			.Add(TEXT("propagate"), FToolParam::Boolean(TEXT("Include attached actors")).Default(true)))
		.Undoable().RequiresWorld()
		.Handle(&SetVisibility);

	B.Tool(TEXT("list_components"), TEXT("Components of an actor (name, class, attach parent)."))
		.Args(FToolSchema()
			.Add(TEXT("target"), FToolParam::Target().Required())
			.Add(TEXT("limit"), FToolParam::Limit()))
		.RequiresWorld()
		.Handle(&ListComponents);
}
