#include "PrimitiveMesh.h"
#include "RuntimeJson.h"
#include "RuntimeToolsetsSettings.h"
#include "Components/DynamicMeshComponent.h"
#include "DynamicMeshActor.h"
#include "Engine/World.h"
#include "GeometryScript/MeshBooleanFunctions.h"
#include "GeometryScript/MeshModelingFunctions.h"
#include "GeometryScript/MeshPrimitiveFunctions.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UDynamicMesh.h"
#include "Algo/Reverse.h"
#include "DynamicMesh/DynamicMesh3.h"

namespace RuntimePrimitives
{
	static const TCHAR* BasicMaterialPath = TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial");
	static const TCHAR* PbrMaterialPath = TEXT("/RuntimeToolsets/Materials/M_RT_PBR.M_RT_PBR");
	static const TCHAR* GlassMaterialPath = TEXT("/RuntimeToolsets/Materials/M_RT_Glass.M_RT_Glass");
	static constexpr int32 MaxOps = 64;

	const TArray<FString>& ShapeNames()
	{
		static const TArray<FString> Names = {TEXT("box"), TEXT("sphere"), TEXT("cylinder"), TEXT("cone"), TEXT("pyramid"), TEXT("torus"), TEXT("wedge"),
			TEXT("capsule"), TEXT("plane"), TEXT("dome"), TEXT("tube"), TEXT("arch"), TEXT("roof"), TEXT("lattice"), TEXT("steps"), TEXT("tiers"),
			TEXT("column"), TEXT("openings"), TEXT("lathe"), TEXT("extrude"), TEXT("sweep")};
		return Names;
	}

	struct FPreset
	{
		FString Name;
		FLinearColor Color;
		float Roughness;
		float Metallic;
		float Variation;
		bool bGlass = false;
		float Emissive = 0.f;
	};

	static const TArray<FPreset>& Presets()
	{
		static const TArray<FPreset> List = {
			{TEXT("marble"), FLinearColor(0.86f, 0.85f, 0.81f), 0.22f, 0.f, 0.07f},
			{TEXT("sandstone"), FLinearColor(0.72f, 0.55f, 0.36f), 0.85f, 0.f, 0.22f},
			{TEXT("limestone"), FLinearColor(0.78f, 0.74f, 0.64f), 0.8f, 0.f, 0.15f},
			{TEXT("granite"), FLinearColor(0.36f, 0.34f, 0.33f), 0.55f, 0.f, 0.3f},
			{TEXT("stone"), FLinearColor(0.45f, 0.44f, 0.42f), 0.9f, 0.f, 0.25f},
			{TEXT("brick"), FLinearColor(0.45f, 0.18f, 0.12f), 0.85f, 0.f, 0.25f},
			{TEXT("terracotta"), FLinearColor(0.6f, 0.28f, 0.16f), 0.8f, 0.f, 0.18f},
			{TEXT("concrete"), FLinearColor(0.55f, 0.55f, 0.53f), 0.9f, 0.f, 0.12f},
			{TEXT("plaster"), FLinearColor(0.85f, 0.82f, 0.76f), 0.9f, 0.f, 0.06f},
			{TEXT("wood"), FLinearColor(0.36f, 0.22f, 0.12f), 0.7f, 0.f, 0.2f},
			{TEXT("slate_roof"), FLinearColor(0.18f, 0.2f, 0.24f), 0.7f, 0.f, 0.15f},
			{TEXT("copper"), FLinearColor(0.95f, 0.64f, 0.54f), 0.35f, 1.f, 0.05f},
			{TEXT("verdigris"), FLinearColor(0.33f, 0.58f, 0.48f), 0.7f, 0.1f, 0.25f},
			{TEXT("bronze"), FLinearColor(0.55f, 0.35f, 0.18f), 0.45f, 1.f, 0.08f},
			{TEXT("gold"), FLinearColor(1.f, 0.77f, 0.34f), 0.22f, 1.f, 0.03f},
			{TEXT("steel"), FLinearColor(0.56f, 0.57f, 0.58f), 0.35f, 1.f, 0.05f},
			{TEXT("iron"), FLinearColor(0.2f, 0.18f, 0.16f), 0.6f, 0.6f, 0.15f},
			{TEXT("painted_iron"), FLinearColor(0.32f, 0.22f, 0.15f), 0.55f, 0.35f, 0.1f},
			{TEXT("glass"), FLinearColor(0.6f, 0.75f, 0.8f), 0.05f, 0.f, 0.f, true},
			{TEXT("tinted_glass"), FLinearColor(0.15f, 0.25f, 0.3f), 0.05f, 0.f, 0.f, true},
			{TEXT("water"), FLinearColor(0.04f, 0.12f, 0.16f), 0.05f, 0.f, 0.02f},
			{TEXT("grass"), FLinearColor(0.12f, 0.3f, 0.06f), 0.9f, 0.f, 0.3f},
			{TEXT("sand"), FLinearColor(0.76f, 0.66f, 0.48f), 0.95f, 0.f, 0.15f},
			{TEXT("asphalt"), FLinearColor(0.08f, 0.08f, 0.08f), 0.85f, 0.f, 0.1f},
			{TEXT("light"), FLinearColor(1.f, 0.9f, 0.7f), 0.5f, 0.f, 0.f, false, 20.f},
			{TEXT("white"), FLinearColor(0.9f, 0.9f, 0.9f), 0.5f, 0.f, 0.03f},
			{TEXT("black"), FLinearColor(0.03f, 0.03f, 0.03f), 0.5f, 0.f, 0.03f},
			{TEXT("red"), FLinearColor(0.7f, 0.06f, 0.05f), 0.5f, 0.f, 0.05f},
			{TEXT("green"), FLinearColor(0.08f, 0.45f, 0.12f), 0.5f, 0.f, 0.05f},
			{TEXT("blue"), FLinearColor(0.06f, 0.2f, 0.7f), 0.5f, 0.f, 0.05f},
		};
		return List;
	}

