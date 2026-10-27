#include "TerrainMesh.h"
#include "RuntimeToolsetExtensions.h"
#include "RuntimeJson.h"
#include "UDynamicMesh.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "DynamicMesh/MeshNormals.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Engine/Texture.h"

namespace RuntimeTerrain
{
	static const TCHAR* TerrainMaterialPath = TEXT("/RuntimeToolsets/Materials/M_RT_Terrain.M_RT_Terrain");
	static constexpr int32 MaxCells = 600;

	const TArray<FString>& BiomeNames()
	{
		static const TArray<FString> Names = {TEXT("temperate"), TEXT("tropical"), TEXT("desert"), TEXT("savanna"), TEXT("alpine"), TEXT("arctic")};
		return Names;
	}

	bool IsTerrain(const TSharedRef<FJsonObject>& Recipe)
	{
		FString Shape;
		return Recipe->TryGetStringField(TEXT("shape"), Shape) && Shape == TEXT("terrain");
	}

	static double Num(const TSharedPtr<FJsonObject>& R, const TCHAR* Field, double Default)
	{
		double V = Default;
		return R.IsValid() && R->TryGetNumberField(Field, V) ? V : Default;
	}

	static double Smooth(double T)
	{
		T = FMath::Clamp(T, 0.0, 1.0);
		return T * T * (3.0 - 2.0 * T);
	}

	static bool ReadXY(const TSharedPtr<FJsonValue>& V, FVector2D& Out)
	{
		const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
		if (!V.IsValid() || !V->TryGetArray(A) || A->Num() < 2)
		{
			return false;
		}
		Out = FVector2D((*A)[0]->AsNumber(), (*A)[1]->AsNumber());
		return true;
	}

	static bool ReadPath(const TSharedPtr<FJsonObject>& J, const TCHAR* Field, TArray<FVector2D>& Out, int32 Min)
	{
		const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
		if (!J->TryGetArrayField(Field, A))
		{
			return false;
		}
		for (const TSharedPtr<FJsonValue>& P : *A)
		{
			FVector2D V;
			if (!ReadXY(P, V))
			{
				return false;
			}
			Out.Add(V);
		}
		return Out.Num() >= Min;
	}

	static bool InPolygon(const TArray<FVector2D>& Poly, const FVector2D& P)
	{
		bool bIn = false;
		for (int32 i = 0, j = Poly.Num() - 1; i < Poly.Num(); j = i++)
		{
			if ((Poly[i].Y > P.Y) != (Poly[j].Y > P.Y) && P.X < (Poly[j].X - Poly[i].X) * (P.Y - Poly[i].Y) / (Poly[j].Y - Poly[i].Y) + Poly[i].X)
			{
				bIn = !bIn;
			}
		}
		return bIn;
	}

	static double DistToPolygonEdge(const TArray<FVector2D>& Poly, const FVector2D& P)
	{
		double Best = TNumericLimits<double>::Max();
		for (int32 i = 0, j = Poly.Num() - 1; i < Poly.Num(); j = i++)
		{
			const FVector2D A = Poly[j], B = Poly[i];
			const FVector2D AB = B - A;
			const double T = FMath::Clamp(FVector2D::DotProduct(P - A, AB) / FMath::Max(AB.SizeSquared(), 1e-6), 0.0, 1.0);
			Best = FMath::Min(Best, FVector2D::Distance(P, A + AB * T));
		}
		return Best;
	}

	/** The height field and its masks. */
	struct FField
	{
		int32 NX = 0, NY = 0;
		double X0 = 0, Y0 = 0, Cell = 100;
		TArray<double> H;
		TArray<float> Wet, Bare;
		double& At(int32 i, int32 j) { return H[j * NX + i]; }
		double Sample(const FVector2D& P) const
		{
			const double FX = FMath::Clamp((P.X - X0) / Cell, 0.0, NX - 1.001), FY = FMath::Clamp((P.Y - Y0) / Cell, 0.0, NY - 1.001);
			const int32 I = FMath::FloorToInt(FX), J = FMath::FloorToInt(FY);
			const double TX = FX - I, TY = FY - J;
			const double A = H[J * NX + I], B = H[J * NX + I + 1], C = H[(J + 1) * NX + I], D = H[(J + 1) * NX + I + 1];
			return FMath::Lerp(FMath::Lerp(A, B, TX), FMath::Lerp(C, D, TX), TY);
		}
		FVector2D Pos(int32 i, int32 j) const { return FVector2D(X0 + i * Cell, Y0 + j * Cell); }
	};

	/** A carved channel: the path resampled every few metres with the surface level along it. */
	struct FCarve
	{
		bool bRiver = true;
		double Width = 1500, Depth = 250, Bank = 900;
		TArray<FVector2D> P;
		TArray<double> Level;  // water surface (rivers) or road surface
	};

