#include "LayoutToolset.h"
#include "RuntimeToolHelpers.h"
#include "CollisionQueryParams.h"
#include "Engine/World.h"
#include "Math/RandomStream.h"

using namespace RuntimeTools;

namespace
{
	int32 AxisIndex(const FString& Axis)
	{
		return Axis == TEXT("y") ? 1 : Axis == TEXT("z") ? 2 : 0;
	}

	/** Applies new world transforms as one undoable step and reports how many actors moved. */
	void ApplyMoves(FToolInvocation& Inv, const FString& Label, const TArray<TPair<AActor*, FTransform>>& Moves, TSharedPtr<FJsonObject> Extra = nullptr)
	{
		TUniquePtr<FCompositeCommand> Cmd = MakeUnique<FCompositeCommand>(Label);
		TArray<FString> Moved;
		for (const TPair<AActor*, FTransform>& M : Moves)
		{
			if (M.Key->GetActorTransform().Equals(M.Value, 0.001))
			{
				continue;
			}
			const FString R = Ref(Inv, M.Key);
			Cmd->Add(MakeUnique<FTransformCommand>(Label, M.Key, M.Value, R));
			Moved.Add(R);
		}
		TSharedRef<FJsonObject> Out = Extra.IsValid() ? Extra.ToSharedRef() : Obj();
		Out->SetNumberField(TEXT("moved"), Moved.Num());
		if (Moved.Num() > 0 && Moved.Num() <= 50)
		{
			Out->SetField(TEXT("nodes"), RuntimeJson::Strings(Moved));
		}
		if (Cmd->IsEmpty())
		{
			Inv.Succeed(Out);
			return;
		}
		if (Inv.RunCommand(MoveTemp(Cmd)))
		{
			Inv.Succeed(Out);
		}
	}

	FTransform WithLocation(const AActor* A, const FVector& Location)
	{
		FTransform T = A->GetActorTransform();
		T.SetLocation(Location);
		return T;
	}

	/** Moves an actor so its bounds point (min/center/max along an axis) lands on Value. */
	FTransform ShiftAxis(const AActor* A, int32 Axis, double Delta)
	{
		FVector L = A->GetActorLocation();
		L[Axis] += Delta;
		return WithLocation(A, L);
	}

	double BoxCoord(const FBox& Box, int32 Axis, const FString& Mode)
	{
		return Mode == TEXT("min") ? Box.Min[Axis] : Mode == TEXT("max") ? Box.Max[Axis] : Box.GetCenter()[Axis];
	}

	void IgnoreWithAttached(FCollisionQueryParams& Params, AActor* Actor)
	{
		Params.AddIgnoredActor(Actor);
		TArray<AActor*> Attached;
		Actor->GetAttachedActors(Attached, true, true);
		Params.AddIgnoredActors(Attached);
	}
}

static void AlignNodes(FToolInvocation& Inv)
{
	TArray<AActor*> Actors;
	if (!ResolveActorList(Inv, TEXT("targets"), Actors))
	{
		return;
	}
	const int32 Axis = AxisIndex(Inv.GetString(TEXT("axis"), TEXT("x")));
	const FString Mode = Inv.GetString(TEXT("mode"), TEXT("center"));

	double Reference = 0.0;
	if (Inv.Has(TEXT("value")))
	{
		Reference = Inv.GetNumber(TEXT("value"));
	}
	else if (!Inv.GetString(TEXT("to")).IsEmpty())
	{
		AActor* To = Inv.ResolveActor(TEXT("to"));
		if (!To)
		{
			return;
		}
		Reference = BoxCoord(ActorBox(To), Axis, Mode);
	}
	else
	{
		FBox All(ForceInit);
		for (AActor* A : Actors)
		{
			All += ActorBox(A);
		}
		Reference = BoxCoord(All, Axis, Mode);
	}

	TArray<TPair<AActor*, FTransform>> Moves;
	for (AActor* A : Actors)
	{
		Moves.Emplace(A, ShiftAxis(A, Axis, Reference - BoxCoord(ActorBox(A), Axis, Mode)));
	}
	TSharedRef<FJsonObject> Extra = Obj();
	Extra->SetNumberField(TEXT("aligned_to"), RuntimeJson::Round(Reference));
	ApplyMoves(Inv, TEXT("align"), Moves, Extra);
}