	const TArray<FString>& PaletteNames()
	{
		static const TArray<FString> Names = []()
		{
			TArray<FString> Out;
			for (const FPreset& P : Presets())
			{
				Out.Add(P.Name);
			}
			return Out;
		}();
		return Names;
	}

	FString ToSource(const TSharedRef<FJsonObject>& Recipe)
	{
		return TEXT("prim:") + RuntimeJson::Write(Recipe);
	}

	TSharedPtr<FJsonObject> FromSource(const FString& Source)
	{
		return Source.StartsWith(TEXT("prim:")) ? RuntimeJson::Parse(Source.Mid(5)) : nullptr;
	}

	// ---- helpers --------------------------------------------------------------------------------------------------

	static FVector ReadSize(const TSharedRef<FJsonObject>& R)
	{
		FVector Size(100.0);
		RuntimeJson::ToVector(R->TryGetField(TEXT("size")), Size);
		return Size.ComponentMax(FVector(0.1));
	}

	static int32 DetailFactor(const TSharedRef<FJsonObject>& R)
	{
		FString Detail = TEXT("medium");
		R->TryGetStringField(TEXT("detail"), Detail);
		return Detail == TEXT("low") ? 1 : Detail == TEXT("high") ? 4 : 2;
	}

	static bool ReadPoints2D(const TSharedRef<FJsonObject>& R, const TCHAR* Field, TArray<FVector2D>& Out, int32 MinPoints, FString& OutError)
	{
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (!R->TryGetArrayField(Field, Arr) || Arr->Num() < MinPoints)
		{
			OutError = FString::Printf(TEXT("%s needs at least %d [x,y] points"), Field, MinPoints);
			return false;
		}
		for (const TSharedPtr<FJsonValue>& V : *Arr)
		{
			const TArray<TSharedPtr<FJsonValue>>* P = nullptr;
			if (!V->TryGetArray(P) || P->Num() != 2)
			{
				OutError = FString::Printf(TEXT("%s points must be [x,y]"), Field);
				return false;
			}
			Out.Emplace((*P)[0]->AsNumber(), (*P)[1]->AsNumber());
		}
		return true;
	}

	/** Counter-clockwise order is required by the polygon extruders. */
	static void MakeCounterClockwise(TArray<FVector2D>& Poly)
	{
		double Area = 0.0;
		for (int32 i = 0; i < Poly.Num(); ++i)
		{
			const FVector2D& A = Poly[i];
			const FVector2D& B = Poly[(i + 1) % Poly.Num()];
			Area += A.X * B.Y - B.X * A.Y;
		}
		if (Area < 0.0)
		{
			Algo::Reverse(Poly);
		}
	}

	/** Maps a polygon drawn in XY (Y = up) onto the XZ plane and extrudes it symmetrically along Y. */
	static FTransform UprightPolygonFrame(double ZOffset)
	{
		// Rows are images of the basis vectors: X->X, Y->Z, Z->-Y (determinant +1, keeps winding).
		const FMatrix M(FPlane(1, 0, 0, 0), FPlane(0, 0, 1, 0), FPlane(0, -1, 0, 0), FPlane(0, 0, ZOffset, 1));
		return FTransform(M);
	}

