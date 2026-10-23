#include "GisToolset.h"
#include "RuntimeJson.h"
#include "RuntimeSceneGraph.h"
#include "RuntimeToolInvocation.h"
#include "RuntimeToolsetsCore.h"
#include "Components/SceneComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Math/RandomStream.h"

FGisPlot* UGisSubsystem::Find(const FString& Id)
{
	return Plots.FindByPredicate([&](const FGisPlot& P) { return P.Id.Equals(Id, ESearchCase::IgnoreCase); });
}

namespace Gis
{
	using FGisPoly = TArray<FVector2D>;
	using FGisFlags = TArray<bool>;

	static double SignedArea(const FGisPoly& P)
	{
		double A = 0.0;
		for (int32 i = 0, n = P.Num(); i < n; ++i)
		{
			const FVector2D& a = P[i];
			const FVector2D& b = P[(i + 1) % n];
			A += a.X * b.Y - b.X * a.Y;
		}
		return A * 0.5;
	}

	static FVector2D Centroid(const FGisPoly& P)
	{
		double A = 0.0, Cx = 0.0, Cy = 0.0;
		for (int32 i = 0, n = P.Num(); i < n; ++i)
		{
			const FVector2D& a = P[i];
			const FVector2D& b = P[(i + 1) % n];
			const double c = a.X * b.Y - b.X * a.Y;
			A += c;
			Cx += (a.X + b.X) * c;
			Cy += (a.Y + b.Y) * c;
		}
		if (FMath::Abs(A) < 1e-6)
		{
			FVector2D S(0, 0);
			for (const FVector2D& p : P)
			{
				S += p;
			}
			return P.Num() ? S / P.Num() : S;
		}
		return FVector2D(Cx / (3.0 * A), Cy / (3.0 * A));
	}

	static bool IsConvex(const FGisPoly& P)
	{
		const int32 n = P.Num();
		if (n < 4)
		{
			return true;
		}
		int32 Sign = 0;
		for (int32 i = 0; i < n; ++i)
		{
			const FVector2D d1 = P[(i + 1) % n] - P[i];
			const FVector2D d2 = P[(i + 2) % n] - P[(i + 1) % n];
			const double c = d1.X * d2.Y - d1.Y * d2.X;
			if (FMath::Abs(c) < 1e-6)
			{
				continue;
			}
			const int32 s = c > 0 ? 1 : -1;
			if (Sign == 0)
			{
				Sign = s;
			}
			else if (s != Sign)
			{
				return false;
			}
		}
		return true;
	}

	/** Positive signed area; street flags follow their edges when the order flips. */
	static void MakeCCW(FGisPoly& P, FGisFlags& F)
	{
		if (SignedArea(P) >= 0.0)
		{
			return;
		}
		const int32 n = P.Num();
		FGisPoly R;
		FGisFlags RF;
		R.SetNum(n);
		RF.SetNum(n);
		for (int32 k = 0; k < n; ++k)
		{
			R[k] = P[n - 1 - k];
			RF[k] = F[((n - 2 - k) % n + n) % n];
		}
		P = MoveTemp(R);
		F = MoveTemp(RF);
	}

	static void Clean(FGisPoly& P, FGisFlags& F)
	{
		for (int32 i = P.Num() - 1; i >= 0 && P.Num() > 2; --i)
		{
			const int32 j = (i + 1) % P.Num();
			if (i != j && FVector2D::Distance(P[i], P[j]) < 1.0)
			{
				P.RemoveAt(j);
				F.RemoveAt(j);
				if (j < i)
				{
					--i;
				}
			}
		}
	}

	/** Keeps the part where dot(p - P0, N) >= Off. Each vertex carries the flag of the edge starting at it; the new edge along
	 *  the cut gets NewFlag. Exact for convex polygons. */
	static void Clip(const FGisPoly& In, const FGisFlags& InF, const FVector2D& P0, const FVector2D& N, double Off, bool NewFlag, FGisPoly& Out, FGisFlags& OutF)
	{
		Out.Reset();
		OutF.Reset();
		const int32 n = In.Num();
		for (int32 i = 0; i < n; ++i)
		{
			const FVector2D& a = In[i];
			const FVector2D& b = In[(i + 1) % n];
			const double da = FVector2D::DotProduct(a - P0, N) - Off;
			const double db = FVector2D::DotProduct(b - P0, N) - Off;
			if (da >= 0.0)
			{
				Out.Add(a);
				OutF.Add(InF[i]);
			}
			if ((da >= 0.0) != (db >= 0.0))
			{
				const double t = da / (da - db);
				Out.Add(a + (b - a) * t);
				OutF.Add(da >= 0.0 ? NewFlag : InF[i]);
			}
		}
		Clean(Out, OutF);
	}

	static FGisPoly Hull(FGisPoly Pts)
	{
		Pts.Sort([](const FVector2D& A, const FVector2D& B) { return A.X < B.X || (A.X == B.X && A.Y < B.Y); });
		if (Pts.Num() < 3)
		{
			return Pts;
		}
		auto Cross = [](const FVector2D& O, const FVector2D& A, const FVector2D& B) { return (A.X - O.X) * (B.Y - O.Y) - (A.Y - O.Y) * (B.X - O.X); };
		FGisPoly H;
		for (const FVector2D& p : Pts)
		{
			while (H.Num() >= 2 && Cross(H[H.Num() - 2], H.Last(), p) <= 0)
			{
				H.Pop();
			}
			H.Add(p);
		}
		const int32 Lower = H.Num() + 1;
		for (int32 i = Pts.Num() - 2; i >= 0; --i)
		{
			while (H.Num() >= Lower && Cross(H[H.Num() - 2], H.Last(), Pts[i]) <= 0)
			{
				H.Pop();
			}
			H.Add(Pts[i]);
		}
		H.Pop();
		return H;
	}

	struct FObb
	{
		FVector2D Center = FVector2D::ZeroVector;
		FVector2D U = FVector2D(1, 0);   // long side
		double LenU = 0.0, LenV = 0.0;
	};

	/** Smallest-area rotated rectangle (rotating calipers over the hull edges). */
	static FObb Obb(const FGisPoly& P)
	{
		const FGisPoly H = Hull(P);
		FObb Best;
		double BestArea = TNumericLimits<double>::Max();
		for (int32 i = 0, n = H.Num(); i < n; ++i)
		{
			FVector2D U = H[(i + 1) % n] - H[i];
			if (U.SizeSquared() < 1e-6)
			{
				continue;
			}
			U.Normalize();
			const FVector2D V(-U.Y, U.X);
			double u0 = 1e30, u1 = -1e30, v0 = 1e30, v1 = -1e30;
			for (const FVector2D& p : H)
			{
				const double a = FVector2D::DotProduct(p, U), b = FVector2D::DotProduct(p, V);
				u0 = FMath::Min(u0, a);
				u1 = FMath::Max(u1, a);
				v0 = FMath::Min(v0, b);
				v1 = FMath::Max(v1, b);
			}
			const double Area = (u1 - u0) * (v1 - v0);
			if (Area < BestArea)
			{
				BestArea = Area;
				const bool bSwap = (v1 - v0) > (u1 - u0);
				Best.Center = U * ((u0 + u1) / 2) + V * ((v0 + v1) / 2);
				Best.U = bSwap ? V : U;
				Best.LenU = bSwap ? (v1 - v0) : (u1 - u0);
				Best.LenV = bSwap ? (u1 - u0) : (v1 - v0);
			}
		}
		return Best;
	}