static void DistributeNodes(FToolInvocation& Inv)
{
	TArray<AActor*> Actors;
	if (!ResolveActorList(Inv, TEXT("targets"), Actors))
	{
		return;
	}
	if (Actors.Num() < 2)
	{
		Inv.Fail(ERuntimeToolError::InvalidArguments, TEXT("need at least 2 targets"));
		return;
	}
	const int32 Axis = AxisIndex(Inv.GetString(TEXT("axis"), TEXT("x")));
	Actors.Sort([Axis](const AActor& A, const AActor& B) { return ActorBox(&A).GetCenter()[Axis] < ActorBox(&B).GetCenter()[Axis]; });

	TArray<TPair<AActor*, FTransform>> Moves;
	if (Inv.Has(TEXT("gap")))
	{
		// Edge-to-edge spacing starting at the first object.
		const double Gap = Inv.GetNumber(TEXT("gap"));
		double Cursor = ActorBox(Actors[0]).Max[Axis] + Gap;
		for (int32 i = 1; i < Actors.Num(); ++i)
		{
			const FBox Box = ActorBox(Actors[i]);
			Moves.Emplace(Actors[i], ShiftAxis(Actors[i], Axis, Cursor - Box.Min[Axis]));
			Cursor += Box.GetSize()[Axis] + Gap;
		}
	}
	else
	{
		// Even centres between the first and last object.
		const double First = ActorBox(Actors[0]).GetCenter()[Axis];
		const double Last = ActorBox(Actors.Last()).GetCenter()[Axis];
		const double Step = (Last - First) / (Actors.Num() - 1);
		for (int32 i = 1; i < Actors.Num() - 1; ++i)
		{
			Moves.Emplace(Actors[i], ShiftAxis(Actors[i], Axis, First + Step * i - ActorBox(Actors[i]).GetCenter()[Axis]));
		}
	}
	ApplyMoves(Inv, TEXT("distribute"), Moves);
}

static void SnapToGrid(FToolInvocation& Inv)
{
	TArray<AActor*> Actors;
	if (!ResolveActorList(Inv, TEXT("targets"), Actors))
	{
		return;
	}
	const double Size = Inv.GetNumber(TEXT("size"), 100.0);
	const double RotationStep = Inv.GetNumber(TEXT("rotation_step"), 0.0);
	const bool bSnapZ = Inv.GetBool(TEXT("snap_z"), false);
	TArray<TPair<AActor*, FTransform>> Moves;
	for (AActor* A : Actors)
	{
		FTransform T = A->GetActorTransform();
		FVector L = T.GetLocation();
		L.X = FMath::GridSnap(L.X, Size);
		L.Y = FMath::GridSnap(L.Y, Size);
		if (bSnapZ)
		{
			L.Z = FMath::GridSnap(L.Z, Size);
		}
		T.SetLocation(L);
		if (RotationStep > 0.0)
		{
			FRotator R = T.Rotator();
			R.Pitch = FMath::GridSnap(R.Pitch, RotationStep);
			R.Yaw = FMath::GridSnap(R.Yaw, RotationStep);
			R.Roll = FMath::GridSnap(R.Roll, RotationStep);
			T.SetRotation(R.Quaternion());
		}
		Moves.Emplace(A, T);
	}
	ApplyMoves(Inv, TEXT("snap to grid"), Moves);
}

static void DropToGround(FToolInvocation& Inv)
{
	TArray<AActor*> Actors;
	if (!ResolveActorList(Inv, TEXT("targets"), Actors))
	{
		return;
	}
	UWorld* World = Inv.GetWorld();
	const double Offset = Inv.GetNumber(TEXT("offset"), 0.0);
	const double MaxDistance = Inv.GetNumber(TEXT("max_distance"), 100000.0);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(RuntimeToolsetsDrop), true);
	for (AActor* A : Actors)
	{
		IgnoreWithAttached(Params, A);
	}
	TArray<TPair<AActor*, FTransform>> Moves;
	TArray<FString> NoGround;
	for (AActor* A : Actors)
	{
		const FBox Box = ActorBox(A);
		const FVector Start(Box.GetCenter().X, Box.GetCenter().Y, Box.Max.Z + 1.0);
		const FVector End = Start - FVector(0, 0, MaxDistance);
		FHitResult Hit;
		if (!World->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params))
		{
			NoGround.Add(Ref(Inv, A));
			continue;
		}
		Moves.Emplace(A, ShiftAxis(A, 2, Hit.ImpactPoint.Z + Offset - Box.Min.Z));
	}
	TSharedRef<FJsonObject> Extra = Obj();
	if (NoGround.Num() > 0)
	{
		Extra->SetField(TEXT("no_ground"), RuntimeJson::Strings(NoGround));
	}
	ApplyMoves(Inv, TEXT("drop to ground"), Moves, Extra);
}