	static bool Generate(const TSharedRef<FJsonObject>& R, FField& F, TArray<FCarve>& Carves, FString& OutError)
	{
		FVector2D Size(20000, 20000);
		ReadXY(R->TryGetField(TEXT("size")), Size);
		if (Size.X < 500 || Size.Y < 500 || Size.X > 400000 || Size.Y > 400000)
		{
			OutError = TEXT("terrain size must be 500-400000 cm per side");
			return false;
		}
		const double Height = Num(R, TEXT("height"), 2000);
		const double Feature = FMath::Max(500.0, Num(R, TEXT("feature_size"), 20000));
		const double Ridged = FMath::Clamp(Num(R, TEXT("ridged"), 0.0), 0.0, 1.0);
		const double Rough = FMath::Clamp(Num(R, TEXT("roughness"), 0.5), 0.0, 1.0);
		const double Seed = Num(R, TEXT("seed"), 1);
		const double Edge = FMath::Max(0.0, Num(R, TEXT("edge"), 0));
		double Cell = Num(R, TEXT("resolution"), 0);
		if (Cell <= 0)
		{
			Cell = FMath::Max(50.0, FMath::Max(Size.X, Size.Y) / 256.0);
		}
		Cell = FMath::Max(Cell, FMath::Max(Size.X, Size.Y) / MaxCells);
		F.Cell = Cell;
		F.NX = FMath::Max(2, FMath::RoundToInt(Size.X / Cell) + 1);
		F.NY = FMath::Max(2, FMath::RoundToInt(Size.Y / Cell) + 1);
		F.Cell = Size.X / (F.NX - 1);
		const double CellY = Size.Y / (F.NY - 1);
		F.Cell = FMath::Max(F.Cell, CellY);  // keep square cells; the grid may overshoot Size.Y by under a cell
		F.NY = FMath::Max(2, FMath::RoundToInt(Size.Y / F.Cell) + 1);
		F.X0 = -Size.X / 2;
		F.Y0 = -Size.Y / 2;
		F.H.SetNumZeroed(F.NX * F.NY);
		F.Wet.SetNumZeroed(F.NX * F.NY);
		F.Bare.SetNumZeroed(F.NX * F.NY);
		const FVector2D Off(Seed * 17.31, Seed * 29.17);
		const double Persist = 0.35 + 0.3 * Rough;

		// 1. layered noise
		for (int32 j = 0; j < F.NY; ++j)
		{
			for (int32 i = 0; i < F.NX; ++i)
			{
				const FVector2D P = F.Pos(i, j);
				double Sum = 0, AmpSum = 0, Amp = 1, Freq = 1.0 / Feature;
				for (int32 o = 0; o < 6; ++o)
				{
					const double N = FMath::PerlinNoise2D(P * Freq + Off * (o + 1));
					const double Hill = N * 0.5 + 0.5;
					const double Ridge = FMath::Square(1.0 - FMath::Abs(N));
					Sum += Amp * FMath::Lerp(Hill, Ridge, Ridged);
					AmpSum += Amp;
					Amp *= Persist;
					Freq *= 2.03;
				}
				double V = Sum / AmpSum;                        // 0..1
				V = FMath::Pow(FMath::Clamp(V, 0.0, 1.0), 1.5);  // flatter lowlands, sharper tops
				double Z = Height * V;
				if (Edge > 0)
				{
					const double D = FMath::Min(FMath::Min(P.X - F.X0, Size.X / 2 - P.X), FMath::Min(P.Y - F.Y0, Size.Y / 2 - P.Y));
					Z *= Smooth(D / Edge);
				}
				F.At(i, j) = Z;
			}
		}
		// 1b. islands: a drawn coastline at sea level, a beach rising inland, a hill filling the interior, the sea floor
		// shelving off outside; the outline is roughened with noise into coves and points
		const TArray<TSharedPtr<FJsonValue>>* Islands = nullptr;
		if (R->TryGetArrayField(TEXT("islands"), Islands) && Islands->Num() > 0)
		{
			const double Sea = Num(R, TEXT("sea_level"), 0.0);
			TArray<double> Land;
			Land.Init(-TNumericLimits<double>::Max(), F.NX * F.NY);
			for (const TSharedPtr<FJsonValue>& V : *Islands)
			{
				const TSharedPtr<FJsonObject> J = V->AsObject();
				TArray<FVector2D> Area;
				if (!J.IsValid() || !ReadPath(J, TEXT("area"), Area, 3))
				{
					OutError = TEXT("islands entries need area:[[x,y],...] (the coastline at sea level, 3+ points)");
					return false;
				}
				const double Top = Num(J, TEXT("height"), 800), Beach = FMath::Max(100.0, Num(J, TEXT("beach"), 1500));
				const double Dune = Num(J, TEXT("dune"), 150), Depth = Num(J, TEXT("depth"), 900), Shelf = FMath::Max(100.0, Num(J, TEXT("shelf"), 3000));
				const double Wobble = Num(J, TEXT("wobble"), 600), WobbleSize = FMath::Max(200.0, Num(J, TEXT("wobble_size"), 2500));
				TArray<double> SDist;
				SDist.SetNumUninitialized(F.NX * F.NY);
				double MaxIn = 1.0;
				for (int32 k = 0; k < F.NX * F.NY; ++k)
				{
					const FVector2D P = F.Pos(k % F.NX, k / F.NX);
					double S = DistToPolygonEdge(Area, P) * (InPolygon(Area, P) ? 1.0 : -1.0);
					S += Wobble * FMath::PerlinNoise2D(P / WobbleSize + Off * 3.1);
					SDist[k] = S;
					MaxIn = FMath::Max(MaxIn, S);
				}
				const double Inland = FMath::Max(1.0, MaxIn - Beach);
				for (int32 k = 0; k < F.NX * F.NY; ++k)
				{
					const double S = SDist[k];
					const double Z = S >= 0 ? Sea + Dune * Smooth(FMath::Min(S / Beach, 1.0)) + (Top - Dune) * Smooth(FMath::Clamp((S - Beach) / Inland, 0.0, 1.0))
											: Sea - Depth * Smooth(FMath::Min(-S / Shelf, 1.0));
					Land[k] = FMath::Max(Land[k], Z);
				}
			}
			// the relief noise roughens the land (and a little the sea floor)
			for (int32 k = 0; k < F.NX * F.NY; ++k)
			{
				F.H[k] = Land[k] + F.H[k] * (Land[k] > Sea ? 1.0 : 0.3);
			}
		}
		// 2. extra hills / basins
		const TArray<TSharedPtr<FJsonValue>>* Hills = nullptr;
		if (R->TryGetArrayField(TEXT("hills"), Hills))
		{
			for (const TSharedPtr<FJsonValue>& V : *Hills)
			{
				const TSharedPtr<FJsonObject> J = V->AsObject();
				FVector2D C;
				if (!J.IsValid() || !ReadXY(J->TryGetField(TEXT("at")), C))
				{
					OutError = TEXT("hills entries need at:[x,y], radius, height");
					return false;
				}
				const double Rad = FMath::Max(100.0, Num(J, TEXT("radius"), 5000)), Hh = Num(J, TEXT("height"), 1000);
				for (int32 j = 0; j < F.NY; ++j)
				{
					for (int32 i = 0; i < F.NX; ++i)
					{
						const double D = FVector2D::Distance(F.Pos(i, j), C);
						if (D < Rad)
						{
							F.At(i, j) += Hh * Smooth(1.0 - D / Rad);
						}
					}
				}
			}
		}
		// 3. flat pads (round or polygon)
		const TArray<TSharedPtr<FJsonValue>>* Pads = nullptr;
		if (R->TryGetArrayField(TEXT("flatten"), Pads))
		{
			for (const TSharedPtr<FJsonValue>& V : *Pads)
			{
				const TSharedPtr<FJsonObject> J = V->AsObject();
				if (!J.IsValid())
				{
					continue;
				}
				TArray<FVector2D> Area;
				FVector2D C;
				const bool bArea = ReadPath(J, TEXT("area"), Area, 3);
				if (!bArea && !ReadXY(J->TryGetField(TEXT("at")), C))
				{
					OutError = TEXT("flatten entries need at:[x,y] + radius, or area:[[x,y],...]");
					return false;
				}
				if (bArea)
				{
					C = FVector2D::ZeroVector;
					for (const FVector2D& A : Area)
					{
						C += A / Area.Num();
					}
				}
				const double Rad = Num(J, TEXT("radius"), 2000);
				const double Blend = FMath::Max(1.0, Num(J, TEXT("blend"), bArea ? 1500 : Rad * 0.5));
				const double Target = J->HasField(TEXT("height")) ? Num(J, TEXT("height"), 0) : F.Sample(C);
				const bool bBare = !J->HasField(TEXT("bare")) || J->GetBoolField(TEXT("bare"));
				for (int32 j = 0; j < F.NY; ++j)
				{
					for (int32 i = 0; i < F.NX; ++i)
					{
						const FVector2D P = F.Pos(i, j);
						double W;
						if (bArea)
						{
							W = InPolygon(Area, P) ? 1.0 : 1.0 - Smooth(DistToPolygonEdge(Area, P) / Blend);
						}
						else
						{
							W = 1.0 - Smooth((FVector2D::Distance(P, C) - Rad) / Blend);
						}
						if (W > 0)
						{
							F.At(i, j) = FMath::Lerp(F.At(i, j), Target, W);
							if (bBare)
							{
								const float Inner = bArea ? (InPolygon(Area, P) ? 1.f : 0.f) : static_cast<float>(1.0 - Smooth((FVector2D::Distance(P, C) - Rad * 0.9) / (Rad * 0.15 + 1)));
								F.Bare[j * F.NX + i] = FMath::Max(F.Bare[j * F.NX + i], Inner);
							}
						}
					}
				}
			}
		}
		// 4. channels: resample each path, compute its level, then lower the ground into it
		const TArray<TSharedPtr<FJsonValue>>* Cuts = nullptr;
		if (R->TryGetArrayField(TEXT("carve"), Cuts))
		{
			for (const TSharedPtr<FJsonValue>& V : *Cuts)
			{
				const TSharedPtr<FJsonObject> J = V->AsObject();
				TArray<FVector2D> Path;
				if (!J.IsValid() || !ReadPath(J, TEXT("path"), Path, 2))
				{
					OutError = TEXT("carve entries need path:[[x,y],...] (at least 2 points, upstream first for rivers)");
					return false;
				}
				FCarve C;
				FString Kind = TEXT("river");
				J->TryGetStringField(TEXT("kind"), Kind);
				C.bRiver = Kind != TEXT("road");
				C.Width = FMath::Max(100.0, Num(J, TEXT("width"), C.bRiver ? 1500 : 500));
				C.Depth = Num(J, TEXT("depth"), C.bRiver ? 250 : 0);
				C.Bank = FMath::Max(50.0, Num(J, TEXT("bank"), C.bRiver ? C.Width * 0.7 : 300));
				const double Step = FMath::Max(F.Cell, 100.0);
				for (int32 k = 0; k + 1 < Path.Num(); ++k)
				{
					const int32 N = FMath::Max(1, FMath::CeilToInt(FVector2D::Distance(Path[k], Path[k + 1]) / Step));
					for (int32 s = 0; s < N; ++s)
					{
						C.P.Add(FMath::Lerp(Path[k], Path[k + 1], double(s) / N));
					}
				}
				C.P.Add(Path.Last());
				// level: the ground along the path, smoothed; rivers never climb (running minimum downstream)
				TArray<double> Raw;
				for (const FVector2D& P : C.P)
				{
					// lowest ground across the channel, so the river finds the valley floor
					double Low = F.Sample(P);
					for (double o : {-0.5, 0.5})
					{
						const int32 k = &P - C.P.GetData();
						const FVector2D Dir = (C.P[FMath::Min(k + 1, C.P.Num() - 1)] - C.P[FMath::Max(k - 1, 0)]).GetSafeNormal();
						Low = FMath::Min(Low, F.Sample(P + FVector2D(-Dir.Y, Dir.X) * C.Width * o));
					}
					Raw.Add(Low);
				}
				const int32 Win = FMath::Max(1, FMath::RoundToInt(C.Width * 2 / Step));
				C.Level.SetNum(Raw.Num());
				for (int32 k = 0; k < Raw.Num(); ++k)
				{
					double S = 0;
					int32 Cnt = 0;
					for (int32 w = FMath::Max(0, k - Win); w <= FMath::Min(Raw.Num() - 1, k + Win); ++w)
					{
						S += Raw[w];
						++Cnt;
					}
					C.Level[k] = S / Cnt;
					if (C.bRiver && k > 0)
					{
						C.Level[k] = FMath::Min(C.Level[k], C.Level[k - 1] - 0.5);
					}
				}
				const double Hw = C.Width / 2;
				for (int32 j = 0; j < F.NY; ++j)
				{
					for (int32 i = 0; i < F.NX; ++i)
					{
						const FVector2D P = F.Pos(i, j);
						double Best = TNumericLimits<double>::Max(), Lv = 0;
						for (int32 k = 0; k + 1 < C.P.Num(); ++k)
						{
							const FVector2D A = C.P[k], AB = C.P[k + 1] - C.P[k];
							const double T = FMath::Clamp(FVector2D::DotProduct(P - A, AB) / FMath::Max(AB.SizeSquared(), 1e-6), 0.0, 1.0);
							const double D = FVector2D::DistSquared(P, A + AB * T);
							if (D < Best)
							{
								Best = D;
								Lv = FMath::Lerp(C.Level[k], C.Level[k + 1], T);
							}
						}
						const double D = FMath::Sqrt(Best);
						if (D > Hw + C.Bank)
						{
							continue;
						}
						double& Z = F.At(i, j);
						const int32 Idx = j * F.NX + i;
						if (C.bRiver)
						{
							const double Water = Lv;
							double Target;
							if (D < Hw)
							{
								Target = Water - C.Depth * (1.0 - 0.6 * FMath::Square(D / Hw));              // dished bed
							}
							else
							{
								const double T = Smooth((D - Hw) / C.Bank);
								Target = FMath::Lerp(Water - C.Depth * 0.4, FMath::Max(Z, Water + 30), T); // bank up to the ground
							}
							Z = FMath::Min(Z, Target);
							F.Wet[Idx] = FMath::Max(F.Wet[Idx], static_cast<float>(1.0 - Smooth((D - Hw * 0.8) / (C.Bank * 0.6 + 1))));
						}
						else
						{
							const double T = D < Hw ? 0.0 : Smooth((D - Hw) / C.Bank);
							Z = FMath::Lerp(Lv - C.Depth, Z, T);                                            // road bed cut and fill
							F.Bare[Idx] = FMath::Max(F.Bare[Idx], static_cast<float>(1.0 - Smooth((D - Hw * 0.85) / (Hw * 0.3 + 1))));
						}
					}
				}
				for (int32 k = 0; k < C.Level.Num(); ++k)
				{
					if (C.bRiver)
					{
						C.Level[k] -= C.Depth * 0.25;  // water surface sits below the bank tops
					}
				}
				Carves.Add(MoveTemp(C));
			}
		}
		return true;
	}

