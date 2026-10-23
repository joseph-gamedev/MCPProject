#include "RuntimeRoadToolset.h"
#include "RuntimeBuildToolset.h"
#include "Math/RandomStream.h"
#include "Engine/StaticMesh.h"
#include "RuntimeCommonCommands.h"
#include "RuntimeSceneGraph.h"
#include "RuntimeToolInvocation.h"
#include "RuntimeToolReflection.h"
#include "CollisionQueryParams.h"
#include "Components/SplineComponent.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "UObject/UnrealType.h"

namespace RtRoad
{
	static const TCHAR* RoadClass = TEXT("/Game/Road_Creator_Pro/Blueprints/BP_Road_Creator.BP_Road_Creator_C");

	struct FKind
	{
		const TCHAR* Name;
		const TCHAR* Path;
	};
	static const FKind Kinds[] = {
		{TEXT("traffic_lights"), TEXT("BP_Traffic_Lights")}, {TEXT("road_sign"), TEXT("BP_Road_Sign")},
		{TEXT("destination_sign"), TEXT("BP_Destination_Sign")}, {TEXT("lamp_post"), TEXT("BP_Lamp_Post")},
		{TEXT("boom_barrier"), TEXT("BP_Boom_Barrier")}, {TEXT("parking_area"), TEXT("BP_Parking_Area")},
		{TEXT("building"), TEXT("BP_Building")}, {TEXT("junction"), TEXT("BP_Junction_Placer")},
		{TEXT("expansion_joint"), TEXT("BP_Expansion_Joint")}, {TEXT("retaining_wall"), TEXT("BP_Retaining_Wall")},
		{TEXT("utility_poles"), TEXT("BP_Utility_Poles")}, {TEXT("power_line"), TEXT("BP_Power_Transmission_Line")},
		{TEXT("aviation_light"), TEXT("BP_Aviation_Obstruction_Light")}, {TEXT("spotlight"), TEXT("BP_Spotlight")},
		{TEXT("zebra_crossing"), TEXT("Decals/BP_Decal_Zebra")}, {TEXT("pedestrian_crossing"), TEXT("Decals/BP_Decal_Ped_Crossing")},
		{TEXT("stop_line"), TEXT("Decals/BP_Decal_Stop_Line")}, {TEXT("speed_limit"), TEXT("Decals/BP_Decal_Speed_Limit")},
		{TEXT("lane_divider"), TEXT("Decals/BP_Decal_Lane_Divider")}, {TEXT("parking_space"), TEXT("Decals/BP_Decal_Parking_Space_Color")},
		{TEXT("graffiti"), TEXT("Decals/BP_Decal_Leaks_Graffiti")}, {TEXT("bench"), TEXT("Props_Instancer/BP_Bench")},
		{TEXT("bike_parking"), TEXT("Props_Instancer/BP_Bike_Parking")}, {TEXT("cones"), TEXT("Props_Instancer/BP_Barrier_Cones")},
		{TEXT("delineator_poles"), TEXT("Props_Instancer/BP_Barrier_Delineator_Poles")}, {TEXT("wheel_stopper"), TEXT("Props_Instancer/BP_Wheel_Stopper")},
		{TEXT("concrete_barrier"), TEXT("Props_Instancer/BP_Barrier_Concrete_1")}, {TEXT("sidewalk_fence"), TEXT("Props_Instancer/BP_Barrier_Sidewalk_Fence")},
		{TEXT("fence_wooden"), TEXT("Fences/BP_Fence_Wooden_A")}, {TEXT("grass_pavers"), TEXT("Props_Instancer/BP_Grass_Pavers")}};

	static FString KindPath(const FKind& K)
	{
		const FString Base = FPaths::GetBaseFilename(K.Path);
		return FString::Printf(TEXT("/Game/Road_Creator_Pro/Blueprints/%s.%s_C"), K.Path, *Base);
	}

	static UClass* LoadClassAt(const FString& Path)
	{
		return LoadObject<UClass>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
	}

	static FProperty* FindProp(UClass* C, const FString& Name)
	{
		for (TFieldIterator<FProperty> It(C); It; ++It)
		{
			if (It->GetAuthoredName().Equals(Name, ESearchCase::IgnoreCase) || It->GetName().Equals(Name, ESearchCase::IgnoreCase))
			{
				return *It;
			}
		}
		return nullptr;
	}

	static UEnum* EnumOf(const FProperty* P)
	{
		if (const FByteProperty* B = CastField<FByteProperty>(P))
		{
			return B->Enum;
		}
		if (const FEnumProperty* E = CastField<FEnumProperty>(P))
		{
			return E->GetEnum();
		}
		return nullptr;
	}

	/** Sets one setting; enums also take their display names (e.g. "2-Lane Road"). */
	static bool SetProp(AActor* A, FProperty* P, const TSharedPtr<FJsonValue>& V, FString& Err)
	{
		if (UEnum* En = EnumOf(P); En && V->Type == EJson::String)
		{
			for (int32 i = 0; i < En->NumEnums() - 1; ++i)
			{
				if (En->GetDisplayNameTextByIndex(i).ToString().Equals(V->AsString(), ESearchCase::IgnoreCase) ||
					En->GetNameStringByIndex(i).Equals(V->AsString(), ESearchCase::IgnoreCase))
				{
					const int64 Value = En->GetValueByIndex(i);
					void* Ptr = P->ContainerPtrToValuePtr<void>(A);
					if (const FEnumProperty* EP = CastField<FEnumProperty>(P))
					{
						EP->GetUnderlyingProperty()->SetIntPropertyValue(Ptr, Value);
					}
					else
					{
						CastField<FByteProperty>(P)->SetIntPropertyValue(Ptr, Value);
					}
					return true;
				}
			}
			TArray<FString> Names;
			for (int32 i = 0; i < En->NumEnums() - 1; ++i)
			{
				Names.Add(En->GetDisplayNameTextByIndex(i).ToString());
			}
			Err = FString::Printf(TEXT("'%s' is not one of %s"), *V->AsString(), *FString::Join(Names, TEXT(", ")));
			return false;
		}
		RuntimeToolReflection::FConvertContext Conv;
		return RuntimeToolReflection::JsonToProperty(V, P, P->ContainerPtrToValuePtr<void>(A), Conv, Err);
	}

	/** Ground height under (x, y): the first thing hit straight down, ignoring characters. */
	static bool Ground(UWorld* W, const FVector& P, double& OutZ)
	{
		FHitResult Hit;
		FCollisionQueryParams Q(TEXT("RoadGround"), true);
		for (TActorIterator<APawn> It(W); It; ++It)
		{
			Q.AddIgnoredActor(*It);
		}
		if (W->LineTraceSingleByChannel(Hit, FVector(P.X, P.Y, P.Z + 100000.0), FVector(P.X, P.Y, P.Z - 100000.0), ECC_Visibility, Q))
		{
			OutZ = Hit.ImpactPoint.Z;
			return true;
		}
		return false;
	}