	static TArray<FVector2D> ArchOutline(const FVector& Size, int32 Steps)
	{
		const double W = Size.X;
		const double H = Size.Z;
		const double R = W * 0.3;                          // opening radius
		const double Spring = FMath::Max(H - R - H * 0.15, 0.0); // where the arc starts
		TArray<FVector2D> P = {{-W / 2, 0}, {-R, 0}, {-R, Spring}};
		for (int32 i = 1; i < Steps; ++i)
		{
			const double A = PI - PI * i / Steps; // left -> right over the top
			P.Emplace(R * FMath::Cos(A), Spring + R * FMath::Sin(A));
		}
		P.Append({{R, Spring}, {R, 0}, {W / 2, 0}, {W / 2, H}, {-W / 2, H}});
		return P;
	}

	static EGeometryScriptPrimitiveOriginMode ReadOrigin(const TSharedRef<FJsonObject>& R)
	{
		FString Pivot = TEXT("base");
		R->TryGetStringField(TEXT("pivot"), Pivot);
		return Pivot == TEXT("center") ? EGeometryScriptPrimitiveOriginMode::Center : EGeometryScriptPrimitiveOriginMode::Base;
	}

	/** A square beam between two points (lattice members). */
	static void AppendBeam(UDynamicMesh* Mesh, const FVector& A, const FVector& B, double Thickness)
	{
		const FVector Dir = B - A;
		const double Length = Dir.Size();
		if (Length < 0.01)
		{
			return;
		}
		const FTransform T(FRotationMatrix::MakeFromX(Dir / Length).ToQuat(), (A + B) * 0.5);
		UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendBox(Mesh, FGeometryScriptPrimitiveOptions(), T, Length + Thickness, Thickness, Thickness, 0, 0, 0,
			EGeometryScriptPrimitiveOriginMode::Center);
	}

	static double Num(const TSharedRef<FJsonObject>& R, const TCHAR* Field, double Default)
	{
		double V = Default;
		R->TryGetNumberField(Field, V);
		return V;
	}

	/** Tapered truss: corner posts, a ring at every level and X-bracing on every face. */
	static void AppendLattice(UDynamicMesh* Mesh, const FVector& S, const TSharedRef<FJsonObject>& R)
	{
		const int32 Levels = FMath::Clamp(static_cast<int32>(Num(R, TEXT("levels"), 6)), 1, 64);
		const double Taper = FMath::Clamp(Num(R, TEXT("taper"), 1.0), 0.0, 4.0);
		const double Beam = Num(R, TEXT("beam"), FMath::Max(2.0, FMath::Min(S.X, S.Y) * 0.04));
		bool bBracing = true;
		R->TryGetBoolField(TEXT("bracing"), bBracing);
		auto Corner = [&](int32 Level, int32 K)
		{
			const double H = S.Z * Level / Levels;
			const double Scale = FMath::Lerp(1.0, Taper, H / S.Z);
			const double X = S.X * 0.5 * Scale * ((K == 0 || K == 3) ? -1.0 : 1.0);
			const double Y = S.Y * 0.5 * Scale * ((K < 2) ? -1.0 : 1.0);
			return FVector(X, Y, H);
		};
		for (int32 L = 0; L <= Levels; ++L)
		{
			for (int32 K = 0; K < 4; ++K)
			{
				const int32 N = (K + 1) % 4;
				if (Taper > 0.0 || L < Levels)
				{
					AppendBeam(Mesh, Corner(L, K), Corner(L, N), Beam);
				}
				if (L < Levels)
				{
					AppendBeam(Mesh, Corner(L, K), Corner(L + 1, K), Beam * 1.4);
					if (bBracing)
					{
						AppendBeam(Mesh, Corner(L, K), Corner(L + 1, N), Beam * 0.7);
						AppendBeam(Mesh, Corner(L, N), Corner(L + 1, K), Beam * 0.7);
					}
				}
			}
		}
	}

	/** Stacked blocks: "steps" keep the width and recede in depth; "tiers" shrink on both axes. */
	static void AppendStack(UDynamicMesh* Mesh, const FVector& S, const TSharedRef<FJsonObject>& R, bool bTiers)
	{
		const int32 Count = FMath::Clamp(static_cast<int32>(Num(R, TEXT("count"), bTiers ? 5 : 8)), 1, 200);
		const double Taper = FMath::Clamp(Num(R, TEXT("taper"), bTiers ? 0.4 : 0.0), 0.0, 1.0);
		const double StepH = S.Z / Count;
		for (int32 i = 0; i < Count; ++i)
		{
			const double A = Count > 1 ? static_cast<double>(i) / (Count - 1) : 0.0;
			if (bTiers)
			{
				const double Scale = FMath::Lerp(1.0, Taper, A);
				UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendBox(Mesh, FGeometryScriptPrimitiveOptions(), FTransform(FVector(0, 0, i * StepH)),
					S.X * Scale, S.Y * Scale, StepH, 0, 0, 0, EGeometryScriptPrimitiveOriginMode::Base);
			}
			else
			{
				const double Depth = S.Y * (Count - i) / Count;
				UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendBox(Mesh, FGeometryScriptPrimitiveOptions(), FTransform(FVector(0, S.Y / 2 - Depth / 2, i * StepH)),
					S.X, Depth, StepH, 0, 0, 0, EGeometryScriptPrimitiveOriginMode::Base);
			}
		}
	}

