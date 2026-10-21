#include "SplineToolset.h"
#include "RuntimeCommonCommands.h"
#include "RuntimeJson.h"
#include "RuntimeSceneGraph.h"
#include "RuntimeToolHelpers.h"
#include "RuntimeToolsetsCore.h"
#include "Components/SplineComponent.h"
#include "Engine/World.h"

using namespace RuntimeTools;

namespace
{
	// Material fields passed straight to the builder (same names as build.create_assembly parts).
	const TCHAR* MaterialFields[] = {TEXT("material"), TEXT("color"), TEXT("roughness"), TEXT("metallic"), TEXT("emissive"), TEXT("emissive_strength"),
		TEXT("opacity"), TEXT("variation"), TEXT("style"), TEXT("tile"), TEXT("relief")};
	constexpr int32 MaxMeshVerts = 45000;

	bool ReadPoints(const TSharedPtr<FJsonObject>& R, TArray<FVector>& Out)
	{
		const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
		if (!R->TryGetArrayField(TEXT("points"), A))
		{
			return false;
		}
		for (const TSharedPtr<FJsonValue>& V : *A)
		{
			FVector P;
			if (!RuntimeJson::ToVector(V, P))
			{
				return false;
			}
			Out.Add(P);
		}
		return Out.Num() >= 2;
	}

	/** Fills a spline component (local space of its owner) from world points. */
	void FillSpline(USplineComponent* S, const TArray<FVector>& Local, bool bClosed, bool bLinear)
	{
		S->ClearSplinePoints(false);
		for (const FVector& P : Local)
		{
			S->AddSplinePoint(P, ESplineCoordinateSpace::Local, false);
		}
		for (int32 i = 0; i < Local.Num(); ++i)
		{
			S->SetSplinePointType(i, bLinear ? ESplinePointType::Linear : ESplinePointType::Curve, false);
		}
		S->SetClosedLoop(bClosed, false);
		S->UpdateSpline();
	}

	/** Cross-section (u = right, v = up) for a profile kind. */
	TArray<FVector2D> Section(const FString& Kind, double W, double H)
	{
		if (Kind == TEXT("tube"))
		{
			TArray<FVector2D> C;
			for (int32 i = 0; i < 12; ++i)
			{
				const double A = 2.0 * PI * i / 12.0;
				C.Add(FVector2D(FMath::Cos(A) * W * 0.5, H + W * 0.5 + FMath::Sin(A) * W * 0.5));
			}
			return C;
		}
		// ribbon (thin slab) and wall (tall slab) share a rectangular section.
		return {FVector2D(-W * 0.5, 0), FVector2D(-W * 0.5, H), FVector2D(W * 0.5, H), FVector2D(W * 0.5, 0)};
	}

	/** Sweeps the section along the spline into raw vertices/triangles (outward-facing, capped when open). */
	void SweepMesh(const USplineComponent* S, const TArray<FVector2D>& Sec, double Step, double ZOffset, TArray<FVector>& V, TArray<FIntVector>& T)
	{
		const bool bClosed = S->IsClosedLoop();
		const double Len = S->GetSplineLength();
		const int32 K = Sec.Num();
		Step = FMath::Max(Step, Len * K / MaxMeshVerts);
		const int32 Segs = FMath::Max(1, FMath::CeilToInt(Len / Step));
		const int32 Rings = bClosed ? Segs : Segs + 1;
		TArray<FVector> Centers;
		TArray<FVector> Dirs;
		for (int32 r = 0; r < Rings; ++r)
		{
			const double D = Len * r / Segs;
			const FVector P = S->GetLocationAtDistanceAlongSpline(D, ESplineCoordinateSpace::Local) + FVector(0, 0, ZOffset);
			FVector Dir = S->GetDirectionAtDistanceAlongSpline(D, ESplineCoordinateSpace::Local);
			FVector Right = FVector::CrossProduct(FVector::UpVector, Dir).GetSafeNormal();
			if (Right.IsNearlyZero())
			{
				Right = FVector::RightVector;
			}
			FVector2D C(0, 0);
			for (const FVector2D& Q : Sec)
			{
				V.Add(P + Right * Q.X + FVector::UpVector * Q.Y);
				C += Q;
			}
			C /= K;
			Centers.Add(P + Right * C.X + FVector::UpVector * C.Y);
			Dirs.Add(Dir);
		}
		auto AddTri = [&](int32 A, int32 B, int32 C, const FVector& Outward)
		{
			const FVector N = FVector::CrossProduct(V[B] - V[A], V[C] - V[A]);
			// Unreal is left-handed: a counter-clockwise-from-outside triangle has its cross product pointing inward.
			if (FVector::DotProduct(N, Outward) > 0.0)
			{
				T.Add(FIntVector(A, C, B));
			}
			else
			{
				T.Add(FIntVector(A, B, C));
			}
		};
		const int32 Joins = bClosed ? Rings : Rings - 1;
		for (int32 r = 0; r < Joins; ++r)
		{
			const int32 r2 = (r + 1) % Rings;
			const FVector Mid = (Centers[r] + Centers[r2]) * 0.5;
			for (int32 j = 0; j < K; ++j)
			{
				const int32 j2 = (j + 1) % K;
				const int32 A = r * K + j, B = r2 * K + j, C = r2 * K + j2, D = r * K + j2;
				const FVector Face = (V[A] + V[B] + V[C] + V[D]) * 0.25;
				AddTri(A, B, C, Face - Mid);
				AddTri(A, C, D, Face - Mid);
			}
		}
		if (!bClosed)
		{
			for (int32 End = 0; End < 2; ++End)
			{
				const int32 r = End == 0 ? 0 : Rings - 1;
				const FVector Out = End == 0 ? -Dirs[r] : Dirs[r];
				for (int32 j = 1; j + 1 < K; ++j)
				{
					AddTri(r * K, r * K + j, r * K + j + 1, Out);
				}
			}
		}
	}