	static bool Contains(const FGisPoly& P, const FVector2D& Q)
	{
		bool bIn = false;
		for (int32 i = 0, j = P.Num() - 1; i < P.Num(); j = i++)
		{
			if (((P[i].Y > Q.Y) != (P[j].Y > Q.Y)) && (Q.X < (P[j].X - P[i].X) * (Q.Y - P[i].Y) / (P[j].Y - P[i].Y) + P[i].X))
			{
				bIn = !bIn;
			}
		}
		return bIn;
	}

	/** Shrinks a polygon by D: exact edge offsets for convex shapes, a scale toward the centre otherwise. */
	static FGisPoly Inset(const FGisPoly& P, double D)
	{
		const int32 n = P.Num();
		if (n < 3)
		{
			return P;
		}
		if (!IsConvex(P))
		{
			const FVector2D C = Centroid(P);
			const double R = FMath::Sqrt(FMath::Abs(SignedArea(P)));
			const double S = FMath::Clamp(1.0 - 2.0 * D / FMath::Max(R, 1.0), 0.5, 1.0);
			FGisPoly Out;
			for (const FVector2D& p : P)
			{
				Out.Add(C + (p - C) * S);
			}
			return Out;
		}
		FGisPoly Out;
		for (int32 i = 0; i < n; ++i)
		{
			const FVector2D a0 = P[(i + n - 1) % n], a1 = P[i], b1 = P[(i + 1) % n];
			FVector2D d1 = (a1 - a0).GetSafeNormal(), d2 = (b1 - a1).GetSafeNormal();
			const FVector2D n1(-d1.Y, d1.X), n2(-d2.Y, d2.X);   // inward normals (CCW)
			const FVector2D p1 = a0 + n1 * D, p2 = a1 + n2 * D;
			const double den = d1.X * d2.Y - d1.Y * d2.X;
			if (FMath::Abs(den) < 1e-9)
			{
				Out.Add(a1 + n2 * D);
				continue;
			}
			const FVector2D w = p2 - p1;
			const double t = (w.X * d2.Y - w.Y * d2.X) / den;
			Out.Add(p1 + d1 * t);
		}
		return SignedArea(Out) > 0 ? Out : P;
	}

