#include "PcgScatter.h"
#include "SplineCore.h"
#include "RuntimeCommonCommands.h"
#include "RuntimeJson.h"
#include "RuntimeSceneGraph.h"
#include "RuntimeToolHelpers.h"
#include "RuntimeToolsetsCore.h"
#include "Components/DynamicMeshComponent.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/SplineComponent.h"
#include "DynamicMeshToMeshDescription.h"
#include "Elements/PCGBoundsModifier.h"
#include "Elements/PCGSelfPruning.h"
#include "Elements/PCGSplineSampler.h"
#include "Elements/PCGCreateSurfaceFromSpline.h"
#include "Elements/PCGSurfaceSampler.h"
#include "Elements/PCGTransformPoints.h"
#include "Elements/PCGTypedGetter.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/PlatformProcess.h"
#include "PCGComponent.h"
#include "PCGGraph.h"
#include "PCGNode.h"
#include "PCGPin.h"
#include "Data/PCGBasePointData.h"
#include "StaticMeshAttributes.h"
#include "Subsystems/PCGSubsystem.h"

using namespace RuntimeTools;

namespace
{
	constexpr int32 MaxInstances = 20000;
	const FName ScatterTag(TEXT("RTScatter"));

	/** Last result per scatter actor (counts for the tool response). */
	TMap<TWeakObjectPtr<AActor>, TSharedPtr<FJsonObject>>& Stats()
	{
		static TMap<TWeakObjectPtr<AActor>, TSharedPtr<FJsonObject>> Map;
		return Map;
	}

	/** Static meshes converted from builder (dynamic mesh) templates, reused while the template is unchanged. */
	struct FMeshCacheEntry
	{
		TWeakObjectPtr<UStaticMesh> Mesh;
		uint32 ChangeStamp = 0;
	};
	TMap<TWeakObjectPtr<UDynamicMeshComponent>, FMeshCacheEntry>& MeshCache()
	{
		static TMap<TWeakObjectPtr<UDynamicMeshComponent>, FMeshCacheEntry> Map;
		return Map;
	}

	double Num(const TSharedPtr<FJsonObject>& J, const TCHAR* F, double Default)
	{
		double V = Default;
		if (J.IsValid())
		{
			J->TryGetNumberField(F, V);
		}
		return V;
	}

	FVector2D Range(const TSharedPtr<FJsonObject>& J, const TCHAR* F, FVector2D Default)
	{
		const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
		if (J.IsValid() && J->TryGetArrayField(F, A) && A->Num() == 2)
		{
			return FVector2D((*A)[0]->AsNumber(), (*A)[1]->AsNumber());
		}
		return Default;
	}

	AActor* NodeActor(UWorld* W, const FString& Id)
	{
		FRuntimeSceneGraph* G = IRuntimeToolsetsCore::Get().GetSceneGraph(W);
		const FRuntimeSceneNode* N = G ? G->Find(Id) : nullptr;
		return N ? N->Actor.Get() : nullptr;
	}