	/** Spawns a class as a scene node with settings in place before its construction script runs. */
	static AActor* SpawnConfigured(const FString& ClassPath, const FTransform& T, const FString& NodeName, const TSharedPtr<FJsonObject>& Settings)
	{
		auto Spawn = [ClassPath, T, Settings](FRuntimeCommandContext& Ctx, FString& Err) -> AActor*
		{
			UClass* C = LoadClassAt(ClassPath);
			if (!C)
			{
				Err = TEXT("class missing: ") + ClassPath;
				return nullptr;
			}
			FActorSpawnParameters P;
			P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			P.bDeferConstruction = true;
			AActor* A = Ctx.World->SpawnActor<AActor>(C, T, P);
			if (!A)
			{
				Err = TEXT("spawn failed");
				return nullptr;
			}
			for (const TPair<FString, TSharedPtr<FJsonValue>>& KV : Settings->Values)
			{
				FProperty* Prop = FindProp(C, KV.Key);
				FString E;
				if (!Prop || !SetProp(A, Prop, KV.Value, E))
				{
					Err = Prop ? FString::Printf(TEXT("%s: %s"), *KV.Key, *E) : FString::Printf(TEXT("no setting '%s' (road.list settings)"), *KV.Key);
					A->Destroy();
					return nullptr;
				}
			}
			A->FinishSpawning(T);
			return A;
		};
		TUniquePtr<FCreateNodeCommand> Cmd = MakeUnique<FCreateNodeCommand>(TEXT("add ") + NodeName, Spawn, NodeName, FString(), ERuntimeNodeKind::Actor,
			TEXT("class:") + ClassPath, TArray<FName>());
		FCreateNodeCommand* Raw = Cmd.Get();
		if (!RuntimeToolCall::RunCommand(MoveTemp(Cmd)))
		{
			return nullptr;
		}
		const FRuntimeSceneNode* Node = RuntimeToolCall::GetGraph()->Find(Raw->GetNodeId());
		return Node ? Node->Actor.Get() : nullptr;
	}

	static bool NeedPack()
	{
		if (!LoadClassAt(RoadClass))
		{
			RuntimeToolCall::Fail(ERuntimeToolError::NotFound, TEXT("Road Creator Pro is not in this project"),
				TEXT("Add the Road Creator Pro pack (Content/Road_Creator_Pro)"));
			return false;
		}
		return true;
	}

	static FString Ref(AActor* A)
	{
		const FRuntimeSceneNode* N = A ? RuntimeToolCall::GetGraph()->FindByActor(A) : nullptr;
		return N ? N->Id : FString();
	}

	/** none / left / right / both / center -> the road's L, R, C switches. */
	static bool Sides(const TSharedRef<FJsonObject>& S, const FString& Which, const TCHAR* Prefix, bool bCenter, FString& Err)
	{
		const FString W = Which.ToLower();
		const bool bL = W == TEXT("left") || W == TEXT("both"), bR = W == TEXT("right") || W == TEXT("both"), bC = W == TEXT("center");
		if (W != TEXT("none") && W != TEXT("") && !bL && !bR && !(bC && bCenter))
		{
			Err = FString::Printf(TEXT("%s: '%s' is not none, left, right, both%s"), Prefix, *Which, bCenter ? TEXT(" or center") : TEXT(""));
			return false;
		}
		S->SetBoolField(FString(Prefix) + TEXT(" L"), bL);
		S->SetBoolField(FString(Prefix) + TEXT(" R"), bR);
		if (bCenter)
		{
			S->SetBoolField(FString(Prefix) + TEXT(" C"), bC);
		}
		return true;
	}

	// ---- junction pieces (measured from the meshes) ----
	static const TCHAR* PieceDir = TEXT("/Game/Road_Creator_Pro/Meshes/Road_Meshes/");
	static const double CrossHalf = 1000.0;                      // SM_Intersection_2x2_Sidewalk_Wide: 20 m square
	static const FVector2D ArmEnd(-5200.0, 1205.0);              // SM_Roundabout_50m_Road_2L stub end (pivot = ring centre)
	static const double RingRadius = 3505.0;
	static const double MouthDepth = 600.0;                      // SM_Junction_90*: road edge -> stub end

	/** Exits of junctions placed by the road tools; roads starting or ending on one snap to it. */
	struct FExit
	{
		FVector P, Dir;
		TWeakObjectPtr<AActor> Owner;
	};
	static TArray<FExit> GExits;

	static AActor* NodeActor(const FString& Id)
	{
		const FRuntimeSceneNode* N = RuntimeToolCall::GetGraph()->Find(Id);
		return N ? N->Actor.Get() : nullptr;
	}

	/** A pack mesh as a scene node (undoable, saved with the scene like build.place_mesh). */
	static FString Piece(const FString& Mesh, const FVector& At, double Yaw, const FString& Name)
	{
		UStaticMesh* M = LoadObject<UStaticMesh>(nullptr, *(FString(PieceDir) + Mesh), nullptr, LOAD_NoWarn | LOAD_Quiet);
		if (!M)
		{
			RuntimeToolCall::Fail(ERuntimeToolError::NotFound, TEXT("road piece missing: ") + Mesh);
			return FString();
		}
		const FString Source = TEXT("mesh:") + M->GetPathName();
		const FTransform T(FRotator(0.0, Yaw, 0.0), At);
		auto Spawn = [Source, T, Name](FRuntimeCommandContext& Ctx, FString& Err) { return RuntimeMeshAssets::SpawnFromSource(Ctx.World, Source, T, Name, Err); };
		TUniquePtr<FCreateNodeCommand> Cmd = MakeUnique<FCreateNodeCommand>(TEXT("add ") + Name, Spawn, Name, FString(), ERuntimeNodeKind::Actor, Source,
			TArray<FName>());
		FCreateNodeCommand* Raw = Cmd.Get();
		return RuntimeToolCall::RunCommand(MoveTemp(Cmd)) ? Raw->GetNodeId() : FString();
	}

	static FRoadToolExit AddExit(const FVector& P, const FVector& Dir, const FString& OwnerId)
	{
		GExits.RemoveAll([](const FExit& E) { return !E.Owner.IsValid(); });
		GExits.Add({P, Dir.GetSafeNormal2D(), NodeActor(OwnerId)});
		FRoadToolExit X;
		X.Point = P;
		X.Direction = Dir.GetSafeNormal2D();
		return X;
	}

	static const FExit* ExitNear(const FVector& P)
	{
		const FExit* Best = nullptr;
		double BestD = 500.0;
		for (const FExit& E : GExits)
		{
			const double D = FVector::Dist2D(E.P, P);
			if (E.Owner.IsValid() && D < BestD)
			{
				Best = &E;
				BestD = D;
			}
		}
		return Best;
	}