	bool Append(UDynamicMesh* Mesh, const TSharedRef<FJsonObject>& Recipe, FString& OutError)
	{
		FField F;
		TArray<FCarve> Carves;
		if (!Generate(Recipe, F, Carves, OutError))
		{
			return false;
		}
		Mesh->EditMesh([&F](UE::Geometry::FDynamicMesh3& M)
		{
			M.Clear();
			M.EnableAttributes();
			M.Attributes()->EnablePrimaryColors();
			UE::Geometry::FDynamicMeshColorOverlay* Colors = M.Attributes()->PrimaryColors();
			UE::Geometry::FDynamicMeshUVOverlay* UVs = M.Attributes()->PrimaryUV();
			TArray<int32> Vid, Cid, Uid;
			Vid.SetNum(F.NX * F.NY);
			Cid.SetNum(F.NX * F.NY);
			Uid.SetNum(F.NX * F.NY);
			for (int32 j = 0; j < F.NY; ++j)
			{
				for (int32 i = 0; i < F.NX; ++i)
				{
					const int32 Idx = j * F.NX + i;
					const FVector2D P = F.Pos(i, j);
					Vid[Idx] = M.AppendVertex(FVector3d(P.X, P.Y, F.H[Idx]));
					Cid[Idx] = Colors->AppendElement(FVector4f(F.Wet[Idx], F.Bare[Idx], 0.f, 1.f));
					Uid[Idx] = UVs->AppendElement(FVector2f(P.X / 1000.0, P.Y / 1000.0));
				}
			}
			for (int32 j = 0; j + 1 < F.NY; ++j)
			{
				for (int32 i = 0; i + 1 < F.NX; ++i)
				{
					const int32 A = j * F.NX + i, B = A + 1, C = A + F.NX, D = C + 1;
					// split along the shorter diagonal-ish rule for nicer ridges: alternate per cell
					const bool bFlip = ((i + j) & 1) != 0;
					const UE::Geometry::FIndex3i T1 = bFlip ? UE::Geometry::FIndex3i(A, C, B) : UE::Geometry::FIndex3i(A, D, B);
					const UE::Geometry::FIndex3i T2 = bFlip ? UE::Geometry::FIndex3i(B, C, D) : UE::Geometry::FIndex3i(A, C, D);
					for (const UE::Geometry::FIndex3i& T : {T1, T2})
					{
						const int32 Tid = M.AppendTriangle(Vid[T.A], Vid[T.B], Vid[T.C]);
						if (Tid >= 0)
						{
							Colors->SetTriangle(Tid, UE::Geometry::FIndex3i(Cid[T.A], Cid[T.B], Cid[T.C]));
							UVs->SetTriangle(Tid, UE::Geometry::FIndex3i(Uid[T.A], Uid[T.B], Uid[T.C]));
						}
					}
				}
			}
			// faces must point up (+Z) in Unreal's left-handed frame
			if (M.TriangleCount() > 0 && M.GetTriNormal(0).Z < 0)
			{
				M.ReverseOrientation();
			}
			UE::Geometry::FMeshNormals::InitializeOverlayToPerVertexNormals(M.Attributes()->PrimaryNormals(), false);
		});
		return true;
	}