static void ArrangeInRow(FToolInvocation& Inv)
{
	TArray<AActor*> Actors;
	if (!ResolveActorList(Inv, TEXT("targets"), Actors))
	{
		return;
	}
	FVector Direction(1, 0, 0);
	Inv.GetVector(TEXT("direction"), Direction);
	if (Direction.IsNearlyZero())
	{
		Inv.Fail(ERuntimeToolError::InvalidArguments, TEXT("direction is zero"));
		return;
	}
	Direction.Normalize();
	FVector Start = ActorBox(Actors[0]).GetCenter();
	Inv.GetVector(TEXT("start"), Start);
	const double Gap = Inv.GetNumber(TEXT("gap"), 50.0);

	// Place bounds centres along the direction, edge to edge with Gap between them.
	TArray<TPair<AActor*, FTransform>> Moves;
	double Cursor = 0.0;
	for (int32 i = 0; i < Actors.Num(); ++i)
	{
		const FBox Box = ActorBox(Actors[i]);
		const double HalfLen = FMath::Abs(FVector::DotProduct(Box.GetExtent(), Direction.GetAbs()));
		if (i > 0)
		{
			Cursor += HalfLen;
		}
		const FVector Centre = Start + Direction * Cursor;
		const FVector Pivot = Actors[i]->GetActorLocation() - Box.GetCenter();
		Moves.Emplace(Actors[i], WithLocation(Actors[i], Centre + Pivot));
		Cursor += HalfLen + Gap;
	}
	ApplyMoves(Inv, TEXT("arrange in row"), Moves);
}

static void ArrangeInGrid(FToolInvocation& Inv)
{
	TArray<AActor*> Actors;
	if (!ResolveActorList(Inv, TEXT("targets"), Actors))
	{
		return;
	}
	const int32 Columns = FMath::Max(1, static_cast<int32>(Inv.GetInt(TEXT("columns"), FMath::CeilToInt(FMath::Sqrt(static_cast<float>(Actors.Num()))))));
	FVector Origin = ActorBox(Actors[0]).GetCenter();
	Inv.GetVector(TEXT("origin"), Origin);
	const double Gap = Inv.GetNumber(TEXT("gap"), 50.0);
	FVector Cell = FVector::ZeroVector;
	for (AActor* A : Actors)
	{
		Cell = Cell.ComponentMax(ActorBox(A).GetSize());
	}
	TArray<TPair<AActor*, FTransform>> Moves;
	for (int32 i = 0; i < Actors.Num(); ++i)
	{
		const int32 Col = i % Columns;
		const int32 Row = i / Columns;
		const FVector Centre = Origin + FVector(Col * (Cell.X + Gap), Row * (Cell.Y + Gap), 0.0);
		const FVector Pivot = Actors[i]->GetActorLocation() - ActorBox(Actors[i]).GetCenter();
		Moves.Emplace(Actors[i], WithLocation(Actors[i], Centre + Pivot));
	}
	TSharedRef<FJsonObject> Extra = Obj();
	Extra->SetNumberField(TEXT("columns"), Columns);
	Extra->SetNumberField(TEXT("rows"), FMath::DivideAndRoundUp(Actors.Num(), Columns));
	ApplyMoves(Inv, TEXT("arrange in grid"), Moves, Extra);
}

