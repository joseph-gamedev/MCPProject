#pragma once

#include "CoreMinimal.h"
#include "RuntimeCommandHistory.h"
#include "RuntimeCommonCommands.h"
#include "RuntimeJson.h"
#include "RuntimeObjectResolver.h"
#include "RuntimeSceneGraph.h"
#include "RuntimeToolInvocation.h"
#include "RuntimeToolSchema.h"
#include "RuntimeToolset.h"
#include "RuntimeToolsetsCore.h"
#include "RuntimeToolsetsSettings.h"
#include "GameFramework/Actor.h"

/** Small helpers shared by toolset implementations in any group module. */
namespace RuntimeTools
{
	inline TSharedRef<FJsonObject> Obj() { return MakeShared<FJsonObject>(); }

	/** Standard node fields offered by list tools. */
	inline TArray<FString> NodeFields()
	{
		return {TEXT("name"), TEXT("path"), TEXT("kind"), TEXT("parent"), TEXT("children"), TEXT("tags"), TEXT("class"), TEXT("source"),
			TEXT("location"), TEXT("rotation"), TEXT("scale"), TEXT("bounds")};
	}

	/** Reads optional location/rotation/scale args into a transform (missing parts keep Base). */
	inline FTransform ReadTransform(const FToolInvocation& Inv, const FTransform& Base = FTransform::Identity)
	{
		FTransform T = Base;
		FVector V;
		FRotator R;
		if (Inv.GetVector(TEXT("location"), V))
		{
			T.SetLocation(V);
		}
		if (Inv.GetRotator(TEXT("rotation"), R))
		{
			T.SetRotation(R.Quaternion());
		}
		if (Inv.GetVector(TEXT("scale"), V))
		{
			T.SetScale3D(V);
		}
		return T;
	}

	inline TArray<FName> ToNames(const TArray<FString>& Strings)
	{
		TArray<FName> Out;
		for (const FString& S : Strings)
		{
			Out.Add(FName(*S));
		}
		return Out;
	}

	/** Undo/redo step result as JSON. */
	inline TSharedRef<FJsonObject> StepToJson(const FRuntimeHistoryStepResult& Step)
	{
		TSharedRef<FJsonObject> Out = Obj();
		Out->SetNumberField(TEXT("steps"), Step.Applied);
		TArray<TSharedPtr<FJsonValue>> Changes;
		for (const TSharedPtr<FJsonObject>& D : Step.Deltas)
		{
			Changes.Add(MakeShared<FJsonValueObject>(D));
		}
		if (Changes.Num() > 0)
		{
			Out->SetArrayField(TEXT("changes"), Changes);
		}
		if (Step.SkippedIrreversible.Num() > 0)
		{
			Out->SetField(TEXT("skipped_irreversible"), RuntimeJson::Strings(Step.SkippedIrreversible));
		}
		if (Step.Stale.Num() > 0)
		{
			Out->SetField(TEXT("stale"), RuntimeJson::Strings(Step.Stale));
		}
		if (Step.PassedMarkers.Num() > 0)
		{
			Out->SetField(TEXT("passed_checkpoints"), RuntimeJson::Strings(Step.PassedMarkers));
		}
		return Out;
	}

	/**
	 * Resolves a string-array argument of targets to actors (duplicates removed, order kept).
	 * Fails the call and returns false on the first bad target or when the list is empty.
	 */
	inline bool ResolveActorList(FToolInvocation& Inv, const TCHAR* ArgName, TArray<AActor*>& Out)
	{
		Out.Reset();
		for (const FString& Target : Inv.GetStringArray(ArgName))
		{
			ERuntimeToolError Error = ERuntimeToolError::None;
			FString Message;
			AActor* Actor = RuntimeObjectResolver::ResolveActor(Target, Inv.GetWorld(), Inv.GetGraph(), Error, Message);
			if (!Actor)
			{
				Inv.Fail(Error, FString::Printf(TEXT("%s: %s"), *Target, *Message), TEXT("Use scene.find_nodes or actors.find_actors to get ids"));
				return false;
			}
			Out.AddUnique(Actor);
		}
		if (Out.Num() == 0)
		{
			Inv.Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("%s is empty"), ArgName));
			return false;
		}
		return true;
	}

	/**
	 * World-space bounding box of an actor and every visible actor attached below it, so groups and parent nodes
	 * report the extent of their children. Falls back to the actor's own (possibly empty) bounds.
	 */
	inline FBox ActorBox(const AActor* Actor)
	{
		TArray<AActor*> All = {const_cast<AActor*>(Actor)};
		Actor->GetAttachedActors(All, false, true);
		FBox Box(ForceInit);
		for (const AActor* A : All)
		{
			if (A != Actor && A->IsHidden())
			{
				continue; // soft-deleted or hidden children
			}
			FVector Origin, Extent;
			A->GetActorBounds(false, Origin, Extent, true);
			if (!Extent.IsNearlyZero())
			{
				Box += FBox(Origin - Extent, Origin + Extent);
			}
		}
		if (!Box.IsValid)
		{
			FVector Origin, Extent;
			Actor->GetActorBounds(false, Origin, Extent, true);
			Box = FBox(Origin - Extent, Origin + Extent);
		}
		return Box;
	}

	/** Reference string for results: node id, actor:Name, or object path. */
	inline FString Ref(const FToolInvocation& Inv, const UObject* Object)
	{
		return RuntimeObjectResolver::MakeReference(Object, Inv.GetGraph());
	}
}
