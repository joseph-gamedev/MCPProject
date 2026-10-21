#include "RuntimeSplineToolset.h"
#include "SplineCore.h"
#include "RuntimeCommonCommands.h"
#include "RuntimeJson.h"
#include "RuntimeSceneGraph.h"
#include "Components/SplineComponent.h"
#include "GameFramework/Actor.h"

FRuntimeSplineInfo URuntimeSplineToolset::CreateSpline(const TArray<FVector>& Points, const FRuntimeSplineProfile& Profile, const FString& Name, bool bClosed,
	bool bLinear, bool bFollowGround, AActor* Parent)
{
	FRuntimeSplineInfo Info;
	if (Points.Num() < 2)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, TEXT("points needs at least 2 positions"));
		return Info;
	}
	TSharedRef<FJsonObject> R = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> Pts;
	for (const FVector& P : Points)
	{
		Pts.Add(RuntimeJson::Vec(P));
	}
	R->SetArrayField(TEXT("points"), Pts);
	R->SetBoolField(TEXT("closed"), bClosed);
	R->SetBoolField(TEXT("linear"), bLinear);
	R->SetBoolField(TEXT("follow_ground"), bFollowGround);
	if (Profile.Kind != ERuntimeSplineProfileKind::None)
	{
		TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
		static const TCHAR* Kinds[] = {TEXT("none"), TEXT("ribbon"), TEXT("wall"), TEXT("tube")};
		P->SetStringField(TEXT("kind"), Kinds[static_cast<uint8>(Profile.Kind)]);
		P->SetNumberField(TEXT("width"), Profile.Width);
		if (Profile.Height >= 0.f)
		{
			P->SetNumberField(TEXT("height"), Profile.Height);
		}
		P->SetNumberField(TEXT("step"), Profile.Step);
		P->SetNumberField(TEXT("z_offset"), Profile.ZOffset);
		if (const TSharedPtr<FJsonObject> M = Profile.Material.AsObject())
		{
			for (const TPair<FString, TSharedPtr<FJsonValue>>& F : M->Values)
			{
				P->SetField(F.Key, F.Value);
			}
		}
		R->SetObjectField(TEXT("profile"), P);
	}
	FString ParentId;
	if (Parent)
	{
		const FRuntimeSceneNode* N = RuntimeToolCall::NodeOf(Parent);
		if (!N)
		{
			RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, TEXT("parent must be a scene node"));
			return Info;
		}
		ParentId = N->Id;
	}
	const FString Source = TEXT("spline:") + RuntimeJson::Write(R);
	auto Spawn = [Source, Name](FRuntimeCommandContext& Ctx, FString& Err) -> AActor*
	{
		return RuntimeSplines::SpawnFromSource(Ctx.World, Source, FTransform::Identity, Name, Err);
	};
	TUniquePtr<FCreateNodeCommand> Cmd = MakeUnique<FCreateNodeCommand>(TEXT("create spline"), Spawn, Name, ParentId, ERuntimeNodeKind::Actor, Source, TArray<FName>{TEXT("spline")});
	FCreateNodeCommand* Raw = Cmd.Get();
	if (!RuntimeToolCall::RunCommand(MoveTemp(Cmd)))
	{
		return Info;
	}
	if (const FRuntimeSceneNode* N = RuntimeToolCall::GetGraph()->Find(Raw->GetNodeId()))
	{
		Info.Spline = N->Actor.Get();
		if (const USplineComponent* S = RuntimeSplines::FindSpline(Info.Spline))
		{
			Info.LengthCm = FMath::RoundToFloat(S->GetSplineLength());
			Info.bClosed = S->IsClosedLoop();
		}
	}
	return Info;
}

FRuntimeSplineSamples URuntimeSplineToolset::SampleSpline(AActor* Spline, float Spacing, int32 Limit)
{
	FRuntimeSplineSamples Out;
	const USplineComponent* S = RuntimeSplines::FindSpline(Spline);
	if (!S)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, TEXT("spline has no spline component"), TEXT("Create one with spline.create_spline"));
		return Out;
	}
	Out.LengthCm = S->GetSplineLength();
	Out.bClosed = S->IsClosedLoop();
	Spacing = FMath::Max(1.f, Spacing);
	const int32 Count = FMath::Clamp(FMath::FloorToInt(Out.LengthCm / Spacing) + 1, 1, FMath::Clamp(Limit, 1, 5000));
	for (int32 i = 0; i < Count; ++i)
	{
		const float D = FMath::Min(Out.LengthCm, i * Spacing);
		FRuntimeSplineSample& P = Out.Points.AddDefaulted_GetRef();
		P.Location = S->GetLocationAtDistanceAlongSpline(D, ESplineCoordinateSpace::World);
		P.Direction = S->GetDirectionAtDistanceAlongSpline(D, ESplineCoordinateSpace::World);
		P.Yaw = S->GetRotationAtDistanceAlongSpline(D, ESplineCoordinateSpace::World).Yaw;
		P.Distance = D;
	}
	return Out;
}