	/** Classical column: square plinth, torus, tapered shaft, echinus and abacus. */
	static void AppendColumn(UDynamicMesh* Mesh, const FVector& S, int32 D)
	{
		const FGeometryScriptPrimitiveOptions Opt;
		const double R = S.X / 2;
		const double H = S.Z;
		const double Plinth = H * 0.05, Torus = H * 0.03, Capital = H * 0.06, Abacus = H * 0.03;
		const double Shaft = H - Plinth - Torus - Capital - Abacus;
		UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendBox(Mesh, Opt, FTransform::Identity, S.X * 1.35, S.X * 1.35, Plinth, 0, 0, 0, EGeometryScriptPrimitiveOriginMode::Base);
		FGeometryScriptRevolveOptions Rev;
		UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendTorus(Mesh, Opt, FTransform(FVector(0, 0, Plinth)), Rev, R * 1.05, Torus / 2, 12 * D, 6 * D,
			EGeometryScriptPrimitiveOriginMode::Base);
		UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendCone(Mesh, Opt, FTransform(FVector(0, 0, Plinth + Torus)), R, R * 0.85, Shaft, 12 * D, 2, true,
			EGeometryScriptPrimitiveOriginMode::Base);
		UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendCone(Mesh, Opt, FTransform(FVector(0, 0, Plinth + Torus + Shaft)), R * 0.85, R * 1.25, Capital,
			12 * D, 1, true, EGeometryScriptPrimitiveOriginMode::Base);
		UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendBox(Mesh, Opt, FTransform(FVector(0, 0, H - Abacus)), S.X * 1.35, S.X * 1.35, Abacus, 0, 0, 0,
			EGeometryScriptPrimitiveOriginMode::Base);
	}

	/** Grid of window/door openings spanning a facade (X wide, Z high), Y deep: use it as a subtract tool. */
	static void AppendOpenings(UDynamicMesh* Mesh, const FVector& S, const TSharedRef<FJsonObject>& R, int32 D)
	{
		const int32 Rows = FMath::Clamp(static_cast<int32>(Num(R, TEXT("rows"), 3)), 1, 100);
		const int32 Cols = FMath::Clamp(static_cast<int32>(Num(R, TEXT("cols"), 5)), 1, 200);
		const double Margin = Num(R, TEXT("margin"), FMath::Min(S.X, S.Z) * 0.08);
		const double CellW = (S.X - 2 * Margin) / Cols;
		const double CellH = (S.Z - 2 * Margin) / Rows;
		FVector2D Opening(CellW * 0.55, CellH * 0.6);
		const TArray<TSharedPtr<FJsonValue>>* O = nullptr;
		if (R->TryGetArrayField(TEXT("opening"), O) && O->Num() == 2)
		{
			Opening = FVector2D((*O)[0]->AsNumber(), (*O)[1]->AsNumber());
		}
		bool bArched = false;
		R->TryGetBoolField(TEXT("arched"), bArched);
		const FGeometryScriptPrimitiveOptions Opt;
		for (int32 Row = 0; Row < Rows; ++Row)
		{
			for (int32 Col = 0; Col < Cols; ++Col)
			{
				const double X = -S.X / 2 + Margin + CellW * (Col + 0.5);
				const double Z = Margin + CellH * Row + (CellH - Opening.Y) / 2;
				const double RectH = bArched ? Opening.Y - Opening.X / 2 : Opening.Y;
				UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendBox(Mesh, Opt, FTransform(FVector(X, 0, Z)), Opening.X, S.Y, RectH, 0, 0, 0,
					EGeometryScriptPrimitiveOriginMode::Base);
				if (bArched)
				{
					// Half-round top: a cylinder lying along Y.
					const FTransform T(FRotator(0, 0, 90).Quaternion(), FVector(X, 0, Z + RectH));
					UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendCylinder(Mesh, Opt, T, Opening.X / 2, S.Y, 8 * D, 0, true,
						EGeometryScriptPrimitiveOriginMode::Center);
				}
			}
		}
	}

	static void ApplyBevel(UDynamicMesh* Mesh, const TSharedRef<FJsonObject>& R)
	{
		const double Bevel = Num(R, TEXT("bevel"), 0.0);
		if (Bevel <= 0.0)
		{
			return;
		}
		FGeometryScriptMeshBevelOptions Options;
		Options.BevelDistance = static_cast<float>(Bevel);
		Options.Subdivisions = 1;
		UGeometryScriptLibrary_MeshModelingFunctions::ApplyMeshPolygroupBevel(Mesh, Options);
	}

