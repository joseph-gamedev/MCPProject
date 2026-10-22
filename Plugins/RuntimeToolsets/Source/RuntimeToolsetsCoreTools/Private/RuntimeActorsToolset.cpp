#include "RuntimeActorsToolset.h"
#include "RuntimeToolCall.h"
#include "RuntimeReflectionToolset.h"
#include "RuntimeToolInvocation.h"
#include "UObject/UObjectIterator.h"
#include "Helpers/SpawnHelpers.h"
#include "Helpers/ToolsCommon.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "EngineUtils.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

using namespace RuntimeTools;

namespace
{
	/** Name filter: wildcard if it has * or ?, else case-insensitive substring. */
	bool NameMatches(const FString& Name, const FString& Filter)
	{
		if (Filter.IsEmpty())
		{
			return true;
		}
		if (Filter.Contains(TEXT("*")) || Filter.Contains(TEXT("?")))
		{
			return Name.MatchesWildcard(Filter);
		}
		return Name.Contains(Filter);
	}

	bool ClassMatches(const UClass* Class, const FString& Filter)
	{
		if (Filter.IsEmpty())
		{
			return true;
		}
		for (const UClass* C = Class; C; C = C->GetSuperClass())
		{
			if (C->GetName().MatchesWildcard(Filter))
			{
				return true;
			}
		}
		return false;
	}

	FString ParentNodeId(AActor* Parent)
	{
		if (!Parent)
		{
			return FString();
		}
		const FRuntimeSceneNode* N = RuntimeToolCall::NodeOf(Parent);
		if (!N)
		{
			RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("parent %s is not a scene node (created by the tools)"), *Parent->GetName()),
				TEXT("Use scene.create_group to make a parent, or actors.attach to attach to a level actor after spawning"));
		}
		return N ? N->Id : FString();
	}
}

FRuntimeNodeInfo URuntimeActorsToolset::SpawnActor(const FVector& Location, const FRotator& Rotation, const FVector& Scale, const FLinearColor& Color,
	const TArray<FString>& Tags, const FString& Shape, const FString& Class, const FString& Name, AActor* Parent)
{
	FToolInvocation& Inv = *RuntimeToolCall::Current();
	if (Shape.IsEmpty() == Class.IsEmpty())
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, TEXT("give exactly one of shape or class"),
			FString::Printf(TEXT("Shapes: %s; classes like /Script/Engine.PointLight"), *FString::Join(RuntimeSpawn::ShapeNames(), TEXT(", "))));
		return {};
	}
	if (!Shape.IsEmpty() && !RuntimeSpawn::ShapeNames().Contains(Shape))
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("unknown shape '%s'"), *Shape),
			FString::Printf(TEXT("Use one of: %s"), *FString::Join(RuntimeSpawn::ShapeNames(), TEXT(", "))));
		return {};
	}
	if (!Class.IsEmpty())
	{
		FString ClassError;
		bool bNotAllowed = false;
		if (!RuntimeSpawn::LoadSpawnClass(Class, ClassError, &bNotAllowed))
		{
			RuntimeToolCall::Fail(bNotAllowed ? ERuntimeToolError::NotAllowed : ERuntimeToolError::NotFound, ClassError,
				TEXT("Find actor classes with reflection.find_class {\"base\":\"Actor\"}"));
			return {};
		}
	}
	const FString ParentId = ParentNodeId(Parent);
	if (RuntimeToolCall::HasFailed())
	{
		return {};
	}

	const FString Source = Shape.IsEmpty() ? TEXT("class:") + Class : TEXT("shape:") + Shape;
	FTransform Transform(Rotation, Location, Inv.Has(TEXT("scale")) ? Scale : FVector::OneVector);
	const FString NodeName = !Name.IsEmpty() ? Name : !Shape.IsEmpty() ? Shape : FPaths::GetExtension(Class).IsEmpty() ? FPaths::GetBaseFilename(Class) : FPaths::GetExtension(Class);
	const bool bColor = Inv.Has(TEXT("color"));
	auto Spawn = [Source, Transform, NodeName, Color, bColor](FRuntimeCommandContext& Ctx, FString& OutError) -> AActor*
	{
		AActor* Actor = RuntimeSpawn::SpawnFromSource(Ctx.World, Source, Transform, NodeName, OutError);
		if (Actor && bColor)
		{
			RuntimeSpawn::SetShapeColor(Actor, Color);
		}
		return Actor;
	};
	TUniquePtr<FCreateNodeCommand> Cmd = MakeUnique<FCreateNodeCommand>(FString::Printf(TEXT("spawn %s"), *NodeName), Spawn, NodeName, ParentId,
		ERuntimeNodeKind::Actor, Source, ToNames(Tags));
	FCreateNodeCommand* Raw = Cmd.Get();
	if (!RuntimeToolCall::RunCommand(MoveTemp(Cmd)))
	{
		return {};
	}
	return RuntimeSceneTypes::MakeNodeInfo(Raw->GetNodeId());
}