static void ArrangeInCircle(FToolInvocation& Inv)
{
	TArray<AActor*> Actors;
	if (!ResolveActorList(Inv, TEXT("targets"), Actors))
	{
		return;
	}
	FVector Center = FVector::ZeroVector;
	if (!Inv.GetVector(TEXT("center"), Center))
	{
		FBox All(ForceInit);
		for (AActor* A : Actors)
		{
			All += ActorBox(A);
		}
		Center = All.GetCenter();
	}
	const double Radius = Inv.GetNumber(TEXT("radius"), 500.0);
	const double StartAngle = Inv.GetNumber(TEXT("start_angle"), 0.0);
	const double Arc = Inv.GetNumber(TEXT("arc"), 360.0);
	const FString Facing = Inv.GetString(TEXT("facing"), TEXT("keep"));
	// A full circle spaces N items N ways; a partial arc includes both ends.
	const int32 Divisions = FMath::IsNearlyEqual(Arc, 360.0) ? Actors.Num() : FMath::Max(1, Actors.Num() - 1);
	TArray<TPair<AActor*, FTransform>> Moves;
	for (int32 i = 0; i < Actors.Num(); ++i)
	{
		const double Angle = StartAngle + Arc * i / Divisions;
		const double Rad = FMath::DegreesToRadians(Angle);
		const FVector Pos = Center + FVector(FMath::Cos(Rad) * Radius, FMath::Sin(Rad) * Radius, 0.0);
		const FVector Pivot = Actors[i]->GetActorLocation() - ActorBox(Actors[i]).GetCenter();
		FTransform T = WithLocation(Actors[i], FVector(Pos.X, Pos.Y, Actors[i]->GetActorLocation().Z) + FVector(Pivot.X, Pivot.Y, 0.0));
		if (Facing != TEXT("keep"))
		{
			FRotator R = T.Rotator();
			R.Yaw = Facing == TEXT("center") ? Angle + 180.0 : Angle;
			T.SetRotation(R.Quaternion());
		}
		Moves.Emplace(Actors[i], T);
	}
	ApplyMoves(Inv, TEXT("arrange in circle"), Moves);
}

static void RandomizeTransform(FToolInvocation& Inv)
{
	TArray<AActor*> Actors;
	if (!ResolveActorList(Inv, TEXT("targets"), Actors))
	{
		return;
	}
	FRandomStream Rng(static_cast<int32>(Inv.GetInt(TEXT("seed"), 0)));
	FVector Jitter = FVector::ZeroVector;
	Inv.GetVector(TEXT("location_jitter"), Jitter);
	const double YawJitter = Inv.GetNumber(TEXT("yaw_jitter"), 0.0);
	const double ScaleJitter = Inv.GetNumber(TEXT("scale_jitter"), 0.0);
	TArray<TPair<AActor*, FTransform>> Moves;
	for (AActor* A : Actors)
	{
		FTransform T = A->GetActorTransform();
		T.AddToTranslation(FVector(Rng.FRandRange(-Jitter.X, Jitter.X), Rng.FRandRange(-Jitter.Y, Jitter.Y), Rng.FRandRange(-Jitter.Z, Jitter.Z)));
		FRotator R = T.Rotator();
		R.Yaw += Rng.FRandRange(-YawJitter, YawJitter);
		T.SetRotation(R.Quaternion());
		T.SetScale3D(T.GetScale3D() * (1.0 + Rng.FRandRange(-ScaleJitter, ScaleJitter)));
		Moves.Emplace(A, T);
	}
	ApplyMoves(Inv, TEXT("randomize"), Moves);
}