	/** A static mesh with the template's geometry and material slots (built at runtime, fast build). */
	UStaticMesh* TemplateMesh(AActor* Template, TArray<UMaterialInterface*>& OutMaterials, FString& OutError)
	{
		UDynamicMeshComponent* C = Template ? Template->FindComponentByClass<UDynamicMeshComponent>() : nullptr;
		if (!C)
		{
			OutError = FString::Printf(TEXT("template '%s' has no builder mesh (use a build.* node)"), Template ? *Template->GetName() : TEXT("?"));
			return nullptr;
		}
		OutMaterials.Reset();
		for (int32 i = 0; i < C->GetNumMaterials(); ++i)
		{
			OutMaterials.Add(C->GetMaterial(i));
		}
		uint32 Stamp = 0;
		C->ProcessMesh([&](const UE::Geometry::FDynamicMesh3& M) { Stamp = M.GetChangeStamp() ^ (M.TriangleCount() * 2654435761u); });
		if (FMeshCacheEntry* E = MeshCache().Find(C); E && E->Mesh.IsValid() && E->ChangeStamp == Stamp)
		{
			return E->Mesh.Get();
		}
		FMeshDescription MD;
		FStaticMeshAttributes Attr(MD);
		Attr.Register();
		C->ProcessMesh([&](const UE::Geometry::FDynamicMesh3& M)
		{
			FDynamicMeshToMeshDescription Conv;
			Conv.Convert(&M, MD, false);
		});
		{
			FString Dbg;
			for (const FPolygonGroupID G : MD.PolygonGroups().GetElementIDs())
			{
				Dbg += FString::Printf(TEXT(" g%d:%d"), G.GetValue(), MD.GetPolygonGroupTriangles(G).Num());
			}
			UE_LOG(LogTemp, Verbose, TEXT("RTScatter template %s: %d groups, %d materials,%s"), *Template->GetName(), MD.PolygonGroups().Num(), OutMaterials.Num(), *Dbg);
		}
		if (MD.Triangles().Num() == 0)
		{
			OutError = TEXT("template mesh is empty");
			return nullptr;
		}
		UStaticMesh* SM = NewObject<UStaticMesh>(GetTransientPackage(), NAME_None, RF_Transient);
		const int32 Groups = MD.PolygonGroups().Num();
		TPolygonGroupAttributesRef<FName> SlotNames = Attr.GetPolygonGroupMaterialSlotNames();
		int32 Slot = 0;
		for (const FPolygonGroupID G : MD.PolygonGroups().GetElementIDs())
		{
			const FName SlotName(*FString::Printf(TEXT("Slot%d"), Slot));
			SlotNames[G] = SlotName;
			SM->GetStaticMaterials().Add(FStaticMaterial(OutMaterials.IsValidIndex(Slot) ? OutMaterials[Slot] : nullptr, SlotName));
			++Slot;
		}
		UStaticMesh::FBuildMeshDescriptionsParams P;
		P.bFastBuild = true;
		P.bBuildSimpleCollision = true;
		P.bCommitMeshDescription = false;
		P.bMarkPackageDirty = false;
		P.bUseHashAsGuid = true;
		if (!SM->BuildFromMeshDescriptions({&MD}, P))
		{
			OutError = TEXT("could not build an instanced mesh from the template");
			return nullptr;
		}
		if (Groups != OutMaterials.Num())
		{
			// Polygon groups follow material ids; keep the material list aligned with the slots.
			OutMaterials.SetNum(Groups);
		}
		MeshCache().Add(C, {SM, Stamp});
		return SM;
	}

	/** Connects the first output pin of From to the first input pin of To (labels read from the nodes themselves). */
	void Link(UPCGGraph* G, UPCGNode* From, UPCGNode* To)
	{
		if (From && To && From->GetOutputPins().Num() > 0 && To->GetInputPins().Num() > 0)
		{
			G->AddEdge(From, From->GetOutputPins()[0]->Properties.Label, To, To->GetInputPins()[0]->Properties.Label);
		}
	}