	/** Snaps route ends onto junction exits and adds a guide point so the road leaves the junction straight. */
	static void SnapEnds(TArray<FVector>& Pts)
	{
		if (Pts.Num() < 2)
		{
			return;
		}
		if (const FExit* E = ExitNear(Pts[0]))
		{
			Pts[0] = E->P;
			if (FVector::Dist2D(E->P, Pts[1]) > 2500.0)
			{
				Pts.Insert(E->P + E->Dir * 1200.0, 1);
			}
		}
		if (const FExit* E = ExitNear(Pts.Last()))
		{
			Pts.Last() = E->P;
			if (FVector::Dist2D(E->P, Pts[Pts.Num() - 2]) > 2500.0)
			{
				Pts.Insert(E->P + E->Dir * 1200.0, Pts.Num() - 1);
			}
		}
	}

	static FVector OnGround(UWorld* W, FVector P, double Lift)
	{
		double Z;
		if (Ground(W, P, Z))
		{
			P.Z = Z + Lift;
		}
		return P;
	}

	static bool BoolProp(AActor* A, const TCHAR* Name)
	{
		const FBoolProperty* B = CastField<FBoolProperty>(FindProp(A->GetClass(), Name));
		return B && B->GetPropertyValue_InContainer(A);
	}

	/** Half the asphalt width of a road actor, from its road type's mesh and width multiplier. */
	static double HalfWidth(AActor* Road)
	{
		FString TypeName = TEXT("2-Lane");
		FProperty* P = FindProp(Road->GetClass(), TEXT("Road Type"));
		if (UEnum* En = EnumOf(P))
		{
			const void* V = P->ContainerPtrToValuePtr<void>(Road);
			const FNumericProperty* Num = CastField<FEnumProperty>(P) ? CastField<FEnumProperty>(P)->GetUnderlyingProperty() : CastField<FNumericProperty>(P);
			if (Num)
			{
				TypeName = En->GetDisplayNameTextByValue(Num->GetSignedIntPropertyValue(V)).ToString();
			}
		}
		const TCHAR* Mesh = TypeName.Contains(TEXT("2+2")) ? TEXT("SM_Road_Wide_2+2_Lanes") : TypeName.Contains(TEXT("1-Lane")) ? TEXT("SM_Road_1_Lane")
			: TypeName.Contains(TEXT("3-Lane")) ? TEXT("SM_Road_3_Lanes") : TypeName.Contains(TEXT("4-Lane")) ? TEXT("SM_Road_4_Lanes") : TEXT("SM_Road_2_Lanes");
		double Half = 450.0;
		if (UStaticMesh* M = LoadObject<UStaticMesh>(nullptr, *(FString(PieceDir) + TEXT("Straight_Roads/") + Mesh), nullptr, LOAD_NoWarn | LOAD_Quiet))
		{
			Half = M->GetBoundingBox().GetSize().Y * 0.5;
		}
		if (const FFloatProperty* F = CastField<FFloatProperty>(FindProp(Road->GetClass(), TEXT("Overall Road Width Multiplier"))))
		{
			Half *= FMath::Max(0.1f, F->GetPropertyValue_InContainer(Road));
		}
		else if (const FDoubleProperty* D = CastField<FDoubleProperty>(FindProp(Road->GetClass(), TEXT("Overall Road Width Multiplier"))))
		{
			Half *= FMath::Max(0.1, D->GetPropertyValue_InContainer(Road));
		}
		return Half;
	}

	/** Where a branch mouth goes on a road: pivot on the asphalt edge, the way out, and the mouth's yaw. */
	struct FMouth
	{
		FVector Pivot, Out;
		double Yaw = 0.0;
	};
	static bool PlanBranch(AActor* Road, const FVector& At, const FString& Side, FMouth& M, FString& Err)
	{
		USplineComponent* Sp = Road ? Road->FindComponentByClass<USplineComponent>() : nullptr;
		if (!Sp)
		{
			Err = TEXT("road is not a road (no spline)");
			return false;
		}
		const float Key = Sp->FindInputKeyClosestToWorldLocation(At);
		const FVector C = Sp->GetLocationAtSplineInputKey(Key, ESplineCoordinateSpace::World);
		const FVector D = Sp->GetDirectionAtSplineInputKey(Key, ESplineCoordinateSpace::World).GetSafeNormal2D();
		const FVector Right(-D.Y, D.X, 0.0);
		bool bRight = Side.Equals(TEXT("right"), ESearchCase::IgnoreCase);
		if (Side.Equals(TEXT("auto"), ESearchCase::IgnoreCase) || Side.IsEmpty())
		{
			bRight = FVector::DotProduct(At - C, Right) >= 0.0;
		}
		else if (!bRight && !Side.Equals(TEXT("left"), ESearchCase::IgnoreCase))
		{
			Err = FString::Printf(TEXT("side '%s' is not left, right or auto"), *Side);
			return false;
		}
		if (BoolProp(Road, bRight ? TEXT("Sidewalk R") : TEXT("Sidewalk L")))
		{
			Err = FString::Printf(TEXT("the road has a sidewalk on the %s"), bRight ? TEXT("right") : TEXT("left"));
			return false;
		}
		M.Out = bRight ? Right : -Right;
		M.Pivot = C + M.Out * HalfWidth(Road);
		M.Yaw = FMath::RadiansToDegrees(FMath::Atan2(M.Out.X, -M.Out.Y));
		return true;
	}

	static FRoadToolJunction PlaceBranch(const FMouth& M, int32 Lanes)
	{
		FRoadToolJunction Out;
		Out.Id = Piece(Lanes >= 2 ? TEXT("Junctions/SM_Junction_90") : TEXT("Junctions/SM_Junction_90_1_Lane"), M.Pivot, M.Yaw, TEXT("RoadBranch"));
		if (!Out.Id.IsEmpty())
		{
			Out.Exits.Add(AddExit(M.Pivot + M.Out * MouthDepth, M.Out, Out.Id));
		}
		return Out;
	}
}

FRoadToolCatalog URuntimeRoadToolset::List()
{
	using namespace RtRoad;
	FRoadToolCatalog Out;
	UClass* C = LoadClassAt(RoadClass);
	Out.bInstalled = C != nullptr;
	for (const FKind& K : Kinds)
	{
		Out.Kinds.Add(K.Name);
	}
	if (!C)
	{
		return Out;
	}
	if (UEnum* En = EnumOf(FindProp(C, TEXT("Road Type"))))
	{
		for (int32 i = 0; i < En->NumEnums() - 1; ++i)
		{
			Out.Types.Add(En->GetDisplayNameTextByIndex(i).ToString());
		}
	}
	for (TFieldIterator<FProperty> It(C, EFieldIteratorFlags::ExcludeSuper); It; ++It)
	{
		const bool bSimple = It->IsA<FBoolProperty>() || It->IsA<FNumericProperty>() || It->IsA<FStructProperty>() || It->IsA<FEnumProperty>() ||
							 It->IsA<FObjectProperty>();
		if (bSimple && !It->HasAnyPropertyFlags(CPF_Transient) && It->HasAnyPropertyFlags(CPF_Edit))
		{
			Out.Settings.Add(It->GetAuthoredName(), It->GetCPPType());
		}
	}
	return Out;
}