	static bool AppendShape(UDynamicMesh* Mesh, const TSharedRef<FJsonObject>& R, FString& OutError)
	{
		FString Shape;
		if (!R->TryGetStringField(TEXT("shape"), Shape))
		{
			OutError = TEXT("recipe has no shape");
			return false;
		}
		const FVector S = ReadSize(R);
		const int32 D = DetailFactor(R);
		const EGeometryScriptPrimitiveOriginMode Origin = ReadOrigin(R);
		const bool bCenter = Origin == EGeometryScriptPrimitiveOriginMode::Center;
		const FGeometryScriptPrimitiveOptions Opt;
		// Round shapes are built at X diameter and stretched to Y with scale (ellipses).
		const FTransform Stretch(FQuat::Identity, FVector::ZeroVector, FVector(1.0, S.Y / S.X, 1.0));

		if (Shape == TEXT("box"))
		{
			UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendBox(Mesh, Opt, FTransform::Identity, S.X, S.Y, S.Z, 0, 0, 0, Origin);
		}
		else if (Shape == TEXT("sphere"))
		{
			const FTransform T(FQuat::Identity, FVector::ZeroVector, FVector(1.0, S.Y / S.X, S.Z / S.X));
			UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendSphereLatLong(Mesh, Opt, T, S.X / 2, 8 * D, 12 * D, Origin);
		}
		else if (Shape == TEXT("cylinder"))
		{
			UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendCylinder(Mesh, Opt, Stretch, S.X / 2, S.Z, 12 * D, 0, true, Origin);
		}
		else if (Shape == TEXT("cone"))
		{
			double TopRatio = 0.0;
			R->TryGetNumberField(TEXT("top_ratio"), TopRatio);
			UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendCone(Mesh, Opt, Stretch, S.X / 2, S.X / 2 * FMath::Clamp(TopRatio, 0.0, 1.0), S.Z, 12 * D, 1, true, Origin);
		}
		else if (Shape == TEXT("pyramid"))
		{
			// Four-sided cone turned 45 degrees so the base is an X by Y rectangle.
			const FTransform T(FRotator(0, 45, 0).Quaternion(), FVector::ZeroVector, FVector(1.0));
			UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendCone(Mesh, Opt, T, S.X / 2 * UE_SQRT_2, 0.f, S.Z, 4, 1, true, Origin);
			if (!FMath::IsNearlyEqual(S.X, S.Y))
			{
				// Stretch to the Y size after the base rotation.
				Mesh->EditMesh([&S](UE::Geometry::FDynamicMesh3& M)
				{
					for (int32 V : M.VertexIndicesItr())
					{
						FVector3d P = M.GetVertex(V);
						P.Y *= S.Y / S.X;
						M.SetVertex(V, P);
					}
				});
			}
		}
		else if (Shape == TEXT("torus"))
		{
			const double Tube = FMath::Min(S.Z, S.X * 0.9);
			FGeometryScriptRevolveOptions Rev;
			UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendTorus(Mesh, Opt, Stretch, Rev, (S.X - Tube) / 2, Tube / 2, 16 * D, 8 * D, Origin);
		}
		else if (Shape == TEXT("capsule"))
		{
			UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendCapsule(Mesh, Opt, Stretch, S.X / 2, FMath::Max(S.Z - S.X, 0.0), 3 * D, 8 * D, 0, Origin);
		}
		else if (Shape == TEXT("plane"))
		{
			UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendRectangleXY(Mesh, Opt, FTransform::Identity, S.X, S.Y);
		}
		else if (Shape == TEXT("wedge"))
		{
			// Ramp: full height at -X, zero at +X.
			TArray<FVector2D> Poly = {{-S.X / 2, 0}, {S.X / 2, 0}, {-S.X / 2, S.Z}};
			MakeCounterClockwise(Poly);
			UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendSimpleExtrudePolygon(Mesh, Opt, UprightPolygonFrame(bCenter ? -S.Z / 2 : 0.0), Poly, S.Y, 0, true,
				EGeometryScriptPrimitiveOriginMode::Center);
		}
		else if (Shape == TEXT("arch"))
		{
			TArray<FVector2D> Poly = ArchOutline(S, 8 * D);
			MakeCounterClockwise(Poly);
			UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendSimpleExtrudePolygon(Mesh, Opt, UprightPolygonFrame(bCenter ? -S.Z / 2 : 0.0), Poly, S.Y, 0, true,
				EGeometryScriptPrimitiveOriginMode::Center);
		}
		else if (Shape == TEXT("roof"))
		{
			// Gable: ridge along Y at full height.
			TArray<FVector2D> Poly = {{-S.X / 2, 0}, {S.X / 2, 0}, {0, S.Z}};
			MakeCounterClockwise(Poly);
			UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendSimpleExtrudePolygon(Mesh, Opt, UprightPolygonFrame(bCenter ? -S.Z / 2 : 0.0), Poly, S.Y, 0, true,
				EGeometryScriptPrimitiveOriginMode::Center);
		}
		else if (Shape == TEXT("lattice"))
		{
			AppendLattice(Mesh, S, R);
		}
		else if (Shape == TEXT("steps") || Shape == TEXT("tiers"))
		{
			AppendStack(Mesh, S, R, Shape == TEXT("tiers"));
		}
		else if (Shape == TEXT("column"))
		{
			AppendColumn(Mesh, S, D);
		}
		else if (Shape == TEXT("openings"))
		{
			AppendOpenings(Mesh, S, R, D);
		}
		else if (Shape == TEXT("dome"))
		{
			// Quarter ellipse revolved around Z: radius X/2, height Z, flat base.
			TArray<FVector2D> Path;
			const int32 Steps = 8 * D;
			for (int32 i = 0; i <= Steps; ++i)
			{
				const double A = HALF_PI * i / Steps;
				Path.Emplace(S.X / 2 * FMath::Cos(A), S.Z * FMath::Sin(A));
			}
			FGeometryScriptRevolveOptions Rev;
			const FTransform T(FQuat::Identity, FVector(0, 0, bCenter ? -S.Z / 2 : 0.0), FVector(1.0, S.Y / S.X, 1.0));
			UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendRevolvePath(Mesh, Opt, T, Path, Rev, 16 * D, true);
		}
		else if (Shape == TEXT("tube"))
		{
			double Thickness = S.X * 0.1;
			R->TryGetNumberField(TEXT("thickness"), Thickness);
			Thickness = FMath::Clamp(Thickness, 0.1, S.X / 2 - 0.1);
			const FVector2D Ring[] = {{S.X / 2 - Thickness, 0}, {S.X / 2, 0}, {S.X / 2, S.Z}, {S.X / 2 - Thickness, S.Z}};
			TArray<FVector2D> Path = {Ring[0], Ring[1], Ring[2], Ring[3], Ring[0]};
			FGeometryScriptRevolveOptions Rev;
			const FTransform T(FQuat::Identity, FVector(0, 0, bCenter ? -S.Z / 2 : 0.0), FVector(1.0, S.Y / S.X, 1.0));
			UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendRevolvePath(Mesh, Opt, T, Path, Rev, 12 * D, false);
		}
		else if (Shape == TEXT("lathe"))
		{
			TArray<FVector2D> Profile;
			if (!ReadPoints2D(R, TEXT("profile"), Profile, 2, OutError))
			{
				return false;
			}
			double Angle = 360.0;
			R->TryGetNumberField(TEXT("angle"), Angle);
			FGeometryScriptRevolveOptions Rev;
			Rev.RevolveDegrees = static_cast<float>(FMath::Clamp(Angle, 1.0, 360.0));
			UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendRevolvePath(Mesh, Opt, FTransform::Identity, Profile, Rev, 16 * D, true);
		}
		else if (Shape == TEXT("extrude"))
		{
			TArray<FVector2D> Profile;
			if (!ReadPoints2D(R, TEXT("profile"), Profile, 3, OutError))
			{
				return false;
			}
			MakeCounterClockwise(Profile);
			double Height = S.Z;
			R->TryGetNumberField(TEXT("height"), Height);
			UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendSimpleExtrudePolygon(Mesh, Opt, FTransform::Identity, Profile, static_cast<float>(Height), 0, true, Origin);
		}
		else if (Shape == TEXT("sweep"))
		{
			TArray<FVector2D> Profile;
			if (!ReadPoints2D(R, TEXT("profile"), Profile, 3, OutError))
			{
				return false;
			}
			MakeCounterClockwise(Profile);
			const TArray<TSharedPtr<FJsonValue>>* PathArr = nullptr;
			if (!R->TryGetArrayField(TEXT("path"), PathArr) || PathArr->Num() < 2)
			{
				OutError = TEXT("sweep needs a path of at least 2 [x,y,z] points");
				return false;
			}
			TArray<FVector> Points;
			for (const TSharedPtr<FJsonValue>& V : *PathArr)
			{
				FVector P;
				if (!RuntimeJson::ToVector(V, P))
				{
					OutError = TEXT("path points must be [x,y,z]");
					return false;
				}
				Points.Add(P);
			}
			bool bClosed = false;
			R->TryGetBoolField(TEXT("closed"), bClosed);
			TArray<FTransform> Frames;
			for (int32 i = 0; i < Points.Num(); ++i)
			{
				const FVector Prev = Points[bClosed ? (i + Points.Num() - 1) % Points.Num() : FMath::Max(i - 1, 0)];
				const FVector Next = Points[bClosed ? (i + 1) % Points.Num() : FMath::Min(i + 1, Points.Num() - 1)];
				const FVector Tangent = (Next - Prev).GetSafeNormal();
				Frames.Emplace(FRotationMatrix::MakeFromX(Tangent.IsNearlyZero() ? FVector::ForwardVector : Tangent).ToQuat(), Points[i]);
			}
			UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendSweepPolygon(Mesh, Opt, FTransform::Identity, Profile, Frames, bClosed, !bClosed);
		}
		else
		{
			OutError = FString::Printf(TEXT("unknown shape '%s'"), *Shape);
			return false;
		}
		return true;
	}

