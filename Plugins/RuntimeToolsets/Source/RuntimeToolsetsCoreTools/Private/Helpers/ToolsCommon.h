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

DECLARE_LOG_CATEGORY_EXTERN(LogRuntimeToolsetsTools, Log, All);

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
}
