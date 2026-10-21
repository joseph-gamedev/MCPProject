#include "SplineCore.h"
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

USplineComponent* RuntimeSplines::FindSpline(const AActor* Actor)
{
	return Actor ? Actor->FindComponentByClass<USplineComponent>() : nullptr;
}

AActor* RuntimeSplines::SpawnFromSource(UWorld* World, const FString& Source, const FTransform& Transform, const FString& Name, FString& OutError)
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