	FVector Ground(UWorld* W, const FVector& P)
	{
		FHitResult Hit;
		FCollisionQueryParams Q(TEXT("SplineGround"), false);
		if (W->LineTraceSingleByChannel(Hit, P + FVector(0, 0, 20000), P - FVector(0, 0, 50000), ECC_Visibility, Q))
		{
			return FVector(P.X, P.Y, Hit.ImpactPoint.Z);
		}
		return P;
	}
}

USplineComponent* FSplineToolset::FindSpline(const AActor* Actor)
{
	return Actor ? Actor->FindComponentByClass<USplineComponent>() : nullptr;
}

AActor* FSplineToolset::SpawnFromSource(UWorld* World, const FString& Source, const FTransform& Transform, const FString& Name, FString& OutError)
{
	const TSharedPtr<FJsonObject> R = RuntimeJson::Parse(Source.Mid(7));
	TArray<FVector> Points;
	if (!R.IsValid() || !ReadPoints(R, Points))
	{
		OutError = TEXT("bad spline: recipe (needs 2+ points)");
		return nullptr;
	}
	bool bClosed = false, bLinear = false, bGround = false;
	R->TryGetBoolField(TEXT("closed"), bClosed);
	R->TryGetBoolField(TEXT("linear"), bLinear);
	R->TryGetBoolField(TEXT("follow_ground"), bGround);
	if (bGround)
	{
		for (FVector& P : Points)
		{
			P = Ground(World, P);
		}
	}
	// The node sits at the first point; the curve is stored relative to it.
	const FVector Origin = Points[0];
	TArray<FVector> Local;
	for (const FVector& P : Points)
	{
		Local.Add(P - Origin);
	}
	FTransform At = Transform;
	At.SetLocation(Origin);

	const TSharedPtr<FJsonObject>* ProfileObj = nullptr;
	FString Kind = TEXT("none");
	if (R->TryGetObjectField(TEXT("profile"), ProfileObj))
	{
		(*ProfileObj)->TryGetStringField(TEXT("kind"), Kind);
	}
	AActor* Actor = nullptr;
	if (Kind != TEXT("none"))
	{
		USplineComponent* Temp = NewObject<USplineComponent>(GetTransientPackage());
		FillSpline(Temp, Local, bClosed, bLinear);
		const TSharedPtr<FJsonObject>& P = *ProfileObj;
		double Width = 400, Height = Kind == TEXT("wall") ? 300 : Kind == TEXT("tube") ? 0 : 8, Step = 100, ZOff = 2;
		P->TryGetNumberField(TEXT("width"), Width);
		P->TryGetNumberField(TEXT("height"), Height);
		P->TryGetNumberField(TEXT("step"), Step);
		P->TryGetNumberField(TEXT("z_offset"), ZOff);
		TArray<FVector> V;
		TArray<FIntVector> T;
		SweepMesh(Temp, Section(Kind, Width, Height), FMath::Max(10.0, Step), ZOff, V, T);
		Temp->MarkAsGarbage();

		TSharedRef<FJsonObject> Prim = Obj();
		Prim->SetStringField(TEXT("shape"), TEXT("raw"));
		TArray<TSharedPtr<FJsonValue>> Verts, Tris;
		for (const FVector& X : V)
		{
			Verts.Add(RuntimeJson::Vec(X));
		}
		for (const FIntVector& X : T)
		{
			Tris.Add(MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>{MakeShared<FJsonValueNumber>(X.X), MakeShared<FJsonValueNumber>(X.Y), MakeShared<FJsonValueNumber>(X.Z)}));
		}
		Prim->SetArrayField(TEXT("vertices"), Verts);
		Prim->SetArrayField(TEXT("triangles"), Tris);
		for (const TCHAR* F : MaterialFields)
		{
			if (P->HasField(F))
			{
				Prim->SetField(F, P->TryGetField(F));
			}
		}
		Actor = IRuntimeToolsetsCore::Get().SpawnFromSource(World, TEXT("prim:") + RuntimeJson::Write(Prim), At, Name, OutError);
		if (!Actor)
		{
			return nullptr;
		}
	}
	else
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Actor = World->SpawnActor<AActor>(AActor::StaticClass(), At, Params);
		if (!Actor)
		{
			OutError = TEXT("could not spawn the spline actor");
			return nullptr;
		}
		USceneComponent* Root = NewObject<USceneComponent>(Actor, TEXT("Root"));
		Root->SetMobility(EComponentMobility::Movable);
		Actor->SetRootComponent(Root);
		Root->RegisterComponent();
		Actor->AddInstanceComponent(Root);
		Actor->SetActorTransform(At);
	}
	USplineComponent* S = NewObject<USplineComponent>(Actor, TEXT("Spline"));
	S->SetMobility(EComponentMobility::Movable);
	S->SetupAttachment(Actor->GetRootComponent());
	S->RegisterComponent();
	Actor->AddInstanceComponent(S);
	FillSpline(S, Local, bClosed, bLinear);
	Actor->Tags.AddUnique(TEXT("RTSpline"));
	return Actor;
}