	static TArray<TSharedPtr<FJsonValue>> Profile(const FGisPoly& P)
	{
		TArray<TSharedPtr<FJsonValue>> Arr;
		for (const FVector2D& p : P)
		{
			Arr.Add(MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>{MakeShared<FJsonValueNumber>(FMath::RoundToDouble(p.X * 10) / 10),
				MakeShared<FJsonValueNumber>(FMath::RoundToDouble(p.Y * 10) / 10)}));
		}
		return Arr;
	}

	static void Rgb(const FString& Zoning, double& R, double& G, double& B)
	{
		// soft masterplan land-use tones (linear colour)
		struct FC { const TCHAR* Name; double R, G, B; };
		static const FC Colors[] = {{TEXT("residential"), 0.92, 0.74, 0.32}, {TEXT("commercial"), 0.80, 0.27, 0.23}, {TEXT("mixed"), 0.88, 0.48, 0.22},
			{TEXT("industrial"), 0.50, 0.42, 0.70}, {TEXT("civic"), 0.30, 0.50, 0.78}, {TEXT("green"), 0.26, 0.50, 0.16}, {TEXT("water"), 0.16, 0.36, 0.60}};
		for (const FC& C : Colors)
		{
			if (Zoning.Equals(C.Name, ESearchCase::IgnoreCase))
			{
				R = C.R;
				G = C.G;
				B = C.B;
				return;
			}
		}
		R = G = B = 0.45;
	}

	/** Runs another tool from inside this one (synchronous tools only); true on success. */
	static bool Nested(const FString& Tool, const TSharedPtr<FJsonObject>& Args, TSharedPtr<FJsonObject>* OutPayload = nullptr, FString* OutError = nullptr)
	{
		FToolInvocation* Cur = RuntimeToolCall::Current();
		if (!Cur)
		{
			return false;
		}
		struct FState { bool bDone = false; bool bOk = false; TSharedPtr<FJsonObject> Payload; FString Message; };
		TSharedRef<FState> S = MakeShared<FState>();
		IRuntimeToolsetsCore::Get().ExecuteNested(Tool, Args, Cur->GetCaller(), Cur->GetDepth() + 1, [S](const FToolResult& R)
		{
			S->bDone = true;
			S->bOk = R.IsOk();
			S->Payload = R.Payload;
			S->Message = R.Message;
		});
		if (OutPayload)
		{
			*OutPayload = S->Payload;
		}
		if (OutError)
		{
			*OutError = S->Message;
		}
		return S->bDone && S->bOk;
	}

	static UGisSubsystem* Sub()
	{
		UWorld* World = RuntimeToolCall::GetWorld();
		return World ? World->GetSubsystem<UGisSubsystem>() : nullptr;
	}

	static FGisPlotSummary Summary(const FGisPlot& P)
	{
		FGisPlotSummary S;
		S.Id = P.Id;
		S.AreaM2 = static_cast<float>(FMath::RoundToDouble(FMath::Abs(SignedArea(P.Poly)) / 1e4 * 10) / 10);
		const FVector2D C = Centroid(P.Poly);
		S.Centroid = FVector(FMath::RoundToDouble(C.X), FMath::RoundToDouble(C.Y), P.Z);
		S.Zoning = P.Zoning;
		S.Vertices = P.Poly.Num();
		S.StreetEdges = P.Street.FilterByPredicate([](bool b) { return b; }).Num();
		S.Building = P.Building;
		S.Block = P.Block;
		return S;
	}

	static FGisPlotInfo Info(const FGisPlot& P)
	{
		FGisPlotInfo I;
		I.Id = P.Id;
		const int32 n = P.Poly.Num();
		double Best = -1, BestAny = -1;
		int32 FrontAny = 0;
		for (int32 i = 0; i < n; ++i)
		{
			const FVector2D a = P.Poly[i], b = P.Poly[(i + 1) % n];
			I.Polygon.Add(FVector(a.X, a.Y, P.Z));
			FGisEdge& E = I.Edges.AddDefaulted_GetRef();
			E.A = FVector(a.X, a.Y, P.Z);
			E.B = FVector(b.X, b.Y, P.Z);
			E.Length = static_cast<float>(FVector2D::Distance(a, b));
			E.bStreet = P.Street.IsValidIndex(i) && P.Street[i];
			const FVector2D d = (b - a).GetSafeNormal();
			E.OutwardYaw = static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(-d.X, d.Y)));
			if (E.bStreet && E.Length > Best)
			{
				Best = E.Length;
				I.Front = i;
			}
			if (E.Length > BestAny)
			{
				BestAny = E.Length;
				FrontAny = i;
			}
		}
		if (Best < 0)
		{
			I.Front = FrontAny;
		}
		const double A = FMath::Abs(SignedArea(P.Poly));
		I.AreaM2 = static_cast<float>(A / 1e4);
		const FVector2D C = Centroid(P.Poly);
		I.Centroid = FVector(C.X, C.Y, P.Z);
		const FObb O = Obb(P.Poly);
		I.ObbCenter = FVector(O.Center.X, O.Center.Y, P.Z);
		I.ObbSize = FVector2D(O.LenU, O.LenV);
		I.ObbYaw = static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(O.U.Y, O.U.X)));
		I.Rectangularity = static_cast<float>(O.LenU * O.LenV > 0 ? A / (O.LenU * O.LenV) : 0);
		I.bConvex = IsConvex(P.Poly);
		I.Zoning = P.Zoning;
		I.Props = FRuntimeToolJson::Make(P.Props.IsValid() ? P.Props : MakeShared<FJsonObject>());
		I.Building = P.Building;
		I.Block = P.Block;
		return I;
	}

	/** Rebuilds the map: street surface, raised blocks, zoning-coloured plots, id labels. */
	static void Refresh(UGisSubsystem& S)
	{
		if (!S.VisNode.IsEmpty())
		{
			TSharedPtr<FJsonObject> Del = MakeShared<FJsonObject>();
			Del->SetStringField(TEXT("target"), S.VisNode);
			Nested(TEXT("scene.delete_node"), Del);
			S.VisNode.Reset();
		}
		if (S.Labels.IsValid())
		{
			S.Labels->Destroy();
			S.Labels.Reset();
		}
		TArray<TSharedPtr<FJsonValue>> Parts;
		auto Slab = [&Parts](const FGisPoly& P, double Z, double H, const TCHAR* Material, double R, double G, double B)
		{
			if (P.Num() < 3 || FMath::Abs(SignedArea(P)) < 100.0)
			{
				return;
			}
			TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetStringField(TEXT("shape"), TEXT("extrude"));
			O->SetArrayField(TEXT("profile"), Profile(P));
			O->SetNumberField(TEXT("height"), H);
			O->SetArrayField(TEXT("location"), {MakeShared<FJsonValueNumber>(0), MakeShared<FJsonValueNumber>(0), MakeShared<FJsonValueNumber>(Z)});
			O->SetStringField(TEXT("material"), Material);
			O->SetArrayField(TEXT("color"), {MakeShared<FJsonValueNumber>(R), MakeShared<FJsonValueNumber>(G), MakeShared<FJsonValueNumber>(B)});
			O->SetNumberField(TEXT("roughness"), 0.9);
			Parts.Add(MakeShared<FJsonValueObject>(O));
		};
		if (S.bShowStreets)
		{
			for (const FGisPoly& A : S.Areas)
			{
				Slab(A, S.AreaZ + 0.5, 3, TEXT("asphalt"), 0.10, 0.10, 0.11);
			}
			for (const FGisPoly& B : S.Blocks)
			{
				Slab(B, S.AreaZ + 0.5, 12, TEXT("concrete"), 0.62, 0.61, 0.58);
			}
		}
		if (S.bShowPlots)
		{
			for (const FGisPlot& P : S.Plots)
			{
				double R, G, B;
				Rgb(P.Zoning, R, G, B);
				const bool bOnBlock = !P.Block.IsEmpty() && S.bShowStreets;
				const bool bGreen = P.Zoning.Equals(TEXT("green"), ESearchCase::IgnoreCase);
				const bool bWater = P.Zoning.Equals(TEXT("water"), ESearchCase::IgnoreCase);
				Slab(Inset(P.Poly, bOnBlock ? 90 : 40), P.Z + (bOnBlock ? 12.5 : 1.0), 3, bGreen ? TEXT("grass") : bWater ? TEXT("water") : TEXT("plaster"), R, G, B);
			}
		}
		for (int32 Start = 0; Start < Parts.Num(); Start += 8000)
		{
			TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("name"), TEXT("GIS_Map"));
			Args->SetArrayField(TEXT("parts"), TArray<TSharedPtr<FJsonValue>>(Parts.GetData() + Start, FMath::Min(8000, Parts.Num() - Start)));
			Args->SetBoolField(TEXT("repair_coplanar"), false);
			TSharedPtr<FJsonObject> Out;
			if (Nested(TEXT("build.create_assembly"), Args, &Out) && Out.IsValid())
			{
				S.VisNode = Out->GetStringField(TEXT("id"));
			}
		}
		UWorld* World = RuntimeToolCall::GetWorld();
		if (S.bShowLabels && World && S.Plots.Num() && S.Plots.Num() <= S.MaxLabels)
		{
			FActorSpawnParameters SP;
			SP.Name = MakeUniqueObjectName(World, AActor::StaticClass(), TEXT("GIS_Labels"));
			AActor* L = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, SP);
			USceneComponent* Root = NewObject<USceneComponent>(L, TEXT("Root"));
			L->SetRootComponent(Root);
			Root->RegisterComponent();
			for (const FGisPlot& P : S.Plots)
			{
				const FVector2D C = Centroid(P.Poly);
				const double Size = FMath::Clamp(FMath::Sqrt(FMath::Abs(SignedArea(P.Poly))) * 0.07, 40.0, 260.0);
				UTextRenderComponent* T = NewObject<UTextRenderComponent>(L);
				T->SetupAttachment(Root);
				T->SetText(FText::FromString(P.Id));
				T->SetWorldSize(static_cast<float>(Size));
				T->SetHorizontalAlignment(EHTA_Center);
				T->SetVerticalAlignment(EVRTA_TextCenter);
				T->SetTextRenderColor(FColor(60, 56, 50));
				T->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				T->SetWorldLocationAndRotation(FVector(C.X, C.Y, P.Z + 40), FRotator(90.0, 180.0, 0.0));
				T->RegisterComponent();
			}
			S.Labels = L;
		}
	}

	struct FZoner
	{
		TArray<TPair<FString, double>> W;
		FVector2D Center;
		double Radius = 1;
		bool bCenter = true;

		FString Pick(FRandomStream& R, const FVector2D& At) const
		{
			const double dn = FMath::Clamp(FVector2D::Distance(At, Center) / FMath::Max(Radius, 1.0), 0.0, 1.0);
			TArray<double> Ws;
			double T = 0;
			for (const TPair<FString, double>& P : W)
			{
				double w = P.Value;
				if (bCenter)
				{
					if (P.Key == TEXT("commercial") || P.Key == TEXT("mixed")) w *= 1.7 - 1.5 * dn;
					else if (P.Key == TEXT("residential")) w *= 0.4 + 1.2 * dn;
					else if (P.Key == TEXT("industrial")) w *= 0.1 + 1.8 * dn;
				}
				w = FMath::Max(0.0, w);
				Ws.Add(w);
				T += w;
			}
			double r = R.FRandRange(0.0, T);
			for (int32 i = 0; i < W.Num(); ++i)
			{
				r -= Ws[i];
				if (r <= 0)
				{
					return W[i].Key;
				}
			}
			return W.Num() ? W.Last().Key : TEXT("residential");
		}
	};

	/** Streets by recursive cuts across the long side: blocks of about the target area. */
	static void SplitBlocks(const FGisPoly& P, const FGisFlags& F, int32 Depth, FRandomStream& R, double Target, double Street, double AngleJitter,
		double OffsetJitter, TArray<TPair<FGisPoly, FGisFlags>>& Out)
	{
		const double A = FMath::Abs(SignedArea(P));
		if (A <= Target * R.FRandRange(0.75, 1.35) || Depth > 14 || P.Num() < 3)
		{
			Out.Add({P, F});
			return;
		}
		const FObb O = Obb(P);
		const double Ang = FMath::DegreesToRadians(R.FRandRange(-AngleJitter, AngleJitter));
		const FVector2D N(O.U.X * FMath::Cos(Ang) - O.U.Y * FMath::Sin(Ang), O.U.X * FMath::Sin(Ang) + O.U.Y * FMath::Cos(Ang));
		const FVector2D P0 = O.Center + O.U * (O.LenU * R.FRandRange(-OffsetJitter, OffsetJitter));
		const double Gap = Street * (Depth < 2 ? 1.4 : 1.0) * 0.5;
		FGisPoly A1, A2;
		FGisFlags F1, F2;
		Clip(P, F, P0, N, Gap, true, A1, F1);
		Clip(P, F, P0, -N, Gap, true, A2, F2);
		if (A1.Num() < 3 || A2.Num() < 3 || FMath::Abs(SignedArea(A1)) < Target * 0.2 || FMath::Abs(SignedArea(A2)) < Target * 0.2)
		{
			Out.Add({P, F});
			return;
		}
		SplitBlocks(A1, F1, Depth + 1, R, Target, Street, AngleJitter, OffsetJitter, Out);
		SplitBlocks(A2, F2, Depth + 1, R, Target, Street, AngleJitter, OffsetJitter, Out);
	}

	/** Keeps the part of a polygon inside a convex area (clip by each of its edges). */
	static void ClipToArea(const FGisPoly& P, const FGisFlags& F, const FGisPoly& Area, FGisPoly& Out, FGisFlags& OutF)
	{
		Out = P;
		OutF = F;
		for (int32 i = 0, n = Area.Num(); i < n && Out.Num() >= 3; ++i)
		{
			const FVector2D a = Area[i], b = Area[(i + 1) % n];
			const FVector2D d = (b - a).GetSafeNormal();
			FGisPoly T;
			FGisFlags TF;
			Clip(Out, OutF, a, FVector2D(-d.Y, d.X), 0, true, T, TF);
			Out = MoveTemp(T);
			OutF = MoveTemp(TF);
		}
	}

	/** Cuts a strip into lots across direction U, widths from the range; returns the lots. */
	static void CutAlong(const FGisPoly& P, const FGisFlags& F, const FVector2D& U, FVector2D Width, int32 Parts, FRandomStream& R, TArray<TPair<FGisPoly, FGisFlags>>& Out)
	{
		double s0 = 1e30, s1 = -1e30;
		for (const FVector2D& p : P)
		{
			const double s = FVector2D::DotProduct(p, U);
			s0 = FMath::Min(s0, s);
			s1 = FMath::Max(s1, s);
		}
		const double L = s1 - s0;
		TArray<double> Cuts;
		if (Parts > 1)
		{
			for (int32 k = 1; k < Parts; ++k)
			{
				Cuts.Add(s0 + L * k / Parts);
			}
		}
		else
		{
			double s = s0;
			Width.X = FMath::Max(200.0, Width.X);
			Width.Y = FMath::Max(Width.X, Width.Y);
			while (s1 - s > Width.Y * 1.25)
			{
				s += R.FRandRange(Width.X, Width.Y);
				Cuts.Add(s);
			}
		}
		FGisPoly Rest = P;
		FGisFlags RestF = F;
		for (double c : Cuts)
		{
			FGisPoly Piece, Next;
			FGisFlags PieceF, NextF;
			const FVector2D P0 = U * c;
			Clip(Rest, RestF, P0, -U, 0, false, Piece, PieceF);
			Clip(Rest, RestF, P0, U, 0, false, Next, NextF);
			if (Piece.Num() >= 3)
			{
				Out.Add({Piece, PieceF});
			}
			Rest = MoveTemp(Next);
			RestF = MoveTemp(NextF);
			if (Rest.Num() < 3)
			{
				return;
			}
		}
		if (Rest.Num() >= 3)
		{
			Out.Add({Rest, RestF});
		}
	}

	static FString NewPlot(UGisSubsystem& S, FGisPoly P, FGisFlags F, double Z, const FString& Zoning, const FString& Block, const TSharedPtr<FJsonObject>& Props)
	{
		MakeCCW(P, F);
		FGisPlot& G = S.Plots.AddDefaulted_GetRef();
		G.Id = FString::Printf(TEXT("P%d"), S.NextPlot++);
		while (S.Plots.ContainsByPredicate([&](const FGisPlot& O) { return &O != &G && O.Id == G.Id; }))
		{
			G.Id = FString::Printf(TEXT("P%d"), S.NextPlot++);
		}
		G.Poly = MoveTemp(P);
		G.Street = MoveTemp(F);
		G.Z = Z;
		G.Zoning = Zoning;
		G.Block = Block;
		G.Props = Props.IsValid() ? Props : MakeShared<FJsonObject>();
		return G.Id;
	}

	static void ClearAll(UGisSubsystem& S)
	{
		S.Plots.Reset();
		S.Blocks.Reset();
		S.Areas.Reset();
		S.NextPlot = 1;
		S.NextBlock = 1;
	}
}