FRoadToolResult URuntimeRoadToolset::Create(const TArray<FVector>& Points, const FRuntimeToolJson& Options, AActor* Spline, const FString& Type, const FString& Sidewalks,
	const FString& Lampposts, const FString& Guardrails, const FString& Fences, bool bLights, bool bFollowGround, bool bClosed, const FString& Name)
{
	using namespace RtRoad;
	FRoadToolResult Out;
	if (!NeedPack())
	{
		return Out;
	}
	USplineComponent* Given = Spline ? Spline->FindComponentByClass<USplineComponent>() : nullptr;
	if (Spline && !Given)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, TEXT("spline has no spline component"), TEXT("Make one with spline.create_spline"));
		return Out;
	}
	if (!Given && Points.Num() < 2)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, TEXT("a road needs a spline or at least 2 points"), TEXT("points: [[0,0,0],[5000,0,0]]"));
		return Out;
	}
	UWorld* W = RuntimeToolCall::GetWorld();
	// one smooth curve through the route (or the spline given), sampled every ~8 m and draped onto the ground; tangents
	// go with the points so the road's own spline stays smooth (no kinks for guardrails and markings to break at)
	USplineComponent* Curve = Given;
	if (!Curve)
	{
		TArray<FVector> Snapped = Points;
		if (!bClosed)
		{
			SnapEnds(Snapped);
		}
		Curve = NewObject<USplineComponent>(GetTransientPackage());
		Curve->SetSplinePoints(Snapped, ESplineCoordinateSpace::World, false);
		Curve->SetClosedLoop(bClosed, false);
		Curve->UpdateSpline();
	}
	const bool bLoop = Curve->IsClosedLoop();
	const double Length = Curve->GetSplineLength();
	const double Step = 800.0;
	const int32 N = FMath::Max(2, FMath::CeilToInt(Length / Step) + (bLoop ? 0 : 1));
	TArray<FVector> Route;
	for (int32 k = 0; k < N; ++k)
	{
		const double D = bLoop ? Length * k / N : Length * k / (N - 1);
		Route.Add(Curve->GetLocationAtDistanceAlongSpline(D, ESplineCoordinateSpace::World));
	}
	if (bFollowGround)
	{
		for (FVector& P : Route)
		{
			double Z;
			if (Ground(W, P, Z))
			{
				P.Z = Z + 3.0;
			}
		}
	}
	// tangents from the draped points (central differences, in spline units: about one segment long)
	TArray<TSharedPtr<FJsonValue>> Tans;
	for (int32 i = 0; i < Route.Num(); ++i)
	{
		const int32 Prev = bLoop ? (i - 1 + Route.Num()) % Route.Num() : FMath::Max(0, i - 1);
		const int32 Next = bLoop ? (i + 1) % Route.Num() : FMath::Min(Route.Num() - 1, i + 1);
		const FVector T = (Route[Next] - Route[Prev]) / FMath::Max(1, Next - Prev + (bLoop && Next < Prev ? Route.Num() : 0));
		Tans.Add(MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>{MakeShared<FJsonValueNumber>(T.X), MakeShared<FJsonValueNumber>(T.Y),
			MakeShared<FJsonValueNumber>(T.Z)}));
	}
	const FVector Origin = Route[0];
	TSharedRef<FJsonObject> S = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> Pts;
	for (const FVector& P : Route)
	{
		Pts.Add(MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>{MakeShared<FJsonValueNumber>(P.X), MakeShared<FJsonValueNumber>(P.Y),
			MakeShared<FJsonValueNumber>(P.Z)}));
	}
	S->SetArrayField(TEXT("Spawn Spline Points"), Pts);
	S->SetArrayField(TEXT("Spawn Spline Tangents"), Tans);
	S->SetBoolField(TEXT("Closed Loop"), bLoop);
	S->SetBoolField(TEXT("💡 Lights ON"), bLights);
	if (!Type.IsEmpty())
	{
		S->SetStringField(TEXT("Road Type"), Type);
	}
	FString Err;
	if (!Sides(S, Sidewalks, TEXT("Sidewalk"), false, Err) || !Sides(S, Lampposts, TEXT("💡 Lampposts"), true, Err) ||
		!Sides(S, Guardrails, TEXT("Guardrails"), false, Err) || !Sides(S, Fences, TEXT("Fence"), false, Err))
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, Err);
		return Out;
	}
	if (Options.IsSet())
	{
		const TSharedPtr<FJsonObject> O = Options.AsObject();
		if (!O.IsValid())
		{
			RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, TEXT("options must be an object of settings"));
			return Out;
		}
		for (const TPair<FString, TSharedPtr<FJsonValue>>& KV : O->Values)
		{
			S->SetField(KV.Key, KV.Value);
		}
	}
	AActor* A = SpawnConfigured(RoadClass, FTransform(Origin), Name, S);
	if (!A)
	{
		return Out;
	}
	Out.Id = Ref(A);
	Out.Points = Route.Num();
	if (USplineComponent* Sp = A->FindComponentByClass<USplineComponent>())
	{
		Out.LengthM = Sp->GetSplineLength() / 100.f;
	}
	return Out;
}

FRoadToolResult URuntimeRoadToolset::Place(const FString& Kind, const FVector& Location, const float& Yaw, const FRuntimeToolJson& Options)
{
	using namespace RtRoad;
	FRoadToolResult Out;
	if (!NeedPack())
	{
		return Out;
	}
	const FKind* K = nullptr;
	TArray<FString> Names;
	for (const FKind& X : Kinds)
	{
		Names.Add(X.Name);
		if (Kind.Equals(X.Name, ESearchCase::IgnoreCase))
		{
			K = &X;
		}
	}
	if (!K)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("unknown kind '%s' (%s)"), *Kind, *FString::Join(Names, TEXT(", "))));
		return Out;
	}
	FVector At = Location;
	double Z;
	if (Ground(RuntimeToolCall::GetWorld(), Location, Z))
	{
		At.Z = Z;
	}
	TSharedPtr<FJsonObject> S = Options.IsSet() && Options.AsObject().IsValid() ? Options.AsObject() : MakeShared<FJsonObject>();
	AActor* A = SpawnConfigured(KindPath(*K), FTransform(FRotator(0.f, Yaw, 0.f), At), K->Name, S);
	Out.Id = A ? Ref(A) : FString();
	return Out;
}

