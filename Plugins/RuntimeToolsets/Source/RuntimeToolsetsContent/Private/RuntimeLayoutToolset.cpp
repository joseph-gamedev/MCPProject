#include "RuntimeLayoutToolset.h"
#include "RuntimeToolCall.h"
#include "RuntimeToolHelpers.h"
#include "CollisionQueryParams.h"
#include "Engine/World.h"
#include "Math/RandomStream.h"

using namespace RuntimeTools;

namespace
{
	bool LayoutHas(const TCHAR* Arg)
	{
		return RuntimeToolCall::Current()->Has(Arg);
	}

	bool LayoutRequireTargets(const TArray<AActor*>& Targets, int32 Min = 1)
	{
		if (Targets.Num() < Min)
		{
			RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, Min > 1 ? FString::Printf(TEXT("need at least %d targets (got %d)"), Min, Targets.Num())
				: FString(TEXT("targets is empty")), TEXT("Pass node ids, e.g. [\"n3\",\"n4\"] (scene.find_nodes lists them)"));
			return false;
		}
		return true;
	}

	/** Applies new world transforms as one undoable step and reports what moved. */
	template <typename TResult>
	TResult LayoutApplyMoves(const FString& Label, const TArray<TPair<AActor*, FTransform>>& Moves, TResult Out)
	{
		TUniquePtr<FCompositeCommand> Cmd = MakeUnique<FCompositeCommand>(Label);
		for (const TPair<AActor*, FTransform>& M : Moves)
		{
			if (M.Key->GetActorTransform().Equals(M.Value, 0.001))
			{
				continue;
			}
			const FString R = RuntimeToolCall::MakeReference(M.Key);
			Cmd->Add(MakeUnique<FTransformCommand>(Label, M.Key, M.Value, R));
			++Out.Moved;
			if (Out.Nodes.Num() < 50)
			{
				Out.Nodes.Add(R);
			}
		}
		if (!Cmd->IsEmpty())
		{
			RuntimeToolCall::RunCommand(MoveTemp(Cmd));
		}
		return Out;
	}

	FTransform LayoutWithLocation(const AActor* A, const FVector& Location)
	{
		FTransform T = A->GetActorTransform();
		T.SetLocation(Location);
		return T;
	}

	FTransform LayoutShiftAxis(const AActor* A, int32 Axis, double Delta)
	{
		FVector L = A->GetActorLocation();
		L[Axis] += Delta;
		return LayoutWithLocation(A, L);
	}

	/** Bounds point along an axis: Mode 0 = min, 1 = centre, 2 = max. */
	double LayoutBoxCoord(const FBox& Box, int32 Axis, int32 Mode)
	{
		return Mode == 0 ? Box.Min[Axis] : Mode == 2 ? Box.Max[Axis] : Box.GetCenter()[Axis];
	}

	void LayoutIgnoreWithAttached(FCollisionQueryParams& Params, AActor* Actor)
	{
		Params.AddIgnoredActor(Actor);
		TArray<AActor*> Attached;
		Actor->GetAttachedActors(Attached, true, true);
		Params.AddIgnoredActors(Attached);
	}
}

FLayoutToolAlignResult URuntimeLayoutToolset::AlignNodes(const TArray<AActor*>& Targets, const FRuntimeToolJson& Value, ELayoutToolAxis Axis, const FString& ModeName, AActor* To)
{
	if (!LayoutRequireTargets(Targets))
	{
		return {};
	}
	// A string, not an enum: the reflection layer drops enum values ending in "max" (it takes them for the _MAX entry).
	const int32 Mode = ModeName.Equals(TEXT("min"), ESearchCase::IgnoreCase) ? 0 : ModeName.Equals(TEXT("center"), ESearchCase::IgnoreCase) ? 1
		: ModeName.Equals(TEXT("max"), ESearchCase::IgnoreCase) ? 2 : INDEX_NONE;
	if (Mode == INDEX_NONE)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("mode '%s' is not one of min, center, max"), *ModeName));
		return {};
	}
	const int32 A = static_cast<int32>(Axis);
	double Reference = 0.0;
	if (Value.IsSet())
	{
		if (!Value.Value->TryGetNumber(Reference))
		{
			RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, TEXT("value must be a number (cm)"), TEXT("e.g. {\"value\": 0}"));
			return {};
		}
	}
	else if (To)
	{
		Reference = LayoutBoxCoord(ActorBox(To), A, Mode);
	}
	else
	{
		FBox All(ForceInit);
		for (AActor* T : Targets)
		{
			All += ActorBox(T);
		}
		Reference = LayoutBoxCoord(All, A, Mode);
	}
	TArray<TPair<AActor*, FTransform>> Moves;
	for (AActor* T : Targets)
	{
		Moves.Emplace(T, LayoutShiftAxis(T, A, Reference - LayoutBoxCoord(ActorBox(T), A, Mode)));
	}
	FLayoutToolAlignResult Out;
	Out.AlignedTo = RuntimeJson::Round(Reference);
	return LayoutApplyMoves(TEXT("align"), Moves, Out);
}