FGisCreateResult URuntimeGisToolset::CreatePlots(const TArray<FVector>& Area, const FRuntimeToolJson& Zoning, FVector Center, FVector2D Size, EGisLayout Layout,
	float StreetWidth, FVector2D BlockSize, FVector2D LotWidth, float LotDepth, bool bCenterCommercial, int32 Seed, bool bReplace)
{
	using namespace Gis;
	FGisCreateResult Out;
	UGisSubsystem* S = Sub();
	if (!S)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::NoWorld, TEXT("no world"));
		return Out;
	}
	FGisPoly Land;
	double Z = Center.Z;
	if (Area.Num() >= 3)
	{
		Z = 0;
		for (const FVector& p : Area)
		{
			Land.Add(FVector2D(p.X, p.Y));
			Z += p.Z / Area.Num();
		}
	}
	else
	{
		const FVector2D H = Size * 0.5;
		Land = {FVector2D(Center.X - H.X, Center.Y - H.Y), FVector2D(Center.X + H.X, Center.Y - H.Y), FVector2D(Center.X + H.X, Center.Y + H.Y),
			FVector2D(Center.X - H.X, Center.Y + H.Y)};
	}
	FGisFlags LandF;
	LandF.Init(true, Land.Num());
	MakeCCW(Land, LandF);
	if (FMath::Abs(SignedArea(Land)) < 1e6)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, TEXT("the area is smaller than 100 m2"), TEXT("Give size in cm, e.g. [40000,30000]"));
		return Out;
	}
	if (bReplace)
	{
		ClearAll(*S);
	}
	FRandomStream R(Seed >= 0 ? Seed : static_cast<int32>(FPlatformTime::Cycles64() & 0x7fffffff));
	StreetWidth = FMath::Max(200.f, StreetWidth);
	BlockSize.X = FMath::Max(1500.0, BlockSize.X);
	BlockSize.Y = FMath::Max(1200.0, BlockSize.Y);
	S->Areas.Add(Land);
	S->AreaZ = Z;
	// a ring road inside the edge of the land
	const FGisPoly Inner = IsConvex(Land) ? Inset(Land, StreetWidth * 0.5) : Land;
	TArray<TPair<FGisPoly, FGisFlags>> Blocks;
	const double Target = BlockSize.X * BlockSize.Y;
	const FVector2D C = Centroid(Land);
	double Radius = 1;
	for (const FVector2D& p : Land)
	{
		Radius = FMath::Max(Radius, FVector2D::Distance(p, C));
	}
	if (Layout == EGisLayout::Grid)
	{
		const FObb O = Obb(Inner);
		const FVector2D U = O.U, V(-O.U.Y, O.U.X);
		const int32 Nx = FMath::Max(1, FMath::RoundToInt(O.LenU / (BlockSize.X + StreetWidth)));
		const int32 Ny = FMath::Max(1, FMath::RoundToInt(O.LenV / (BlockSize.Y + StreetWidth)));
		const double Cx = O.LenU / Nx, Cy = O.LenV / Ny, h = StreetWidth * 0.5;
		const FVector2D Base = O.Center - U * (O.LenU / 2) - V * (O.LenV / 2);
		for (int32 i = 0; i < Nx; ++i)
		{
			for (int32 j = 0; j < Ny; ++j)
			{
				const FVector2D a = Base + U * (i * Cx + (i ? h : 0)) + V * (j * Cy + (j ? h : 0));
				const FVector2D b = Base + U * ((i + 1) * Cx - (i < Nx - 1 ? h : 0)) + V * ((j + 1) * Cy - (j < Ny - 1 ? h : 0));
				const FVector2D du = U * FVector2D::DotProduct(b - a, U), dv = V * FVector2D::DotProduct(b - a, V);
				FGisPoly Cell = {a, a + du, a + du + dv, a + dv};
				FGisFlags CF;
				CF.Init(true, 4);
				MakeCCW(Cell, CF);
				FGisPoly Clipped;
				FGisFlags ClippedF;
				ClipToArea(Cell, CF, Inner, Clipped, ClippedF);
				if (Clipped.Num() >= 3 && FMath::Abs(SignedArea(Clipped)) > Target * 0.15)
				{
					Blocks.Add({Clipped, ClippedF});
				}
			}
		}
	}
	else if (Layout == EGisLayout::Radial)
	{
		const double Ring = BlockSize.Y + StreetWidth, h = StreetWidth * 0.5;
		const int32 Rings = FMath::Max(1, FMath::CeilToInt(Radius / Ring));
		for (int32 k = 0; k < Rings; ++k)
		{
			const double r0 = k * Ring + (k ? h : Ring * 0.35), r1 = (k + 1) * Ring - h;
			if (k == 0)
			{
				// a round place in the middle
				FGisPoly Circle;
				for (int32 a = 0; a < 24; ++a)
				{
					const double t = 2 * PI * a / 24;
					Circle.Add(C + FVector2D(FMath::Cos(t), FMath::Sin(t)) * (r1 * 0.6));
				}
				FGisFlags CF;
				CF.Init(true, Circle.Num());
				FGisPoly Cl;
				FGisFlags ClF;
				ClipToArea(Circle, CF, Inner, Cl, ClF);
				if (Cl.Num() >= 3)
				{
					Blocks.Add({Cl, ClF});
				}
				continue;
			}
			const double rm = (r0 + r1) / 2;
			const int32 Sectors = FMath::Max(4, FMath::RoundToInt(2 * PI * rm / (BlockSize.X + StreetWidth)));
			const double Delta = h / rm;
			for (int32 s = 0; s < Sectors; ++s)
			{
				const double a0 = 2 * PI * s / Sectors + Delta, a1 = 2 * PI * (s + 1) / Sectors - Delta;
				FGisPoly Sec;
				const int32 Steps = 4;
				for (int32 t = 0; t <= Steps; ++t)
				{
					const double a = a0 + (a1 - a0) * t / Steps;
					Sec.Add(C + FVector2D(FMath::Cos(a), FMath::Sin(a)) * r0);
				}
				for (int32 t = Steps; t >= 0; --t)
				{
					const double a = a0 + (a1 - a0) * t / Steps;
					Sec.Add(C + FVector2D(FMath::Cos(a), FMath::Sin(a)) * r1);
				}
				FGisFlags SF;
				SF.Init(true, Sec.Num());
				MakeCCW(Sec, SF);
				FGisPoly Cl;
				FGisFlags ClF;
				ClipToArea(Sec, SF, Inner, Cl, ClF);
				if (Cl.Num() >= 3 && FMath::Abs(SignedArea(Cl)) > Target * 0.15)
				{
					Blocks.Add({Cl, ClF});
				}
			}
		}
	}
	else
	{
		const bool bOrganic = Layout == EGisLayout::Organic;
		FGisFlags InnerF;
		InnerF.Init(true, Inner.Num());
		SplitBlocks(Inner, InnerF, 0, R, Target, StreetWidth, bOrganic ? 24.0 : 4.0, bOrganic ? 0.3 : 0.12, Blocks);
	}
	// zoning per block, then lots along the streets
	FZoner Zoner;
	Zoner.Center = C;
	Zoner.Radius = Radius;
	Zoner.bCenter = bCenterCommercial;
	if (const TSharedPtr<FJsonObject> ZO = Zoning.AsObject())
	{
		for (const TPair<FString, TSharedPtr<FJsonValue>>& P : ZO->Values)
		{
			Zoner.W.Add({P.Key.ToLower(), P.Value->AsNumber()});
		}
	}
	if (Zoner.W.Num() == 0)
	{
		Zoner.W = {{TEXT("residential"), 5}, {TEXT("commercial"), 2}, {TEXT("mixed"), 2}, {TEXT("industrial"), 1}, {TEXT("green"), 1}, {TEXT("civic"), 0.5}};
	}
	for (const TPair<FGisPoly, FGisFlags>& B : Blocks)
	{
		const FString BlockId = FString::Printf(TEXT("B%d"), S->NextBlock++);
		S->Blocks.Add(B.Key);
		const FString Z0 = Zoner.Pick(R, Centroid(B.Key));
		if (Z0 == TEXT("green") || Z0 == TEXT("civic") || Layout == EGisLayout::Radial && FVector2D::Distance(Centroid(B.Key), C) < 1.0)
		{
			NewPlot(*S, B.Key, B.Value, Z, Layout == EGisLayout::Radial && FVector2D::Distance(Centroid(B.Key), C) < 1.0 ? TEXT("green") : Z0, BlockId, nullptr);
			continue;
		}
		const FObb O = Obb(B.Key);
		const FVector2D U = O.U, V(-O.U.Y, O.U.X);
		TArray<TPair<FGisPoly, FGisFlags>> Rows;
		const double Depth = LotDepth > 0 ? LotDepth : O.LenV / 2;
		if (O.LenV < 2400)
		{
			Rows.Add(B);   // shallow block: one row of through-lots
		}
		else if (LotDepth > 0 && O.LenV > 2 * LotDepth + 800)
		{
			// front row, back row and a shared yard in between
			FGisPoly Front, Back, Mid, T;
			FGisFlags FrontF, BackF, MidF, TF;
			const FVector2D P0 = O.Center - V * (O.LenV / 2 - Depth);
			const FVector2D P1 = O.Center + V * (O.LenV / 2 - Depth);
			Clip(B.Key, B.Value, P0, -V, 0, false, Front, FrontF);
			Clip(B.Key, B.Value, P1, V, 0, false, Back, BackF);
			Clip(B.Key, B.Value, P0, V, 0, false, T, TF);
			Clip(T, TF, P1, -V, 0, false, Mid, MidF);
			Rows.Add({Front, FrontF});
			Rows.Add({Back, BackF});
			if (Mid.Num() >= 3)
			{
				NewPlot(*S, Mid, MidF, Z, TEXT("green"), BlockId, nullptr);
			}
		}
		else
		{
			FGisPoly A1, A2;
			FGisFlags F1, F2;
			Clip(B.Key, B.Value, O.Center, V, 0, false, A1, F1);
			Clip(B.Key, B.Value, O.Center, -V, 0, false, A2, F2);
			Rows.Add({A1, F1});
			Rows.Add({A2, F2});
		}
		for (const TPair<FGisPoly, FGisFlags>& Row : Rows)
		{
			if (Row.Key.Num() < 3)
			{
				continue;
			}
			TArray<TPair<FGisPoly, FGisFlags>> Lots;
			CutAlong(Row.Key, Row.Value, U, LotWidth, 0, R, Lots);
			for (TPair<FGisPoly, FGisFlags>& L : Lots)
			{
				if (FMath::Abs(SignedArea(L.Key)) < 3e5)   // under 30 m2: a sliver
				{
					continue;
				}
				// mostly the block's use, sometimes a neighbour's
				const FString Zn = R.FRand() < 0.85 ? Z0 : Zoner.Pick(R, Centroid(L.Key));
				NewPlot(*S, L.Key, L.Value, Z, Zn == TEXT("green") ? Z0 : Zn, BlockId, nullptr);
			}
		}
	}
	Refresh(*S);
	Out.Plots = S->Plots.Num();
	Out.Blocks = S->Blocks.Num();
	Out.AreaM2 = static_cast<float>(FMath::Abs(SignedArea(Land)) / 1e4);
	for (const FGisPlot& P : S->Plots)
	{
		Out.Zoning.FindOrAdd(P.Zoning)++;
		if (Out.Sample.Num() < 12)
		{
			Out.Sample.Add(Summary(P));
		}
	}
	return Out;
}