static void CreateSpline(FToolInvocation& Inv)
{
	TSharedRef<FJsonObject> R = Obj();
	R->SetField(TEXT("points"), Inv.GetValue(TEXT("points")));
	R->SetBoolField(TEXT("closed"), Inv.GetBool(TEXT("closed")));
	R->SetBoolField(TEXT("linear"), Inv.GetBool(TEXT("linear")));
	R->SetBoolField(TEXT("follow_ground"), Inv.GetBool(TEXT("follow_ground")));
	if (const TSharedPtr<FJsonObject> P = Inv.GetObject(TEXT("profile")))
	{
		FString Kind;
		if (P->TryGetStringField(TEXT("kind"), Kind) && Kind != TEXT("none") && Kind != TEXT("ribbon") && Kind != TEXT("wall") && Kind != TEXT("tube"))
		{
			Inv.Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("unknown profile kind '%s'"), *Kind), TEXT("ribbon (road, path, river), wall, tube, none"));
			return;
		}
		R->SetObjectField(TEXT("profile"), P);
	}
	TArray<FVector> Pts;
	if (!ReadPoints(R, Pts))
	{
		Inv.Fail(ERuntimeToolError::InvalidArguments, TEXT("points must be 2+ [x,y,z] world positions"));
		return;
	}
	FString ParentId;
	if (Inv.Has(TEXT("parent")))
	{
		FResolvedTarget T;
		if (!Inv.ResolveTarget(TEXT("parent"), T))
		{
			return;
		}
		ParentId = T.Node ? T.Node->Id : FString();
	}
	const FString Source = TEXT("spline:") + RuntimeJson::Write(R);
	const FString Name = Inv.GetString(TEXT("name"), TEXT("Spline"));
	auto Spawn = [Source, Name](FRuntimeCommandContext& Ctx, FString& Err) -> AActor*
	{
		return FSplineToolset::SpawnFromSource(Ctx.World, Source, FTransform::Identity, Name, Err);
	};
	TUniquePtr<FCreateNodeCommand> Cmd = MakeUnique<FCreateNodeCommand>(TEXT("create spline"), Spawn, Name, ParentId, ERuntimeNodeKind::Actor, Source, TArray<FName>{TEXT("spline")});
	FCreateNodeCommand* Raw = Cmd.Get();
	if (!Inv.RunCommand(MoveTemp(Cmd)))
	{
		return;
	}
	TSharedRef<FJsonObject> Out = Obj();
	Out->SetStringField(TEXT("id"), Raw->GetNodeId());
	if (const FRuntimeSceneNode* N = Inv.GetGraph()->Find(Raw->GetNodeId()))
	{
		if (const USplineComponent* S = FSplineToolset::FindSpline(N->Actor.Get()))
		{
			Out->SetNumberField(TEXT("length_cm"), FMath::RoundToDouble(S->GetSplineLength()));
		}
	}
	Inv.Succeed(Out);
}