	bool BuildMesh(UDynamicMesh* Mesh, const TSharedRef<FJsonObject>& Recipe, FString& OutError)
	{
		Mesh->Reset();
		if (!AppendShape(Mesh, Recipe, OutError))
		{
			return false;
		}
		ApplyBevel(Mesh, Recipe);
		const TArray<TSharedPtr<FJsonValue>>* Ops = nullptr;
		if (!Recipe->TryGetArrayField(TEXT("ops"), Ops))
		{
			return true;
		}
		if (Ops->Num() > MaxOps)
		{
			OutError = FString::Printf(TEXT("too many boolean ops (%d, max %d)"), Ops->Num(), MaxOps);
			return false;
		}
		for (const TSharedPtr<FJsonValue>& OpValue : *Ops)
		{
			const TSharedPtr<FJsonObject> Op = OpValue->AsObject();
			const TSharedPtr<FJsonObject>* ToolRecipe = nullptr;
			FString Kind;
			if (!Op.IsValid() || !Op->TryGetObjectField(TEXT("tool"), ToolRecipe) || !Op->TryGetStringField(TEXT("op"), Kind))
			{
				OutError = TEXT("boolean op needs op and tool");
				return false;
			}
			UDynamicMesh* Tool = NewObject<UDynamicMesh>();
			if (!BuildMesh(Tool, ToolRecipe->ToSharedRef(), OutError))
			{
				return false;
			}
			FTransform ToolTransform = FTransform::Identity;
			FVector V;
			FRotator Rot;
			if (RuntimeJson::ToVector(Op->TryGetField(TEXT("location")), V)) ToolTransform.SetLocation(V);
			if (RuntimeJson::ToRotator(Op->TryGetField(TEXT("rotation")), Rot)) ToolTransform.SetRotation(Rot.Quaternion());
			if (RuntimeJson::ToVector(Op->TryGetField(TEXT("scale")), V)) ToolTransform.SetScale3D(V);
			const EGeometryScriptBooleanOperation Operation = Kind == TEXT("union") ? EGeometryScriptBooleanOperation::Union
				: Kind == TEXT("intersect") ? EGeometryScriptBooleanOperation::Intersection : EGeometryScriptBooleanOperation::Subtract;
			FGeometryScriptMeshBooleanOptions Options;
			UGeometryScriptLibrary_MeshBooleanFunctions::ApplyMeshBoolean(Mesh, FTransform::Identity, Tool, ToolTransform, Operation, Options);
		}
		return true;
	}