FRuntimeDestroyResult URuntimeActorsToolset::DestroyActor(AActor* Target)
{
	FRuntimeDestroyResult Out;
	FRuntimeSceneGraph* Graph = RuntimeToolCall::GetGraph();
	const FRuntimeSceneNode* Node = RuntimeToolCall::NodeOf(Target);
	TArray<AActor*> Actors;
	if (Node)
	{
		for (const FString& Id : Graph->CollectSubtree(Node->Id))
		{
			if (AActor* A = Graph->Find(Id)->Actor.Get())
			{
				Actors.Add(A);
			}
		}
	}
	else
	{
		if (!URuntimeToolsetsSettings::Get().bAllowDestroyLevelActors)
		{
			RuntimeToolCall::Fail(ERuntimeToolError::NotAllowed, FString::Printf(TEXT("%s is a level actor and destroying level actors is disabled"), *Target->GetName()),
				TEXT("Hide it with actors.set_visibility, or enable bAllowDestroyLevelActors in the project settings"));
			return Out;
		}
		Actors.Add(Target);
	}
	for (const AActor* A : Actors)
	{
		Out.Deleted.Add(RuntimeToolCall::MakeReference(A));
	}
	if (!RuntimeToolCall::RunCommand(MakeUnique<FSoftDeleteActorsCommand>(FString::Printf(TEXT("destroy %s"), *Target->GetName()), Actors)))
	{
		return {};
	}
	return Out;
}