static void SampleSpline(FToolInvocation& Inv)
{
	AActor* A = Inv.ResolveActor(TEXT("target"));
	if (!A)
	{
		return;
	}
	const USplineComponent* S = FSplineToolset::FindSpline(A);
	if (!S)
	{
		Inv.Fail(ERuntimeToolError::InvalidArguments, TEXT("target has no spline"), TEXT("Create one with spline.create"));
		return;
	}
	const double Len = S->GetSplineLength();
	const double Spacing = FMath::Max(1.0, Inv.GetNumber(TEXT("spacing"), 500.0));
	const int32 Count = FMath::Min(Inv.GetLimit(), FMath::FloorToInt(Len / Spacing) + 1);
	TArray<TSharedPtr<FJsonValue>> Points;
	for (int32 i = 0; i < Count; ++i)
	{
		const double D = FMath::Min(Len, i * Spacing);
		TSharedRef<FJsonObject> P = Obj();
		P->SetField(TEXT("location"), RuntimeJson::Vec(S->GetLocationAtDistanceAlongSpline(D, ESplineCoordinateSpace::World)));
		P->SetField(TEXT("direction"), RuntimeJson::Vec(S->GetDirectionAtDistanceAlongSpline(D, ESplineCoordinateSpace::World)));
		P->SetNumberField(TEXT("yaw"), S->GetRotationAtDistanceAlongSpline(D, ESplineCoordinateSpace::World).Yaw);
		P->SetNumberField(TEXT("distance"), D);
		Points.Add(MakeShared<FJsonValueObject>(P));
	}
	TSharedRef<FJsonObject> Out = Obj();
	Out->SetNumberField(TEXT("length_cm"), Len);
	Out->SetBoolField(TEXT("closed"), S->IsClosedLoop());
	Out->SetArrayField(TEXT("points"), Points);
	Inv.Succeed(Out);
}

void FSplineToolset::RegisterTools(FRuntimeToolsetBuilder& B)
{
	B.Tool(TEXT("create"), TEXT("A spline through world points (smooth or linear, open or closed loop), optionally shown as a swept mesh: ribbon (road, path, river), wall or tube. Undoable."))
		.Args(FToolSchema()
			.Add(TEXT("points"), FToolParam::Array(TEXT("World points [x,y,z] (2+)"), FToolParam::Vec3(TEXT("Point")), 512).Required())
			.Add(TEXT("name"), FToolParam::String(TEXT("Node name"), 64))
			.Add(TEXT("closed"), FToolParam::Boolean(TEXT("Closed loop (an area for pcg.scatter mode inside)")).Default(false))
			.Add(TEXT("linear"), FToolParam::Boolean(TEXT("Straight segments instead of a smooth curve")).Default(false))
			.Add(TEXT("follow_ground"), FToolParam::Boolean(TEXT("Drop each point onto the ground below")).Default(false))
			.Add(TEXT("profile"), FToolParam::Object(TEXT("Mesh along the spline: {kind: ribbon|wall|tube|none, width cm, height cm (ribbon thickness / wall height / tube lift), step cm, z_offset cm, "
				"plus builder material fields: material, color, roughness, metallic, emissive, opacity, variation, tile, relief}")))
			.Add(TEXT("parent"), FToolParam::Target(TEXT("Parent node"))))
		.Undoable().RequiresWorld()
		.Handle(&CreateSpline);

	B.Tool(TEXT("sample"), TEXT("Points along a spline every `spacing` cm, with direction and yaw (for placing things in scripts)."))
		.Args(FToolSchema()
			.Add(TEXT("target"), FToolParam::Target(TEXT("Spline node")).Required())
			.Add(TEXT("spacing"), FToolParam::Number(TEXT("Distance between samples (cm)"), 1.0, 1000000.0).Default(500.0))
			.Add(TEXT("limit"), FToolParam::Limit()))
		.RequiresWorld()
		.Handle(&SampleSpline);
}
