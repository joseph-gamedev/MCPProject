#include "PrimitiveMesh.h"
#include "RuntimeJson.h"
#include "RuntimeToolsetsSettings.h"
#include "Components/DynamicMeshComponent.h"
#include "DynamicMeshActor.h"
#include "Engine/World.h"
#include "AI/NavigationSystemBase.h"
#include "GeometryScript/CollisionFunctions.h"
#include "GeometryScript/MeshBasicEditFunctions.h"
#include "GeometryScript/MeshBooleanFunctions.h"
#include "GeometryScript/MeshQueryFunctions.h"
#include "GeometryScript/MeshMaterialFunctions.h"
#include "GeometryScript/MeshNormalsFunctions.h"
#include "CompGeom/PolygonTriangulation.h"
#include "GeometryScript/MeshModelingFunctions.h"
#include "GeometryScript/MeshPrimitiveFunctions.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UDynamicMesh.h"
#include "Algo/Reverse.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"

namespace RuntimePrimitives
{
	static const TCHAR* BasicMaterialPath = TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial");
	static const TCHAR* PbrMaterialPath = TEXT("/RuntimeToolsets/Materials/M_RT_PBR.M_RT_PBR");
	static const TCHAR* GlassMaterialPath = TEXT("/RuntimeToolsets/Materials/M_RT_Glass.M_RT_Glass");
	/** Scanned-texture master (project content, from Scripts/architecture/import_architecture.py); used when a preset has textures. */
	static const TCHAR* ScannedMaterialPath = TEXT("/Game/RuntimeArchitecture/Materials/M_RT_Scanned.M_RT_Scanned");
	static constexpr int32 MaxOps = 64;
	static constexpr int32 MaxParts = 512;
	static constexpr int32 MaxAssemblyTriangles = 400000;