	/** Spline data -> spline sampler -> offset/jitter branches -> bounds -> self pruning -> output. */
	UPCGGraph* BuildGraph(UObject* Outer, const TSharedRef<FJsonObject>& R, int32 Seed)
	{
		UPCGGraph* G = NewObject<UPCGGraph>(Outer, NAME_None, RF_Transient);
		const FString Mode = R->GetStringField(TEXT("mode"));
		const double Spacing = FMath::Max(10.0, Num(R, TEXT("spacing"), 400.0));
		const double Jitter = Num(R, TEXT("jitter"), Spacing * 0.35);
		const double MinDist = Num(R, TEXT("min_distance"), Spacing * 0.6);

		UPCGGetSplineSettings* GetS = nullptr;
		UPCGNode* GetN = G->AddNodeOfType<UPCGGetSplineSettings>(GetS);
		Link(G, G->GetInputNode(), GetN); // harmless: the getter reads this actor's spline

		UPCGNode* SamN = nullptr;
		if (Mode == TEXT("inside"))
		{
			// The usual PCG area recipe: closed spline -> surface -> surface sampler (density per square metre).
			UPCGCreateSurfaceFromSplineSettings* SurfS = nullptr;
			UPCGNode* SurfN = G->AddNodeOfType<UPCGCreateSurfaceFromSplineSettings>(SurfS);
			Link(G, GetN, SurfN);
			UPCGSurfaceSamplerSettings* SS = nullptr;
			SamN = G->AddNodeOfType<UPCGSurfaceSamplerSettings>(SS);
			SS->Seed = Seed;
			SS->PointsPerSquaredMeter = static_cast<float>(FMath::Square(100.0 / Spacing));
			SS->PointExtents = FVector(Spacing * 0.5);
			SS->Looseness = 1.0f;
			SS->bUnbounded = true;
			SS->bApplyDensityToPoints = false;
			Link(G, SurfN, SamN);
		}
		else
		{
			UPCGSplineSamplerSettings* SamS = nullptr;
			SamN = G->AddNodeOfType<UPCGSplineSamplerSettings>(SamS);
			SamS->Seed = Seed;
			SamS->SamplerParams.Dimension = EPCGSplineSamplingDimension::OnSpline;
			SamS->SamplerParams.Mode = EPCGSplineSamplingMode::Distance;
			SamS->SamplerParams.DistanceIncrement = Spacing;
			Link(G, GetN, SamN);
		}

		UPCGBoundsModifierSettings* BndS = nullptr;
		UPCGNode* BndN = G->AddNodeOfType<UPCGBoundsModifierSettings>(BndS);
		BndS->Mode = EPCGBoundsModifierMode::Set;
		BndS->BoundsMin = FVector(-MinDist * 0.5);
		BndS->BoundsMax = FVector(MinDist * 0.5);

		// Lateral bands: "both" sides of the path, one side, or on the line itself (+ jitter along it).
		const FVector2D Off = Range(R, TEXT("offset"), FVector2D(0, 0));
		const FString Side = R->HasField(TEXT("side")) ? R->GetStringField(TEXT("side")) : TEXT("both");
		TArray<FVector2D> Bands;
		if (Mode == TEXT("inside"))
		{
			Bands.Add(FVector2D(-Jitter, Jitter));
		}
		else if (Off.Y <= 0.0)
		{
			Bands.Add(FVector2D(-Jitter * 0.0, Jitter * 0.0));
		}
		else
		{
			if (Side != TEXT("left"))
			{
				Bands.Add(FVector2D(Off.X, Off.Y));
			}
			if (Side != TEXT("right"))
			{
				Bands.Add(FVector2D(-Off.Y, -Off.X));
			}
		}
		int32 Branch = 0;
		for (const FVector2D& Band : Bands)
		{
			UPCGTransformPointsSettings* TS = nullptr;
			UPCGNode* TN = G->AddNodeOfType<UPCGTransformPointsSettings>(TS);
			TS->Seed = Seed + 7919 * ++Branch;
			const double Along = Mode == TEXT("inside") ? Jitter : FMath::Min(Jitter, Spacing * 0.45);
			TS->OffsetMin = FVector(-Along, Mode == TEXT("inside") ? -Jitter : Band.X, 0);
			TS->OffsetMax = FVector(Along, Mode == TEXT("inside") ? Jitter : Band.Y, 0);
			Link(G, SamN, TN);
			Link(G, TN, BndN);
		}

		UPCGSelfPruningSettings* PruS = nullptr;
		UPCGNode* PruN = G->AddNodeOfType<UPCGSelfPruningSettings>(PruS);
		PruS->Seed = Seed + 31;
		PruS->Parameters.bRandomizedPruning = true;
		Link(G, BndN, PruN);
		Link(G, PruN, G->GetOutputNode());
		return G;
	}

}

/** Runs the component's graph to completion on this thread (PCG normally spreads work over frames). */
bool RuntimePcg::Run(UWorld* W, UPCGComponent* Comp, FString& OutError)
	{
		UPCGSubsystem* Sub = UPCGSubsystem::GetInstance(W);
		if (!Sub)
		{
			OutError = TEXT("PCG is not available in this world");
			return false;
		}
		Comp->GenerateLocal(true);
		const double End = FPlatformTime::Seconds() + 20.0;
		bool bStarted = Comp->IsGenerating();
		while (FPlatformTime::Seconds() < End)
		{
			Sub->Tick(1.f / 60.f);
			if (Comp->IsGenerating())
			{
				bStarted = true;
			}
			else if (bStarted || Comp->GetGeneratedGraphOutput().TaggedData.Num() > 0)
			{
				return true;
			}
			FPlatformProcess::Sleep(0.0005f);
		}
		OutError = bStarted ? TEXT("PCG generation timed out") : TEXT("PCG generation did not start");
		return false;
	}

namespace
{
	struct FItem
	{
		FString Template;
		double Weight = 1.0;
		FVector2D Scale = FVector2D(1, 1);
		bool bAlign = false;
		bool bRandomYaw = true;
		double ZOffset = 0.0;
		bool bCollision = false;
		UStaticMesh* Mesh = nullptr;
		TArray<UMaterialInterface*> Materials;
		TArray<FTransform> Instances;
	};
}