static void FindFreeSpace(FToolInvocation& Inv)
{
	UWorld* World = Inv.GetWorld();
	FVector Size;
	if (!Inv.GetVector(TEXT("size"), Size) || Size.GetMin() <= 0.0)
	{
		Inv.Fail(ERuntimeToolError::InvalidArguments, TEXT("size must be [x,y,z] > 0"));
		return;
	}
	FVector Near = FVector::ZeroVector;
	Inv.GetVector(TEXT("near"), Near);
	const double Radius = Inv.GetNumber(TEXT("radius"), 3000.0);
	const double Step = Inv.GetNumber(TEXT("step"), FMath::Max(Size.X, Size.Y));
	const bool bOnGround = Inv.GetBool(TEXT("on_ground"), true);
	const FCollisionShape Shape = FCollisionShape::MakeBox(Size * 0.5 * 0.98);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(RuntimeToolsetsFreeSpace), false);
	const FCollisionObjectQueryParams Objects(FCollisionObjectQueryParams::AllObjects);

	// Square spiral outward from Near; first free spot wins.
	const int32 Rings = FMath::Clamp(FMath::CeilToInt(Radius / Step), 0, 64);
	int32 Tested = 0;
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
				FVector Center = Near + FVector(X * Step, Y * Step, 0.0);
				if (bOnGround)
				{
					FHitResult Hit;
					const FVector Start = Center + FVector(0, 0, Radius);
					if (!World->LineTraceSingleByChannel(Hit, Start, Center - FVector(0, 0, Radius), ECC_Visibility, Params))
					{
						continue;
					}
					Center.Z = Hit.ImpactPoint.Z + Size.Z * 0.5 + 1.0;
				}
				++Tested;
				if (!World->OverlapAnyTestByObjectType(Center, FQuat::Identity, Objects, Shape, Params))
				{
					TSharedRef<FJsonObject> Out = Obj();
					Out->SetBoolField(TEXT("found"), true);
					Out->SetField(TEXT("center"), RuntimeJson::Vec(Center));
					Out->SetField(TEXT("ground_location"), RuntimeJson::Vec(Center - FVector(0, 0, Size.Z * 0.5)));
					Out->SetNumberField(TEXT("tested"), Tested);
					Inv.Succeed(Out);
					return;
				}
			}
		}
	}
	TSharedRef<FJsonObject> Out = Obj();
	Out->SetBoolField(TEXT("found"), false);
	Out->SetNumberField(TEXT("tested"), Tested);
	Inv.Succeed(Out);
}