	const TArray<FString>& ShapeNames()
	{
		static const TArray<FString> Names = {TEXT("box"), TEXT("sphere"), TEXT("cylinder"), TEXT("cone"), TEXT("pyramid"), TEXT("torus"), TEXT("wedge"),
			TEXT("capsule"), TEXT("plane"), TEXT("dome"), TEXT("tube"), TEXT("arch"), TEXT("roof"), TEXT("lattice"), TEXT("steps"), TEXT("tiers"),
			TEXT("column"), TEXT("openings"), TEXT("lathe"), TEXT("revolve"), TEXT("extrude"), TEXT("sweep"), TEXT("loft"), TEXT("raw"), TEXT("assembly")};
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
			{TEXT("roof_tile"), FLinearColor(0.5f, 0.2f, 0.12f), 0.75f, 0.f, 0.2f},
			{TEXT("cobblestone"), FLinearColor(0.38f, 0.36f, 0.33f), 0.85f, 0.f, 0.3f},
			{TEXT("paving"), FLinearColor(0.55f, 0.54f, 0.5f), 0.85f, 0.f, 0.15f},
			{TEXT("corrugated"), FLinearColor(0.5f, 0.51f, 0.53f), 0.5f, 0.6f, 0.1f},
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


	const TArray<FString>& StyleNames()
	{
		static const TArray<FString> Names = {TEXT("smooth"), TEXT("brick"), TEXT("blocks"), TEXT("marble"), TEXT("plaster"), TEXT("concrete"),
			TEXT("roof_tiles"), TEXT("planks"), TEXT("brushed"), TEXT("ground")};
		return Names;
	}

	const TArray<FString>& MaterialFieldNames()
	{
		static const TArray<FString> Names = {TEXT("material"), TEXT("color"), TEXT("roughness"), TEXT("metallic"), TEXT("emissive"),
			TEXT("emissive_strength"), TEXT("opacity"), TEXT("variation"), TEXT("style"), TEXT("tile"), TEXT("relief"), TEXT("weathering"), TEXT("scanned")};
		return Names;
	}

	struct FStyle
	{
		int32 Style;
		float Tile;   // pattern size in cm (brick length, block width, plank width, ...)
		float Relief; // bump depth in cm
	};

	static FStyle PresetStyle(const FString& Preset)
	{
		static const TMap<FString, FStyle> Map = {
			{TEXT("marble"), {3, 260.f, 0.05f}},
			{TEXT("sandstone"), {2, 110.f, 1.2f}},
			{TEXT("limestone"), {2, 140.f, 1.0f}},
			{TEXT("granite"), {2, 160.f, 0.6f}},
			{TEXT("stone"), {2, 70.f, 2.0f}},
			{TEXT("cobblestone"), {2, 22.f, 2.5f}},
			{TEXT("brick"), {1, 24.f, 0.8f}},
			{TEXT("terracotta"), {6, 28.f, 2.0f}},
			{TEXT("roof_tile"), {6, 30.f, 2.5f}},
			{TEXT("slate_roof"), {6, 24.f, 1.2f}},
			{TEXT("concrete"), {5, 240.f, 0.3f}},
			{TEXT("plaster"), {4, 150.f, 0.25f}},
			{TEXT("verdigris"), {4, 80.f, 0.3f}},
			{TEXT("asphalt"), {4, 40.f, 0.25f}},
			{TEXT("wood"), {7, 18.f, 0.4f}},
			{TEXT("copper"), {8, 60.f, 0.03f}},
			{TEXT("bronze"), {8, 60.f, 0.03f}},
			{TEXT("gold"), {8, 60.f, 0.02f}},
			{TEXT("steel"), {8, 60.f, 0.03f}},
			{TEXT("iron"), {8, 60.f, 0.05f}},
			{TEXT("painted_iron"), {4, 60.f, 0.05f}},
			{TEXT("grass"), {9, 250.f, 2.0f}},
			{TEXT("sand"), {9, 300.f, 1.5f}},
			{TEXT("paving"), {2, 50.f, 0.6f}},
			{TEXT("corrugated"), {8, 60.f, 0.05f}},
		};
		const FStyle* Found = Map.Find(Preset);
		return Found ? *Found : FStyle{0, 100.f, 0.f};
	}

	static FAssemblyStats GAssemblyStats;
	const FAssemblyStats& LastAssemblyStats() { return GAssemblyStats; }
	/** Collision boxes of the most recent assembly build, one per part (computed while building: no shape fitting). */
	static FKAggregateGeom GAssemblySimple;

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

	static void SplitNormals(UDynamicMesh* Mesh, float AngleDeg);

	/**
	 * Box with chamfered edges (24 points, 44 triangles; 8 points and 12 triangles when Bevel is 0), with outward
	 * triangles and one flat normal per triangle. Local space: centred in X/Y, Z from 0 (base pivot) or centred.
	 */
	static void ChamferBoxGeometry(const FVector& Size, bool bCenter, double Bevel, TArray<FVector>& OutVerts, TArray<FIntVector>& OutTris,
		TArray<FVector3f>& OutNormals)
	{
		const FVector H = Size * 0.5;
		const double B = FMath::Clamp(Bevel, 0.0, H.GetMin() * 0.45);
		const FVector Offset(0, 0, bCenter ? 0.0 : H.Z);
		int32 Index[24];
		for (int32& I : Index)
		{
			I = INDEX_NONE;
		}
		// Point on face Axis (0 x, 1 y, 2 z) near the corner with signs S.
		auto Id = [&](int32 Axis, const FIntVector& S) -> int32
		{
			const int32 Key = Axis * 8 + (S.X > 0 ? 1 : 0) + (S.Y > 0 ? 2 : 0) + (S.Z > 0 ? 4 : 0);
			if (Index[Key] == INDEX_NONE)
			{
				FVector V;
				for (int32 c = 0; c < 3; ++c)
				{
					V[c] = S[c] * (c == Axis ? H[c] : H[c] - B);
				}
				Index[Key] = OutVerts.Add(V + Offset);
			}
			return Index[Key];
		};
		auto Tri = [&](int32 A, int32 Bv, int32 C)
		{
			const FVector& PA = OutVerts[A];
			const FVector& PB = OutVerts[Bv];
			const FVector& PC = OutVerts[C];
			const FVector Cross = FVector::CrossProduct(PB - PA, PC - PA);
			if (Cross.SizeSquared() < 1e-8)
			{
				return;  // degenerate (no bevel): edge strips and corners collapse
			}
			// Convex: the stored winding has its cross product pointing inward (UE is left-handed).
			const bool bFlip = FVector::DotProduct(Cross, (PA + PB + PC) / 3.0 - Offset) > 0.0;
			OutTris.Add(bFlip ? FIntVector(A, C, Bv) : FIntVector(A, Bv, C));
			const FVector Out = bFlip ? Cross.GetSafeNormal() : -Cross.GetSafeNormal();
			OutNormals.Add(FVector3f(Out));  // outward
		};
		auto Quad = [&](int32 A, int32 Bv, int32 C, int32 D) { Tri(A, Bv, C); Tri(A, C, D); };
		const int32 Cyc[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			const int32 U = (Axis + 1) % 3, V = (Axis + 2) % 3;
			for (int32 Sign : {-1, 1})
			{
				int32 Q[4];
				for (int32 k = 0; k < 4; ++k)
				{
					FIntVector S;
					S[Axis] = Sign;
					S[U] = Cyc[k][0];
					S[V] = Cyc[k][1];
					Q[k] = Id(Axis, S);
				}
				Quad(Q[0], Q[1], Q[2], Q[3]);  // main face
			}
			if (B > 0.0)
			{
				for (int32 k = 0; k < 4; ++k)  // edges between faces U and V run along Axis
				{
					FIntVector Lo, Hi;
					Lo[Axis] = -1;
					Hi[Axis] = 1;
					Lo[U] = Hi[U] = Cyc[k][0];
					Lo[V] = Hi[V] = Cyc[k][1];
					Quad(Id(U, Lo), Id(V, Lo), Id(V, Hi), Id(U, Hi));
				}
			}
		}
		if (B > 0.0)
		{
			for (int32 c = 0; c < 8; ++c)
			{
				const FIntVector S(c & 1 ? 1 : -1, c & 2 ? 1 : -1, c & 4 ? 1 : -1);
				Tri(Id(0, S), Id(1, S), Id(2, S));  // corner
			}
		}
	}

	/** Appends a (chamfered) box, transformed, into M with flat normals and a material id: no intermediate meshes. */
	static void AppendBoxDirect(UE::Geometry::FDynamicMesh3& M, const FVector& Size, bool bCenter, double Bevel, const FTransform& T, int32 MaterialId,
		TArray<int32>* TriPart, int32 PartIndex)
	{
		TArray<FVector> Verts;
		TArray<FIntVector> Tris;
		TArray<FVector3f> Normals;
		ChamferBoxGeometry(Size, bCenter, Bevel, Verts, Tris, Normals);
		const FVector InvScale = FVector(1.0) / T.GetScale3D().ComponentMax(FVector(1e-4));
		const bool bMirror = T.GetDeterminant() < 0.0;
		TArray<int32, TInlineAllocator<24>> Ids;
		for (const FVector& P : Verts)
		{
			Ids.Add(M.AppendVertex(FVector3d(T.TransformPosition(P))));
		}
		UE::Geometry::FDynamicMeshNormalOverlay* NormalOverlay = M.Attributes()->PrimaryNormals();
		UE::Geometry::FDynamicMeshMaterialAttribute* MatIds = M.Attributes()->GetMaterialID();
		for (int32 i = 0; i < Tris.Num(); ++i)
		{
			const FIntVector& Tr = Tris[i];
			const int32 Tid = M.AppendTriangle(bMirror ? UE::Geometry::FIndex3i(Ids[Tr.X], Ids[Tr.Z], Ids[Tr.Y]) : UE::Geometry::FIndex3i(Ids[Tr.X], Ids[Tr.Y], Ids[Tr.Z]));
			if (Tid < 0)
			{
				continue;
			}
			const FVector3f N(T.GetRotation().RotateVector(FVector(Normals[i]) * InvScale).GetSafeNormal());
			NormalOverlay->SetTriangle(Tid, UE::Geometry::FIndex3i(NormalOverlay->AppendElement(N), NormalOverlay->AppendElement(N), NormalOverlay->AppendElement(N)));
			if (MatIds)
			{
				MatIds->SetValue(Tid, MaterialId);
			}
			if (TriPart)
			{
				TriPart->SetNum(FMath::Max(TriPart->Num(), Tid + 1));
				(*TriPart)[Tid] = PartIndex;
			}
		}
	}

	/** Box with chamfered edges built directly (24 points, 44 triangles): far cheaper than a polygroup bevel. */
	static void AppendChamferBox(UDynamicMesh* Mesh, const FVector& Size, bool bCenter, double Bevel)
	{
		Mesh->EditMesh([&](UE::Geometry::FDynamicMesh3& M)
		{
			M.EnableAttributes();
			AppendBoxDirect(M, Size, bCenter, Bevel, FTransform::Identity, 0, nullptr, 0);
		});
	}

	/** The bevel a box gets: the recipe's value, or automatic (small chamfer; none on small trims). */
	static double BoxBevel(const TSharedRef<FJsonObject>& R, const FVector& Size)
	{
		double Bevel = -1.0;
		R->TryGetNumberField(TEXT("bevel"), Bevel);
		if (Bevel >= 0.0)
		{
			return Bevel;
		}
		const double Thin = Size.GetMin();
		return Thin > 2.0 && Size.GetMax() >= 40.0 ? FMath::Clamp(Thin * 0.04, 0.3, 1.5) : 0.0;
	}

	static void ApplyBevel(UDynamicMesh* Mesh, const TSharedRef<FJsonObject>& R)
	{
		// Perfectly sharp edges are the strongest "CG" tell: flat-sided shapes get a small chamfer that catches light
		// unless the recipe says otherwise ("bevel": 0 turns it off).
		double Bevel = Num(R, TEXT("bevel"), -1.0);
		if (Bevel < 0.0)
		{
			static const TSet<FString> Flat = {TEXT("box"), TEXT("wedge"), TEXT("extrude"), TEXT("steps"), TEXT("tiers"), TEXT("frustum")};
			FString Shape;
			R->TryGetStringField(TEXT("shape"), Shape);
			Bevel = 0.0;
			if (Flat.Contains(Shape))
			{
				const FBox B = UGeometryScriptLibrary_MeshQueryFunctions::GetMeshBoundingBox(Mesh);
				const double Thin = B.IsValid ? B.GetSize().GetMin() : 0.0;
				const double Long = B.IsValid ? B.GetSize().GetMax() : 0.0;
				// small trims and frames are too small to show it: keep them cheap
				Bevel = Thin > 2.0 && Long >= 40.0 ? FMath::Clamp(Thin * 0.04, 0.3, 1.5) : 0.0;
			}
		}
		if (Bevel <= 0.0)
		{
			return;
		}
		FString BoxShape;
		R->TryGetStringField(TEXT("shape"), BoxShape);
		if (BoxShape == TEXT("box") && !R->HasField(TEXT("ops")))
		{
			Mesh->Reset();
			AppendChamferBox(Mesh, ReadSize(R), ReadOrigin(R) == EGeometryScriptPrimitiveOriginMode::Center, Bevel);
			return;
		}
		FGeometryScriptMeshBevelOptions Options;
		Options.BevelDistance = static_cast<float>(Bevel);
		Options.Subdivisions = 0;  // one chamfer face per edge catches the light; rounder bevels cost triangles
		UGeometryScriptLibrary_MeshModelingFunctions::ApplyMeshPolygroupBevel(Mesh, Options);
	}

	static bool ReadPoints3D(const TSharedPtr<FJsonValue>& Value, TArray<FVector>& Out, int32 MinPoints, int32 MaxPoints, const TCHAR* What, FString& OutError)
	{
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (!Value.IsValid() || !Value->TryGetArray(Arr) || Arr->Num() < MinPoints || Arr->Num() > MaxPoints)
		{
			OutError = FString::Printf(TEXT("%s needs %d-%d [x,y,z] points"), What, MinPoints, MaxPoints);
			return false;
		}
		Out.Reset(Arr->Num());
		for (const TSharedPtr<FJsonValue>& V : *Arr)
		{
			FVector P;
			if (!RuntimeJson::ToVector(V, P))
			{
				OutError = FString::Printf(TEXT("%s points must be [x,y,z]"), What);
				return false;
			}
			Out.Add(P);
		}
		return true;
	}

	static void SplitNormals(UDynamicMesh* Mesh, float AngleDeg)
	{
		FGeometryScriptSplitNormalsOptions Split;
		Split.OpeningAngleDeg = AngleDeg;
		UGeometryScriptLibrary_MeshNormalsFunctions::ComputeSplitNormals(Mesh, Split, FGeometryScriptCalculateNormalsOptions());
	}

	/** Rings with the same point count joined into a surface, with optional flat caps. Faces point outward. */
	static void AppendRings(UDynamicMesh* Mesh, const TArray<TArray<FVector>>& Rings, bool bClosedRing, bool bCap, float SmoothAngle)
	{
		const int32 N = Rings[0].Num();
		const int32 NumRings = Rings.Num();
		TArray<FVector> Centers;
		for (const TArray<FVector>& Ring : Rings)
		{
			FVector C = FVector::ZeroVector;
			for (const FVector& P : Ring)
			{
				C += P;
			}
			Centers.Add(C / Ring.Num());
		}
		Mesh->EditMesh([&](UE::Geometry::FDynamicMesh3& M)
		{
			TArray<TArray<int32>> Ids;
			for (const TArray<FVector>& Ring : Rings)
			{
				TArray<int32>& Row = Ids.AddDefaulted_GetRef();
				for (const FVector& P : Ring)
				{
					Row.Add(M.AppendVertex(FVector3d(P)));
				}
			}
			const int32 Segs = bClosedRing ? N : N - 1;
			// Orientation: the first non-degenerate side quad must face away from the axis through the ring centres.
			bool bFlip = false;
			for (int32 r = 0; r < NumRings - 1 && !bFlip; ++r)
			{
				bool bFound = false;
				for (int32 i = 0; i < Segs; ++i)
				{
					const FVector& A = Rings[r][i];
					const FVector& B = Rings[r][(i + 1) % N];
					const FVector& C = Rings[r + 1][(i + 1) % N];
					const FVector Nrm = FVector::CrossProduct(B - A, C - A);
					if (Nrm.SizeSquared() > 1e-8)
					{
						const FVector Out = (A + B + C) / 3.0 - (Centers[r] + Centers[r + 1]) * 0.5;
						bFlip = FVector::DotProduct(Nrm, Out) < 0.0;
						bFound = true;
						break;
					}
				}
				if (bFound)
				{
					break;
				}
			}
			auto Tri = [&M](int32 A, int32 B, int32 C, bool bReverse)
			{
				M.AppendTriangle(bReverse ? UE::Geometry::FIndex3i(A, C, B) : UE::Geometry::FIndex3i(A, B, C));
			};
			for (int32 r = 0; r < NumRings - 1; ++r)
			{
				for (int32 i = 0; i < Segs; ++i)
				{
					const int32 j = (i + 1) % N;
					Tri(Ids[r][i], Ids[r][j], Ids[r + 1][j], bFlip);
					Tri(Ids[r][i], Ids[r + 1][j], Ids[r + 1][i], bFlip);
				}
			}
			if (bCap && bClosedRing && N >= 3 && NumRings >= 2)
			{
				for (const int32 End : {0, NumRings - 1})
				{
					TArray<FVector3d> Poly;
					for (const FVector& P : Rings[End])
					{
						Poly.Add(FVector3d(P));
					}
					TArray<UE::Geometry::FIndex3i> Tris;
					::PolygonTriangulation::TriangulateSimplePolygon(Poly, Tris, false);
					const FVector Away = Centers[End] - Centers[End == 0 ? 1 : NumRings - 2];
					for (const UE::Geometry::FIndex3i& T : Tris)
					{
						const FVector Nrm = FVector::CrossProduct(Poly[T.B] - Poly[T.A], Poly[T.C] - Poly[T.A]);
						Tri(Ids[End][T.A], Ids[End][T.B], Ids[End][T.C], FVector::DotProduct(Nrm, Away) < 0.0);
					}
				}
			}
		});
		SplitNormals(Mesh, SmoothAngle);
	}

	static bool AppendLoft(UDynamicMesh* Mesh, const TSharedRef<FJsonObject>& R, FString& OutError)
	{
		const TArray<TSharedPtr<FJsonValue>>* Sections = nullptr;
		if (!R->TryGetArrayField(TEXT("sections"), Sections) || Sections->Num() < 2 || Sections->Num() > 256)
		{
			OutError = TEXT("loft needs 2-256 sections (rings of [x,y,z] points, same count each)");
			return false;
		}
		TArray<TArray<FVector>> Rings;
		for (const TSharedPtr<FJsonValue>& V : *Sections)
		{
			if (!ReadPoints3D(V, Rings.AddDefaulted_GetRef(), 2, 512, TEXT("loft section"), OutError))
			{
				return false;
			}
			if (Rings.Last().Num() != Rings[0].Num())
			{
				OutError = TEXT("all loft sections need the same number of points");
				return false;
			}
		}
		bool bClosed = true, bCap = true;
		R->TryGetBoolField(TEXT("closed"), bClosed);
		R->TryGetBoolField(TEXT("cap"), bCap);
		AppendRings(Mesh, Rings, bClosed, bCap, static_cast<float>(Num(R, TEXT("smooth_angle"), 40.0)));
		return true;
	}

	static bool AppendRaw(UDynamicMesh* Mesh, const TSharedRef<FJsonObject>& R, FString& OutError)
	{
		TArray<FVector> Verts;
		if (!ReadPoints3D(R->TryGetField(TEXT("vertices")), Verts, 3, 50000, TEXT("raw vertices"), OutError))
		{
			return false;
		}
		const TArray<TSharedPtr<FJsonValue>>* Tris = nullptr;
		if (!R->TryGetArrayField(TEXT("triangles"), Tris) || Tris->Num() == 0 || Tris->Num() > 100000)
		{
			OutError = TEXT("raw needs 1-100000 triangles [a,b,c] (vertex indices, counter-clockwise seen from outside)");
			return false;
		}
		TArray<UE::Geometry::FIndex3i> Indices;
		for (const TSharedPtr<FJsonValue>& T : *Tris)
		{
			const TArray<TSharedPtr<FJsonValue>>* Idx = nullptr;
			if (!T->TryGetArray(Idx) || Idx->Num() != 3)
			{
				OutError = TEXT("raw triangles must be [a,b,c]");
				return false;
			}
			UE::Geometry::FIndex3i I((*Idx)[0]->AsNumber(), (*Idx)[1]->AsNumber(), (*Idx)[2]->AsNumber());
			if (!Verts.IsValidIndex(I.A) || !Verts.IsValidIndex(I.B) || !Verts.IsValidIndex(I.C))
			{
				OutError = TEXT("raw triangle index out of range");
				return false;
			}
			Indices.Add(I);
		}
		Mesh->EditMesh([&](UE::Geometry::FDynamicMesh3& M)
		{
			TArray<int32> Ids;
			for (const FVector& P : Verts)
			{
				Ids.Add(M.AppendVertex(FVector3d(P)));
			}
			for (const UE::Geometry::FIndex3i& I : Indices)
			{
				// UE is left-handed: flip so counter-clockwise (seen from outside) faces outward.
				M.AppendTriangle(UE::Geometry::FIndex3i(Ids[I.A], Ids[I.C], Ids[I.B]));
			}
		});
		SplitNormals(Mesh, static_cast<float>(Num(R, TEXT("smooth_angle"), 40.0)));
		return true;
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
		else if (Shape == TEXT("lathe") || Shape == TEXT("revolve"))
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
			const int32 Segments = FMath::Clamp(static_cast<int32>(Num(R, TEXT("segments"), 16 * D)), 3, 256);
			const FTransform T(FRotator(0, Num(R, TEXT("start_angle"), 0.0), 0).Quaternion());
			UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendRevolvePath(Mesh, Opt, T, Profile, Rev, Segments, true);
		}
		else if (Shape == TEXT("loft"))
		{
			return AppendLoft(Mesh, R, OutError);
		}
		else if (Shape == TEXT("raw"))
		{
			return AppendRaw(Mesh, R, OutError);
		}
		else if (Shape == TEXT("extrude") && R->HasField(TEXT("direction")))
		{
			// Planar 3D outline pushed along a direction (a panel on a facade, a slanted slab).
			TArray<FVector> Profile;
			FVector Dir;
			if (!ReadPoints3D(R->TryGetField(TEXT("profile")), Profile, 3, 512, TEXT("extrude profile"), OutError))
			{
				return false;
			}
			if (!RuntimeJson::ToVector(R->TryGetField(TEXT("direction")), Dir) || Dir.IsNearlyZero())
			{
				OutError = TEXT("extrude direction must be a non-zero [x,y,z]");
				return false;
			}
			TArray<FVector> Top = Profile;
			for (FVector& P : Top)
			{
				P += Dir;
			}
			bool bCap = true;
			R->TryGetBoolField(TEXT("cap"), bCap);
			AppendRings(Mesh, {Profile, Top}, true, bCap, 20.f);
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

	/** Coplanar faces of different parts that overlap and face the same way z-fight. Later parts are nudged outward. */
	struct FCoplanarTri
	{
		FVector3d N;
		double D = 0.0;
		FVector3d A, B, C;
		FBox Box;
		int32 Part = -1;
		int32 Level = 0;
	};

	static uint64 PlaneKey(const FVector3d& N, double D, int32 DOffset)
	{
		const uint64 Nx = static_cast<uint64>(FMath::RoundToInt64(N.X * 50.0) + 64);
		const uint64 Ny = static_cast<uint64>(FMath::RoundToInt64(N.Y * 50.0) + 64);
		const uint64 Nz = static_cast<uint64>(FMath::RoundToInt64(N.Z * 50.0) + 64);
		const uint64 Dq = static_cast<uint64>(FMath::RoundToInt64(D / 0.5) + DOffset + (int64(1) << 38)) & ((uint64(1) << 40) - 1);
		return (Nx << 56) | (Ny << 48) | (Nz << 40) | Dq;
	}

	static bool OverlapCoplanar(const FCoplanarTri& Q, const FCoplanarTri& R)
	{
		const FVector3d An = R.N.GetAbs();
		const int32 Drop = An.X > An.Y ? (An.X > An.Z ? 0 : 2) : (An.Y > An.Z ? 1 : 2);
		auto P2 = [Drop](const FVector3d& P) { return Drop == 0 ? FVector2d(P.Y, P.Z) : Drop == 1 ? FVector2d(P.X, P.Z) : FVector2d(P.X, P.Y); };
		const FVector2d T1[3] = {P2(Q.A), P2(Q.B), P2(Q.C)};
		const FVector2d T2[3] = {P2(R.A), P2(R.B), P2(R.C)};
		auto Separated = [](const FVector2d* X, const FVector2d* Y)
		{
			for (int32 e = 0; e < 3; ++e)
			{
				const FVector2d Edge = X[(e + 1) % 3] - X[e];
				const FVector2d Axis(-Edge.Y, Edge.X);
				const double Len = Axis.Size();
				if (Len < 1e-9)
				{
					continue;
				}
				double MinX = DBL_MAX, MaxX = -DBL_MAX, MinY = DBL_MAX, MaxY = -DBL_MAX;
				for (int32 k = 0; k < 3; ++k)
				{
					const double PX = FVector2d::DotProduct(X[k], Axis), PY = FVector2d::DotProduct(Y[k], Axis);
					MinX = FMath::Min(MinX, PX); MaxX = FMath::Max(MaxX, PX);
					MinY = FMath::Min(MinY, PY); MaxY = FMath::Max(MaxY, PY);
				}
				const double Tol = 0.05 * Len; // touching edges are not overlaps
				if (MaxX <= MinY + Tol || MaxY <= MinX + Tol)
				{
					return true;
				}
			}
			return false;
		};
		return !Separated(T1, T2) && !Separated(T2, T1);
	}

	static int32 SeparateCoplanar(UE::Geometry::FDynamicMesh3& M, const TArray<int32>& TriPart)
	{
		constexpr double Step = 0.3; // cm per layer: well above depth precision at a few km
		TArray<FCoplanarTri> Tris;
		Tris.SetNum(M.MaxTriangleID());
		TMap<uint64, TArray<int32>> Buckets;
		for (const int32 T : M.TriangleIndicesItr())
		{
			FCoplanarTri& R = Tris[T];
			M.GetTriVertices(T, R.A, R.B, R.C);
			const FVector3d Cross = FVector3d::CrossProduct(R.B - R.A, R.C - R.A);
			const double Len = Cross.Size();
			if (Len < 1e-6 || !TriPart.IsValidIndex(T))
			{
				continue;
			}
			R.N = Cross / Len;
			R.D = FVector3d::DotProduct(R.N, R.A);
			R.Box = FBox(ForceInit);
			R.Box += R.A;
			R.Box += R.B;
			R.Box += R.C;
			R.Box = R.Box.ExpandBy(0.01);
			R.Part = TriPart[T];
			Buckets.FindOrAdd(PlaneKey(R.N, R.D, 0)).Add(T);
		}
		int32 Moved = 0;
		for (int32 T = 0; T < Tris.Num(); ++T)
		{
			FCoplanarTri& R = Tris[T];
			if (R.Part < 0)
			{
				continue;
			}
			int32 MaxLevel = -1;
			for (int32 Off = -1; Off <= 1; ++Off)
			{
				const TArray<int32>* Cands = Buckets.Find(PlaneKey(R.N, R.D, Off));
				if (!Cands)
				{
					continue;
				}
				for (const int32 O : *Cands)
				{
					if (O >= T)
					{
						break; // buckets are in triangle order, which is part order
					}
					const FCoplanarTri& Q = Tris[O];
					if (Q.Part >= R.Part || FVector3d::DotProduct(Q.N, R.N) < 0.999 || FMath::Abs(Q.D - R.D) > 0.3 || !Q.Box.Intersect(R.Box)
						|| !OverlapCoplanar(Q, R))
					{
						continue;
					}
					MaxLevel = FMath::Max(MaxLevel, Q.Level);
				}
			}
			if (MaxLevel >= 0)
			{
				R.Level = MaxLevel + 1;
				++Moved;
			}
		}
		if (Moved == 0)
		{
			return 0;
		}
		// Push each vertex out along every nudged face normal it belongs to (max level per normal).
		TMap<int32, TMap<uint64, TPair<FVector3d, int32>>> Offsets;
		for (int32 T = 0; T < Tris.Num(); ++T)
		{
			const FCoplanarTri& R = Tris[T];
			if (R.Level <= 0)
			{
				continue;
			}
			const UE::Geometry::FIndex3i V = M.GetTriangle(T);
			for (int32 k = 0; k < 3; ++k)
			{
				TPair<FVector3d, int32>& E = Offsets.FindOrAdd(V[k]).FindOrAdd(PlaneKey(R.N, 0.0, 0), TPair<FVector3d, int32>(R.N, 0));
				E.Value = FMath::Max(E.Value, R.Level);
			}
		}
		for (const TPair<int32, TMap<uint64, TPair<FVector3d, int32>>>& Vert : Offsets)
		{
			FVector3d P = M.GetVertex(Vert.Key);
			for (const TPair<uint64, TPair<FVector3d, int32>>& E : Vert.Value)
			{
				P += E.Value.Key * (E.Value.Value * Step);
			}
			M.SetVertex(Vert.Key, P);
		}
		return Moved;
	}

	/** Effective material fields of each part (part fields override the assembly's) and the unique set. */
	static void CollectAssemblyMaterials(const TSharedRef<FJsonObject>& Recipe, TArray<TSharedRef<FJsonObject>>& OutMaterials, TArray<int32>& OutPartSlot)
	{
		TMap<FString, int32> Seen;
		const TArray<TSharedPtr<FJsonValue>>* Parts = nullptr;
		if (!Recipe->TryGetArrayField(TEXT("parts"), Parts))
		{
			return;
		}
		for (const TSharedPtr<FJsonValue>& V : *Parts)
		{
			const TSharedPtr<FJsonObject>* Part = nullptr;
			V->TryGetObject(Part);
			TSharedRef<FJsonObject> M = MakeShared<FJsonObject>();
			for (const FString& F : MaterialFieldNames())
			{
				TSharedPtr<FJsonValue> Val = Part ? (*Part)->TryGetField(F) : nullptr;
				if (!Val.IsValid())
				{
					Val = Recipe->TryGetField(F);
				}
				if (Val.IsValid())
				{
					M->SetField(F, Val);
				}
			}
			const FString Key = RuntimeJson::Write(M);
			if (const int32* Slot = Seen.Find(Key))
			{
				OutPartSlot.Add(*Slot);
				continue;
			}
			OutPartSlot.Add(OutMaterials.Num());
			Seen.Add(Key, OutMaterials.Num());
			OutMaterials.Add(M);
		}
	}

	static bool IsAssembly(const TSharedRef<FJsonObject>& Recipe)
	{
		FString Shape;
		return Recipe->TryGetStringField(TEXT("shape"), Shape) && Shape == TEXT("assembly");
	}

	static bool BuildAssembly(UDynamicMesh* Mesh, const TSharedRef<FJsonObject>& Recipe, FString& OutError)
	{
		const TArray<TSharedPtr<FJsonValue>>* Parts = nullptr;
		if (!Recipe->TryGetArrayField(TEXT("parts"), Parts) || Parts->Num() == 0 || Parts->Num() > MaxParts)
		{
			OutError = FString::Printf(TEXT("assembly needs 1-%d parts"), MaxParts);
			return false;
		}
		GAssemblySimple.EmptyElements();
		TArray<TSharedRef<FJsonObject>> Materials;
		TArray<int32> PartSlot;
		CollectAssemblyMaterials(Recipe, Materials, PartSlot);
		UDynamicMesh* Accum = NewObject<UDynamicMesh>();
		Accum->EditMesh([](UE::Geometry::FDynamicMesh3& M) { M.EnableAttributes(); }, EDynamicMeshChangeType::GeneralEdit, EDynamicMeshAttributeChangeFlags::Unknown, true);
		UGeometryScriptLibrary_MeshMaterialFunctions::EnableMaterialIDs(Accum);
		UDynamicMesh* Temp = NewObject<UDynamicMesh>();
		TArray<int32> TriPart;
		for (int32 i = 0; i < Parts->Num(); ++i)
		{
			const TSharedPtr<FJsonObject>* PartObj = nullptr;
			if (!(*Parts)[i]->TryGetObject(PartObj))
			{
				OutError = FString::Printf(TEXT("part %d is not an object"), i);
				return false;
			}
			const TSharedRef<FJsonObject> Part = PartObj->ToSharedRef();
			FString Shape;
			Part->TryGetStringField(TEXT("shape"), Shape);
			if (Shape == TEXT("assembly"))
			{
				OutError = FString::Printf(TEXT("part %d: assemblies cannot be nested"), i);
				return false;
			}
			FTransform T = FTransform::Identity;
			FVector V;
			FRotator Rot;
			if (RuntimeJson::ToVector(Part->TryGetField(TEXT("location")), V)) T.SetLocation(V);
			if (RuntimeJson::ToRotator(Part->TryGetField(TEXT("rotation")), Rot)) T.SetRotation(Rot.Quaternion());
			if (RuntimeJson::ToVector(Part->TryGetField(TEXT("scale")), V)) T.SetScale3D(V);
			// Fast path for plain boxes (most parts): triangles go straight into the result.
			if (Shape == TEXT("box") && !Part->HasField(TEXT("ops")))
			{
				const FVector Size = ReadSize(Part);
				const bool bCenter = ReadOrigin(Part) == EGeometryScriptPrimitiveOriginMode::Center;
				Accum->EditMesh([&](UE::Geometry::FDynamicMesh3& M)
				{
					AppendBoxDirect(M, Size, bCenter, BoxBevel(Part, Size), T, PartSlot[i], &TriPart, i);
				}, EDynamicMeshChangeType::GeneralEdit, EDynamicMeshAttributeChangeFlags::Unknown, true);
				const FVector World = Size * T.GetScale3D().GetAbs();
				if (World.GetMax() >= 20.0 && GAssemblySimple.BoxElems.Num() < 1024)
				{
					FKBoxElem Box(World.X, World.Y, World.Z);
					Box.Center = T.TransformPosition(FVector(0, 0, bCenter ? 0.0 : Size.Z * 0.5));
					Box.Rotation = T.Rotator();
					GAssemblySimple.BoxElems.Add(Box);
				}
				if (Accum->GetTriangleCount() > MaxAssemblyTriangles)
				{
					OutError = FString::Printf(TEXT("assembly exceeds %d triangles at part %d; lower detail/segments or split it"), MaxAssemblyTriangles, i);
					return false;
				}
				continue;
			}
			FString PartError;
			if (!BuildMesh(Temp, Part, PartError))
			{
				OutError = FString::Printf(TEXT("part %d (%s): %s"), i, *Shape, *PartError);
				return false;
			}
			UGeometryScriptLibrary_MeshMaterialFunctions::ClearMaterialIDs(Temp, PartSlot[i]);
			// Collision: the part's own box, placed like the part. Small trims (frames, sills) need none.
			const UE::Geometry::FAxisAlignedBox3d PartBox = Temp->GetMeshRef().GetBounds();
			if (!PartBox.IsEmpty() && GAssemblySimple.BoxElems.Num() < 1024)
			{
				const FVector Size = FVector(PartBox.Diagonal()) * T.GetScale3D().GetAbs();
				if (Size.GetMax() >= 20.0)
				{
					FKBoxElem Box(Size.X, Size.Y, Size.Z);
					Box.Center = T.TransformPosition(FVector(PartBox.Center()));
					Box.Rotation = T.Rotator();
					GAssemblySimple.BoxElems.Add(Box);
				}
			}
			const int32 Before = Accum->GetMeshRef().MaxTriangleID();
			UGeometryScriptLibrary_MeshBasicEditFunctions::AppendMesh(Accum, Temp, T, true);
			const int32 After = Accum->GetMeshRef().MaxTriangleID();
			TriPart.SetNum(After);
			for (int32 t = Before; t < After; ++t)
			{
				TriPart[t] = i;
			}
			if (Accum->GetTriangleCount() > MaxAssemblyTriangles)
			{
				OutError = FString::Printf(TEXT("assembly exceeds %d triangles at part %d; lower detail/segments or split it"), MaxAssemblyTriangles, i);
				return false;
			}
		}
		bool bRepair = true;
		Recipe->TryGetBoolField(TEXT("repair_coplanar"), bRepair);
		int32 Separated = 0;
		if (bRepair)
		{
			Separated = SeparateCoplanar(Accum->GetMeshRef(), TriPart);
		}
		GAssemblyStats.Parts = Parts->Num();
		GAssemblyStats.Triangles = Accum->GetTriangleCount();
		GAssemblyStats.Materials = Materials.Num();
		GAssemblyStats.CoplanarSeparated = Separated;
		Accum->EditMesh([Mesh](UE::Geometry::FDynamicMesh3& Built) { Mesh->SetMesh(MoveTemp(Built)); });
		return true;
	}

	bool BuildMesh(UDynamicMesh* Mesh, const TSharedRef<FJsonObject>& Recipe, FString& OutError)
	{
		if (IsAssembly(Recipe))
		{
			return BuildAssembly(Mesh, Recipe, OutError);
		}
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

	/** Material for a set of material fields: a preset (PBR + procedural style) or an allowed material path. */
	/** Scanned surface textures of a preset (colour, normal, roughness, AO), if the project has them; cached. */
	struct FScanned
	{
		TWeakObjectPtr<UTexture> Color, Normal, Rough, AO;
		float Tile = 200.f;  // cm one texture repeat covers
	};

	static const FScanned* ScannedFor(const FString& Preset)
	{
		static const TMap<FString, float> Tiles = {
			{TEXT("brick"), 160.f}, {TEXT("plaster"), 220.f}, {TEXT("concrete"), 260.f}, {TEXT("limestone"), 200.f}, {TEXT("sandstone"), 320.f},
			{TEXT("granite"), 260.f}, {TEXT("marble"), 260.f}, {TEXT("stone"), 220.f}, {TEXT("roof_tile"), 200.f}, {TEXT("slate_roof"), 160.f},
			{TEXT("wood"), 160.f}, {TEXT("corrugated"), 200.f}, {TEXT("asphalt"), 400.f},  // steel / painted_iron stay procedural (car paint, frames)
			{TEXT("cobblestone"), 220.f}, {TEXT("paving"), 220.f}, {TEXT("terracotta"), 200.f}};
		static TMap<FString, FScanned> Cache;
		const float* Tile = Tiles.Find(Preset);
		if (!Tile)
		{
			return nullptr;
		}
		FScanned* Hit = Cache.Find(Preset);
		if (!Hit)
		{
			auto Load = [&Preset](const TCHAR* Suffix)
			{
				const FString Name = FString::Printf(TEXT("%s_%s"), *Preset, Suffix);
				return TWeakObjectPtr<UTexture>(LoadObject<UTexture>(nullptr, *FString::Printf(TEXT("/Game/RuntimeArchitecture/Surfaces/%s.%s"), *Name, *Name),
					nullptr, LOAD_NoWarn | LOAD_Quiet));
			};
			FScanned S;
			S.Color = Load(TEXT("diff"));
			S.Normal = Load(TEXT("nor_gl"));
			S.Rough = Load(TEXT("rough"));
			S.AO = Load(TEXT("ao"));
			S.Tile = *Tile;
			Hit = &Cache.Add(Preset, S);
		}
		return Hit->Color.IsValid() ? Hit : nullptr;
	}

	static UMaterialInterface* MakeMaterial(const TSharedRef<FJsonObject>& Recipe, UObject* Outer, FString& OutError)
	{
		FString Material = TEXT("white");
		Recipe->TryGetStringField(TEXT("material"), Material);
		FVector C;
		const bool bColor = RuntimeJson::ToVector(Recipe->TryGetField(TEXT("color")), C);
		if (const FPreset* Preset = Presets().FindByPredicate([&Material](const FPreset& P) { return P.Name == Material; }))
		{
			// Real scanned surfaces when the project has them ("scanned": false keeps the procedural look).
			bool bWantScan = true;
			Recipe->TryGetBoolField(TEXT("scanned"), bWantScan);
			// an explicit surface style picks the matching scan (concrete with style blocks = paving stones)
			FString ScanKey = Material;
			FString StyleField;
			if (Recipe->TryGetStringField(TEXT("style"), StyleField))
			{
				static const TMap<FString, FString> StyleScan = {{TEXT("blocks"), TEXT("paving")}, {TEXT("brick"), TEXT("brick")}, {TEXT("planks"), TEXT("wood")},
					{TEXT("roof_tiles"), TEXT("roof_tile")}, {TEXT("marble"), TEXT("marble")}, {TEXT("plaster"), TEXT("plaster")}, {TEXT("concrete"), TEXT("concrete")}};
				if (const FString* K = StyleScan.Find(StyleField))
				{
					ScanKey = *K;
				}
				else
				{
					bWantScan = false;  // smooth, brushed, ground ...: keep the procedural surface the recipe asked for
				}
			}
			const FScanned* Scan = !Preset->bGlass && bWantScan ? ScannedFor(ScanKey) : nullptr;
			const FPreset* ScanPreset = Presets().FindByPredicate([&ScanKey](const FPreset& P) { return P.Name == ScanKey; });
			const FLinearColor ScanRef = ScanPreset ? ScanPreset->Color : Preset->Color;
			UMaterialInterface* ScanBase = Scan ? LoadObject<UMaterialInterface>(nullptr, ScannedMaterialPath, nullptr, LOAD_NoWarn | LOAD_Quiet) : nullptr;
			if (ScanBase)
			{
				UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(ScanBase, Outer);
				const FLinearColor Color = bColor ? FLinearColor(C.X, C.Y, C.Z) : Preset->Color;
				// the part colour tints the scan relative to the preset's own colour (default colour = the scan as photographed)
				auto Ratio = [](float A, float B) { return FMath::Clamp(A / FMath::Max(B, 0.02f), 0.f, 4.f); };
				MID->SetVectorParameterValue(TEXT("Color"), Color);
				MID->SetVectorParameterValue(TEXT("ScanTint"), bColor ? FLinearColor(Ratio(Color.R, ScanRef.R), Ratio(Color.G, ScanRef.G),
					Ratio(Color.B, ScanRef.B)) : FLinearColor::White);
				MID->SetTextureParameterValue(TEXT("ScanColor"), Scan->Color.Get());
				if (Scan->Normal.IsValid()) MID->SetTextureParameterValue(TEXT("ScanNormal"), Scan->Normal.Get());
				if (Scan->Rough.IsValid()) MID->SetTextureParameterValue(TEXT("ScanRough"), Scan->Rough.Get());
				if (Scan->AO.IsValid()) MID->SetTextureParameterValue(TEXT("ScanAO"), Scan->AO.Get());
				// "tile" scales the pattern: relative to the preset's procedural tile, so old recipes keep their proportions
				const double StyleTile = PresetStyle(ScanKey).Tile;
				MID->SetScalarParameterValue(TEXT("ScanTile"), static_cast<float>(Scan->Tile * Num(Recipe, TEXT("tile"), StyleTile) / FMath::Max(StyleTile, 1.0)));
				MID->SetScalarParameterValue(TEXT("Variation"), static_cast<float>(Num(Recipe, TEXT("variation"), Preset->Variation)));
				MID->SetScalarParameterValue(TEXT("RoughnessScale"), static_cast<float>(Num(Recipe, TEXT("roughness"), Preset->Roughness) / FMath::Max(Preset->Roughness, 0.05f)));
				MID->SetScalarParameterValue(TEXT("Metallic"), static_cast<float>(Num(Recipe, TEXT("metallic"), Preset->Metallic)));
				FVector E;
				const bool bEmissiveColor = RuntimeJson::ToVector(Recipe->TryGetField(TEXT("emissive")), E);
				MID->SetVectorParameterValue(TEXT("Emissive"), bEmissiveColor ? FLinearColor(E.X, E.Y, E.Z) : Color);
				MID->SetScalarParameterValue(TEXT("EmissiveStrength"), static_cast<float>(Num(Recipe, TEXT("emissive_strength"), bEmissiveColor ? 5.0 : Preset->Emissive)));
				return MID;
			}
			UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, Preset->bGlass ? GlassMaterialPath : PbrMaterialPath);
			const bool bPbr = Base != nullptr;
			if (!Base)
			{
				Base = LoadObject<UMaterialInterface>(nullptr, BasicMaterialPath); // plugin content not cooked
			}
			UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Base, Outer);
			const FLinearColor Color = bColor ? FLinearColor(C.X, C.Y, C.Z) : Preset->Color;
			MID->SetVectorParameterValue(TEXT("Color"), Color);
			if (bPbr)
			{
				MID->SetScalarParameterValue(TEXT("Roughness"), static_cast<float>(Num(Recipe, TEXT("roughness"), Preset->Roughness)));
				MID->SetScalarParameterValue(TEXT("Metallic"), static_cast<float>(Num(Recipe, TEXT("metallic"), Preset->Metallic)));
				MID->SetScalarParameterValue(TEXT("Variation"), static_cast<float>(Num(Recipe, TEXT("variation"), Preset->Variation)));
				// -1 = follow the scene weathering (style material Weathering, default light wear)
				MID->SetScalarParameterValue(TEXT("PartWeathering"), static_cast<float>(FMath::Clamp(Num(Recipe, TEXT("weathering"), -1.0), -1.0, 1.0)));
				MID->SetScalarParameterValue(TEXT("Opacity"), static_cast<float>(Num(Recipe, TEXT("opacity"), 0.3)));
				FVector E;
				const bool bEmissiveColor = RuntimeJson::ToVector(Recipe->TryGetField(TEXT("emissive")), E);
				const double Strength = Num(Recipe, TEXT("emissive_strength"), bEmissiveColor ? 5.0 : Preset->Emissive);
				MID->SetVectorParameterValue(TEXT("Emissive"), bEmissiveColor ? FLinearColor(E.X, E.Y, E.Z) : Color);
				MID->SetScalarParameterValue(TEXT("EmissiveStrength"), static_cast<float>(Strength));
				FStyle Style = PresetStyle(Material);
				FString StyleName;
				if (Recipe->TryGetStringField(TEXT("style"), StyleName))
				{
					const int32 Index = StyleNames().IndexOfByKey(StyleName);
					if (Index == INDEX_NONE)
					{
						OutError = FString::Printf(TEXT("unknown style '%s' (%s)"), *StyleName, *FString::Join(StyleNames(), TEXT(", ")));
						return nullptr;
					}
					Style.Style = Index;
				}
				MID->SetScalarParameterValue(TEXT("Style"), static_cast<float>(Style.Style));
				MID->SetScalarParameterValue(TEXT("TileCm"), static_cast<float>(Num(Recipe, TEXT("tile"), Style.Tile)));
				MID->SetScalarParameterValue(TEXT("Relief"), static_cast<float>(Num(Recipe, TEXT("relief"), Style.Relief)));
			}
			return MID;
		}
		bool bAllowed = URuntimeToolsetsSettings::Get().AllowedObjectRoots.Num() == 0;
		for (const FString& Root : URuntimeToolsetsSettings::Get().AllowedObjectRoots)
		{
			bAllowed |= Material.StartsWith(Root);
		}
		UMaterialInterface* Base = bAllowed ? LoadObject<UMaterialInterface>(nullptr, *Material) : nullptr;
		if (!Base)
		{
			OutError = FString::Printf(TEXT("material '%s' is not a preset (%s) or an allowed material path"), *Material, *FString::Join(PaletteNames(), TEXT(", ")));
			return nullptr;
		}
		if (!bColor)
		{
			return Base;
		}
		UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Base, Outer);
		MID->SetVectorParameterValue(TEXT("Color"), FLinearColor(C.X, C.Y, C.Z));
		return MID;
	}