FLayoutToolMoveResult URuntimeLayoutToolset::DistributeNodes(const TArray<AActor*>& Targets, ELayoutToolAxis Axis, float Gap)
{
	if (!LayoutRequireTargets(Targets, 2))
	{
		return {};
	}
	const int32 A = static_cast<int32>(Axis);
	TArray<AActor*> Sorted = Targets;
	Sorted.Sort([A](const AActor& L, const AActor& R) { return ActorBox(&L).GetCenter()[A] < ActorBox(&R).GetCenter()[A]; });
	TArray<TPair<AActor*, FTransform>> Moves;
	if (Gap >= 0.f)
	{
		// Edge-to-edge spacing starting at the first object.
		double Cursor = ActorBox(Sorted[0]).Max[A] + Gap;
		for (int32 i = 1; i < Sorted.Num(); ++i)
		{
			const FBox Box = ActorBox(Sorted[i]);
			Moves.Emplace(Sorted[i], LayoutShiftAxis(Sorted[i], A, Cursor - Box.Min[A]));
			Cursor += Box.GetSize()[A] + Gap;
		}
	}
	else
	{
		// Even centres between the first and last object.
		const double First = ActorBox(Sorted[0]).GetCenter()[A];
		const double Last = ActorBox(Sorted.Last()).GetCenter()[A];
		const double Step = (Last - First) / (Sorted.Num() - 1);
		for (int32 i = 1; i < Sorted.Num() - 1; ++i)
		{
			Moves.Emplace(Sorted[i], LayoutShiftAxis(Sorted[i], A, First + Step * i - ActorBox(Sorted[i]).GetCenter()[A]));
		}
	}
	return LayoutApplyMoves(TEXT("distribute"), Moves, FLayoutToolMoveResult());
}

FLayoutToolMoveResult URuntimeLayoutToolset::SnapToGrid(const TArray<AActor*>& Targets, float Size, bool bSnapZ, float RotationStep)
{
	if (!LayoutRequireTargets(Targets))
	{
		return {};
	}
	if (Size <= 0.f || RotationStep < 0.f)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("size must be > 0 (got %g) and rotation_step >= 0 (got %g)"), Size, RotationStep));
		return {};
	}
	TArray<TPair<AActor*, FTransform>> Moves;
	for (AActor* A : Targets)
	{
		FTransform T = A->GetActorTransform();
		FVector L = T.GetLocation();
		L.X = FMath::GridSnap(L.X, static_cast<double>(Size));
		L.Y = FMath::GridSnap(L.Y, static_cast<double>(Size));
		if (bSnapZ)
		{
			L.Z = FMath::GridSnap(L.Z, static_cast<double>(Size));
		}
		T.SetLocation(L);
		if (RotationStep > 0.f)
		{
			FRotator R = T.Rotator();
			R.Pitch = FMath::GridSnap(R.Pitch, static_cast<double>(RotationStep));
			R.Yaw = FMath::GridSnap(R.Yaw, static_cast<double>(RotationStep));
			R.Roll = FMath::GridSnap(R.Roll, static_cast<double>(RotationStep));
			T.SetRotation(R.Quaternion());
		}
		Moves.Emplace(A, T);
	}
	return LayoutApplyMoves(TEXT("snap to grid"), Moves, FLayoutToolMoveResult());
}

FLayoutToolDropResult URuntimeLayoutToolset::DropToGround(const TArray<AActor*>& Targets, float Offset, float MaxDistance)
{
	if (!LayoutRequireTargets(Targets))
	{
		return {};
	}
	if (MaxDistance <= 0.f)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("max_distance must be > 0 (got %g)"), MaxDistance));
		return {};
	}
	UWorld* World = RuntimeToolCall::GetWorld();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(RuntimeToolsetsDrop), true);
	for (AActor* A : Targets)
	{
		LayoutIgnoreWithAttached(Params, A);
	}
	FLayoutToolDropResult Out;
	TArray<TPair<AActor*, FTransform>> Moves;
	for (AActor* A : Targets)
	{
		const FBox Box = ActorBox(A);
		const FVector Start(Box.GetCenter().X, Box.GetCenter().Y, Box.Max.Z + 1.0);
		FHitResult Hit;
		if (!World->LineTraceSingleByChannel(Hit, Start, Start - FVector(0, 0, MaxDistance), ECC_Visibility, Params))
		{
			Out.NoGround.Add(RuntimeToolCall::MakeReference(A));
			continue;
		}
		Moves.Emplace(A, LayoutShiftAxis(A, 2, Hit.ImpactPoint.Z + Offset - Box.Min.Z));
	}
	return LayoutApplyMoves(TEXT("drop to ground"), Moves, Out);
}