FRuntimeActorList URuntimeActorsToolset::FindActors(const FRuntimeBox& Bounds, const TArray<TEnumAsByte<ECollisionChannel>>& CollisionChannels,
	const FString& Name, const FString& Class, const FString& Tag, AActor* Root, bool bIncludeHidden, int32 Limit)
{
	FToolInvocation& Inv = *RuntimeToolCall::Current();
	FRuntimeActorList Out;
	UWorld* World = RuntimeToolCall::GetWorld();
	FRuntimeSceneGraph* Graph = RuntimeToolCall::GetGraph();
	const bool bBounds = Inv.Has(TEXT("bounds"));
	if (CollisionChannels.Num() > 0 && !bBounds)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, TEXT("collision_channels needs bounds"), TEXT("Pass bounds {min:[x,y,z], max:[x,y,z]}"));
		return Out;
	}
	const FBox Box(Bounds.Min.ComponentMin(Bounds.Max), Bounds.Min.ComponentMax(Bounds.Max));
	const int32 MaxItems = URuntimeToolsetsSettings::Get().ClampLimit(Limit);
	const FName TagName = Tag.IsEmpty() ? NAME_None : FName(*Tag);

	// Candidates: the subtree under Root, the physics overlap, or every actor.
	TArray<AActor*> Candidates;
	if (Root)
	{
		Candidates.Add(Root);
		Root->GetAttachedActors(Candidates, false, true);
	}
	TSet<AActor*> Overlapping;
	if (CollisionChannels.Num() > 0)
	{
		FCollisionObjectQueryParams Query;
		for (const TEnumAsByte<ECollisionChannel>& C : CollisionChannels)
		{
			Query.AddObjectTypesToQuery(C.GetValue());
		}
		TArray<FOverlapResult> Overlaps;
		World->OverlapMultiByObjectType(Overlaps, Box.GetCenter(), FQuat::Identity, Query, FCollisionShape::MakeBox(Box.GetExtent()));
		for (const FOverlapResult& O : Overlaps)
		{
			if (AActor* A = O.GetActor())
			{
				Overlapping.Add(A);
			}
		}
	}
	if (!Root)
	{
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			Candidates.Add(*It);
		}
	}

	for (AActor* A : Candidates)
	{
		if (!IsValid(A))
		{
			continue;
		}
		const FRuntimeSceneNode* Node = Graph ? Graph->FindByActor(A) : nullptr;
		if (Node && Node->bSoftDeleted)
		{
			continue;
		}
		if (!bIncludeHidden && A->IsHidden() && !Node)
		{
			continue;
		}
		if (!Name.IsEmpty() && !NameMatches(A->GetName(), Name) && !(Node && NameMatches(Node->Name, Name)))
		{
			continue;
		}
		if (!ClassMatches(A->GetClass(), Class))
		{
			continue;
		}
		if (!TagName.IsNone() && !A->ActorHasTag(TagName))
		{
			continue;
		}
		if (CollisionChannels.Num() > 0)
		{
			if (!Overlapping.Contains(A))
			{
				continue;
			}
		}
		else if (bBounds)
		{
			FVector Origin, Extent;
			A->GetActorBounds(false, Origin, Extent, true);
			if (!FBox(Origin - Extent, Origin + Extent).Intersect(Box))
			{
				continue;
			}
		}
		++Out.Total;
		if (Out.Actors.Num() >= MaxItems)
		{
			continue;
		}
		FRuntimeActorSummary& S = Out.Actors.AddDefaulted_GetRef();
		S.Ref = Node ? Node->Id : FString::Printf(TEXT("actor:%s"), *A->GetName());
		S.Name = Node ? Node->Name : A->GetName();
		S.Class = A->GetClass()->GetName();
		S.Location = A->GetActorLocation();
		S.Rotation = A->GetActorRotation();
		for (const FName& T : A->Tags)
		{
			S.Tags.Add(T.ToString());
		}
		S.bHidden = A->IsHidden();
	}
	Out.bTruncated = Out.Total > Out.Actors.Num();
	return Out;
}

FRuntimeTransformResult URuntimeActorsToolset::SetTransform(AActor* Target, const FVector& Location, const FRotator& Rotation, const FVector& Scale,
	ERuntimeActorSpace Space)
{
	FToolInvocation& Inv = *RuntimeToolCall::Current();
	USceneComponent* RootComp = Target->GetRootComponent();
	if (!RootComp)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::Unsupported, FString::Printf(TEXT("%s has no root component to move"), *Target->GetName()));
		return {};
	}
	auto Apply = [&Inv, &Location, &Rotation, &Scale](FTransform T)
	{
		if (Inv.Has(TEXT("location")))
		{
			T.SetLocation(Location);
		}
		if (Inv.Has(TEXT("rotation")))
		{
			T.SetRotation(Rotation.Quaternion());
		}
		if (Inv.Has(TEXT("scale")))
		{
			T.SetScale3D(Scale);
		}
		return T;
	};
	FTransform NewWorld;
	if (Space == ERuntimeActorSpace::Relative)
	{
		const FTransform Rel = Apply(RootComp->GetRelativeTransform());
		const USceneComponent* AttachParent = RootComp->GetAttachParent();
		NewWorld = AttachParent ? Rel * AttachParent->GetSocketTransform(RootComp->GetAttachSocketName()) : Rel;
	}
	else
	{
		NewWorld = Apply(Target->GetActorTransform());
	}
	const FString Ref = RuntimeToolCall::MakeReference(Target);
	if (!RuntimeToolCall::RunCommand(MakeUnique<FTransformCommand>(FString::Printf(TEXT("transform %s"), *Ref), Target, NewWorld, Ref)))
	{
		return {};
	}
	return RuntimeSceneTypes::MakeTransformResult(Target);
}

