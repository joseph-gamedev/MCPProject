#include "TreeSerializer.h"
#include "SpawnHelpers.h"
#include "RuntimeCommonCommands.h"
#include "RuntimeJson.h"
#include "RuntimeSceneGraph.h"
#include "Components/SceneComponent.h"
#include "GameFramework/Actor.h"

namespace RuntimeTreeSerializer
{
	static constexpr int32 MaxDepth = 32;

	static TSharedRef<FJsonObject> ExportNode(FRuntimeSceneGraph& Graph, const FRuntimeSceneNode& Node)
	{
		TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
		J->SetStringField(TEXT("id"), Node.Id);
		J->SetStringField(TEXT("name"), Node.Name);
		J->SetStringField(TEXT("source"), Node.Source.IsEmpty() ? TEXT("group") : Node.Source);
		if (const AActor* A = Node.Actor.Get())
		{
			const FTransform Rel = A->GetRootComponent() ? A->GetRootComponent()->GetRelativeTransform() : A->GetActorTransform();
			J->SetField(TEXT("location"), RuntimeJson::Vec(Rel.GetLocation()));
			if (!Rel.Rotator().IsNearlyZero(0.001))
			{
				J->SetField(TEXT("rotation"), RuntimeJson::Rot(Rel.Rotator()));
			}
			if (!Rel.GetScale3D().Equals(FVector::OneVector, 0.0001))
			{
				J->SetField(TEXT("scale"), RuntimeJson::Vec(Rel.GetScale3D()));
			}
			FLinearColor Color;
			if (RuntimeSpawn::GetShapeColor(const_cast<AActor*>(A), Color))
			{
				J->SetField(TEXT("color"), RuntimeJson::Vec(FVector(Color.R, Color.G, Color.B)));
			}
		}
		if (Node.Tags.Num() > 0)
		{
			J->SetField(TEXT("tags"), RuntimeJson::Names(Node.Tags));
		}
		TArray<TSharedPtr<FJsonValue>> Children;
		for (const FString& ChildId : Node.Children)
		{
			const FRuntimeSceneNode* Child = Graph.Find(ChildId);
			if (Child && !Child->bSoftDeleted)
			{
				Children.Add(MakeShared<FJsonValueObject>(ExportNode(Graph, *Child)));
			}
		}
		if (Children.Num() > 0)
		{
			J->SetArrayField(TEXT("children"), Children);
		}
		return J;
	}

	TSharedRef<FJsonObject> Export(FRuntimeSceneGraph& Graph, const FString& RootId)
	{
		TArray<TSharedPtr<FJsonValue>> Nodes;
		const TArray<FString> Roots = RootId.IsEmpty() ? Graph.GetRootIds() : TArray<FString>{RootId};
		for (const FString& Id : Roots)
		{
			if (const FRuntimeSceneNode* N = Graph.Find(Id))
			{
				Nodes.Add(MakeShared<FJsonValueObject>(ExportNode(Graph, *N)));
			}
		}
		TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
		Out->SetNumberField(TEXT("version"), 1);
		Out->SetArrayField(TEXT("nodes"), Nodes);
		return Out;
	}

