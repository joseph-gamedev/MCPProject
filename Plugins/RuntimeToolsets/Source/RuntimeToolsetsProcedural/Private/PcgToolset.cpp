#include "PcgToolset.h"
#include "SplineToolset.h"
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
			OutError = FString::Printf(TEXT("template '%s' has no builder mesh (use a build.* node)"), Template ? *Template->GetActorLabel() : TEXT("?"));
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

	/** Runs the component's graph to completion on this thread (PCG normally spreads work over frames). */
	bool RunPcg(UWorld* W, UPCGComponent* Comp, FString& OutError)
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

AActor* FPcgToolset::SpawnFromSource(UWorld* World, const FString& Source, const FTransform& Transform, const FString& Name, FString& OutError)
{
	const TSharedPtr<FJsonObject> RP = RuntimeJson::Parse(Source.Mid(8));
	if (!RP.IsValid())
	{
		OutError = TEXT("bad scatter: recipe");
		return nullptr;
	}
	const TSharedRef<FJsonObject> R = RP.ToSharedRef();
	AActor* SplineActor = NodeActor(World, R->GetStringField(TEXT("spline")));
	const USplineComponent* Src = FSplineToolset::FindSpline(SplineActor);
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
		It.Template = J->GetStringField(TEXT("template"));
		It.Weight = FMath::Max(0.0, Num(J, TEXT("weight"), 1.0));
		It.Scale = Range(J, TEXT("scale"), FVector2D(1, 1));
		J->TryGetBoolField(TEXT("align"), It.bAlign);
		It.bRandomYaw = !It.bAlign;
		J->TryGetBoolField(TEXT("random_yaw"), It.bRandomYaw);
		It.ZOffset = Num(J, TEXT("z_offset"), 0.0);
		J->TryGetBoolField(TEXT("collision"), It.bCollision);
		It.Mesh = TemplateMesh(NodeActor(World, It.Template), It.Materials, OutError);
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
	if (!RunPcg(World, Pcg, OutError))
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
	struct FExclude { const USplineComponent* S; double Clearance; };
	TArray<FExclude> Excludes;
	const TArray<TSharedPtr<FJsonValue>>* Ex = nullptr;
	if (R->TryGetArrayField(TEXT("exclude"), Ex))
	{
		for (const TSharedPtr<FJsonValue>& V : *Ex)
		{
			const TSharedPtr<FJsonObject> J = V->AsObject();
			if (const USplineComponent* XS = FSplineToolset::FindSpline(NodeActor(World, J->GetStringField(TEXT("spline")))))
			{
				Excludes.Add({XS, Num(J, TEXT("clearance"), 300.0)});
			}
		}
	}
	bool bGround = true;
	R->TryGetBoolField(TEXT("ground"), bGround);
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
				L.Z = Hit.ImpactPoint.Z;
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

static void Scatter(FToolInvocation& Inv)
{
	FResolvedTarget ST;
	if (!Inv.ResolveTarget(TEXT("spline"), ST))
	{
		return;
	}
	if (!ST.Node || !FSplineToolset::FindSpline(ST.Actor))
	{
		Inv.Fail(ERuntimeToolError::InvalidArguments, TEXT("spline must be a spline node"), TEXT("Create one with spline.create"));
		return;
	}
	TSharedRef<FJsonObject> R = Obj();
	R->SetStringField(TEXT("spline"), ST.Node->Id);
	R->SetStringField(TEXT("mode"), Inv.GetString(TEXT("mode"), TEXT("along")));
	for (const TCHAR* F : {TEXT("spacing"), TEXT("jitter"), TEXT("min_distance"), TEXT("keep"), TEXT("noise_scale"), TEXT("seed")})
	{
		if (Inv.Has(F))
		{
			R->SetNumberField(F, Inv.GetNumber(F));
		}
	}
	if (Inv.Has(TEXT("offset")))
	{
		R->SetField(TEXT("offset"), Inv.GetValue(TEXT("offset")));
	}
	R->SetStringField(TEXT("side"), Inv.GetString(TEXT("side"), TEXT("both")));
	R->SetBoolField(TEXT("ground"), Inv.GetBool(TEXT("ground"), true));

	// Resolve item templates and exclusion splines to node ids (names/paths are accepted).
	TArray<TSharedPtr<FJsonValue>> Items;
	const TArray<TSharedPtr<FJsonValue>>* In = nullptr;
	if (!Inv.GetArgs()->TryGetArrayField(TEXT("items"), In) || In->Num() == 0)
	{
		Inv.Fail(ERuntimeToolError::InvalidArguments, TEXT("items needs at least one {template}"));
		return;
	}
	FRuntimeSceneGraph* G = Inv.GetGraph();
	auto ResolveId = [&](const FString& Ref, FString& OutId) -> bool
	{
		FResolvedTarget T;
		ERuntimeToolError Err = ERuntimeToolError::NotFound;
		FString Msg;
		if (RuntimeObjectResolver::Resolve(Ref, Inv.GetWorld(), G, T, Err, Msg) && T.Node)
		{
			OutId = T.Node->Id;
			return true;
		}
		Inv.Fail(ERuntimeToolError::NotFound, FString::Printf(TEXT("no node '%s' %s"), *Ref, *Msg));
		return false;
	};
	for (const TSharedPtr<FJsonValue>& V : *In)
	{
		const TSharedPtr<FJsonObject> J = V->AsObject();
		FString Ref, Id;
		if (!J.IsValid() || !J->TryGetStringField(TEXT("template"), Ref))
		{
			Inv.Fail(ERuntimeToolError::InvalidArguments, TEXT("each item needs a template node"));
			return;
		}
		if (!ResolveId(Ref, Id))
		{
			return;
		}
		TSharedRef<FJsonObject> Copy = RuntimeJson::Parse(RuntimeJson::Write(J.ToSharedRef())).ToSharedRef();
		Copy->SetStringField(TEXT("template"), Id);
		Items.Add(MakeShared<FJsonValueObject>(Copy));
	}
	R->SetArrayField(TEXT("items"), Items);
	const TArray<TSharedPtr<FJsonValue>>* Ex = nullptr;
	if (Inv.GetArgs()->TryGetArrayField(TEXT("exclude"), Ex))
	{
		TArray<TSharedPtr<FJsonValue>> Out;
		for (const TSharedPtr<FJsonValue>& V : *Ex)
		{
			const TSharedPtr<FJsonObject> J = V->AsObject();
			FString Ref, Id;
			if (!J.IsValid() || !J->TryGetStringField(TEXT("spline"), Ref) || !ResolveId(Ref, Id))
			{
				if (!Inv.IsComplete())
				{
					Inv.Fail(ERuntimeToolError::InvalidArguments, TEXT("each exclude needs {spline, clearance}"));
				}
				return;
			}
			TSharedRef<FJsonObject> Copy = Obj();
			Copy->SetStringField(TEXT("spline"), Id);
			Copy->SetNumberField(TEXT("clearance"), Num(J, TEXT("clearance"), 300.0));
			Out.Add(MakeShared<FJsonValueObject>(Copy));
		}
		R->SetArrayField(TEXT("exclude"), Out);
	}

	const FString Source = TEXT("scatter:") + RuntimeJson::Write(R);
	const FString Name = Inv.GetString(TEXT("name"), TEXT("Scatter"));
	auto Spawn = [Source, Name](FRuntimeCommandContext& Ctx, FString& Err) -> AActor*
	{
		return FPcgToolset::SpawnFromSource(Ctx.World, Source, FTransform::Identity, Name, Err);
	};
	TUniquePtr<FCreateNodeCommand> Cmd = MakeUnique<FCreateNodeCommand>(TEXT("pcg scatter"), Spawn, Name, FString(), ERuntimeNodeKind::Actor, Source, TArray<FName>{TEXT("scatter")});
	FCreateNodeCommand* Raw = Cmd.Get();
	if (!Inv.RunCommand(MoveTemp(Cmd)))
	{
		return;
	}
	TSharedRef<FJsonObject> Out = Obj();
	Out->SetStringField(TEXT("id"), Raw->GetNodeId());
	if (const FRuntimeSceneNode* N = G->Find(Raw->GetNodeId()))
	{
		if (const TSharedPtr<FJsonObject>* S = Stats().Find(N->Actor))
		{
			for (const TPair<FString, TSharedPtr<FJsonValue>>& P : (*S)->Values)
			{
				Out->SetField(P.Key, P.Value);
			}
		}
	}
	Inv.Succeed(Out);
}

void FPcgToolset::RegisterTools(FRuntimeToolsetBuilder& B)
{
	B.Tool(TEXT("scatter"), TEXT("Scatter copies of builder objects along a spline (both sides of a road, a fence line) or inside a closed spline (a forest, a field) "
			"with a real PCG graph (spline sampler, jitter, self pruning), then noise thinning, exclusion zones and ground snapping. "
			"Rendered as GPU instances (thousands are cheap). Undoable; same seed = same result."))
		.Args(FToolSchema()
			.Add(TEXT("spline"), FToolParam::Target(TEXT("Spline node (spline.create)")).Required())
			.Add(TEXT("items"), FToolParam::Array(TEXT("What to place: {template: node with a builder mesh, weight, scale:[min,max], align (face along the spline), random_yaw, z_offset, collision}"),
				FToolParam::Object(TEXT("Item")), 16).Required())
			.Add(TEXT("mode"), FToolParam::Enum(TEXT("along: on/beside the spline; inside: fill a closed spline"), {TEXT("along"), TEXT("inside")}).Default(TEXT("along")))
			.Add(TEXT("spacing"), FToolParam::Number(TEXT("Sample spacing (cm)"), 10.0, 100000.0).Default(400.0))
			.Add(TEXT("offset"), FToolParam::Any(TEXT("along: lateral band [min,max] cm from the spline (e.g. [500,1200] keeps trees off a road)")))
			.Add(TEXT("side"), FToolParam::Enum(TEXT("along with offset: which side"), {TEXT("both"), TEXT("left"), TEXT("right")}).Default(TEXT("both")))
			.Add(TEXT("jitter"), FToolParam::Number(TEXT("Random position offset (cm, default 35% of spacing)"), 0.0, 100000.0))
			.Add(TEXT("min_distance"), FToolParam::Number(TEXT("Minimum gap between objects (cm, PCG self pruning)"), 0.0, 100000.0))
			.Add(TEXT("keep"), FToolParam::Number(TEXT("0-1 share kept, clumped by noise (1 = all)"), 0.0, 1.0).Default(1.0))
			.Add(TEXT("noise_scale"), FToolParam::Number(TEXT("Clump size for keep (cm)"), 100.0, 1000000.0).Default(3000.0))
			.Add(TEXT("exclude"), FToolParam::Array(TEXT("Keep clear of splines: {spline, clearance cm}"), FToolParam::Object(TEXT("Exclusion")), 32))
			.Add(TEXT("ground"), FToolParam::Boolean(TEXT("Snap to the ground below")).Default(true))
			.Add(TEXT("seed"), FToolParam::Integer(TEXT("Random seed"), 0, 1000000000).Default(1))
			.Add(TEXT("name"), FToolParam::String(TEXT("Node name"), 64)))
		.Undoable().RequiresWorld()
		.Handle(&Scatter);
}