	bool ValidateMaterials(const TSharedRef<FJsonObject>& Recipe, FString& OutError)
	{
		if (!IsAssembly(Recipe))
		{
			return MakeMaterial(Recipe, GetTransientPackage(), OutError) != nullptr;
		}
		TArray<TSharedRef<FJsonObject>> Fields;
		TArray<int32> PartSlot;
		CollectAssemblyMaterials(Recipe, Fields, PartSlot);
		for (const TSharedRef<FJsonObject>& F : Fields)
		{
			if (!MakeMaterial(F, GetTransientPackage(), OutError))
			{
				return false;
			}
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
		if (IsAssembly(Recipe))
		{
			TArray<TSharedRef<FJsonObject>> Fields;
			TArray<int32> PartSlot;
			CollectAssemblyMaterials(Recipe, Fields, PartSlot);
			TArray<UMaterialInterface*> Set;
			for (const TSharedRef<FJsonObject>& F : Fields)
			{
				UMaterialInterface* M = MakeMaterial(F, Comp, OutError);
				if (!M)
				{
					return false;
				}
				Set.Add(M);
			}
			Comp->ConfigureMaterialSet(Set);
			return true;
		}
		UMaterialInterface* Final = MakeMaterial(Recipe, Comp, OutError);
		if (!Final)
		{
			return false;
		}
		for (int32 i = 0; i < FMath::Max(1, Comp->GetNumMaterials()); ++i)
		{
			Comp->SetMaterial(i, Final);
		}
		return true;
	}

	/** Collision for a freshly built mesh: one cook (updates were deferred while building). Assemblies use their part boxes;
	 *  single shapes get fitted simple shapes. Big meshes cook the exact triangles in the background. */
	static void FinishCollision(UDynamicMeshComponent* Comp, const FKAggregateGeom* Simple)
	{
		if (Simple)
		{
			Comp->SetSimpleCollisionShapes(*Simple, false);
		}
		else
		{
			// Simple shapes fitted per part (boxes, spheres, capsules, hulls) for physics and navigation (Recast does not
			// reliably export complex-only dynamic mesh collision); the exact triangles stay available to complex traces.
			FGeometryScriptCollisionFromMeshOptions Collision;
			Collision.bEmitTransaction = false;
			Collision.Method = EGeometryScriptCollisionGenerationMethod::MinVolumeShapes;
			Collision.MaxShapeCount = 1024;
			UGeometryScriptLibrary_CollisionFunctions::SetDynamicMeshCollisionFromMesh(Comp->GetDynamicMesh(), Comp, Collision);
		}
		Comp->bEnableComplexCollision = true;
		Comp->CollisionType = ECollisionTraceFlag::CTF_UseSimpleAndComplex;
		// Platforms the player stands on right away stay synchronous; big scenery cooks off the game thread.
		Comp->bUseAsyncCooking = Comp->GetDynamicMesh()->GetTriangleCount() > 5000;
		Comp->SetDeferredCollisionUpdatesEnabled(false, false);
		Comp->UpdateCollision(false);
		// The navmesh rebuilds around changed geometry (dynamic runtime generation).
		FNavigationSystem::UpdateComponentData(*Comp);
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
		Comp->SetDeferredCollisionUpdatesEnabled(true, false);
		if (!BuildMesh(Comp->GetDynamicMesh(), Recipe, OutError) || !ApplyMaterial(Actor, Recipe, OutError))
		{
			Comp->SetDeferredCollisionUpdatesEnabled(false, true);
			return false;
		}
		FinishCollision(Comp, IsAssembly(Recipe) ? &GAssemblySimple : nullptr);
		return true;
	}

	bool BuildPrebuilt(const TSharedRef<FJsonObject>& Recipe, FPrebuilt& Out, FString& OutError)
	{
		UDynamicMesh* Scratch = NewObject<UDynamicMesh>();
		if (!BuildMesh(Scratch, Recipe, OutError))
		{
			return false;
		}
		Scratch->EditMesh([&Out](UE::Geometry::FDynamicMesh3& Built) { Out.Mesh = MoveTemp(Built); });
		Out.bHasSimple = IsAssembly(Recipe);
		if (Out.bHasSimple)
		{
			Out.Simple = GAssemblySimple;
		}
		return true;
	}

	static ADynamicMeshActor* SpawnMeshActor(UWorld* World, const FTransform& Transform, const FString& Name, FString& OutError)
	{
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
		return Actor;
	}

	AActor* SpawnPrebuilt(UWorld* World, const TSharedRef<const FPrebuilt>& Built, const TSharedRef<FJsonObject>& Recipe, const FTransform& Transform,
		const FString& Name, FString& OutError)
	{
		ADynamicMeshActor* Actor = SpawnMeshActor(World, Transform, Name, OutError);
		if (!Actor)
		{
			return nullptr;
		}
		UDynamicMeshComponent* Comp = Actor->GetDynamicMeshComponent();
		Comp->SetDeferredCollisionUpdatesEnabled(true, false);
		Comp->GetDynamicMesh()->SetMesh(Built->Mesh);
		if (!ApplyMaterial(Actor, Recipe, OutError))
		{
			Actor->Destroy();
			return nullptr;
		}
		FinishCollision(Comp, Built->bHasSimple ? &Built->Simple : nullptr);
		return Actor;
	}


	AActor* SpawnFromSource(UWorld* World, const FString& Source, const FTransform& Transform, const FString& Name, FString& OutError)
	{
		const TSharedPtr<FJsonObject> Recipe = FromSource(Source);
		if (!Recipe.IsValid())
		{
			OutError = TEXT("bad prim: recipe");
			return nullptr;
		}
		ADynamicMeshActor* Actor = SpawnMeshActor(World, Transform, Name, OutError);
		if (!Actor)
		{
			return nullptr;
		}
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