	static bool BuildNode(FRuntimeSceneGraph& Graph, const TSharedPtr<FJsonObject>& Spec, const FString& ParentId, const FString& NameOverride,
		const FVector& LocalOffset, int32 Depth, FCompositeCommand& Out, FString& OutNewId, TMap<FString, FString>* IdMap, FString& OutError)
	{
		if (!Spec.IsValid())
		{
			OutError = TEXT("node spec must be an object");
			return false;
		}
		if (Depth > MaxDepth)
		{
			OutError = FString::Printf(TEXT("tree deeper than %d levels"), MaxDepth);
			return false;
		}
		FString Source = TEXT("group");
		Spec->TryGetStringField(TEXT("source"), Source);
		if (Source != TEXT("group") && !Source.StartsWith(TEXT("shape:")) && !Source.StartsWith(TEXT("class:")))
		{
			OutError = FString::Printf(TEXT("bad source '%s' (group, shape:<name>, class:<path>)"), *Source);
			return false;
		}
		FString Name = NameOverride;
		if (Name.IsEmpty() && !Spec->TryGetStringField(TEXT("name"), Name))
		{
			Name = TEXT("node");
		}

		FTransform Rel = FTransform::Identity;
		FVector V;
		FRotator R;
		if (RuntimeJson::ToVector(Spec->TryGetField(TEXT("location")), V))
		{
			Rel.SetLocation(V);
		}
		if (RuntimeJson::ToRotator(Spec->TryGetField(TEXT("rotation")), R))
		{
			Rel.SetRotation(R.Quaternion());
		}
		if (RuntimeJson::ToVector(Spec->TryGetField(TEXT("scale")), V))
		{
			Rel.SetScale3D(V);
		}
		Rel.AddToTranslation(LocalOffset);

		bool bColor = false;
		FLinearColor Color;
		if (RuntimeJson::ToVector(Spec->TryGetField(TEXT("color")), V))
		{
			bColor = true;
			Color = FLinearColor(V.X, V.Y, V.Z);
		}
		TArray<FName> Tags;
		const TArray<TSharedPtr<FJsonValue>>* TagArr = nullptr;
		if (Spec->TryGetArrayField(TEXT("tags"), TagArr))
		{
			for (const TSharedPtr<FJsonValue>& T : *TagArr)
			{
				Tags.Add(FName(*T->AsString()));
			}
		}

		const FString NewId = Graph.ReserveId();
		auto Spawn = [Source, Rel, Name, bColor, Color](FRuntimeCommandContext& Ctx, FString& Error) -> AActor*
		{
			AActor* A = RuntimeSpawn::SpawnFromSource(Ctx.World, Source, Rel, Name, Error);
			if (A && bColor)
			{
				RuntimeSpawn::SetShapeColor(A, Color);
			}
			return A;
		};
		TUniquePtr<FCreateNodeCommand> Cmd = MakeUnique<FCreateNodeCommand>(FString::Printf(TEXT("create %s"), *Name), Spawn, Name, ParentId,
			Source == TEXT("group") ? ERuntimeNodeKind::Group : ERuntimeNodeKind::Actor, Source, Tags);
		Cmd->SetForcedId(NewId);
		Out.Add(MoveTemp(Cmd));
		OutNewId = NewId;

		FString SpecId;
		if (IdMap && Spec->TryGetStringField(TEXT("id"), SpecId))
		{
			IdMap->Add(SpecId, NewId);
		}

		const TArray<TSharedPtr<FJsonValue>>* Children = nullptr;
		if (Spec->TryGetArrayField(TEXT("children"), Children))
		{
			for (const TSharedPtr<FJsonValue>& Child : *Children)
			{
				FString ChildId;
				if (!BuildNode(Graph, Child->AsObject(), NewId, FString(), FVector::ZeroVector, Depth + 1, Out, ChildId, IdMap, OutError))
				{
					return false;
				}
			}
		}
		return true;
	}

	TUniquePtr<FCompositeCommand> BuildImport(FRuntimeSceneGraph& Graph, const TSharedRef<FJsonObject>& Spec, const FString& ParentId,
		const FString& RootNameOverride, const FVector& WorldOffset, TArray<FString>& OutRootIds, TMap<FString, FString>* OutIdMap, FString& OutError)
	{
		const TArray<TSharedPtr<FJsonValue>>* Nodes = nullptr;
		if (!Spec->TryGetArrayField(TEXT("nodes"), Nodes) || Nodes->Num() == 0)
		{
			OutError = TEXT("spec needs a non-empty 'nodes' array");
			return nullptr;
		}
		// Convert the world offset into the parent's local space.
		FVector LocalOffset = WorldOffset;
		if (const FRuntimeSceneNode* Parent = Graph.Find(ParentId))
		{
			if (const AActor* PA = Parent->Actor.Get())
			{
				LocalOffset = PA->GetActorTransform().InverseTransformVector(WorldOffset);
			}
		}
		TUniquePtr<FCompositeCommand> Cmd = MakeUnique<FCompositeCommand>(TEXT("import"));
		for (int32 i = 0; i < Nodes->Num(); ++i)
		{
			FString RootId;
			if (!BuildNode(Graph, (*Nodes)[i]->AsObject(), ParentId, i == 0 ? RootNameOverride : FString(), LocalOffset, 0, *Cmd, RootId, OutIdMap, OutError))
			{
				return nullptr;
			}
			OutRootIds.Add(RootId);
		}
		return Cmd;
	}
}