void FLayoutToolset::RegisterTools(FRuntimeToolsetBuilder& B)
{
	const auto Targets = []() { return FToolParam::StringArray(TEXT("Targets (node ids, paths, actor:Name)"), 500).Required(); };
	const auto Axis = []() { return FToolParam::Enum(TEXT("Axis"), {TEXT("x"), TEXT("y"), TEXT("z")}).Default(TEXT("x")); };

	B.Tool(TEXT("align_nodes"), TEXT("Align bounds (min/center/max) on an axis to the selection, a target, or a value."))
		.Args(FToolSchema()
			.Add(TEXT("targets"), Targets())
			.Add(TEXT("axis"), Axis())
			.Add(TEXT("mode"), FToolParam::Enum(TEXT("Which bounds point"), {TEXT("min"), TEXT("center"), TEXT("max")}).Default(TEXT("center")))
			.Add(TEXT("to"), FToolParam::Target(TEXT("Align to this target's bounds")))
			.Add(TEXT("value"), FToolParam::Number(TEXT("Align to this coordinate"))))
		.Undoable().RequiresWorld()
		.Handle(&AlignNodes);

	B.Tool(TEXT("distribute_nodes"), TEXT("Space objects evenly between the first and last, or with a fixed gap."))
		.Args(FToolSchema()
			.Add(TEXT("targets"), Targets())
			.Add(TEXT("axis"), Axis())
			.Add(TEXT("gap"), FToolParam::Number(TEXT("Edge-to-edge gap cm (omit for even centres)"))))
		.Undoable().RequiresWorld()
		.Handle(&DistributeNodes);

	B.Tool(TEXT("snap_to_grid"), TEXT("Snap locations (and optionally rotations) to a grid."))
		.Args(FToolSchema()
			.Add(TEXT("targets"), Targets())
			.Add(TEXT("size"), FToolParam::Number(TEXT("Grid size cm"), 0.1).Default(100.0))
			.Add(TEXT("snap_z"), FToolParam::Boolean(TEXT("Also snap Z")).Default(false))
			.Add(TEXT("rotation_step"), FToolParam::Number(TEXT("Rotation snap degrees (0 = off)"), 0.0, 180.0).Default(0.0)))
		.Undoable().RequiresWorld()
		.Handle(&SnapToGrid);

	B.Tool(TEXT("drop_to_ground"), TEXT("Move objects down until their bounds rest on what is below."))
		.Args(FToolSchema()
			.Add(TEXT("targets"), Targets())
			.Add(TEXT("offset"), FToolParam::Number(TEXT("Extra height above ground cm")).Default(0.0))
			.Add(TEXT("max_distance"), FToolParam::Number(TEXT("Trace length cm"), 1.0).Default(100000.0)))
		.Undoable().RequiresWorld()
		.Handle(&DropToGround);

	B.Tool(TEXT("arrange_in_row"), TEXT("Line objects up edge to edge along a direction."))
		.Args(FToolSchema()
			.Add(TEXT("targets"), Targets())
			.Add(TEXT("start"), FToolParam::Vec3(TEXT("Centre of the first object (default: its current centre)")))
			.Add(TEXT("direction"), FToolParam::Vec3(TEXT("Row direction")).Default(RuntimeJson::Vec(FVector(1, 0, 0))))
			.Add(TEXT("gap"), FToolParam::Number(TEXT("Gap cm")).Default(50.0)))
		.Undoable().RequiresWorld()
		.Handle(&ArrangeInRow);

	B.Tool(TEXT("arrange_in_grid"), TEXT("Place objects in a grid on the XY plane (cell = largest object + gap)."))
		.Args(FToolSchema()
			.Add(TEXT("targets"), Targets())
			.Add(TEXT("columns"), FToolParam::Integer(TEXT("Columns (default: square)"), 1, 1000))
			.Add(TEXT("origin"), FToolParam::Vec3(TEXT("Centre of the first cell")))
			.Add(TEXT("gap"), FToolParam::Number(TEXT("Gap cm")).Default(50.0)))
		.Undoable().RequiresWorld()
		.Handle(&ArrangeInGrid);

	B.Tool(TEXT("arrange_in_circle"), TEXT("Place objects on a circle or arc around a centre."))
		.Args(FToolSchema()
			.Add(TEXT("targets"), Targets())
			.Add(TEXT("center"), FToolParam::Vec3(TEXT("Circle centre (default: selection centre)")))
			.Add(TEXT("radius"), FToolParam::Number(TEXT("Radius cm"), 0.0).Default(500.0))
			.Add(TEXT("start_angle"), FToolParam::Number(TEXT("Degrees")).Default(0.0))
			.Add(TEXT("arc"), FToolParam::Number(TEXT("Arc degrees"), 1.0, 360.0).Default(360.0))
			.Add(TEXT("facing"), FToolParam::Enum(TEXT("Rotation"), {TEXT("keep"), TEXT("center"), TEXT("outward")}).Default(TEXT("keep"))))
		.Undoable().RequiresWorld()
		.Handle(&ArrangeInCircle);

	B.Tool(TEXT("randomize_transform"), TEXT("Add seeded random offset, yaw and uniform scale."))
		.Args(FToolSchema()
			.Add(TEXT("targets"), Targets())
			.Add(TEXT("location_jitter"), FToolParam::Vec3(TEXT("Max offset per axis cm")))
			.Add(TEXT("yaw_jitter"), FToolParam::Number(TEXT("Max yaw change deg"), 0.0, 180.0).Default(0.0))
			.Add(TEXT("scale_jitter"), FToolParam::Number(TEXT("Max scale change fraction"), 0.0, 0.95).Default(0.0))
			.Add(TEXT("seed"), FToolParam::Integer(TEXT("Random seed"), 0).Default(0)))
		.Undoable().RequiresWorld()
		.Handle(&RandomizeTransform);

	B.Tool(TEXT("find_free_space"), TEXT("Find the nearest spot where a box of a given size fits without overlaps."))
		.Args(FToolSchema()
			.Add(TEXT("size"), FToolParam::Vec3(TEXT("Box size [x,y,z] cm")).Required())
			.Add(TEXT("near"), FToolParam::Vec3(TEXT("Search from here")))
			.Add(TEXT("radius"), FToolParam::Number(TEXT("Search radius cm"), 1.0).Default(3000.0))
			.Add(TEXT("step"), FToolParam::Number(TEXT("Grid step cm (default: box size)"), 1.0))
			.Add(TEXT("on_ground"), FToolParam::Boolean(TEXT("Rest the box on the ground")).Default(true)))
		.RequiresWorld()
		.Handle(&FindFreeSpace);
}