FRoadToolJunction URuntimeRoadToolset::Intersection(const FVector& Location, const float& Yaw)
{
	using namespace RtRoad;
	FRoadToolJunction Out;
	if (!NeedPack())
	{
		return Out;
	}
	const FVector C = OnGround(RuntimeToolCall::GetWorld(), Location, 3.0);
	Out.Id = Piece(TEXT("Intersections/SM_Intersection_2x2_Sidewalk_Wide"), C, Yaw, TEXT("Intersection"));
	if (Out.Id.IsEmpty())
	{
		return Out;
	}
	for (int32 k = 0; k < 4; ++k)
	{
		const FVector D = FRotator(0.0, Yaw + 90.0 * k, 0.0).Vector();
		Out.Exits.Add(AddExit(C + D * CrossHalf, D, Out.Id));
	}
	return Out;
}

FRoadToolJunction URuntimeRoadToolset::Roundabout(const FVector& Location, const TArray<FVector>& Toward)
{
	using namespace RtRoad;
	FRoadToolJunction Out;
	if (!NeedPack())
	{
		return Out;
	}
	if (Toward.Num() < 1 || Toward.Num() > 6)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, TEXT("toward needs 1 to 6 points (one arm each)"));
		return Out;
	}
	const FVector C = OnGround(RuntimeToolCall::GetWorld(), Location, 3.0);
	Out.Id = Piece(TEXT("Roundabout/SM_Roundabout_50m_Circle"), C, 0.0, TEXT("Roundabout"));
	if (Out.Id.IsEmpty())
	{
		return Out;
	}
	Piece(TEXT("Roundabout/SM_Roundabout_50m_Center"), C, 0.0, TEXT("RoundaboutIsland"));
	for (const FVector& T : Toward)
	{
		const FVector O = (T - C).GetSafeNormal2D();
		// the arm's stub points along its local -X: turn local -X onto the way out
		const double Yaw = FMath::RadiansToDegrees(FMath::Atan2(-O.Y, -O.X));
		Piece(TEXT("Roundabout/SM_Roundabout_50m_Road_2L"), C, Yaw, TEXT("RoundaboutArm"));
		const FVector End = C + FRotator(0.0, Yaw, 0.0).RotateVector(FVector(ArmEnd.X, ArmEnd.Y, 0.0));
		Out.Exits.Add(AddExit(End, O, Out.Id));
	}
	return Out;
}

FRoadToolJunction URuntimeRoadToolset::Branch(AActor* Road, const FVector& At, const FString& Side, int32 Lanes)
{
	using namespace RtRoad;
	FRoadToolJunction Out;
	if (!NeedPack())
	{
		return Out;
	}
	FMouth M;
	FString Err;
	if (!PlanBranch(Road, At, Side, M, Err))
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, Err, TEXT("branch from a side without a sidewalk (road.create sidewalks)"));
		return Out;
	}
	return PlaceBranch(M, Lanes);
}

namespace RtRoad
{
	struct FHouse
	{
		const TCHAR* Mesh;
		double W, D, Ox, Oy;   // width along the road, depth, bounds centre offset (fronts face -Y at yaw 0)
	};
	static const FHouse Houses[] = {{TEXT("SM_Building_Townhouse"), 804, 1231, 200, 306}, {TEXT("SM_Building_Seaside"), 3224, 2177, 200, -62},
		{TEXT("SM_Building_Modern_B"), 5430, 1587, -105, 17}, {TEXT("SM_Building_Panel_High"), 3419, 1927, -100, 113},
		{TEXT("SM_Building_Modern_A_1E"), 5122, 1887, -1559, 233}, {TEXT("SM_Building_Modern_A_2E"), 6340, 1887, -950, 233},
		{TEXT("SM_Building_Panel_2E"), 6619, 1927, -1500, -113}};
	static const int32 PoolLow[] = {0, 0, 1, 2}, PoolMid[] = {1, 2, 4, 6}, PoolTall[] = {3, 4, 5, 3};
	static const FColor HouseColors[] = {FColor(230, 222, 200), FColor(196, 120, 80), FColor(150, 155, 160), FColor(210, 190, 140), FColor(120, 90, 70),
		FColor(235, 235, 230)};

	struct FCircle
	{
		FVector2D P;
		double R;
	};
	static bool Clear(const TArray<FCircle>& A, const TArray<FCircle>& Taken)
	{
		for (const FCircle& X : A)
		{
			for (const FCircle& T : Taken)
			{
				if (FVector2D::Distance(X.P, T.P) < X.R + T.R)
				{
					return false;
				}
			}
		}
		return true;
	}
	static void RoadCircles(const TArray<FVector>& R, double Half, TArray<FCircle>& Out)
	{
		for (int32 i = 0; i + 1 < R.Num(); ++i)
		{
			const int32 N = FMath::Max(1, FMath::CeilToInt(FVector::Dist2D(R[i], R[i + 1]) / 400.0));
			for (int32 t = 0; t <= N; ++t)
			{
				Out.Add({FVector2D(FMath::Lerp(R[i], R[i + 1], double(t) / N)), Half + 300.0});
			}
		}
	}

	struct FStreet
	{
		TArray<FVector> Route;
		double Half;      // to the outer edge (sidewalks included)
		int32 Pool;       // 0 low, 1 mid, 2 tall, -1 by distance from the centre
	};

	/** Buildings along the streets, fronts to the street, clear of every road, junction and each other. */
	static int32 LineStreets(FRandomStream& Rng, const TArray<FStreet>& Streets, TArray<FCircle>& Taken, const FVector& Centre, double Radius, int32 Budget,
		bool bLights)
	{
		int32 Placed = 0;
		for (const FStreet& St : Streets)
		{
			for (int32 i = 0; i + 1 < St.Route.Num() && Placed < Budget; ++i)
			{
				const FVector A = St.Route[i], B = St.Route[i + 1];
				const double L = FVector::Dist2D(A, B);
				const FVector U = (B - A).GetSafeNormal2D();
				double S = Rng.FRandRange(200.0, 900.0);
				while (S < L && Placed < Budget)
				{
					int32 Pool = St.Pool;
					if (Pool < 0)
					{
						const double R = FVector::Dist2D(A + U * S, Centre) / Radius;
						Pool = R < 0.35 ? 2 : R < 0.7 ? 1 : 0;
					}
					const int32* Pick = Pool == 2 ? PoolTall : Pool == 1 ? PoolMid : PoolLow;
					const FHouse& H = Houses[Pick[Rng.RandRange(0, 3)]];
					const double SideSign = Rng.FRand() < 0.5 ? -1.0 : 1.0;
					const FVector N(-U.Y * SideSign, U.X * SideSign, 0.0);
					const FVector Ctr = A + U * (S + H.W * 0.5) + N * (St.Half + 500.0 + H.D * 0.5);
					// front toward the road: the house's local -Y must point along -N
					const double Yaw = FMath::RadiansToDegrees(FMath::Atan2(-N.X, N.Y));
					const FVector Along = FRotator(0.0, Yaw, 0.0).Vector();
					const int32 Parts = FMath::Max(1, FMath::RoundToInt(H.W / H.D));
					TArray<FCircle> Fp;
					for (int32 k = 0; k < Parts; ++k)
					{
						Fp.Add({FVector2D(Ctr + Along * (-H.W * 0.5 + (k + 0.5) * H.W / Parts)), FMath::Max(H.D, H.W / Parts) * 0.5 + 150.0});
					}
					if (Clear(Fp, Taken))
					{
						const FVector Off = FRotator(0.0, Yaw, 0.0).RotateVector(FVector(H.Ox, H.Oy, 0.0));
						const FVector At = OnGround(RuntimeToolCall::GetWorld(), Ctr, -30.0) - FVector(Off.X, Off.Y, 0.0);
						const FColor Col = HouseColors[Rng.RandRange(0, UE_ARRAY_COUNT(HouseColors) - 1)];
						TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
						O->SetStringField(TEXT("Building Mesh"), FString::Printf(TEXT("/Game/Road_Creator_Pro/Meshes/Buildings/%s.%s"), H.Mesh, H.Mesh));
						TSharedRef<FJsonObject> C = MakeShared<FJsonObject>();
						C->SetNumberField(TEXT("r"), Col.R);
						C->SetNumberField(TEXT("g"), Col.G);
						C->SetNumberField(TEXT("b"), Col.B);
						C->SetNumberField(TEXT("a"), 255);
						O->SetObjectField(TEXT("Building Color"), C);
						O->SetBoolField(TEXT("Lights ON"), bLights);
						if (!URuntimeRoadToolset::Place(TEXT("building"), At, static_cast<float>(Yaw), FRuntimeToolJson::Make(TSharedPtr<FJsonObject>(O))).Id.IsEmpty())
						{
							Taken.Append(Fp);
							++Placed;
						}
						S += H.W + Rng.FRandRange(300.0, 1200.0);
					}
					else
					{
						S += 400.0;
					}
				}
			}
		}
		return Placed;
	}