AActor* RuntimePcg::SpawnScatterFromSource(UWorld* World, const FString& Source, const FTransform& Transform, const FString& Name, FString& OutError)
{
	const TSharedPtr<FJsonObject> RP = RuntimeJson::Parse(Source.Mid(8));
	if (!RP.IsValid())
	{
		OutError = TEXT("bad scatter: recipe");
		return nullptr;
	}
	const TSharedRef<FJsonObject> R = RP.ToSharedRef();
	AActor* SplineActor = NodeActor(World, R->GetStringField(TEXT("spline")));
	const USplineComponent* Src = RuntimeSplines::FindSpline(SplineActor);
	if (!Src)
	{
		OutError = TEXT("the scatter's spline node is gone");
		return nullptr;
	}
	if (R->GetStringField(TEXT("mode")) == TEXT("inside") && !Src->IsClosedLoop())
	{
		OutError = TEXT("mode inside needs a closed spline (spline.create closed:true)");
		return nullptr;
	}
	// Items first (fail before spawning anything).
	TArray<FItem> Items;
	double TotalWeight = 0.0;
	for (const TSharedPtr<FJsonValue>& V : R->GetArrayField(TEXT("items")))
	{
		const TSharedPtr<FJsonObject> J = V->AsObject();
		FItem It;
		FString AssetPath;
		const bool bAsset = J->TryGetStringField(TEXT("asset"), AssetPath) && !AssetPath.IsEmpty();
		It.Template = bAsset ? AssetPath : J->GetStringField(TEXT("template"));
		It.Weight = FMath::Max(0.0, Num(J, TEXT("weight"), 1.0));
		It.Scale = Range(J, TEXT("scale"), FVector2D(1, 1));
		J->TryGetBoolField(TEXT("align"), It.bAlign);
		It.bRandomYaw = !It.bAlign;
		J->TryGetBoolField(TEXT("random_yaw"), It.bRandomYaw);
		It.ZOffset = Num(J, TEXT("z_offset"), 0.0);
		J->TryGetBoolField(TEXT("collision"), It.bCollision);
		if (bAsset)
		{
			It.Mesh = LoadObject<UStaticMesh>(nullptr, *AssetPath);
			if (!It.Mesh)
			{
				OutError = FString::Printf(TEXT("no static mesh asset '%s'"), *AssetPath);
				return nullptr;
			}
			for (const FStaticMaterial& M : It.Mesh->GetStaticMaterials())
			{
				It.Materials.Add(M.MaterialInterface);
			}
		}
		else
		{
			It.Mesh = TemplateMesh(NodeActor(World, It.Template), It.Materials, OutError);
		}
		if (!It.Mesh)
		{
			return nullptr;
		}
		TotalWeight += It.Weight;
		Items.Add(MoveTemp(It));
	}
	if (Items.Num() == 0 || TotalWeight <= 0.0)
	{
		OutError = TEXT("scatter needs items with weight > 0");
		return nullptr;
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AActor* Actor = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, Params);
	if (!Actor)
	{
		OutError = TEXT("could not spawn the scatter actor");
		return nullptr;
	}
	USceneComponent* Root = NewObject<USceneComponent>(Actor, TEXT("Root"));
	Actor->SetRootComponent(Root);
	Root->RegisterComponent();
	Actor->AddInstanceComponent(Root);
	Actor->Tags.Add(ScatterTag);

	// The PCG component samples its own actor's spline: copy the source curve (world space) onto this actor.
	USplineComponent* S = NewObject<USplineComponent>(Actor, TEXT("Spline"));
	S->SetupAttachment(Root);
	S->RegisterComponent();
	Actor->AddInstanceComponent(S);
	S->ClearSplinePoints(false);
	for (int32 i = 0; i < Src->GetNumberOfSplinePoints(); ++i)
	{
		S->AddSplinePoint(Src->GetLocationAtSplinePoint(i, ESplineCoordinateSpace::World), ESplineCoordinateSpace::World, false);
		S->SetSplinePointType(i, Src->GetSplinePointType(i), false);
		S->SetTangentsAtSplinePoint(i, Src->GetArriveTangentAtSplinePoint(i, ESplineCoordinateSpace::World),
			Src->GetLeaveTangentAtSplinePoint(i, ESplineCoordinateSpace::World), ESplineCoordinateSpace::World, false);
	}
	S->SetClosedLoop(Src->IsClosedLoop(), false);
	S->UpdateSpline();

	const int32 Seed = static_cast<int32>(Num(R, TEXT("seed"), 1));
	UPCGComponent* Pcg = NewObject<UPCGComponent>(Actor, TEXT("PCG"));
	Pcg->GenerationTrigger = EPCGComponentGenerationTrigger::GenerateOnDemand;
	Pcg->Seed = Seed;
	Pcg->RegisterComponent();
	Actor->AddInstanceComponent(Pcg);
	Pcg->SetGraphLocal(BuildGraph(Actor, R, Seed));
	if (!RuntimePcg::Run(World, Pcg, OutError))
	{
		Actor->Destroy();
		return nullptr;
	}

	// Collect PCG points.
	TArray<FTransform> Points;
	for (const FPCGTaggedData& D : Pcg->GetGeneratedGraphOutput().TaggedData)
	{
		if (const UPCGBasePointData* PD = Cast<UPCGBasePointData>(D.Data))
		{
			const auto Tr = PD->GetConstTransformValueRange();
			for (int32 i = 0; i < PD->GetNumPoints() && Points.Num() < MaxInstances * 2; ++i)
			{
				Points.Add(Tr[i]);
			}
		}
	}
	const int32 Sampled = Points.Num();
	UE_LOG(LogTemp, Verbose, TEXT("RTScatter %s: %d output data, %d points"), *R->GetStringField(TEXT("mode")), Pcg->GetGeneratedGraphOutput().TaggedData.Num(), Sampled);

	// Filters: clumping noise, exclusion splines, ground.
	const double Keep = FMath::Clamp(Num(R, TEXT("keep"), 1.0), 0.0, 1.0);
	const double NoiseScale = FMath::Max(100.0, Num(R, TEXT("noise_scale"), 3000.0));
	// Closed exclusion splines keep their whole inside clear (a village clearing, a field); open ones a band either side.
	struct FExclude { const USplineComponent* S; double Clearance; TArray<FVector2D> Poly; };
	TArray<FExclude> Excludes;
	const TArray<TSharedPtr<FJsonValue>>* Ex = nullptr;
	if (R->TryGetArrayField(TEXT("exclude"), Ex))
	{
		for (const TSharedPtr<FJsonValue>& V : *Ex)
		{
			const TSharedPtr<FJsonObject> J = V->AsObject();
			if (const USplineComponent* XS = RuntimeSplines::FindSpline(NodeActor(World, J->GetStringField(TEXT("spline")))))
			{
				FExclude E{XS, Num(J, TEXT("clearance"), 300.0), {}};
				if (XS->IsClosedLoop())
				{
					const double Len = XS->GetSplineLength();
					const int32 N = FMath::Clamp(int32(Len / 200.0), 16, 512);
					for (int32 i = 0; i < N; ++i)
					{
						const FVector P = XS->GetLocationAtDistanceAlongSpline(Len * i / N, ESplineCoordinateSpace::World);
						E.Poly.Add(FVector2D(P.X, P.Y));
					}
				}
				Excludes.Add(MoveTemp(E));
			}
		}
	}
	bool bGround = true;
	R->TryGetBoolField(TEXT("ground"), bGround);
	// surface: only plant where this actor is the first thing below (not on roofs, roads, walls)
	const AActor* SurfaceActor = R->HasField(TEXT("surface")) ? NodeActor(World, R->GetStringField(TEXT("surface"))) : nullptr;
	FCollisionQueryParams Q(TEXT("ScatterGround"), false);
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (It->Tags.Contains(ScatterTag))
		{
			Q.AddIgnoredActor(*It);
		}
	}
	for (const FItem& It : Items)
	{
		Q.AddIgnoredActor(NodeActor(World, It.Template));
	}

	FRandomStream Rng(Seed * 7 + 3);
	int32 Rejected = 0;
	for (const FTransform& P : Points)
	{
		FVector L = P.GetLocation();
		if (Keep < 1.0)
		{
			const float N = FMath::PerlinNoise2D(FVector2D(L.X, L.Y) / NoiseScale + FVector2D(Seed * 0.37, Seed * 0.71)) * 0.5f + 0.5f;
			if (N > Keep * 1.3 - 0.15 + Rng.FRandRange(-0.1, 0.1))
			{
				++Rejected;
				continue;
			}
		}
		bool bOut = false;
		for (const FExclude& X : Excludes)
		{
			const FVector C = X.S->FindLocationClosestToWorldLocation(L, ESplineCoordinateSpace::World);
			if (FVector::Dist2D(C, L) < X.Clearance)
			{
				bOut = true;
				break;
			}
			if (X.Poly.Num() >= 3)  // even-odd point in polygon
			{
				bool bInside = false;
				for (int32 i = 0, j = X.Poly.Num() - 1; i < X.Poly.Num(); j = i++)
				{
					const FVector2D& A = X.Poly[i];
					const FVector2D& B = X.Poly[j];
					if ((A.Y > L.Y) != (B.Y > L.Y) && L.X < (B.X - A.X) * (L.Y - A.Y) / (B.Y - A.Y) + A.X)
					{
						bInside = !bInside;
					}
				}
				if (bInside)
				{
					bOut = true;
					break;
				}
			}
		}
		if (bOut)
		{
			++Rejected;
			continue;
		}
		if (bGround)
		{
			FHitResult Hit;
			if (World->LineTraceSingleByChannel(Hit, L + FVector(0, 0, 20000), L - FVector(0, 0, 50000), ECC_Visibility, Q))
			{
				if (SurfaceActor && Hit.GetActor() != SurfaceActor)
				{
					++Rejected;
					continue;
				}
				L.Z = Hit.ImpactPoint.Z;
			}
			else if (SurfaceActor)
			{
				++Rejected;
				continue;
			}
		}
		// Weighted pick.
		double Pick = Rng.FRandRange(0.0, TotalWeight);
		FItem* Chosen = &Items.Last();
		for (FItem& It : Items)
		{
			Pick -= It.Weight;
			if (Pick <= 0.0)
			{
				Chosen = &It;
				break;
			}
		}
		if (Chosen->Instances.Num() >= MaxInstances / Items.Num())
		{
			continue;
		}
		const double Sc = Rng.FRandRange(Chosen->Scale.X, Chosen->Scale.Y);
		FRotator Rot = FRotator::ZeroRotator;
		if (Chosen->bAlign)
		{
			Rot.Yaw = S->FindRotationClosestToWorldLocation(L, ESplineCoordinateSpace::World).Yaw;
		}
		if (Chosen->bRandomYaw)
		{
			Rot.Yaw += Rng.FRandRange(0.0, 360.0);
		}
		Chosen->Instances.Add(FTransform(Rot, L + FVector(0, 0, Chosen->ZOffset), FVector(Sc)));
	}

	TSharedRef<FJsonObject> Out = Obj();
	int32 Placed = 0;
	TArray<TSharedPtr<FJsonValue>> PerItem;
	for (FItem& It : Items)
	{
		if (It.Instances.Num() > 0)
		{
			UHierarchicalInstancedStaticMeshComponent* H = NewObject<UHierarchicalInstancedStaticMeshComponent>(Actor);
			H->SetStaticMesh(It.Mesh);
			for (int32 m = 0; m < It.Materials.Num(); ++m)
			{
				H->SetMaterial(m, It.Materials[m]);
			}
			H->SetCollisionEnabled(It.bCollision ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
			H->SetupAttachment(Root);
			H->RegisterComponent();
			Actor->AddInstanceComponent(H);
			H->AddInstances(It.Instances, false, true);
		}
		TSharedRef<FJsonObject> J = Obj();
		J->SetStringField(TEXT("template"), It.Template);
		J->SetNumberField(TEXT("instances"), It.Instances.Num());
		PerItem.Add(MakeShared<FJsonValueObject>(J));
		Placed += It.Instances.Num();
	}
	Out->SetNumberField(TEXT("pcg_points"), Sampled);
	Out->SetNumberField(TEXT("filtered_out"), Rejected);
	Out->SetNumberField(TEXT("instances"), Placed);
	Out->SetArrayField(TEXT("items"), PerItem);
	Stats().Add(Actor, Out);
	return Actor;
}

TSharedPtr<FJsonObject> RuntimePcg::ScatterStats(const AActor* Scatter)
{
	const TSharedPtr<FJsonObject>* S = Stats().Find(const_cast<AActor*>(Scatter));
	return S ? *S : nullptr;
}