FRuntimeAttachResult URuntimeActorsToolset::Attach(AActor* Child, AActor* Parent, const FString& Socket, bool bKeepWorld)
{
	if (Parent && (Parent == Child || Parent->IsAttachedTo(Child)))
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("cannot attach %s under itself or its own child %s"), *Child->GetName(), *Parent->GetName()));
		return {};
	}
	const FString Label = Parent ? FString::Printf(TEXT("attach %s to %s"), *Child->GetName(), *Parent->GetName()) : FString::Printf(TEXT("detach %s"), *Child->GetName());
	if (!RuntimeToolCall::RunCommand(MakeUnique<FAttachCommand>(Label, Child, Parent, Socket.IsEmpty() ? NAME_None : FName(*Socket), bKeepWorld)))
	{
		return {};
	}
	FRuntimeAttachResult Out;
	Out.Child = RuntimeToolCall::MakeReference(Child);
	Out.Parent = Parent ? RuntimeToolCall::MakeReference(Parent) : FString();
	return Out;
}

FRuntimeVisibilityResult URuntimeActorsToolset::SetVisibility(AActor* Target, bool bVisible, bool bPropagate)
{
	TArray<AActor*> Actors = {Target};
	if (bPropagate)
	{
		Target->GetAttachedActors(Actors, false, true);
	}
	TArray<TWeakObjectPtr<AActor>> Weak;
	TArray<bool> OldHidden;
	for (AActor* A : Actors)
	{
		Weak.Add(A);
		OldHidden.Add(A->IsHidden());
	}
	FRuntimeVisibilityResult Out;
	Out.Target = RuntimeToolCall::MakeReference(Target);
	Out.bVisible = bVisible;
	Out.Actors = Actors.Num();

	TSharedRef<FJsonObject> Delta = Obj();
	Delta->SetStringField(TEXT("target"), Out.Target);
	Delta->SetBoolField(TEXT("visible"), bVisible);
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
	if (!RuntimeToolCall::RunCommand(MakeUnique<FLambdaCommand>(FString::Printf(TEXT("%s %s"), bVisible ? TEXT("show") : TEXT("hide"), *Out.Target), DoFn, UndoFn, Delta)))
	{
		return {};
	}
	return Out;
}

FRuntimeComponentList URuntimeActorsToolset::ListComponents(AActor* Target, const FString& Class, int32 Limit)
{
	FRuntimeComponentList Out;
	Out.Actor = RuntimeToolCall::MakeReference(Target);
	const int32 MaxItems = URuntimeToolsetsSettings::Get().ClampLimit(Limit);
	for (UActorComponent* C : Target->GetComponents())
	{
		if (!C || !ClassMatches(C->GetClass(), Class))
		{
			continue;
		}
		++Out.Total;
		if (Out.Components.Num() >= MaxItems)
		{
			continue;
		}
		FRuntimeComponentInfo& I = Out.Components.AddDefaulted_GetRef();
		I.Ref = RuntimeToolCall::MakeReference(C);
		I.Name = C->GetName();
		I.Class = C->GetClass()->GetName();
		if (const USceneComponent* S = Cast<USceneComponent>(C))
		{
			I.bRoot = S == Target->GetRootComponent();
			if (!I.bRoot && S->GetAttachParent())
			{
				I.Parent = S->GetAttachParent()->GetName();
			}
			I.bVisible = S->IsVisible();
		}
	}
	return Out;
}