	bool Describe(const TSharedRef<FJsonObject>& Recipe, TArray<TSharedPtr<FJsonValue>>& OutRivers, double& OutMinZ, double& OutMaxZ, FString& OutError)
	{
		FField F;
		TArray<FCarve> Carves;
		if (!Generate(Recipe, F, Carves, OutError))
		{
			return false;
		}
		OutMinZ = TNumericLimits<double>::Max();
		OutMaxZ = TNumericLimits<double>::Lowest();
		for (double Z : F.H)
		{
			OutMinZ = FMath::Min(OutMinZ, Z);
			OutMaxZ = FMath::Max(OutMaxZ, Z);
		}
		for (const FCarve& C : Carves)
		{
			if (!C.bRiver)
			{
				continue;
			}
			TArray<TSharedPtr<FJsonValue>> Pts;
			const int32 Stride = FMath::Max(1, C.P.Num() / 200);
			for (int32 k = 0; k < C.P.Num(); k += Stride)
			{
				Pts.Add(RuntimeJson::Vec(FVector(C.P[k].X, C.P[k].Y, C.Level[k])));
			}
			if ((C.P.Num() - 1) % Stride != 0)
			{
				Pts.Add(RuntimeJson::Vec(FVector(C.P.Last().X, C.P.Last().Y, C.Level.Last())));
			}
			TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
			J->SetArrayField(TEXT("path"), Pts);
			J->SetNumberField(TEXT("width"), C.Width);
			OutRivers.Add(MakeShared<FJsonValueObject>(J));
		}
		return true;
	}