	bool ApplyMaterial(AActor* Actor, const TSharedRef<FJsonObject>& Recipe, FString& OutError)
	{
		ADynamicMeshActor* MeshActor = Cast<ADynamicMeshActor>(Actor);
		UDynamicMeshComponent* Comp = MeshActor ? MeshActor->GetDynamicMeshComponent() : nullptr;
		if (!Comp)
		{
			OutError = TEXT("not a builder mesh");
			return false;
		}
		FString Material = TEXT("white");
		Recipe->TryGetStringField(TEXT("material"), Material);
		UMaterialInterface* Final = nullptr;
		if (const FPreset* Preset = Presets().FindByPredicate([&Material](const FPreset& P) { return P.Name == Material; }))
		{
			UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, Preset->bGlass ? GlassMaterialPath : PbrMaterialPath);
			const bool bPbr = Base != nullptr;
			if (!Base)
			{
				Base = LoadObject<UMaterialInterface>(nullptr, BasicMaterialPath); // plugin content not cooked
			}
			UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Base, Comp);
			FLinearColor Color = Preset->Color;
			FVector C;
			if (RuntimeJson::ToVector(Recipe->TryGetField(TEXT("color")), C))
			{
				Color = FLinearColor(C.X, C.Y, C.Z);
			}
			MID->SetVectorParameterValue(TEXT("Color"), Color);
			if (bPbr)
			{
				MID->SetScalarParameterValue(TEXT("Roughness"), static_cast<float>(Num(Recipe, TEXT("roughness"), Preset->Roughness)));
				MID->SetScalarParameterValue(TEXT("Metallic"), static_cast<float>(Num(Recipe, TEXT("metallic"), Preset->Metallic)));
				MID->SetScalarParameterValue(TEXT("Variation"), static_cast<float>(Num(Recipe, TEXT("variation"), Preset->Variation)));
				MID->SetScalarParameterValue(TEXT("Opacity"), static_cast<float>(Num(Recipe, TEXT("opacity"), 0.3)));
				FVector E;
				const bool bEmissiveColor = RuntimeJson::ToVector(Recipe->TryGetField(TEXT("emissive")), E);
				const double Strength = Num(Recipe, TEXT("emissive_strength"), bEmissiveColor ? 5.0 : Preset->Emissive);
				MID->SetVectorParameterValue(TEXT("Emissive"), bEmissiveColor ? FLinearColor(E.X, E.Y, E.Z) : Color);
				MID->SetScalarParameterValue(TEXT("EmissiveStrength"), static_cast<float>(Strength));
			}
			Final = MID;
		}
		else
		{
			bool bAllowed = URuntimeToolsetsSettings::Get().AllowedObjectRoots.Num() == 0;
			for (const FString& Root : URuntimeToolsetsSettings::Get().AllowedObjectRoots)
			{
				bAllowed |= Material.StartsWith(Root);
			}
			UMaterialInterface* Base = bAllowed ? LoadObject<UMaterialInterface>(nullptr, *Material) : nullptr;
			if (!Base)
			{
				OutError = FString::Printf(TEXT("material '%s' is not a preset (%s) or an allowed material path"), *Material,
					*FString::Join(PaletteNames(), TEXT(", ")));
				return false;
			}
			Final = Base;
			FVector C;
			if (RuntimeJson::ToVector(Recipe->TryGetField(TEXT("color")), C))
			{
				UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Base, Comp);
				MID->SetVectorParameterValue(TEXT("Color"), FLinearColor(C.X, C.Y, C.Z));
				Final = MID;
			}
		}
		for (int32 i = 0; i < FMath::Max(1, Comp->GetNumMaterials()); ++i)
		{
			Comp->SetMaterial(i, Final);
		}
		return true;
	}

	bool Rebuild(AActor* Actor, const TSharedRef<FJsonObject>& Recipe, FString& OutError)
	{
		ADynamicMeshActor* MeshActor = Cast<ADynamicMeshActor>(Actor);
		UDynamicMeshComponent* Comp = MeshActor ? MeshActor->GetDynamicMeshComponent() : nullptr;
		if (!Comp)
		{
			OutError = TEXT("not a builder mesh");
			return false;
		}
		if (!BuildMesh(Comp->GetDynamicMesh(), Recipe, OutError) || !ApplyMaterial(Actor, Recipe, OutError))
		{
			return false;
		}
		Comp->SetComplexAsSimpleCollisionEnabled(true, true);
		return true;
	}

	AActor* SpawnFromSource(UWorld* World, const FString& Source, const FTransform& Transform, const FString& Name, FString& OutError)
	{
		const TSharedPtr<FJsonObject> Recipe = FromSource(Source);
		if (!Recipe.IsValid())
		{
			OutError = TEXT("bad prim: recipe");
			return nullptr;
		}
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		if (!Name.IsEmpty())
		{
			FString Clean = Name;
			for (TCHAR& Ch : Clean)
			{
				if (!FChar::IsAlnum(Ch) && Ch != TEXT('_'))
				{
					Ch = TEXT('_');
				}
			}
			Params.Name = MakeUniqueObjectName(World->PersistentLevel, ADynamicMeshActor::StaticClass(), FName(*Clean));
		}
		ADynamicMeshActor* Actor = World->SpawnActor<ADynamicMeshActor>(ADynamicMeshActor::StaticClass(), Transform, Params);
		if (!Actor)
		{
			OutError = TEXT("could not spawn a dynamic mesh actor");
			return nullptr;
		}
		Actor->GetRootComponent()->SetMobility(EComponentMobility::Movable);
		Actor->GetDynamicMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
		if (!Rebuild(Actor, Recipe.ToSharedRef(), OutError))
		{
			Actor->Destroy();
			return nullptr;
		}
		return Actor;
	}

	int32 TriangleCount(const AActor* Actor)
	{
		const ADynamicMeshActor* MeshActor = Cast<ADynamicMeshActor>(Actor);
		UDynamicMeshComponent* Comp = MeshActor ? MeshActor->GetDynamicMeshComponent() : nullptr;
		UDynamicMesh* Mesh = Comp ? Comp->GetDynamicMesh() : nullptr;
		return Mesh ? Mesh->GetTriangleCount() : 0;
	}
}