FGisIds URuntimeGisToolset::AddPlots(const TArray<FRuntimeToolJson>& Plots, bool bReplace, bool bRedraw)
{
	using namespace Gis;
	FGisIds Out;
	UGisSubsystem* S = Sub();
	if (!S)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::NoWorld, TEXT("no world"));
		return Out;
	}
	if (bReplace)
	{
		ClearAll(*S);
	}
	for (int32 k = 0; k < Plots.Num(); ++k)
	{
		const TSharedPtr<FJsonObject> O = Plots[k].AsObject();
		const TArray<TSharedPtr<FJsonValue>>* Pts = nullptr;
		if (!O.IsValid() || !O->TryGetArrayField(TEXT("polygon"), Pts) || Pts->Num() < 3)
		{
			RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("plots[%d] needs polygon [[x,y,z],...] with 3+ corners"), k));
			return Out;
		}
		FGisPoly P;
		double Z = 0;
		for (const TSharedPtr<FJsonValue>& V : *Pts)
		{
			const TArray<TSharedPtr<FJsonValue>>* C = nullptr;
			if (!V->TryGetArray(C) || C->Num() < 2)
			{
				RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("plots[%d].polygon: corners are [x,y] or [x,y,z]"), k));
				return Out;
			}
			P.Add(FVector2D((*C)[0]->AsNumber(), (*C)[1]->AsNumber()));
			Z += C->Num() > 2 ? (*C)[2]->AsNumber() : 0;
		}
		Z /= Pts->Num();
		if (P.Num() > 3 && FVector2D::Distance(P[0], P.Last()) < 1.0)
		{
			P.Pop();
		}
		FGisFlags F;
		F.Init(false, P.Num());
		const TArray<TSharedPtr<FJsonValue>>* SE = nullptr;
		if (O->TryGetArrayField(TEXT("street_edges"), SE))
		{
			for (const TSharedPtr<FJsonValue>& E : *SE)
			{
				const int32 i = static_cast<int32>(E->AsNumber());
				if (F.IsValidIndex(i))
				{
					F[i] = true;
				}
			}
		}
		if (FMath::Abs(SignedArea(P)) < 100.0)
		{
			continue;   // degenerate
		}
		FString Kind;
		if (O->TryGetStringField(TEXT("kind"), Kind) && (Kind == TEXT("area") || Kind == TEXT("block")))
		{
			// land the plots sit in (a sector, an estate) or a street block: drawn under the plots, not a plot
			FGisFlags KF = F;
			MakeCCW(P, KF);
			(Kind == TEXT("area") ? S->Areas : S->Blocks).Add(P);
			if (Kind == TEXT("area"))
			{
				S->AreaZ = Z;
			}
			continue;
		}
		const TSharedPtr<FJsonObject>* Props = nullptr;
		O->TryGetObjectField(TEXT("props"), Props);
		FString Zn = TEXT("residential"), Id;
		O->TryGetStringField(TEXT("zoning"), Zn);
		const FString NewId = NewPlot(*S, P, F, Z, Zn, TEXT(""), Props ? *Props : nullptr);
		if (O->TryGetStringField(TEXT("id"), Id) && !Id.IsEmpty() && !S->Find(Id))
		{
			S->Find(NewId)->Id = Id;
			Out.Ids.Add(Id);
		}
		else
		{
			Out.Ids.Add(NewId);
		}
	}
	if (bRedraw)
	{
		Refresh(*S);
	}
	return Out;
}