	bool HeightAt(const TSharedRef<FJsonObject>& Recipe, const FVector2D& Local, double& OutZ, FString& OutError)
	{
		FField F;
		TArray<FCarve> Carves;
		if (!Generate(Recipe, F, Carves, OutError))
		{
			return false;
		}
		OutZ = F.Sample(Local);
		return true;
	}

	// ------------------------------------------------------------------------------------------------ rivers

	static const TCHAR* RiverMaterialPath = TEXT("/RuntimeToolsets/Materials/M_RT_River.M_RT_River");

	const TArray<FString>& WaterNames()
	{
		static const TArray<FString> Names = {TEXT("muddy"), TEXT("clear"), TEXT("green"), TEXT("blue"), TEXT("dark")};
		return Names;
	}

	bool IsRiver(const TSharedRef<FJsonObject>& Recipe)
	{
		FString Shape;
		return Recipe->TryGetStringField(TEXT("shape"), Shape) && Shape == TEXT("river");
	}

	bool AppendRiver(UDynamicMesh* Mesh, const TSharedRef<FJsonObject>& R, FString& OutError)
	{
		TArray<FVector> In;
		const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
		if (R->TryGetArrayField(TEXT("path"), A))
		{
			for (const TSharedPtr<FJsonValue>& V : *A)
			{
				FVector P;
				if (!RuntimeJson::ToVector(V, P))
				{
					OutError = TEXT("river path points are [x,y,z] (z = water level)");
					return false;
				}
				In.Add(P);
			}
		}
		if (In.Num() < 2 || In.Num() > 5000)
		{
			OutError = TEXT("river needs a path of 2-5000 points [[x,y,z],...]");
			return false;
		}
		const double Width = FMath::Clamp(Num(R, TEXT("width"), 1500), 50.0, 100000.0);
		const double Overlap = FMath::Max(0.0, Num(R, TEXT("overlap"), Width * 0.25));
		const double Foam = FMath::Max(1.0, Num(R, TEXT("foam_width"), FMath::Min(120.0, Width * 0.06)));
		const double Step = FMath::Clamp(Num(R, TEXT("segment"), FMath::Min(300.0, Width / 3)), 25.0, 5000.0);
		// resample evenly so the ribbon bends smoothly
		TArray<FVector> P;
		for (int32 k = 0; k + 1 < In.Num(); ++k)
		{
			const int32 N = FMath::Max(1, FMath::CeilToInt(FVector::Dist2D(In[k], In[k + 1]) / Step));
			for (int32 s = 0; s < N; ++s)
			{
				P.Add(FMath::Lerp(In[k], In[k + 1], double(s) / N));
			}
		}
		P.Add(In.Last());
		const int32 Across = 8;
		const double Half = Width / 2 + Overlap;
		Mesh->EditMesh([&](UE::Geometry::FDynamicMesh3& M)
		{
			M.Clear();
			M.EnableAttributes();
			M.Attributes()->EnablePrimaryColors();
			UE::Geometry::FDynamicMeshColorOverlay* Colors = M.Attributes()->PrimaryColors();
			UE::Geometry::FDynamicMeshUVOverlay* UVs = M.Attributes()->PrimaryUV();
			TArray<int32> V, C, U;
			double Dist = 0;
			for (int32 k = 0; k < P.Num(); ++k)
			{
				if (k > 0)
				{
					Dist += FVector::Dist2D(P[k], P[k - 1]);
				}
				const FVector2D Dir = FVector2D(P[FMath::Min(k + 1, P.Num() - 1)] - P[FMath::Max(k - 1, 0)]).GetSafeNormal();
				const FVector2D Side(-Dir.Y, Dir.X);
				for (int32 j = 0; j <= Across; ++j)
				{
					const double T = double(j) / Across;
					const double Off = FMath::Lerp(-Half, Half, T);
					const FVector2D XY = FVector2D(P[k]) + Side * Off;
					V.Add(M.AppendVertex(FVector3d(XY.X, XY.Y, P[k].Z)));
					// foam is a thin line at the bank, fading both ways (the overlap strip lies under the banks)
					const double Edge = Width / 2 - FMath::Abs(Off);
					const float FoamV = static_cast<float>(1.0 - Smooth(FMath::Abs(Edge) / Foam));
					C.Add(Colors->AppendElement(FVector4f(FoamV, 0.f, 0.f, 1.f)));
					U.Add(UVs->AppendElement(FVector2f(static_cast<float>(Dist / 100.0), static_cast<float>(T))));
				}
			}
			const int32 Row = Across + 1;
			for (int32 k = 0; k + 1 < P.Num(); ++k)
			{
				for (int32 j = 0; j < Across; ++j)
				{
					const int32 A0 = k * Row + j, B0 = A0 + 1, C0 = A0 + Row, D0 = C0 + 1;
					for (const UE::Geometry::FIndex3i& T : {UE::Geometry::FIndex3i(A0, D0, B0), UE::Geometry::FIndex3i(A0, C0, D0)})
					{
						const int32 Tid = M.AppendTriangle(V[T.A], V[T.B], V[T.C]);
						if (Tid >= 0)
						{
							Colors->SetTriangle(Tid, UE::Geometry::FIndex3i(C[T.A], C[T.B], C[T.C]));
							UVs->SetTriangle(Tid, UE::Geometry::FIndex3i(U[T.A], U[T.B], U[T.C]));
						}
					}
				}
			}
			if (M.TriangleCount() > 0 && M.GetTriNormal(0).Z < 0)
			{
				M.ReverseOrientation();
			}
			UE::Geometry::FMeshNormals::InitializeOverlayToPerVertexNormals(M.Attributes()->PrimaryNormals(), false);
		});
		return true;
	}