FLayoutToolMoveResult URuntimeLayoutToolset::ArrangeInRow(const TArray<AActor*>& Targets, const FVector& Start, const FVector& Direction, float Gap)
{
	if (!LayoutRequireTargets(Targets))
	{
		return {};
	}
	FVector Dir = LayoutHas(TEXT("direction")) ? Direction : FVector(1, 0, 0);
	if (Dir.IsNearlyZero())
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, TEXT("direction is zero"), TEXT("e.g. [1,0,0] for a row along X"));
		return {};
	}
	Dir.Normalize();
	const FVector First = LayoutHas(TEXT("start")) ? Start : ActorBox(Targets[0]).GetCenter();
	// Place bounds centres along the direction, edge to edge with Gap between them.
	TArray<TPair<AActor*, FTransform>> Moves;
	double Cursor = 0.0;
	for (int32 i = 0; i < Targets.Num(); ++i)
	{
		const FBox Box = ActorBox(Targets[i]);
		const double HalfLen = FMath::Abs(FVector::DotProduct(Box.GetExtent(), Dir.GetAbs()));
		if (i > 0)
		{
			Cursor += HalfLen;
		}
		const FVector Centre = First + Dir * Cursor;
		const FVector Pivot = Targets[i]->GetActorLocation() - Box.GetCenter();
		Moves.Emplace(Targets[i], LayoutWithLocation(Targets[i], Centre + Pivot));
		Cursor += HalfLen + Gap;
	}
	return LayoutApplyMoves(TEXT("arrange in row"), Moves, FLayoutToolMoveResult());
}

FLayoutToolGridResult URuntimeLayoutToolset::ArrangeInGrid(const TArray<AActor*>& Targets, const FVector& Origin, int32 Columns, float Gap)
{
	if (!LayoutRequireTargets(Targets))
	{
		return {};
	}
	if (Columns < 0)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("columns must be >= 0 (got %d); 0 = square"), Columns));
		return {};
	}
	const int32 Cols = Columns > 0 ? Columns : FMath::Max(1, FMath::CeilToInt(FMath::Sqrt(static_cast<float>(Targets.Num()))));
	const FVector First = LayoutHas(TEXT("origin")) ? Origin : ActorBox(Targets[0]).GetCenter();
	FVector Cell = FVector::ZeroVector;
	for (AActor* A : Targets)
	{
		Cell = Cell.ComponentMax(ActorBox(A).GetSize());
	}
	TArray<TPair<AActor*, FTransform>> Moves;
	for (int32 i = 0; i < Targets.Num(); ++i)
	{
		const FVector Centre = First + FVector((i % Cols) * (Cell.X + Gap), (i / Cols) * (Cell.Y + Gap), 0.0);
		const FVector Pivot = Targets[i]->GetActorLocation() - ActorBox(Targets[i]).GetCenter();
		Moves.Emplace(Targets[i], LayoutWithLocation(Targets[i], Centre + Pivot));
	}
	FLayoutToolGridResult Out;
	Out.Columns = Cols;
	Out.Rows = FMath::DivideAndRoundUp(Targets.Num(), Cols);
	return LayoutApplyMoves(TEXT("arrange in grid"), Moves, Out);
}

FLayoutToolMoveResult URuntimeLayoutToolset::ArrangeInCircle(const TArray<AActor*>& Targets, const FVector& Center, float Radius, float StartAngle, float Arc,
	ELayoutToolFacing Facing)
{
	if (!LayoutRequireTargets(Targets))
	{
		return {};
	}
	if (Radius < 0.f || Arc < 1.f || Arc > 360.f)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("radius must be >= 0 (got %g) and arc 1-360 (got %g)"), Radius, Arc));
		return {};
	}
	FVector C = Center;
	if (!LayoutHas(TEXT("center")))
	{
		FBox All(ForceInit);
		for (AActor* A : Targets)
		{
			All += ActorBox(A);
		}
		C = All.GetCenter();
	}
	// A full circle spaces N items N ways; a partial arc includes both ends.
	const int32 Divisions = FMath::IsNearlyEqual(Arc, 360.f) ? Targets.Num() : FMath::Max(1, Targets.Num() - 1);
	TArray<TPair<AActor*, FTransform>> Moves;
	for (int32 i = 0; i < Targets.Num(); ++i)
	{
		const double Angle = StartAngle + static_cast<double>(Arc) * i / Divisions;
		const double Rad = FMath::DegreesToRadians(Angle);
		const FVector Pos = C + FVector(FMath::Cos(Rad) * Radius, FMath::Sin(Rad) * Radius, 0.0);
		const FVector Pivot = Targets[i]->GetActorLocation() - ActorBox(Targets[i]).GetCenter();
		FTransform T = LayoutWithLocation(Targets[i], FVector(Pos.X + Pivot.X, Pos.Y + Pivot.Y, Targets[i]->GetActorLocation().Z));
		if (Facing != ELayoutToolFacing::Keep)
		{
			FRotator R = T.Rotator();
			R.Yaw = Facing == ELayoutToolFacing::Center ? Angle + 180.0 : Angle;
			T.SetRotation(R.Quaternion());
		}
		Moves.Emplace(Targets[i], T);
	}
	return LayoutApplyMoves(TEXT("arrange in circle"), Moves, FLayoutToolMoveResult());
}