FGisPlotList URuntimeGisToolset::ListPlots(const FString& Zoning, bool bOnlyEmpty, float MinAreaM2, int32 Limit, int32 Offset)
{
	FGisPlotList Out;
	UGisSubsystem* S = Gis::Sub();
	if (!S)
	{
		return Out;
	}
	for (const FGisPlot& P : S->Plots)
	{
		if ((!Zoning.IsEmpty() && !P.Zoning.Equals(Zoning, ESearchCase::IgnoreCase)) || (bOnlyEmpty && !P.Building.IsEmpty()) ||
			FMath::Abs(Gis::SignedArea(P.Poly)) / 1e4 < MinAreaM2)
		{
			continue;
		}
		if (Out.Total++ < Offset)
		{
			continue;
		}
		if (Out.Plots.Num() < FMath::Clamp(Limit, 1, 5000))
		{
			Out.Plots.Add(Gis::Summary(P));
		}
	}
	return Out;
}

FGisPlotInfo URuntimeGisToolset::GetPlot(const FString& Id)
{
	UGisSubsystem* S = Gis::Sub();
	const FGisPlot* P = S ? S->Find(Id) : nullptr;
	if (!P)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::NotFound, FString::Printf(TEXT("no plot '%s'"), *Id), TEXT("gis.list_plots lists them"));
		return {};
	}
	return Gis::Info(*P);
}