	UMaterialInterface* MakeRiverMaterial(const TSharedRef<FJsonObject>& Recipe, UObject* Outer)
	{
		UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, RiverMaterialPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
		if (!Base)
		{
			return nullptr;
		}
		// Single layer water: light scattered back (the water's own colour) and absorbed, per metre.
		struct FWater { const TCHAR* Name; FLinearColor Scatter, Absorb, Surface; float Rough; };
		static const FWater Waters[] = {
			{TEXT("muddy"), {0.32f, 0.20f, 0.09f}, {0.25f, 0.30f, 0.45f}, {0.30f, 0.22f, 0.12f}, 0.08f},   // sediment-laden (Amazon, Mekong)
			{TEXT("clear"), {0.004f, 0.012f, 0.015f}, {0.35f, 0.06f, 0.025f}, {0.10f, 0.14f, 0.14f}, 0.04f},
			{TEXT("green"), {0.02f, 0.06f, 0.03f}, {0.40f, 0.12f, 0.20f}, {0.08f, 0.14f, 0.08f}, 0.05f},   // algae, jungle streams
			{TEXT("blue"), {0.005f, 0.02f, 0.04f}, {0.45f, 0.08f, 0.02f}, {0.05f, 0.10f, 0.16f}, 0.04f},   // glacial, lake blue
			{TEXT("dark"), {0.03f, 0.02f, 0.012f}, {0.45f, 0.50f, 0.55f}, {0.05f, 0.04f, 0.03f}, 0.05f},   // blackwater (Rio Negro)
		};
		FString Name = TEXT("clear");
		Recipe->TryGetStringField(TEXT("water"), Name);
		const FWater* W = &Waters[1];
		for (const FWater& X : Waters)
		{
			if (Name == X.Name)
			{
				W = &X;
			}
		}
		UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Base, Outer);
		MID->SetVectorParameterValue(TEXT("Scattering"), W->Scatter);
		MID->SetVectorParameterValue(TEXT("Absorption"), W->Absorb);
		MID->SetVectorParameterValue(TEXT("SurfaceColor"), W->Surface);
		MID->SetScalarParameterValue(TEXT("Roughness"), W->Rough);
		MID->SetScalarParameterValue(TEXT("FlowSpeed"), static_cast<float>(Num(Recipe, TEXT("flow_speed"), 1.0)));
		MID->SetScalarParameterValue(TEXT("Ripple"), static_cast<float>(Num(Recipe, TEXT("ripple"), 1.0)));
		MID->SetScalarParameterValue(TEXT("Foam"), static_cast<float>(Num(Recipe, TEXT("foam"), 1.0)));
		const double Width = Num(Recipe, TEXT("width"), 1500);
		MID->SetScalarParameterValue(TEXT("WidthM"), static_cast<float>((Width + 2 * Num(Recipe, TEXT("overlap"), Width * 0.25)) / 100.0));
		return MID;
	}

	UMaterialInterface* MakeMaterial(const TSharedRef<FJsonObject>& Recipe, UObject* Outer)
	{
		// a weather-aware variant (puddles, mud, snow) when an extension provides one
		const FString WeatherPath = RuntimeToolsetExtensions::GetAsset(TEXT("material.terrain"));
		UMaterialInterface* Base = WeatherPath.IsEmpty() ? nullptr : LoadObject<UMaterialInterface>(nullptr, *WeatherPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
		if (!Base)
		{
			Base = LoadObject<UMaterialInterface>(nullptr, TerrainMaterialPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
		}
		if (!Base)
		{
			return nullptr;
		}
		struct FBiome { const TCHAR* Name; FLinearColor GrassA, GrassB, Dirt, Rock, Mud, Snow; float RockSlope, SnowLine; FLinearColor Sand, WetSand; float Patches; };
		static const FBiome Biomes[] = {
			{TEXT("temperate"), {0.10f, 0.16f, 0.045f}, {0.15f, 0.19f, 0.06f}, {0.20f, 0.15f, 0.10f}, {0.30f, 0.29f, 0.27f}, {0.12f, 0.10f, 0.07f}, {0.9f, 0.92f, 0.95f}, 0.45f, 1e9f,
				{0.50f, 0.43f, 0.31f}, {0.27f, 0.22f, 0.15f}, 0.62f},
			{TEXT("tropical"), {0.06f, 0.13f, 0.025f}, {0.10f, 0.17f, 0.035f}, {0.21f, 0.15f, 0.08f}, {0.26f, 0.24f, 0.21f}, {0.16f, 0.11f, 0.065f}, {0.9f, 0.92f, 0.95f}, 0.5f, 1e9f,
				{0.60f, 0.51f, 0.36f}, {0.36f, 0.29f, 0.19f}, 0.84f},
			{TEXT("desert"), {0.55f, 0.42f, 0.27f}, {0.60f, 0.46f, 0.30f}, {0.46f, 0.30f, 0.17f}, {0.42f, 0.30f, 0.21f}, {0.36f, 0.27f, 0.18f}, {0.9f, 0.92f, 0.95f}, 0.4f, 1e9f,
				{0.66f, 0.52f, 0.34f}, {0.38f, 0.29f, 0.19f}, 0.62f},
			{TEXT("savanna"), {0.36f, 0.30f, 0.13f}, {0.28f, 0.27f, 0.11f}, {0.40f, 0.22f, 0.12f}, {0.33f, 0.28f, 0.22f}, {0.20f, 0.14f, 0.09f}, {0.9f, 0.92f, 0.95f}, 0.45f, 1e9f,
				{0.62f, 0.50f, 0.33f}, {0.36f, 0.28f, 0.18f}, 0.62f},
			{TEXT("alpine"), {0.10f, 0.14f, 0.07f}, {0.14f, 0.16f, 0.08f}, {0.18f, 0.15f, 0.12f}, {0.32f, 0.32f, 0.33f}, {0.12f, 0.10f, 0.08f}, {0.9f, 0.92f, 0.95f}, 0.35f, 2500.f,
				{0.45f, 0.41f, 0.34f}, {0.24f, 0.22f, 0.18f}, 0.62f},
			{TEXT("arctic"), {0.80f, 0.83f, 0.88f}, {0.86f, 0.88f, 0.92f}, {0.30f, 0.28f, 0.26f}, {0.30f, 0.30f, 0.32f}, {0.25f, 0.24f, 0.22f}, {0.9f, 0.92f, 0.95f}, 0.5f, -1e9f,
				{0.48f, 0.45f, 0.40f}, {0.26f, 0.25f, 0.23f}, 0.62f},
		};
		FString Name = TEXT("temperate");
		Recipe->TryGetStringField(TEXT("biome"), Name);
		const FBiome* B = &Biomes[0];
		for (const FBiome& X : Biomes)
		{
			if (Name == X.Name)
			{
				B = &X;
			}
		}
		UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Base, Outer);
		MID->SetVectorParameterValue(TEXT("GrassA"), B->GrassA);
		MID->SetVectorParameterValue(TEXT("GrassB"), B->GrassB);
		MID->SetVectorParameterValue(TEXT("Dirt"), B->Dirt);
		MID->SetVectorParameterValue(TEXT("Rock"), B->Rock);
		MID->SetVectorParameterValue(TEXT("Mud"), B->Mud);
		MID->SetVectorParameterValue(TEXT("Snow"), B->Snow);
		MID->SetScalarParameterValue(TEXT("RockSlope"), B->RockSlope);
		// rippled beach sand when the project provides a ripple texture
		const FString RipplePath = RuntimeToolsetExtensions::GetAsset(TEXT("texture.sand_ripple"));
		if (UTexture* Ripple = RipplePath.IsEmpty() ? nullptr : LoadObject<UTexture>(nullptr, *RipplePath, nullptr, LOAD_NoWarn | LOAD_Quiet))
		{
			MID->SetTextureParameterValue(TEXT("SandTex"), Ripple);
			MID->SetScalarParameterValue(TEXT("SandRipple"), 1.f);
		}
		// heights in the recipe are relative to the terrain; the material compares world z
		const USceneComponent* At = Cast<USceneComponent>(Outer);
		const double BaseZ = At ? At->GetComponentLocation().Z : 0.0;
		MID->SetScalarParameterValue(TEXT("SnowLine"), static_cast<float>(Num(Recipe, TEXT("snow_line"), B->SnowLine) + BaseZ));
		MID->SetVectorParameterValue(TEXT("Sand"), B->Sand);
		MID->SetVectorParameterValue(TEXT("WetSand"), B->WetSand);
		MID->SetScalarParameterValue(TEXT("PatchStart"), B->Patches);
		if (Recipe->HasField(TEXT("sand_line")))
		{
			MID->SetScalarParameterValue(TEXT("SandLine"), static_cast<float>(Num(Recipe, TEXT("sand_line"), 0.0) + BaseZ));
		}
		if (Recipe->HasField(TEXT("sea_level")))
		{
			MID->SetScalarParameterValue(TEXT("SeaLevel"), static_cast<float>(Num(Recipe, TEXT("sea_level"), 0.0) + BaseZ));
		}
		FVector C;
		if (RuntimeJson::ToVector(Recipe->TryGetField(TEXT("color")), C))  // a colour overrides the ground cover colour
		{
			MID->SetVectorParameterValue(TEXT("GrassA"), FLinearColor(C.X, C.Y, C.Z));
			MID->SetVectorParameterValue(TEXT("GrassB"), FLinearColor(C.X * 1.15f, C.Y * 1.1f, C.Z));
		}
		return MID;
	}
}