FLayoutToolMoveResult URuntimeLayoutToolset::RandomizeTransform(const TArray<AActor*>& Targets, const FVector& LocationJitter, float YawJitter, float ScaleJitter,
	int32 Seed)
{
	if (!LayoutRequireTargets(Targets))
	{
		return {};
	}
	if (ScaleJitter < 0.f || ScaleJitter > 0.95f || YawJitter < 0.f)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("scale_jitter must be 0-0.95 (got %g) and yaw_jitter >= 0 (got %g)"), ScaleJitter, YawJitter));
		return {};
	}
	FRandomStream Rng(Seed);
	const FVector Jitter = LayoutHas(TEXT("location_jitter")) ? LocationJitter.GetAbs() : FVector::ZeroVector;
	TArray<TPair<AActor*, FTransform>> Moves;
	for (AActor* A : Targets)
	{
		FTransform T = A->GetActorTransform();
		T.AddToTranslation(FVector(Rng.FRandRange(-Jitter.X, Jitter.X), Rng.FRandRange(-Jitter.Y, Jitter.Y), Rng.FRandRange(-Jitter.Z, Jitter.Z)));
		FRotator R = T.Rotator();
		R.Yaw += Rng.FRandRange(-YawJitter, YawJitter);
		T.SetRotation(R.Quaternion());
		T.SetScale3D(T.GetScale3D() * (1.0 + Rng.FRandRange(-ScaleJitter, ScaleJitter)));
		Moves.Emplace(A, T);
	}
	return LayoutApplyMoves(TEXT("randomize"), Moves, FLayoutToolMoveResult());
}

FLayoutToolFreeSpace URuntimeLayoutToolset::FindFreeSpace(const FVector& Size, const FVector& Near, float Radius, float Step, bool bOnGround)
{
	FLayoutToolFreeSpace Out;
	if (Size.GetMin() <= 0.0 || Radius <= 0.f || Step < 0.f)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, TEXT("size must be [x,y,z] > 0, radius > 0 and step >= 0"), TEXT("e.g. {\"size\":[400,400,300]}"));
		return Out;
	}
	UWorld* World = RuntimeToolCall::GetWorld();
	const FVector From = LayoutHas(TEXT("near")) ? Near : FVector::ZeroVector;
	const double Cell = Step > 0.f ? Step : FMath::Max(Size.X, Size.Y);
	const FCollisionShape Shape = FCollisionShape::MakeBox(Size * 0.5 * 0.98);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(RuntimeToolsetsFreeSpace), false);
	const FCollisionObjectQueryParams Objects(FCollisionObjectQueryParams::AllObjects);
	// Square spiral outward; the first free spot wins.
	const int32 Rings = FMath::Clamp(FMath::CeilToInt(Radius / Cell), 0, 64);
	for (int32 Ring = 0; Ring <= Rings; ++Ring)
	{
		for (int32 X = -Ring; X <= Ring; ++X)
		{
			for (int32 Y = -Ring; Y <= Ring; ++Y)
			{
				if (FMath::Max(FMath::Abs(X), FMath::Abs(Y)) != Ring)
				{
					continue;
				}
				FVector C = From + FVector(X * Cell, Y * Cell, 0.0);
				if (bOnGround)
				{
					FHitResult Hit;
					if (!World->LineTraceSingleByChannel(Hit, C + FVector(0, 0, Radius), C - FVector(0, 0, Radius), ECC_Visibility, Params))
					{
						continue;
					}
					C.Z = Hit.ImpactPoint.Z + Size.Z * 0.5 + 1.0;
				}
				++Out.Tested;
				if (!World->OverlapAnyTestByObjectType(C, FQuat::Identity, Objects, Shape, Params))
				{
					Out.bFound = true;
					Out.Center = C;
					Out.GroundLocation = C - FVector(0, 0, Size.Z * 0.5);
					return Out;
				}
			}
		}
	}
	return Out;
}