FGisPlotInfo URuntimeGisToolset::FindPlot(FVector Location)
{
	UGisSubsystem* S = Gis::Sub();
	if (S)
	{
		for (const FGisPlot& P : S->Plots)
		{
			if (Gis::Contains(P.Poly, FVector2D(Location.X, Location.Y)))
			{
				return Gis::Info(P);
			}
		}
	}
	RuntimeToolCall::Fail(ERuntimeToolError::NotFound, TEXT("no plot at that position"));
	return {};
}

FGisPlotSummary URuntimeGisToolset::SetPlot(const FString& Id, const FRuntimeToolJson& Props, const FString& Zoning, AActor* Building, bool bClearBuilding)
{
	UGisSubsystem* S = Gis::Sub();
	FGisPlot* P = S ? S->Find(Id) : nullptr;
	if (!P)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::NotFound, FString::Printf(TEXT("no plot '%s'"), *Id), TEXT("gis.list_plots lists them"));
		return {};
	}
	bool bRedraw = false;
	if (!Zoning.IsEmpty() && Zoning != P->Zoning)
	{
		P->Zoning = Zoning.ToLower();
		bRedraw = true;
	}
	if (const TSharedPtr<FJsonObject> O = Props.AsObject())
	{
		if (!P->Props.IsValid())
		{
			P->Props = MakeShared<FJsonObject>();
		}
		for (const TPair<FString, TSharedPtr<FJsonValue>>& F : O->Values)
		{
			if (!F.Value.IsValid() || F.Value->IsNull())
			{
				P->Props->RemoveField(F.Key);
			}
			else
			{
				P->Props->SetField(F.Key, F.Value);
			}
		}
	}
	if (bClearBuilding)
	{
		P->Building.Reset();
	}
	if (Building)
	{
		const FRuntimeSceneNode* N = RuntimeToolCall::NodeOf(Building);
		P->Building = N ? N->Id : Building->GetName();
	}
	const FGisPlotSummary Out = Gis::Summary(*P);
	if (bRedraw)
	{
		Gis::Refresh(*S);
	}
	return Out;
}

FGisIds URuntimeGisToolset::SubdividePlot(const FString& Id, int32 Parts, FVector2D LotWidth, int32 Seed)
{
	using namespace Gis;
	FGisIds Out;
	UGisSubsystem* S = Sub();
	FGisPlot* P = S ? S->Find(Id) : nullptr;
	if (!P)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::NotFound, FString::Printf(TEXT("no plot '%s'"), *Id), TEXT("gis.list_plots lists them"));
		return Out;
	}
	const FGisPlotInfo I = Info(*P);
	const FGisEdge& E = I.Edges[I.Front];
	const FVector2D U = FVector2D(E.B.X - E.A.X, E.B.Y - E.A.Y).GetSafeNormal();
	FRandomStream R(Seed >= 0 ? Seed : static_cast<int32>(FPlatformTime::Cycles64() & 0x7fffffff));
	TArray<TPair<FGisPoly, FGisFlags>> Lots;
	CutAlong(P->Poly, P->Street, U, LotWidth, Parts, R, Lots);
	if (Lots.Num() < 2)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, TEXT("the plot is too small to cut into lots of that width"), TEXT("Use parts or a smaller lot_width"));
		return Out;
	}
	const FGisPlot Old = *P;
	S->Plots.RemoveAll([&](const FGisPlot& X) { return X.Id == Old.Id; });
	for (TPair<FGisPoly, FGisFlags>& L : Lots)
	{
		TSharedPtr<FJsonObject> Props = MakeShared<FJsonObject>();
		if (Old.Props.IsValid())
		{
			Props->Values = Old.Props->Values;
		}
		Out.Ids.Add(NewPlot(*S, L.Key, L.Value, Old.Z, Old.Zoning, Old.Block, Props));
	}
	Refresh(*S);
	return Out;
}