FRuntimeComponentInfo URuntimeActorsToolset::AddComponent(AActor* Target, const FString& Class, const FString& Name, const FRuntimeToolJson& Properties,
	FVector Location, const FString& AttachTo)
{
	FRuntimeComponentInfo Out;
	if (!Target)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, TEXT("target is required"), TEXT("Pass a node id, path or actor:Name"));
		return Out;
	}
	FString Wanted = Class.TrimStartAndEnd();
	if (Wanted.StartsWith(TEXT("U")) && Wanted.Len() > 1 && FChar::IsUpper(Wanted[1]))
	{
		Wanted.RightChopInline(1);
	}
	UClass* Found = nullptr;
	TArray<FString> Similar;
	for (TObjectIterator<UClass> It; It; ++It)
	{
		if (!It->IsChildOf(UActorComponent::StaticClass()) || It->HasAnyClassFlags(CLASS_Deprecated | CLASS_NewerVersionExists))
		{
			continue;
		}
		const FString N = It->GetName();
		if (N.Equals(Wanted, ESearchCase::IgnoreCase) || N.Equals(Wanted + TEXT("Component"), ESearchCase::IgnoreCase))
		{
			Found = *It;
			break;
		}
		if (Similar.Num() < 12 && !It->HasAnyClassFlags(CLASS_Abstract) && N.Contains(Wanted.Left(6)) && !N.StartsWith(TEXT("SKEL_")))
		{
			Similar.Add(N);
		}
	}
	if (!Found || Found->HasAnyClassFlags(CLASS_Abstract))
	{
		RuntimeToolCall::Fail(ERuntimeToolError::NotFound, FString::Printf(TEXT("no component class '%s'%s"), *Class, Found ? TEXT(" (it is abstract)") : TEXT("")),
			Similar.Num() ? TEXT("Similar: ") + FString::Join(Similar, TEXT(", ")) : TEXT("Examples: RotatingMovementComponent, ProjectileMovementComponent, SphereComponent, PointLightComponent"));
		return Out;
	}
	const FName CompName = Name.IsEmpty() ? MakeUniqueObjectName(Target, Found, FName(*Found->GetName().Replace(TEXT("Component"), TEXT("")))) : FName(*Name);
	if (FindObjectFast<UObject>(Target, CompName))
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("the actor already has a component named '%s'"), *CompName.ToString()));
		return Out;
	}
	UActorComponent* Comp = NewObject<UActorComponent>(Target, Found, CompName, RF_Transactional);
	if (USceneComponent* SC = Cast<USceneComponent>(Comp))
	{
		USceneComponent* Parent = Target->GetRootComponent();
		if (!AttachTo.IsEmpty())
		{
			TInlineComponentArray<USceneComponent*> Scenes(Target);
			USceneComponent* const* P = Scenes.FindByPredicate([&AttachTo](USceneComponent* X) { return X->GetName() == AttachTo; });
			if (!P)
			{
				Comp->MarkAsGarbage();
				RuntimeToolCall::Fail(ERuntimeToolError::NotFound, FString::Printf(TEXT("no scene component '%s' to attach to"), *AttachTo), TEXT("See actors.list_components"));
				return Out;
			}
			Parent = *P;
		}
		if (Parent)
		{
			SC->SetMobility(Parent->Mobility == EComponentMobility::Static ? EComponentMobility::Movable : EComponentMobility::Type(Parent->Mobility));
			SC->SetupAttachment(Parent);
		}
		else
		{
			Target->SetRootComponent(SC);
		}
		SC->SetRelativeLocation(Location);
	}
	Target->AddInstanceComponent(Comp);
	Comp->RegisterComponent();
	if (Properties.IsSet())
	{
		URuntimeReflectionToolset::SetProperties(Comp, Properties);
		const FToolInvocation* Inv = RuntimeToolCall::Current();
		if (Inv && Inv->IsComplete()) // set_properties failed the call
		{
			Comp->DestroyComponent();
			return Out;
		}
	}
	Out.Ref = RuntimeToolCall::MakeReference(Comp);
	Out.Name = Comp->GetName();
	Out.Class = Found->GetName();
	if (USceneComponent* SC = Cast<USceneComponent>(Comp); SC && SC->GetAttachParent())
	{
		Out.Parent = SC->GetAttachParent()->GetName();
	}
	return Out;
}

void URuntimeActorsToolset::RemoveComponent(UActorComponent* Target)
{
	if (!Target)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, TEXT("target is required"), TEXT("Pass \"actor#Component\" from actors.list_components"));
		return;
	}
	AActor* Owner = Target->GetOwner();
	if (Owner && Owner->GetRootComponent() == Target)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, TEXT("cannot remove the root component"), TEXT("Destroy the actor instead"));
		return;
	}
	Target->DestroyComponent();
}