	/** Trees as GPU instances, one node per species: street trees on the verges, then woods in the open land. */
	static int32 PlantTrees(FRandomStream& Rng, const TArray<FStreet>& Streets, TArray<FCircle>& Taken, const FVector& Centre, double Half, int32 Budget)
	{
		static const TCHAR* Street[] = {TEXT("SM_Oak_01"), TEXT("SM_Maple_01"), TEXT("SM_Maple_02"), TEXT("SM_Birch_01"), TEXT("SM_Aspen_01"),
			TEXT("SM_JapaneseMaple_01")};
		static const TCHAR* Wood[] = {TEXT("SM_Oak_02"), TEXT("SM_Oak_03"), TEXT("SM_Pine_01"), TEXT("SM_Pine_02"), TEXT("SM_Fir_01"), TEXT("SM_Birch_02"),
			TEXT("SM_Bush_01"), TEXT("SM_Bush_02")};
		TMap<FString, TArray<TSharedPtr<FJsonValue>>> Rows;
		UWorld* W = RuntimeToolCall::GetWorld();
		int32 Planted = 0;
		auto Plant = [&](const TCHAR* Mesh, const FVector& P, double Radius) -> bool
		{
			const TArray<FCircle> Me = {{FVector2D(P), Radius}};
			if (!Clear(Me, Taken))
			{
				return false;
			}
			double Z;
			if (!Ground(W, P, Z))
			{
				return false;
			}
			const double Sc = Rng.FRandRange(0.8, 1.25);
			Rows.FindOrAdd(Mesh).Add(MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>{MakeShared<FJsonValueNumber>(FMath::RoundToDouble(P.X)),
				MakeShared<FJsonValueNumber>(FMath::RoundToDouble(P.Y)), MakeShared<FJsonValueNumber>(FMath::RoundToDouble(Z - 5.0)),
				MakeShared<FJsonValueNumber>(FMath::RoundToDouble(Rng.FRandRange(0.0, 360.0))), MakeShared<FJsonValueNumber>(Sc),
				MakeShared<FJsonValueNumber>(Sc), MakeShared<FJsonValueNumber>(Sc)}));
			Taken.Add({FVector2D(P), Radius});
			++Planted;
			return true;
		};
		// street trees: a row on each verge, every ~12 m
		const int32 StreetBudget = Budget * 2 / 5;
		for (const FStreet& St : Streets)
		{
			const TCHAR* Kind = Street[Rng.RandRange(0, UE_ARRAY_COUNT(Street) - 1)];
			for (int32 i = 0; i + 1 < St.Route.Num() && Planted < StreetBudget; ++i)
			{
				const FVector A = St.Route[i], B = St.Route[i + 1];
				const FVector U = (B - A).GetSafeNormal2D(), N(-U.Y, U.X, 0.0);
				for (double S = 600.0; S < FVector::Dist2D(A, B) && Planted < StreetBudget; S += Rng.FRandRange(1000.0, 1500.0))
				{
					for (double Side : {-1.0, 1.0})
					{
						Plant(Kind, A + U * S + N * Side * (St.Half + 450.0), 250.0);
					}
				}
			}
		}
		// woods: clumps in the open land
		for (int32 Tries = 0; Planted < Budget && Tries < Budget * 4; ++Tries)
		{
			const FVector C = Centre + FVector(Rng.FRandRange(-Half, Half), Rng.FRandRange(-Half, Half), 0.0);
			const TCHAR* Kind = Wood[Rng.RandRange(0, UE_ARRAY_COUNT(Wood) - 1)];
			const int32 Clump = Rng.RandRange(4, 12);
			for (int32 k = 0; k < Clump && Planted < Budget; ++k)
			{
				const double R = Rng.FRandRange(0.0, 2500.0), T = Rng.FRandRange(0.0, 2.0 * PI);
				Plant(Rng.FRand() < 0.75 ? Kind : Wood[Rng.RandRange(0, UE_ARRAY_COUNT(Wood) - 1)], C + FVector(FMath::Cos(T) * R, FMath::Sin(T) * R, 0.0), 450.0);
			}
		}
		int32 Made = 0;
		for (TPair<FString, TArray<TSharedPtr<FJsonValue>>>& KV : Rows)
		{
			const FString Path = FString::Printf(TEXT("/Game/RuntimeFoliage/Trees/%s.%s"), *KV.Key, *KV.Key);
			if (!LoadObject<UStaticMesh>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet))
			{
				continue;
			}
			TSharedRef<FJsonObject> Src = MakeShared<FJsonObject>();
			Src->SetStringField(TEXT("a"), Path);
			Src->SetBoolField(TEXT("c"), false);
			Src->SetArrayField(TEXT("t"), KV.Value);
			FString Json;
			TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Wr = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Json);
			FJsonSerializer::Serialize(Src, Wr);
			const FString Source = TEXT("inst:") + Json;
			const FString Name = TEXT("Trees_") + KV.Key.RightChop(3);
			auto Spawn = [Source, Name](FRuntimeCommandContext& Ctx, FString& Err)
			{ return RuntimeMeshAssets::SpawnInstancesFromSource(Ctx.World, Source, FTransform::Identity, Name, Err); };
			if (RuntimeToolCall::RunCommand(MakeUnique<FCreateNodeCommand>(TEXT("add ") + Name, Spawn, Name, FString(), ERuntimeNodeKind::Actor, Source,
					TArray<FName>())))
			{
				Made += KV.Value.Num();
			}
		}
		return Made;
	}

	static FString MakeRoad(const TArray<FVector>& Pts, const FString& Type, const FString& Sidewalks, const FString& Lampposts, bool bLights, const FString& Name)
	{
		return URuntimeRoadToolset::Create(Pts, FRuntimeToolJson(), nullptr, Type, Sidewalks, Lampposts, TEXT("none"), TEXT("none"), bLights, true, false, Name).Id;
	}
}