FGisIds URuntimeGisToolset::MergePlots(const TArray<FString>& Ids)
{
	using namespace Gis;
	FGisIds Out;
	UGisSubsystem* S = Sub();
	if (!S || Ids.Num() < 2)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, TEXT("ids needs 2 or more plots"));
		return Out;
	}
	TArray<FGisPlot> Parts;
	for (const FString& Id : Ids)
	{
		const FGisPlot* P = S->Find(Id);
		if (!P)
		{
			RuntimeToolCall::Fail(ERuntimeToolError::NotFound, FString::Printf(TEXT("no plot '%s'"), *Id));
			return Out;
		}
		Parts.Add(*P);
	}
	FGisPoly All;
	for (const FGisPlot& P : Parts)
	{
		All.Append(P.Poly);
	}
	FGisPoly H = Hull(All);
	FGisFlags F;
	for (int32 i = 0; i < H.Num(); ++i)
	{
		// an outline edge is a street edge when its middle lies on a street edge of one of the plots
		const FVector2D M = (H[i] + H[(i + 1) % H.Num()]) * 0.5;
		bool bStreet = false;
		for (const FGisPlot& P : Parts)
		{
			for (int32 e = 0; e < P.Poly.Num() && !bStreet; ++e)
			{
				if (!P.Street.IsValidIndex(e) || !P.Street[e])
				{
					continue;
				}
				const FVector2D a = P.Poly[e], b = P.Poly[(e + 1) % P.Poly.Num()];
				const FVector2D ab = b - a;
				const double t = FMath::Clamp(FVector2D::DotProduct(M - a, ab) / FMath::Max(ab.SizeSquared(), 1e-6), 0.0, 1.0);
				bStreet = FVector2D::Distance(a + ab * t, M) < 50.0;
			}
		}
		F.Add(bStreet);
	}
	for (const FGisPlot& P : Parts)
	{
		S->Plots.RemoveAll([&](const FGisPlot& X) { return X.Id == P.Id; });
	}
	Out.Ids.Add(NewPlot(*S, H, F, Parts[0].Z, Parts[0].Zoning, Parts[0].Block, Parts[0].Props));
	Refresh(*S);
	return Out;
}

FGisIds URuntimeGisToolset::DeletePlots(const TArray<FString>& Ids, bool bAll)
{
	FGisIds Out;
	UGisSubsystem* S = Gis::Sub();
	if (!S)
	{
		return Out;
	}
	if (bAll)
	{
		for (const FGisPlot& P : S->Plots)
		{
			Out.Ids.Add(P.Id);
		}
		Gis::ClearAll(*S);
	}
	else
	{
		for (const FString& Id : Ids)
		{
			if (S->Plots.RemoveAll([&](const FGisPlot& P) { return P.Id.Equals(Id, ESearchCase::IgnoreCase); }))
			{
				Out.Ids.Add(Id);
			}
		}
	}
	Gis::Refresh(*S);
	return Out;
}

FGisPlotList URuntimeGisToolset::ShowPlots(bool bPlots, bool bLabels, bool bStreets, int32 MaxLabels)
{
	UGisSubsystem* S = Gis::Sub();
	if (!S)
	{
		return {};
	}
	S->bShowPlots = bPlots;
	S->bShowLabels = bLabels;
	S->bShowStreets = bStreets;
	S->MaxLabels = FMath::Max(0, MaxLabels);
	Gis::Refresh(*S);
	return ListPlots(TEXT(""), false, 0.f, 50, 0);
}

FGisOrigin URuntimeGisToolset::SetOrigin(double Latitude, double Longitude)
{
	UGisSubsystem* S = Gis::Sub();
	if (!S)
	{
		return {};
	}
	if (FMath::Abs(Latitude) > 89.9 || FMath::Abs(Longitude) > 180.0)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, TEXT("latitude must be within +-89.9 and longitude within +-180"));
		return {};
	}
	S->Origin.bSet = true;
	S->Origin.Latitude = Latitude;
	S->Origin.Longitude = Longitude;
	return S->Origin;
}

FGisOrigin URuntimeGisToolset::GetOrigin()
{
	UGisSubsystem* S = Gis::Sub();
	return S ? S->Origin : FGisOrigin();
}

FRuntimeToolJson URuntimeGisToolset::ExportGeoJson(bool bLonLat, int32 Limit, int32 Offset)
{
	UGisSubsystem* S = Gis::Sub();
	TSharedPtr<FJsonObject> FC = MakeShared<FJsonObject>();
	FC->SetStringField(TEXT("type"), TEXT("FeatureCollection"));
	TArray<TSharedPtr<FJsonValue>> Features;
	if (S)
	{
		const bool bGeo = bLonLat && S->Origin.bSet;
		const double CosLat = FMath::Cos(FMath::DegreesToRadians(S->Origin.Latitude));
		FC->SetNumberField(TEXT("total"), S->Plots.Num());
		for (int32 k = FMath::Max(0, Offset); k < FMath::Min(S->Plots.Num(), FMath::Max(0, Offset) + FMath::Clamp(Limit, 1, 5000)); ++k)
		{
			const FGisPlot& P = S->Plots[k];
			TArray<TSharedPtr<FJsonValue>> Ring;
			// world positive area (+Y south) is clockwise on a map: write the ring reversed (counter-clockwise, RFC 7946)
			for (int32 i = P.Poly.Num(); i >= 0; --i)
			{
				const FVector2D& p = P.Poly[i % P.Poly.Num()];
				const double East = p.X / 100.0, North = -p.Y / 100.0;
				const double X = bGeo ? S->Origin.Longitude + East / (111320.0 * CosLat) : East;
				const double Y = bGeo ? S->Origin.Latitude + North / 111320.0 : North;
				Ring.Add(MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>{MakeShared<FJsonValueNumber>(X), MakeShared<FJsonValueNumber>(Y)}));
			}
			TSharedPtr<FJsonObject> Geom = MakeShared<FJsonObject>();
			Geom->SetStringField(TEXT("type"), TEXT("Polygon"));
			Geom->SetArrayField(TEXT("coordinates"), {MakeShared<FJsonValueArray>(Ring)});
			TSharedPtr<FJsonObject> Props = MakeShared<FJsonObject>();
			if (P.Props.IsValid())
			{
				Props->Values = P.Props->Values;
			}
			Props->SetStringField(TEXT("id"), P.Id);
			Props->SetStringField(TEXT("zoning"), P.Zoning);
			Props->SetNumberField(TEXT("area_m2"), FMath::RoundToDouble(FMath::Abs(Gis::SignedArea(P.Poly)) / 1e4 * 10) / 10);
			Props->SetStringField(TEXT("building"), P.Building);
			TSharedPtr<FJsonObject> F = MakeShared<FJsonObject>();
			F->SetStringField(TEXT("type"), TEXT("Feature"));
			F->SetObjectField(TEXT("geometry"), Geom);
			F->SetObjectField(TEXT("properties"), Props);
			Features.Add(MakeShared<FJsonValueObject>(F));
		}
	}
	FC->SetArrayField(TEXT("features"), Features);
	return FRuntimeToolJson::Make(FC);
}