FRoadToolTown URuntimeRoadToolset::Generate(const FVector& Location, const FString& Layout, float Size, int32 Junctions, int32 SideStreets, int32 Cols,
	int32 Rows, float Block, int32 Buildings, int32 Trees, int32 Seed, bool bLights)
{
	using namespace RtRoad;
	FRoadToolTown Out;
	if (!NeedPack())
	{
		return Out;
	}
	FRandomStream Rng(Seed);
	TArray<FStreet> Streets;
	TArray<FCircle> Taken;
	const FVector Base(Location.X, Location.Y, 0.0);

	if (Layout.Equals(TEXT("grid"), ESearchCase::IgnoreCase))
	{
		Cols = FMath::Clamp(Cols, 1, 12);
		Rows = FMath::Clamp(Rows, 1, 12);
		Block = FMath::Max(Block, 4000.f);
		const double X0 = -Cols * Block * 0.5, Y0 = -Rows * Block * 0.5, Stub = Block * 0.4;
		TArray<TArray<FRoadToolJunction>> J;
		for (int32 i = 0; i <= Cols; ++i)
		{
			J.AddDefaulted();
			for (int32 j = 0; j <= Rows; ++j)
			{
				const FVector C = Base + FVector(X0 + i * Block, Y0 + j * Block, 0.0);
				J[i].Add(Intersection(C, 0.f));
				Taken.Add({FVector2D(C), CrossHalf * 1.42 + 300.0});
				++Out.Junctions;
			}
		}
		// exits: 0 +X, 1 +Y, 2 -X, 3 -Y
		auto Street = [&](const FVector& A, const FVector& B, const FString& Name)
		{
			const FString Id = MakeRoad({A, B}, TEXT("2-Lane Road"), TEXT("both"), TEXT("both"), bLights, Name);
			if (!Id.IsEmpty())
			{
				Out.RoadIds.Add(Id);
				Streets.Add({{A, B}, CrossHalf, -1});
				RoadCircles({A, B}, CrossHalf, Taken);
			}
		};
		for (int32 j = 0; j <= Rows; ++j)
		{
			Street(J[0][j].Exits[2].Point - FVector(Stub, 0, 0), J[0][j].Exits[2].Point, TEXT("Street"));
			for (int32 i = 0; i < Cols; ++i)
			{
				Street(J[i][j].Exits[0].Point, J[i + 1][j].Exits[2].Point, TEXT("Street"));
			}
			Street(J[Cols][j].Exits[0].Point, J[Cols][j].Exits[0].Point + FVector(Stub, 0, 0), TEXT("Street"));
		}
		for (int32 i = 0; i <= Cols; ++i)
		{
			Street(J[i][0].Exits[3].Point - FVector(0, Stub, 0), J[i][0].Exits[3].Point, TEXT("Avenue"));
			for (int32 j = 0; j < Rows; ++j)
			{
				Street(J[i][j].Exits[1].Point, J[i][j + 1].Exits[3].Point, TEXT("Avenue"));
			}
			Street(J[i][Rows].Exits[1].Point, J[i][Rows].Exits[1].Point + FVector(0, Stub, 0), TEXT("Avenue"));
		}
		for (int32 i = 1; i < Cols; ++i)
		{
			for (int32 j = 1; j < Rows; ++j)
			{
				const FVector C = Base + FVector(X0 + i * Block, Y0 + j * Block, 0.0);
				Place(TEXT("traffic_lights"), C, 0.f, FRuntimeToolJson());
				for (int32 k = 0; k < 4; ++k)
				{
					const FVector D = FRotator(0.0, 90.0 * k, 0.0).Vector();
					Place(TEXT("zebra_crossing"), C + D * (CrossHalf + 250.0), 90.f * k + 90.f, FRuntimeToolJson());
				}
			}
		}
		Out.Roads = Out.RoadIds.Num();
		Out.Buildings = LineStreets(Rng, Streets, Taken, Base, FMath::Max(Cols, Rows) * Block * 0.5, Buildings, bLights);
		Out.Trees = Trees > 0 ? PlantTrees(Rng, Streets, Taken, Base, FMath::Max(Cols, Rows) * Block * 0.5 + Stub, Trees) : 0;
		return Out;
	}

	if (!Layout.Equals(TEXT("organic"), ESearchCase::IgnoreCase))
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("layout '%s' is not organic or grid"), *Layout));
		return Out;
	}
	// junctions scattered at random, joined into a Gabriel graph (no crossings, no long detours)
	Junctions = FMath::Clamp(Junctions, 2, 40);
	const double Gap = Size * 0.8 / FMath::Sqrt(double(Junctions));
	TArray<FVector> Pts;
	for (int32 Tries = 0; Pts.Num() < Junctions && Tries < 5000; ++Tries)
	{
		const FVector P = Base + FVector(Rng.FRandRange(-0.45, 0.45) * Size, Rng.FRandRange(-0.45, 0.45) * Size, 0.0);
		if (!Pts.ContainsByPredicate([&](const FVector& Q) { return FVector::Dist2D(P, Q) < Gap; }))
		{
			Pts.Add(P);
		}
	}
	const int32 N = Pts.Num();
	TArray<TPair<int32, int32>> Edges;
	for (int32 a = 0; a < N; ++a)
	{
		for (int32 b = a + 1; b < N; ++b)
		{
			const FVector M = (Pts[a] + Pts[b]) * 0.5;
			const double R = FVector::Dist2D(Pts[a], Pts[b]) * 0.5;
			bool bOk = true;
			for (int32 c = 0; c < N && bOk; ++c)
			{
				bOk = c == a || c == b || FVector::Dist2D(M, Pts[c]) >= R;
			}
			if (bOk)
			{
				Edges.Add({a, b});
			}
		}
	}
	TArray<TArray<TPair<int32, int32>>> Adj;   // node -> (other node, edge)
	Adj.SetNum(N);
	for (int32 k = 0; k < Edges.Num(); ++k)
	{
		Adj[Edges[k].Key].Add({Edges[k].Value, k});
		Adj[Edges[k].Value].Add({Edges[k].Key, k});
	}
	auto IsHub = [&](int32 i) { return Adj[i].Num() >= 3; };
	// chains: roads run on through two-way junctions
	TArray<TArray<int32>> Chains;
	TSet<int32> Used;
	TArray<int32> Starts;
	for (int32 i = 0; i < N; ++i)
	{
		if (Adj[i].Num() != 2)
		{
			Starts.Add(i);
		}
	}
	for (int32 i = 0; i < N; ++i)
	{
		Starts.Add(i);
	}
	for (int32 Start : Starts)
	{
		for (const TPair<int32, int32>& First : Adj[Start])
		{
			if (Used.Contains(First.Value))
			{
				continue;
			}
			TArray<int32> Chain = {Start};
			int32 Cur = First.Key, Edge = First.Value;
			while (true)
			{
				Used.Add(Edge);
				Chain.Add(Cur);
				if (IsHub(Cur) || Adj[Cur].Num() != 2 || Cur == Start)
				{
					break;
				}
				const TPair<int32, int32>* Next = Adj[Cur].FindByPredicate([&](const TPair<int32, int32>& E) { return !Used.Contains(E.Value); });
				if (!Next)
				{
					break;
				}
				Cur = Next->Key;
				Edge = Next->Value;
			}
			Chains.Add(Chain);
		}
	}
	// each chain's inner route: a gentle wobble between junctions
	TArray<TArray<FVector>> Inner;
	for (const TArray<int32>& Chain : Chains)
	{
		TArray<FVector> R;
		for (int32 i = 0; i + 1 < Chain.Num(); ++i)
		{
			const FVector A = Pts[Chain[i]], B = Pts[Chain[i + 1]];
			const FVector D = B - A, Nrm = FVector(-D.Y, D.X, 0.0).GetSafeNormal();
			const double Off = Rng.FRandRange(-0.1, 0.1) * D.Size2D();
			if (i > 0)
			{
				R.Add(A);
			}
			R.Add(A + D * 0.33 + Nrm * Off);
			R.Add(A + D * 0.66 + Nrm * Off * 0.6);
		}
		Inner.Add(R);
	}
	// roundabouts where 3+ roads meet, one arm toward each road
	TMap<int32, TArray<TPair<int32, bool>>> HubRoads;   // hub -> (chain, at the chain's end)
	for (int32 c = 0; c < Chains.Num(); ++c)
	{
		if (IsHub(Chains[c][0]))
		{
			HubRoads.FindOrAdd(Chains[c][0]).Add({c, false});
		}
		if (IsHub(Chains[c].Last()))
		{
			HubRoads.FindOrAdd(Chains[c].Last()).Add({c, true});
		}
	}
	TMap<TPair<int32, bool>, FVector> EndAt;
	for (const TPair<int32, TArray<TPair<int32, bool>>>& H : HubRoads)
	{
		TArray<FVector> Toward;
		for (const TPair<int32, bool>& R : H.Value)
		{
			Toward.Add(R.Value ? Inner[R.Key].Last() : Inner[R.Key][0]);
		}
		const FRoadToolJunction J = Roundabout(Pts[H.Key], Toward);
		if (J.Id.IsEmpty())
		{
			continue;
		}
		for (int32 k = 0; k < J.Exits.Num(); ++k)
		{
			EndAt.Add(H.Value[k], J.Exits[k].Point);
		}
		Taken.Add({FVector2D(Pts[H.Key]), 5200.0});
		++Out.Junctions;
	}
	// the roads: main roads (roundabout to roundabout) with sidewalks and lamps, the rest plain 2-lane
	TArray<int32> Plain;   // index into Streets / RoadIds
	for (int32 c = 0; c < Chains.Num(); ++c)
	{
		TArray<FVector> Route;
		Route.Add(EndAt.Contains({c, false}) ? EndAt[{c, false}] : Pts[Chains[c][0]]);
		Route.Append(Inner[c]);
		Route.Add(EndAt.Contains({c, true}) ? EndAt[{c, true}] : Pts[Chains[c].Last()]);
		const bool bMain = IsHub(Chains[c][0]) && IsHub(Chains[c].Last()) && Rng.FRand() < 0.4;
		const FString Id = MakeRoad(Route, TEXT("2-Lane Road"), bMain ? TEXT("both") : TEXT("none"), bMain ? TEXT("right") : TEXT("none"), bLights,
			bMain ? TEXT("MainRoad") : TEXT("Road"));
		if (Id.IsEmpty())
		{
			continue;
		}
		Out.RoadIds.Add(Id);
		const double Half = bMain ? CrossHalf : 450.0;
		Streets.Add({Route, Half, bMain ? 1 : 0});
		RoadCircles(Route, Half, Taken);
		if (!bMain)
		{
			Plain.Add(Streets.Num() - 1);
		}
	}
	// dead-end side streets off the plain roads, where they fit
	for (int32 Tries = 0; Out.SideStreets < SideStreets && Plain.Num() && Tries < SideStreets * 25; ++Tries)
	{
		const int32 From = Plain[Rng.RandRange(0, Plain.Num() - 1)];
		const TArray<FVector> FromRoute = Streets[From].Route;
		if (FromRoute.Num() < 3)
		{
			continue;
		}
		AActor* Road = NodeActor(Out.RoadIds[From]);
		const FVector At = FromRoute[Rng.RandRange(1, FromRoute.Num() - 2)];
		FMouth M;
		FString Err;
		if (!Road || !PlanBranch(Road, At, Rng.FRand() < 0.5 ? TEXT("left") : TEXT("right"), M, Err))
		{
			continue;
		}
		const FVector Exit = M.Pivot + M.Out * MouthDepth;
		const FVector Along(M.Out.Y, -M.Out.X, 0.0);
		const double Len = Rng.FRandRange(6000.0, 13000.0), Bend = Rng.FRandRange(-0.25, 0.25);
		const TArray<FVector> Route = {Exit, Exit + M.Out * Len * 0.5 + Along * Len * Bend * 0.3, Exit + M.Out * Len + Along * Len * Bend};
		TArray<FCircle> Mine;
		RoadCircles({Route[1], Route[2]}, 300.0, Mine);
		Mine.Add({FVector2D(Exit + M.Out * 1500.0), 600.0});
		if (!Clear(Mine, Taken))
		{
			continue;
		}
		if (PlaceBranch(M, 1).Id.IsEmpty())
		{
			continue;
		}
		const FString Id = MakeRoad(Route, TEXT("1-Lane Road"), TEXT("none"), TEXT("none"), bLights, TEXT("SideStreet"));
		if (Id.IsEmpty())
		{
			continue;
		}
		Out.RoadIds.Add(Id);
		Taken.Append(Mine);
		Streets.Add({Route, 250.0, 0});
		++Out.SideStreets;
	}
	Out.Roads = Out.RoadIds.Num();
	Out.Buildings = LineStreets(Rng, Streets, Taken, Base, Size * 0.5, Buildings, bLights);
	Out.Trees = Trees > 0 ? PlantTrees(Rng, Streets, Taken, Base, Size * 0.5, Trees) : 0;
	return Out;
}
