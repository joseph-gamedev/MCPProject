#include "RuntimeFluidToolset.h"
#include "PresentationToolCall.h"
#include "RuntimeCommand.h"
#include "RuntimeCommonCommands.h"
#include "RuntimeSceneGraph.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "NiagaraActor.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "Engine/StaticMesh.h"
#include "NiagaraSystem.h"
#include "UObject/UnrealType.h"
#include "Dom/JsonObject.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/Texture.h"
#include "Engine/TextureRenderTarget2D.h"
#include "RuntimeFluidComponents.h"
#include "UObject/StrongObjectPtr.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Components/BoxComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Engine/ExponentialHeightFog.h"
#include "UObject/UObjectIterator.h"
#include "Engine/DirectionalLight.h"
#include "Components/StaticMeshComponent.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

// effects harvested from the FluidNinja demo levels by Scripts/fluid/harvest_fluid_effects.py
#include "Generated/FluidEffects.gen.inl"

using namespace PresentationTool;

// FluidNinja LIVE is a content pack (Blueprints): driven through reflection on its own names, so the plugin builds and
// runs without it. Its simulation reads its settings once, when it starts (materials, surface links), so a preset is
// applied to the component template before the actor is spawned, the way the editor's preset loading does.
namespace RtFluid
{
	static const TCHAR* ActorClassPath = TEXT("/Game/FluidNinjaLive/NinjaLive.NinjaLive_C");
	static const TCHAR* SurfaceSystemPath = TEXT("/Game/FluidNinjaLive/OutputNiagara/SurfaceAlignedMeshes.SurfaceAlignedMeshes");
	static const TCHAR* PresetFolder = TEXT("/Game/FluidNinjaLive/Presets/");
	static const TCHAR* WaterFolders[] = {TEXT("/Game/FluidNinjaLive/OutputMaterials/Surface_Water_Sparse"), TEXT("/Game/FluidNinjaLive/OutputMaterials/Surface_Water_Dense")};
	static const TCHAR* FluidTag = TEXT("RTFluid");
	// the water surface: 1 x 1 m plane tiles (high vertex count) laid in a grid over the simulated area
	static const TCHAR* TileMeshPath = TEXT("/Game/FluidNinjaLive/Meshes/Planes/SM_plane_500x500_MultiLOD.SM_plane_500x500_MultiLOD");
	static constexpr float TileMeters = 5.f;

	struct FPresetName
	{
		const TCHAR* Name;
		const TCHAR* Asset;
	};
	static const FPresetName Presets[] = {{TEXT("lake"), TEXT("NP_Water_Sparse_Lake1")}, {TEXT("pool"), TEXT("NP_Water_Sparse_Pool1")},
		{TEXT("sea"), TEXT("NP_Water_Dense_Sea_Still_100m")}, {TEXT("sea_wavy"), TEXT("NP_Water_Dense_Sea_Wavy_100m")},
		{TEXT("sea_200m"), TEXT("NP_Water_Dense_Sea_Still_200m")}, {TEXT("sea_wavy_200m"), TEXT("NP_Water_Dense_Sea_Wavy_200m")},
		{TEXT("river"), TEXT("NP_Water_Sparse_River_ClearWater_Inverse")}, {TEXT("river_whitewater"), TEXT("NP_Water_Sparse_River_WhiteWater")},
		{TEXT("river_whitewater_inverse"), TEXT("NP_Water_Sparse_River_WhiteWater_Inverse")}, {TEXT("water_tank"), TEXT("NP_WaterTank")},
		{TEXT("tracks"), TEXT("NP_Tracks_SimplePainter1")}, {TEXT("default"), TEXT("NP_default")}};

	struct FKind
	{
		const TCHAR* Name;
		const TCHAR* System;
	};
	static const FKind SourceKinds[] = {{TEXT("water_hose"), TEXT("/Game/FluidNinjaLive/Input/Niagara/NS_WriteDataCh_GPU_WaterHose")},
		{TEXT("water_hose_mini"), TEXT("/Game/FluidNinjaLive/Input/Niagara/NS_WriteDataCh_GPU_WaterHoseMini")},
		{TEXT("jet"), TEXT("/Game/FluidNinjaLive/Input/Niagara/NS_InputForFluidSim_JetStream")},
		{TEXT("vortex"), TEXT("/Game/FluidNinjaLive/Input/Niagara/NS_InputForFluidSim_Vortex")},
		{TEXT("avalanche"), TEXT("/Game/FluidNinjaLive/Input/Niagara/NS_InputForFluidSim_Avalanche")},
		{TEXT("bonfire"), TEXT("/Game/FluidNinjaLive/Input/Niagara/NS_WriteDataCh_GPU_Bonfire")},
		{TEXT("smoke"), TEXT("/Game/FluidNinjaLive/Input/Niagara/NS_WriteDataCh_GPU_BonfireSmoke")},
		{TEXT("smoke_light"), TEXT("/Game/FluidNinjaLive/Input/Niagara/NS_WriteDataCh_GPU_BonfireSmoke_Light")},
		{TEXT("burst"), TEXT("/Game/FluidNinjaLive/Input/Niagara/NS_WriteDataCh_GPU_Source_Burst")},
		{TEXT("burst_radial"), TEXT("/Game/FluidNinjaLive/Input/Niagara/NS_WriteDataCh_GPU_Source_BurstRadial")},
		{TEXT("radial"), TEXT("/Game/FluidNinjaLive/Input/Niagara/NS_WriteDataCh_GPU_Source_Radial")},
		{TEXT("linear"), TEXT("/Game/FluidNinjaLive/Input/Niagara/NS_WriteDataCh_GPU_Source_Linear")},
		{TEXT("turret"), TEXT("/Game/FluidNinjaLive/Input/Niagara/NS_WriteDataCh_GPU_Turret")},
		{TEXT("cauldron"), TEXT("/Game/FluidNinjaLive/Input/Niagara/NS_WriteDataCh_GPU_MagicCauldron")},
		{TEXT("grid"), TEXT("/Game/FluidNinjaLive/Input/Niagara/NS_WriteDataCh_GPU_Source_Grid_10Kparticles")}};
	static const FKind AmbientKinds[] = {{TEXT("blizzard"), TEXT("/Game/FluidNinjaLive/OutputNiagara/PassiveDecorSystems/NS_PASSIVE_Blizzard")},
		{TEXT("sandstorm"), TEXT("/Game/FluidNinjaLive/OutputNiagara/PassiveDecorSystems/NS_PASSIVE_SandStorm")},
		{TEXT("rain"), TEXT("/Game/FluidNinjaLive/OutputNiagara/PassiveDecorSystems/NS_PASSIVE_Rain_Additive")},
		{TEXT("rain_translucent"), TEXT("/Game/FluidNinjaLive/OutputNiagara/PassiveDecorSystems/NS_PASSIVE_Rain_Translucent")},
		{TEXT("sparks"), TEXT("/Game/FluidNinjaLive/OutputNiagara/PassiveDecorSystems/NS_PASSIVE_Sparks")},
		{TEXT("sparks_burning"), TEXT("/Game/FluidNinjaLive/OutputNiagara/PassiveDecorSystems/NS_PASSIVE_Sparks_BurningMan")},
		{TEXT("magic"), TEXT("/Game/FluidNinjaLive/OutputNiagara/PassiveDecorSystems/NS_PASSIVE_MagicCauldron")},
		{TEXT("plankton"), TEXT("/Game/FluidNinjaLive/OutputNiagara/PassiveDecorSystems/NS_PASSIVE_Plankton_Additive_v1")},
		{TEXT("plankton_soft"), TEXT("/Game/FluidNinjaLive/OutputNiagara/PassiveDecorSystems/NS_PASSIVE_Plankton_Additive_v2")}};

	static FString SystemPath(const TCHAR* Short)
	{
		const FString S(Short);
		return S + TEXT(".") + FPaths::GetBaseFilename(S);
	}

	/** The harvested effects: name -> {about, preset, sim_rot, extents, output_tags, companions[]}. */
	static TSharedPtr<FJsonObject> Effects()
	{
		static TSharedPtr<FJsonObject> Parsed;
		if (!Parsed.IsValid())
		{
			FString Text;
			for (const TCHAR* Part : GFluidEffectsJsonParts)
			{
				Text += Part;
			}
			TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(Text);
			if (!FJsonSerializer::Deserialize(R, Parsed) || !Parsed.IsValid())
			{
				Parsed = MakeShared<FJsonObject>();
			}
		}
		return Parsed;
	}

	static TSharedPtr<FJsonObject> Effect(const FString& Name)
	{
		const TSharedPtr<FJsonObject>* E = nullptr;
		return Effects()->TryGetObjectField(Name.ToLower(), E) ? *E : nullptr;
	}

	static FVector Vec(const TSharedPtr<FJsonObject>& O, const TCHAR* Field, const FVector& Default = FVector::ZeroVector)
	{
		const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
		return O->TryGetArrayField(Field, A) && A->Num() >= 3 ? FVector((*A)[0]->AsNumber(), (*A)[1]->AsNumber(), (*A)[2]->AsNumber()) : Default;
	}

	/** A copy of particle parameters with the sea-level clamp moved by Shift cm. */
	static TSharedPtr<FJsonObject> ShiftSea(const TSharedPtr<FJsonObject>& P, double Shift)
	{
		double V = 0.0;
		if (!P.IsValid() || Shift == 0.0 || !P->TryGetNumberField(TEXT("ClampingValue"), V) || V == 0.0)
		{
			return P;
		}
		TSharedPtr<FJsonObject> C = MakeShared<FJsonObject>(*P);
		C->SetNumberField(TEXT("ClampingValue"), V + Shift);
		return C;
	}

	/** Sets a Niagara component's user parameters from JSON, typed by the system's exposed parameters. */
	static void ApplyNiagaraParams(UNiagaraComponent* N, const TSharedPtr<FJsonObject>& P)
	{
		if (!N || !P.IsValid() || !N->GetAsset())
		{
			return;
		}
		TArray<FNiagaraVariable> Vars;
		N->GetAsset()->GetExposedParameters().GetUserParameters(Vars);
		for (const TPair<FString, TSharedPtr<FJsonValue>>& KV : P->Values)
		{
			const FNiagaraVariable* Var = Vars.FindByPredicate([&KV](const FNiagaraVariable& V)
			{
				FString Name = V.GetName().ToString();
				Name.RemoveFromStart(TEXT("User."));
				return Name == KV.Key;
			});
			if (!Var)
			{
				continue;
			}
			const FName Name(*KV.Key);
			const FNiagaraTypeDefinition& T = Var->GetType();
			const TSharedPtr<FJsonValue>& V = KV.Value;
			TArray<double> A;
			if (V->Type == EJson::Array)
			{
				for (const TSharedPtr<FJsonValue>& E : V->AsArray())
				{
					A.Add(E->AsNumber());
				}
			}
			if (T == FNiagaraTypeDefinition::GetBoolDef())
			{
				N->SetVariableBool(Name, V->AsBool());
			}
			else if (T == FNiagaraTypeDefinition::GetIntDef())
			{
				N->SetVariableInt(Name, static_cast<int32>(V->AsNumber()));
			}
			else if (T == FNiagaraTypeDefinition::GetFloatDef())
			{
				N->SetVariableFloat(Name, V->AsNumber());
			}
			else if (T == FNiagaraTypeDefinition::GetVec2Def() && A.Num() >= 2)
			{
				N->SetVariableVec2(Name, FVector2D(A[0], A[1]));
			}
			else if (T == FNiagaraTypeDefinition::GetPositionDef() && A.Num() >= 3)
			{
				N->SetVariablePosition(Name, FVector(A[0], A[1], A[2]));
			}
			else if (T == FNiagaraTypeDefinition::GetVec3Def() && A.Num() >= 3)
			{
				N->SetVariableVec3(Name, FVector(A[0], A[1], A[2]));
			}
			else if (T == FNiagaraTypeDefinition::GetVec4Def() && A.Num() >= 4)
			{
				N->SetVariableVec4(Name, FVector4(A[0], A[1], A[2], A[3]));
			}
			else if (T == FNiagaraTypeDefinition::GetColorDef() && A.Num() >= 4)
			{
				N->SetVariableLinearColor(Name, FLinearColor(A[0], A[1], A[2], A[3]));
			}
			else if (V->Type == EJson::String)
			{
				UObject* O = LoadObject<UObject>(nullptr, *V->AsString(), nullptr, LOAD_NoWarn | LOAD_Quiet);
				if (UStaticMesh* M = Cast<UStaticMesh>(O))
				{
					N->SetVariableStaticMesh(Name, M);
				}
				else if (UMaterialInterface* Mat = Cast<UMaterialInterface>(O))
				{
					N->SetVariableMaterial(Name, Mat);
				}
				else if (UTexture* Tex = Cast<UTexture>(O))
				{
					N->SetVariableTexture(Name, Tex);
				}
			}
		}
	}

	static UClass* ActorClass()
	{
		return LoadObject<UClass>(nullptr, ActorClassPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
	}

	static UObject* LoadPreset(const FString& Name, FString& OutName)
	{
		if (TSharedPtr<FJsonObject> E = Effect(Name))
		{
			OutName = Name.ToLower();
			return LoadObject<UObject>(nullptr, *E->GetStringField(TEXT("preset")), nullptr, LOAD_NoWarn | LOAD_Quiet);
		}
		for (const FPresetName& P : Presets)
		{
			if (Name.Equals(P.Name, ESearchCase::IgnoreCase) || Name.Equals(P.Asset, ESearchCase::IgnoreCase))
			{
				OutName = P.Name;
				return LoadObject<UObject>(nullptr, *FString::Printf(TEXT("%s%s.%s"), PresetFolder, P.Asset, P.Asset), nullptr, LOAD_NoWarn | LOAD_Quiet);
			}
		}
		OutName = Name;
		return Name.StartsWith(TEXT("/")) ? LoadObject<UObject>(nullptr, *Name, nullptr, LOAD_NoWarn | LOAD_Quiet) : nullptr;
	}

	static TArray<FString> PresetNames()
	{
		TArray<FString> Out;
		for (const FPresetName& P : Presets)
		{
			Out.Add(P.Name);
		}
		return Out;
	}

	static TArray<FAssetData> WaterMaterialAssets()
	{
		TArray<FAssetData> Out;
		IAssetRegistry& AR = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
		for (const TCHAR* Folder : WaterFolders)
		{
			AR.GetAssetsByPath(FName(Folder), Out, true);
		}
		return Out;
	}

	static UMaterialInterface* LoadWaterMaterial(const FString& Name)
	{
		if (Name.StartsWith(TEXT("/")))
		{
			return LoadObject<UMaterialInterface>(nullptr, *Name, nullptr, LOAD_NoWarn | LOAD_Quiet);
		}
		for (const FAssetData& A : WaterMaterialAssets())
		{
			if (A.AssetName.ToString().Equals(Name, ESearchCase::IgnoreCase))
			{
				return Cast<UMaterialInterface>(A.GetAsset());
			}
		}
		return nullptr;
	}

	/** The simulation component template the Blueprint builds its component from. */
	static UActorComponent* ComponentTemplate(UClass* Class)
	{
		for (UClass* C = Class; C; C = C->GetSuperClass())
		{
			UBlueprintGeneratedClass* BPGC = Cast<UBlueprintGeneratedClass>(C);
			if (!BPGC || !BPGC->SimpleConstructionScript)
			{
				continue;
			}
			for (USCS_Node* N : BPGC->SimpleConstructionScript->GetAllNodes())
			{
				if (N && N->ComponentTemplate && N->ComponentTemplate->GetClass()->GetName().Contains(TEXT("NinjaLiveComponent")))
				{
					return N->ComponentTemplate;
				}
			}
		}
		return nullptr;
	}

	static FProperty* Prop(const UObject* Obj, const TCHAR* Name)
	{
		for (TFieldIterator<FProperty> It(Obj->GetClass()); It; ++It)
		{
			if (It->GetAuthoredName() == Name || It->GetName() == Name)
			{
				return *It;
			}
		}
		return nullptr;
	}

	/** Copies every setting the preset has onto the component (same name and type), like the pack's own preset loading. */
	static void CopyPreset(const UObject* Preset, UObject* Target)
	{
		for (TFieldIterator<FProperty> It(Preset->GetClass()); It; ++It)
		{
			if (It->GetOwnerClass() == UObject::StaticClass() || It->GetOwnerClass() == UDataAsset::StaticClass() ||
				It->GetOwnerClass() == UPrimaryDataAsset::StaticClass())
			{
				continue;
			}
			FProperty* To = Prop(Target, *It->GetAuthoredName());
			if (To && To->SameType(*It))
			{
				To->CopyCompleteValue(To->ContainerPtrToValuePtr<void>(Target), It->ContainerPtrToValuePtr<void>(Preset));
			}
		}
	}

	static void Set(UObject* Obj, const TCHAR* Name, const FString& Text)
	{
		if (FProperty* P = Prop(Obj, Name))
		{
			P->ImportText_Direct(*Text, P->ContainerPtrToValuePtr<void>(Obj), Obj, PPF_None);
		}
	}

	static FString Get(const UObject* Obj, const TCHAR* Name)
	{
		FString Out;
		if (FProperty* P = Prop(Obj, Name))
		{
			P->ExportTextItem_Direct(Out, P->ContainerPtrToValuePtr<void>(Obj), nullptr, nullptr, PPF_None);
		}
		return Out;
	}

	/** Every property value of an object as text (to put a template back exactly). */
	static TMap<FProperty*, FString> Snapshot(const UObject* Obj)
	{
		TMap<FProperty*, FString> Out;
		for (TFieldIterator<FProperty> It(Obj->GetClass()); It; ++It)
		{
			FString V;
			It->ExportTextItem_Direct(V, It->ContainerPtrToValuePtr<void>(Obj), nullptr, nullptr, PPF_None);
			Out.Add(*It, V);
		}
		return Out;
	}

	static void Restore(UObject* Obj, const TMap<FProperty*, FString>& Snap)
	{
		for (const TPair<FProperty*, FString>& P : Snap)
		{
			P.Key->ImportText_Direct(*P.Value, P.Key->ContainerPtrToValuePtr<void>(Obj), Obj, PPF_None);
		}
	}

	static UActorComponent* SimComponent(AActor* A)
	{
		TInlineComponentArray<UActorComponent*> Comps(A);
		for (UActorComponent* C : Comps)
		{
			if (C->GetClass()->GetName().Contains(TEXT("NinjaLiveComponent")))
			{
				return C;
			}
		}
		return nullptr;
	}

	static FRuntimeFluidInfo Info(AActor* A)
	{
		FRuntimeFluidInfo Out;
		Out.Id = Ref(A);
		Out.Location = A->GetActorLocation();
		for (const FName& T : A->Tags)
		{
			const FString S = T.ToString();
			if (S.StartsWith(TEXT("RTFluidPreset:")))
			{
				Out.Preset = S.Mid(14);
			}
			if (S.StartsWith(TEXT("RTFluidSurface:")))
			{
				const FName SurfaceTag(*S.Mid(15));
				for (TActorIterator<ANiagaraActor> It(A->GetWorld()); It; ++It)
				{
					if (It->Tags.Contains(SurfaceTag) && !It->IsHidden())
					{
						Out.Surface = Ref(*It);
					}
				}
			}
		}
		if (UActorComponent* C = SimComponent(A))
		{
			FVector E;
			if (FStructProperty* P = CastField<FStructProperty>(Prop(C, TEXT("ExtentsXYZ"))))
			{
				E = *P->ContainerPtrToValuePtr<FVector>(C);
				Out.SizeMeters = FVector2D(E.X, E.Y);
			}
			Out.Resolution = FCString::Atoi(*Get(C, TEXT("ResolutionX")));
			FString Mat = Get(C, TEXT("MI_Output"));
			Out.Material = Mat;
		}
		return Out;
	}

	static TArray<AActor*> Fluids(UWorld* World)
	{
		TArray<AActor*> Out;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (It->Tags.Contains(FName(FluidTag)) && !It->IsActorBeingDestroyed() && !It->IsHidden())
			{
				Out.Add(*It);
			}
		}
		return Out;
	}

	static AActor* SpawnNode(const FString& Name, const FString& Source, TFunction<AActor*(UWorld*, FString&)> Make)
	{
		auto Spawn = [Make](FRuntimeCommandContext& Ctx, FString& Err) -> AActor* { return Make(Ctx.World, Err); };
		TUniquePtr<FCreateNodeCommand> Cmd = MakeUnique<FCreateNodeCommand>(FString::Printf(TEXT("add %s"), *Name), Spawn, Name, FString(),
			ERuntimeNodeKind::Actor, Source, TArray<FName>());
		FCreateNodeCommand* Raw = Cmd.Get();
		if (!RuntimeToolCall::RunCommand(MoveTemp(Cmd)))
		{
			return nullptr;
		}
		const FRuntimeSceneNode* Node = RuntimeToolCall::GetGraph()->Find(Raw->GetNodeId());
		return Node ? Node->Actor.Get() : nullptr;
	}
}

FRuntimeFluidList URuntimeFluidToolset::List()
{
	using namespace RtFluid;
	FRuntimeFluidList Out;
	Out.bInstalled = ActorClass() != nullptr;
	Out.Presets = PresetNames();
	for (const TPair<FString, TSharedPtr<FJsonValue>>& E : Effects()->Values)
	{
		const TSharedPtr<FJsonObject> O = E.Value->AsObject();
		Out.Effects.Add(E.Key + TEXT(": ") + (O.IsValid() ? O->GetStringField(TEXT("about")) : FString()));
	}
	for (const FKind& K : SourceKinds)
	{
		Out.Sources.Add(K.Name);
	}
	for (const FKind& K : AmbientKinds)
	{
		Out.Ambient.Add(K.Name);
	}
	if (!Out.bInstalled)
	{
		return Out;
	}
	for (const FAssetData& A : WaterMaterialAssets())
	{
		Out.WaterMaterials.Add(A.AssetName.ToString());
	}
	Out.WaterMaterials.Sort();
	for (AActor* A : Fluids(RuntimeToolCall::GetWorld()))
	{
		Out.Fluids.Add(Info(A));
	}
	return Out;
}

FRuntimeFluidInfo URuntimeFluidToolset::Add(const FString& Preset, const FVector& Location, const float& SizeMeters, const int32& Resolution,
	const FString& Material, const FString& Name, const float& Yaw, bool bFollowPlayer, const FString& Ground)
{
	using namespace RtFluid;
	UClass* Class = ActorClass();
	if (!Class)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::NotFound, TEXT("FluidNinja LIVE is not in this project"),
			TEXT("Add the FluidNinja LIVE pack to the project (Content/FluidNinjaLive)"));
		return {};
	}
	FString PresetName;
	UObject* PresetAsset = LoadPreset(Preset, PresetName);
	if (!PresetAsset)
	{
		TArray<FString> All = PresetNames();
		for (const TPair<FString, TSharedPtr<FJsonValue>>& E : Effects()->Values)
		{
			All.Add(E.Key);
		}
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("unknown fluid preset or effect '%s' (%s)"), *Preset, *JoinOrNone(All)));
		return {};
	}
	if (Passed(TEXT("size_meters")) && (SizeMeters < 2.f || SizeMeters > 1000.f))
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("size_meters %g is outside 2-1000"), SizeMeters));
		return {};
	}
	if (Passed(TEXT("resolution")) && (Resolution < 64 || Resolution > 4096))
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("resolution %d is outside 64-4096"), Resolution));
		return {};
	}
	UMaterialInterface* Mat = nullptr;
	if (Passed(TEXT("material")) && !Material.IsEmpty())
	{
		Mat = LoadWaterMaterial(Material);
		if (!Mat)
		{
			RuntimeToolCall::Fail(ERuntimeToolError::NotFound, FString::Printf(TEXT("water material '%s' not found"), *Material), TEXT("See fluid.list water_materials"));
			return {};
		}
	}
	UActorComponent* Template = ComponentTemplate(Class);
	if (!Template)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::Internal, TEXT("the NinjaLive Blueprint has no NinjaLiveComponent"));
		return {};
	}
	static int32 Counter = 0;
	const FString SurfaceTag = FString::Printf(TEXT("RTFluidSurface_%d_%d"), FPlatformTime::Cycles() & 0xffff, ++Counter);
	const FString NodeName = Name.IsEmpty() ? FString(TEXT("Fluid_")) + PresetName : Name;

	// the preset's own settings, with this simulation's surface, size, resolution and material
	const TSharedPtr<FJsonObject> FX = Effect(Preset);
	bool bNeedsLandscape = false;
	if (FX.IsValid() && FX->TryGetBoolField(TEXT("drive_landscape"), bNeedsLandscape) && bNeedsLandscape)
	{
		bool bLandscape = false;
		for (TActorIterator<AActor> It(RuntimeToolCall::GetWorld()); It && !bLandscape; ++It)
		{
			bLandscape = It->GetClass()->GetName().Contains(TEXT("Landscape"));
		}
		TArray<UMaterialInstanceDynamic*> TrailMats;
		URuntimeFluidTrailFeed::FindTerrainMaterials(RuntimeToolCall::GetWorld(), TrailMats);
		if (!bLandscape && TrailMats.Num() == 0)
		{
			RuntimeToolCall::Fail(ERuntimeToolError::WrongState, FString::Printf(TEXT("%s needs ground to paint: a Landscape or builder terrain"), *Preset),
				TEXT("Make the ground first (build.create_terrain)"));
			return {};
		}
	}
	const FString PresetTag = Get(PresetAsset, TEXT("DriveNiagaraComponentInTaggedActors"));
	const bool bSurface = !FX.IsValid() && !PresetTag.IsEmpty() && PresetTag != TEXT("None");   // water: a tiled surface to drive
	UNiagaraSystem* SurfaceSystem = bSurface ? LoadObject<UNiagaraSystem>(nullptr, SurfaceSystemPath, nullptr, LOAD_NoWarn | LOAD_Quiet) : nullptr;
	AActor* Surface = nullptr;
	FVector PresetExtents(80.f, 80.f, 1.f);
	PresetExtents.InitFromString(Get(PresetAsset, TEXT("ExtentsXYZ")));
	const float AreaMeters = Passed(TEXT("size_meters")) ? SizeMeters : static_cast<float>(FMath::Max(PresetExtents.X, PresetExtents.Y));
	if (SurfaceSystem)
	{
		TStrongObjectPtr<UNiagaraSystem> Keep(SurfaceSystem);
		TStrongObjectPtr<UStaticMesh> Tile(LoadObject<UStaticMesh>(nullptr, TileMeshPath, nullptr, LOAD_NoWarn | LOAD_Quiet));
		const int32 Tiles = FMath::Max(1, FMath::CeilToInt(AreaMeters / TileMeters));
		const FVector At = Location;
		Surface = SpawnNode(NodeName + TEXT("_Surface"), FString::Printf(TEXT("vfx:%s"), *SurfaceSystem->GetPathName()),
			[Keep, Tile, Tiles, At, SurfaceTag](UWorld* World, FString& Err) -> AActor*
			{
				FActorSpawnParameters P;
				P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
				ANiagaraActor* A = World->SpawnActor<ANiagaraActor>(ANiagaraActor::StaticClass(), FTransform(At), P);
				if (!A)
				{
					Err = TEXT("could not spawn the water surface");
					return nullptr;
				}
				A->Tags.Add(FName(*SurfaceTag));
				UNiagaraComponent* C = A->GetNiagaraComponent();
				C->SetAsset(Keep.Get());
				if (Tile.IsValid())
				{
					C->SetVariableStaticMesh(FName(TEXT("TemplateMesh")), Tile.Get());
				}
				C->SetVariableInt(FName(TEXT("MeshNumberMultX")), Tiles);
				C->SetVariableInt(FName(TEXT("MeshNumberMultY")), Tiles);
				// the system spaces tiles at extents / (tiles - 1): tiles exactly that size meet without gaps or overlaps
				// (overlapping coplanar tiles would flicker)
				const float Extent = Tiles * TileMeters;
				const float Step = Tiles > 1 ? Extent / (Tiles - 1) : Extent;
				C->SetVariableVec3(FName(TEXT("MeshScale")), FVector(Step * 1.002f, Step * 1.002f, 1.f));
				C->SetVariableVec3(FName(TEXT("EmitterExtents")), FVector(Extent, Extent, 100.f));
				C->SetVariableFloat(FName(TEXT("MeshRotationRandom")), 0.f);
				C->Activate(true);
				return A;
			});
		if (!Surface)
		{
			return {};
		}
	}
	const FString GroupTag = TEXT("RTFluidGroup:") + SurfaceTag;
	TMap<FString, FString> OutputTagProps;       // original output tag -> simulation property naming it
	TMap<FString, FString> TagRemap;             // original output tag -> this instance's tag
	FRotator SimRot(0.f, Passed(TEXT("yaw")) ? Yaw : 0.f, 0.f);
	float Ratio = 1.f;
	FVector Lifted = Location;
	// trails (sand / snow / coast sand) over builder terrain paint the terrain itself
	bool bTerrainTrails = false;
	if (Preset.Contains(TEXT("trails")) || Preset == TEXT("coast_sand"))
	{
		TArray<UMaterialInstanceDynamic*> TrailMats;
		URuntimeFluidTrailFeed::FindTerrainMaterials(RuntimeToolCall::GetWorld(), TrailMats);
		bTerrainTrails = TrailMats.Num() > 0;
	}
	double SeaShift = 0.0;   // coast effects: how far this sea sits above the demo's
	TArray<FName> SelfTags;                         // extra tags on the simulation actor
	TArray<TPair<FString, FTransform>> OwnSystems;  // Niagara systems the simulation hosts (fog volumes, blizzard ...)
	TArray<TSharedPtr<FJsonObject>> OwnParams;     // their user parameters
	if (FX.IsValid())
	{
		const FVector R = Vec(FX, TEXT("sim_rot"));
		SimRot = FRotator(R.X, R.Y + (Passed(TEXT("yaw")) ? Yaw : 0.f), R.Z);
		const FVector Ext = Vec(FX, TEXT("extents"), FVector(1.f));
		Ratio = Passed(TEXT("size_meters")) ? SizeMeters / FMath::Max(1e-3, FMath::Max(FMath::Abs(Ext.X), FMath::Abs(Ext.Y))) : 1.f;
		const TSharedPtr<FJsonObject>* Tags = nullptr;
		if (FX->TryGetObjectField(TEXT("output_tags"), Tags))
		{
			int32 i = 0;
			for (const TPair<FString, TSharedPtr<FJsonValue>>& T : (*Tags)->Values)
			{
				OutputTagProps.Add(T.Key, T.Value->AsString());
				TagRemap.Add(T.Key, FString::Printf(TEXT("%s_o%d"), *SurfaceTag, i++));
			}
		}
		const TArray<TSharedPtr<FJsonValue>>* Companions = nullptr;
		FX->TryGetArrayField(TEXT("companions"), Companions);
		// sources below the simulation (smoke columns) stand on the ground point given: lift the whole effect
		double Lowest = 0.0;
		for (const TSharedPtr<FJsonValue>& V : Companions ? *Companions : TArray<TSharedPtr<FJsonValue>>())
		{
			const TSharedPtr<FJsonObject> Comp = V->AsObject();
			if (Comp->HasField(TEXT("system")))
			{
				Lowest = FMath::Min(Lowest, FTransform(SimRot).TransformVector(Vec(Comp, TEXT("loc")) * Ratio).Z);
			}
		}
		bool bExact = false;   // placed exactly as in its demo level (coast water / sand)
		FX->TryGetBoolField(TEXT("exact"), bExact);
		if (bExact || (FMath::IsNearlyZero(SimRot.Pitch) && FMath::IsNearlyZero(SimRot.Roll)))
		{
			Lowest = 0.0;   // only upright effects (smoke columns) stand on their sources; flat ones lie at the ground point
		}
		Lifted.Z -= Lowest;
		double SeaLevel = 0.0, RefZ = 0.0;
		if (FX->TryGetNumberField(TEXT("sea_level"), SeaLevel) && FX->TryGetNumberField(TEXT("ref_z"), RefZ))
		{
			// the pack's coast presets only make breaking waves at their own sea level (absolute heights inside them): the water
			// always sits there, and scenes put their sea level at that z
			Lifted.Z = RefZ;
			SelfTags.Add(FName(*FString::Printf(TEXT("RTFluidSea:%g"), SeaLevel)));   // the water surface, for fluid.float
		}
		if (!bExact && FMath::IsNearlyZero(SimRot.Pitch) && FMath::IsNearlyZero(SimRot.Roll))
		{
			Lifted.Z += 5.0;   // flat effects (mud, blood, lava, trails) lie just above the ground point
		}
		const FTransform SimT(SimRot, Lifted);
		bool bPlaced = false;   // output plane
		TSet<FString> UsedTags;  // output tags some part carries
		auto Place = [&NodeName](const FString& Label, const FString& System, const FString& Mesh, const TArray<FString>& Mats, const FTransform& World,
						 const TArray<FName>& NewTags, const FVector2D& PlaneCm, TSharedPtr<FJsonObject> Params = nullptr) -> AActor*
		{
			return SpawnNode(NodeName + TEXT("_") + Label.Replace(TEXT(" "), TEXT("_")),
				System.IsEmpty() ? TEXT("class:/Script/Engine.StaticMeshActor") : TEXT("vfx:") + System,
				[System, Mesh, Mats, World, NewTags, PlaneCm, Params](UWorld* W, FString& Err) -> AActor*
				{
					FActorSpawnParameters P;
					P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
					AActor* A = nullptr;
					if (!System.IsEmpty())
					{
						UNiagaraSystem* Sys = LoadObject<UNiagaraSystem>(nullptr, *System, nullptr, LOAD_NoWarn | LOAD_Quiet);
						ANiagaraActor* N = Sys ? W->SpawnActor<ANiagaraActor>(ANiagaraActor::StaticClass(), World, P) : nullptr;
						if (N)
						{
							N->GetNiagaraComponent()->SetAsset(Sys);
							ApplyNiagaraParams(N->GetNiagaraComponent(), Params);
							N->GetNiagaraComponent()->Activate(true);
						}
						A = N;
					}
					else
					{
						UStaticMesh* SM = LoadObject<UStaticMesh>(nullptr, *Mesh, nullptr, LOAD_NoWarn | LOAD_Quiet);
						AStaticMeshActor* S = SM ? W->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), World, P) : nullptr;
						if (S)
						{
							UStaticMeshComponent* C = S->GetStaticMeshComponent();
							C->SetMobility(EComponentMobility::Movable);
							C->SetStaticMesh(SM);
							const FVector B = SM->GetBoundingBox().GetSize();
							if (PlaneCm.X > 0.0 && B.X > 1e-3 && B.Y > 1e-3)
							{
								S->SetActorScale3D(FVector(PlaneCm.X / B.X, PlaneCm.Y / B.Y, 1.0));
							}
							C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
							for (int32 i = 0; i < Mats.Num(); ++i)
							{
								C->SetMaterial(i, LoadObject<UMaterialInterface>(nullptr, *Mats[i], nullptr, LOAD_NoWarn | LOAD_Quiet));
							}
						}
						A = S;
					}
					if (!A)
					{
						Err = TEXT("could not spawn an effect part");
						return nullptr;
					}
					A->Tags.Append(NewTags);
					return A;
				});
		};
		for (const TSharedPtr<FJsonValue>& V : Companions ? *Companions : TArray<TSharedPtr<FJsonValue>>())
		{
			const TSharedPtr<FJsonObject> Comp = V->AsObject();
			FString System, Mesh;
			Comp->TryGetStringField(TEXT("system"), System);
			Comp->TryGetStringField(TEXT("mesh"), Mesh);
			// output surfaces made of the pack's plane meshes come along; other linked meshes were demo scenery
			if (System.IsEmpty() && !Mesh.Contains(TEXT("/Meshes/Planes/")))
			{
				continue;
			}
			// the demo levels spread several huge output planes around; one plane covering the simulation does the same job
			FVector2D PlaneCm(0.0, 0.0);
			if (System.IsEmpty())
			{
				if (bPlaced)
				{
					continue;
				}
				bPlaced = true;
				PlaneCm = FVector2D(FMath::Abs(Ext.X), FMath::Abs(Ext.Y)) * 100.0 * Ratio;
			}
			const FVector L = System.IsEmpty() ? FVector(0.0, 0.0, 2.0) : Vec(Comp, TEXT("loc")) * Ratio;
			const FVector RR = System.IsEmpty() ? FVector::ZeroVector : Vec(Comp, TEXT("rot"));
			const FVector Sc = System.IsEmpty() ? FVector(1.f) : Vec(Comp, TEXT("scale"), FVector(1.f));
			const FTransform World = System.IsEmpty() ? FTransform(FRotator(0.f, SimRot.Yaw, 0.f), Lifted + L)
													 : FTransform(FRotator(RR.X, RR.Y, RR.Z), L, Sc) * SimT;
			TArray<FName> NewTags = {FName(*GroupTag)};
			const TArray<TSharedPtr<FJsonValue>>* CT = nullptr;
			if (Comp->TryGetArrayField(TEXT("tags"), CT))
			{
				for (const TSharedPtr<FJsonValue>& T : *CT)
				{
					if (const FString* N = TagRemap.Find(T->AsString()))
					{
						NewTags.Add(FName(**N));
						UsedTags.Add(T->AsString());
					}
				}
			}
			TArray<FString> Mats;
			const TArray<TSharedPtr<FJsonValue>>* MA = nullptr;
			if (Comp->TryGetArrayField(TEXT("materials"), MA))
			{
				for (const TSharedPtr<FJsonValue>& M : *MA)
				{
					Mats.Add(M->AsString());
				}
			}
			const TSharedPtr<FJsonObject>* CP = nullptr;
			AActor* Part = Place(Comp->GetStringField(TEXT("label")), System, Mesh, Mats, World, NewTags, PlaneCm,
				ShiftSea(Comp->TryGetObjectField(TEXT("params"), CP) ? *CP : nullptr, SeaShift));
			if (!Part)
			{
				return {};
			}
		}
		// outputs nothing in the effect carries: material outputs get a plane over the simulation (trails need a surface to
		// mark), a Niagara drive tag goes on the simulation itself, which hosts those systems
		for (const TPair<FString, FString>& O : OutputTagProps)
		{
			if (UsedTags.Contains(O.Key))
			{
				continue;
			}
			if (O.Value.StartsWith(TEXT("Apply")) && O.Value.EndsWith(TEXT("ToActorsWithTag")) && !bPlaced && bTerrainTrails)
			{
				bPlaced = true;   // the terrain shows the trails (URuntimeFluidTrailFeed): no surface plane
				continue;
			}
			if (O.Value.StartsWith(TEXT("Apply")) && O.Value.EndsWith(TEXT("ToActorsWithTag")) && !bPlaced)
			{
				bPlaced = true;
				if (!Place(TEXT("Surface"), FString(), TileMeshPath, {}, FTransform(FRotator(0.f, SimRot.Yaw, 0.f), Lifted + FVector(0.0, 0.0, 2.0)),
						{FName(*GroupTag), FName(*TagRemap[O.Key])}, FVector2D(FMath::Abs(Ext.X), FMath::Abs(Ext.Y)) * 100.0 * Ratio))
				{
					return {};
				}
			}
			else if (O.Value == TEXT("DriveNiagaraComponentInTaggedActors"))
			{
				SelfTags.Add(FName(*TagRemap[O.Key]));
			}
		}
		const TArray<TSharedPtr<FJsonValue>>* Own = nullptr;
		if (FX->TryGetArrayField(TEXT("own"), Own))
		{
			for (const TSharedPtr<FJsonValue>& V : *Own)
			{
				const TSharedPtr<FJsonObject> O = V->AsObject();
				const FVector RR = Vec(O, TEXT("rot"));
				OwnSystems.Add(TPair<FString, FTransform>(O->GetStringField(TEXT("system")),
					FTransform(FRotator(RR.X, RR.Y, RR.Z), Vec(O, TEXT("loc")) * Ratio, Vec(O, TEXT("scale"), FVector(1.f)))));
				const TSharedPtr<FJsonObject>* OP = nullptr;
				OwnParams.Add(ShiftSea(O->TryGetObjectField(TEXT("params"), OP) ? *OP : nullptr, SeaShift));
			}
		}
	}
	// ground height for shore waves and downhill flow: a Landscape is read directly; anything else (generated terrain,
	// meshes) through a top-down depth camera that travels with the simulation
	bool bHasLandscape = false;
	for (TActorIterator<AActor> It(RuntimeToolCall::GetWorld()); It && !bHasLandscape; ++It)
	{
		bHasLandscape = It->GetClass()->GetName().Contains(TEXT("Landscape"));
	}
	const FString GroundMode = Ground.IsEmpty() ? TEXT("auto") : Ground.ToLower();
	if (GroundMode != TEXT("auto") && GroundMode != TEXT("landscape") && GroundMode != TEXT("capture") && GroundMode != TEXT("flat"))
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("ground '%s' is not auto, landscape, capture or flat"), *Ground));
		return {};
	}
	TStrongObjectPtr<UTextureRenderTarget2D> HeightRT;
	if (GroundMode == TEXT("capture") || (GroundMode == TEXT("auto") && !bHasLandscape))
	{
		HeightRT.Reset(NewObject<UTextureRenderTarget2D>(RuntimeToolCall::GetWorld()));
		HeightRT->RenderTargetFormat = RTF_R32f;
		HeightRT->ClearColor = FLinearColor(1e7f, 1e7f, 1e7f, 1.f);
		HeightRT->InitAutoFormat(512, 512);
		HeightRT->UpdateResourceImmediate(true);
	}
	TStrongObjectPtr<UObject> KeepPreset(PresetAsset);
	TStrongObjectPtr<UMaterialInterface> KeepMat(Mat);
	const bool bSize = Passed(TEXT("size_meters"));
	const bool bRes = Passed(TEXT("resolution"));
	const FVector At = Lifted;
	const float Size = SizeMeters;
	const int32 Res = Resolution;
	AActor* Sim = SpawnNode(NodeName, TEXT("class:") + FString(ActorClassPath),
		[Class, KeepPreset, KeepMat, bSize, bRes, At, Size, Res, SurfaceTag, bSurface, PresetName, GroupTag, OutputTagProps, TagRemap, SimRot,
			SelfTags, OwnSystems, OwnParams, HeightRT, SeaShift, bEffect = FX.IsValid()](UWorld* World, FString& Err) -> AActor*
		{
			UActorComponent* T = ComponentTemplate(Class);
			if (!T)
			{
				Err = TEXT("the NinjaLive Blueprint has no NinjaLiveComponent");
				return nullptr;
			}
			const TMap<FProperty*, FString> Saved = Snapshot(T);
			CopyPreset(KeepPreset.Get(), T);
			Set(T, TEXT("Preset"), TEXT("None"));
			Set(T, TEXT("ForceAutoLoadPreset"), TEXT("False"));
			Set(T, TEXT("ExecuteCommandsAtStart"), TEXT("()"));     // the pack's presets change t.MaxFPS; the game sets its own
			Set(T, TEXT("ExecuteCommandsAtEnd"), TEXT("()"));
			if (bSurface)
			{
				Set(T, TEXT("DriveNiagaraComponentInTaggedActors"), SurfaceTag);
			}
			for (const TPair<FString, FString>& O : OutputTagProps)   // this instance's own output surfaces
			{
				Set(T, *O.Value, TagRemap[O.Key]);
			}
			if (SeaShift != 0.0)
			{
				Set(T, TEXT("ClampingValue"), FString::SanitizeFloat(FCString::Atod(*Get(T, TEXT("ClampingValue"))) + SeaShift));
			}
			const bool bGroundCapture = HeightRT.IsValid() && Get(T, TEXT("EnableHeightField")) == TEXT("True");
			if (bGroundCapture)
			{
				Set(T, TEXT("UseExternalHeightData"), TEXT("True"));
				if (FObjectProperty* RP = CastField<FObjectProperty>(Prop(T, TEXT("ExternalHeightDataFromRenderTarget"))))
				{
					RP->SetObjectPropertyValue_InContainer(T, HeightRT.Get());
				}
			}
			// volumes (smoke, fog, mist) are shaded from a light direction: the level's sun (a hidden one still points the way)
			// or an anchor angled like an afternoon sun
			AActor* Light = nullptr;
			for (TActorIterator<ADirectionalLight> It(World); It && !Light; ++It)
			{
				Light = *It;
			}
			if (!Light)
			{
				FActorSpawnParameters LP;
				LP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
				Light = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform(FRotator(-40.f, 30.f, 0.f), At), LP);
				if (Light)
				{
					USceneComponent* Root = NewObject<USceneComponent>(Light, TEXT("Root"));
					Light->SetRootComponent(Root);
					Root->RegisterComponent();
					Light->SetActorRotation(FRotator(-40.f, 30.f, 0.f));
					Light->Tags.Add(FName(*GroupTag));
				}
			}
			if (Light)
			{
				Set(T, TEXT("LightDirectionProvider"), Light->GetPathName());
			}
			if (bSize && bEffect)
			{
				FVector E(1.f);
				E.InitFromString(Get(T, TEXT("ExtentsXYZ")));
				const float K = Size / FMath::Max(1e-3f, static_cast<float>(FMath::Max(FMath::Abs(E.X), FMath::Abs(E.Y))));
				Set(T, TEXT("ExtentsXYZ"), FString::Printf(TEXT("(X=%g,Y=%g,Z=%g)"), E.X * K, E.Y * K, E.Z * K));
			}
			else if (bSize)
			{
				const FString Z = Get(T, TEXT("ExtentsXYZ"));
				Set(T, TEXT("ExtentsXYZ"), FString::Printf(TEXT("(X=%g,Y=%g,Z=%s)"), Size, Size,
					*FString(Z.Contains(TEXT("Z=")) ? Z.Mid(Z.Find(TEXT("Z=")) + 2).LeftChop(1) : FString(TEXT("1")))));
				Set(T, TEXT("InteractionVolumeSize"), FString::Printf(TEXT("(X=%g,Y=%g,Z=0.5)"), Size, Size));
			}
			if (bRes)
			{
				Set(T, TEXT("ResolutionX"), FString::FromInt(Res));
				Set(T, TEXT("ResolutionY"), FString::FromInt(Res));
			}
			if (KeepMat.IsValid() && !bEffect)
			{
				// the chosen water on whichever output feeds the surface
				const FString Path = FString::Printf(TEXT("(\"%s\")"), *KeepMat->GetPathName());
				Set(T, TEXT("SecondaryOutputMaterials"), Path);
				Set(T, TEXT("SecondaryOutputMaterialSelected"), TEXT("0"));
				Set(T, TEXT("PrimaryOutputMaterials"), Path);
				Set(T, TEXT("PrimaryOutputMaterialSelected"), TEXT("0"));
			}
			FActorSpawnParameters P;
			P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AActor* A = World->SpawnActorDeferred<AActor>(Class, FTransform(SimRot, At), nullptr, nullptr,
				ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
			TArray<UNiagaraComponent*> Hosted;
			if (A)
			{
				A->Tags.Append(SelfTags);
				for (int32 i = 0; i < OwnSystems.Num(); ++i)
				{
					const TPair<FString, FTransform>& O = OwnSystems[i];
					if (UNiagaraSystem* Sys = LoadObject<UNiagaraSystem>(nullptr, *O.Key, nullptr, LOAD_NoWarn | LOAD_Quiet))
					{
						UNiagaraComponent* N = NewObject<UNiagaraComponent>(A);
						N->SetAsset(Sys);
						ApplyNiagaraParams(N, OwnParams.IsValidIndex(i) ? OwnParams[i] : nullptr);
						N->SetRelativeTransform(O.Value);
						A->AddInstanceComponent(N);
						Hosted.Add(N);
					}
				}
				A->FinishSpawning(FTransform(SimRot, At));
				for (UNiagaraComponent* N : Hosted)
				{
					if (!N->GetAttachParent() && A->GetRootComponent())
					{
						N->AttachToComponent(A->GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
					}
					if (!N->IsRegistered())
					{
						N->RegisterComponent();
					}
					N->Activate(true);
				}
				if (bGroundCapture && A->GetRootComponent())
				{
					FVector Ext(1.0);
					Ext.InitFromString(Get(SimComponent(A), TEXT("ExtentsXYZ")));
					URuntimeFluidHeightCapture* Cap = NewObject<URuntimeFluidHeightCapture>(A, TEXT("GroundHeightCapture"));
					Cap->Sim = SimComponent(A);
					Cap->TextureTarget = HeightRT.Get();
					Cap->OrthoWidth = FMath::Max(Ext.X, Ext.Y) * 100.f;
					Cap->SetupAttachment(A->GetRootComponent());
					Cap->SetRelativeLocation(FVector(0.0, 0.0, 5000.0));
					// world-aligned like FluidNinja's SceneCaptureCameraUtility (pitch -90, yaw -90), whatever the simulation's own turn
					Cap->SetUsingAbsoluteRotation(true);
					Cap->SetWorldRotation(FRotator(-90.f, -90.f, 0.f));
					A->AddInstanceComponent(Cap);
					Cap->RegisterComponent();
					Cap->Refresh();
				}
				// hand the simulation's output to the volumes it hosts (in the demo levels these were set on the level's copies)
				UActorComponent* SC = SimComponent(A);
				auto OutMat = [SC](const TCHAR* Choice) -> UMaterialInterface*
				{
					const bool bSecondary = Get(SC, Choice).Contains(TEXT("Secondary"));
					FObjectProperty* MP = CastField<FObjectProperty>(Prop(SC, bSecondary ? TEXT("MI_SecondaryOutput") : TEXT("MI_Output")));
					return MP ? Cast<UMaterialInterface>(MP->GetObjectPropertyValue_InContainer(SC)) : nullptr;
				};
				bool bFogVolumes = false;
				for (UNiagaraComponent* N : Hosted)
				{
					if (!SC)
					{
						break;
					}
					if (Get(SC, TEXT("ExposeOutputMaterialToSurfaceAlignedNiagaraFVOL")) == TEXT("True"))
					{
						N->SetVariableBool(TEXT("EnableFVolumeRenderer"), true);
						N->SetVariableMaterial(TEXT("FVOLMaterial"), OutMat(TEXT("FVOLMaterial")));
						bFogVolumes = true;
					}
					if (Get(SC, TEXT("ExposeOutputMaterialToSurfaceAlignedNiagaraHVOL")) == TEXT("True"))
					{
						N->SetVariableBool(TEXT("EnableHVolumeRenderer"), true);
						N->SetVariableMaterial(TEXT("HVOLMaterial"), OutMat(TEXT("HVOLMaterial")));
					}
				}
				// fog volumes render through volumetric fog: switch it on in the level's height fog, or add a thin one
				if (bFogVolumes)
				{
					bool bHasFog = false;
					for (TObjectIterator<UExponentialHeightFogComponent> It; It; ++It)
					{
						if (It->GetWorld() == World && It->IsRegistered() && It->IsVisible())
						{
							It->SetVolumetricFog(true);
							bHasFog = true;
						}
					}
					if (!bHasFog)
					{
						FActorSpawnParameters FP;
						FP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
						if (AExponentialHeightFog* Fog = World->SpawnActor<AExponentialHeightFog>(AExponentialHeightFog::StaticClass(), FTransform(At), FP))
						{
							Fog->GetComponent()->SetFogDensity(0.002f);
							Fog->GetComponent()->SetVolumetricFog(true);
							Fog->Tags.Add(FName(*GroupTag));
						}
					}
				}
			}
			Restore(T, Saved);
			if (!A)
			{
				Err = TEXT("could not spawn the fluid simulation");
				return nullptr;
			}
			A->Tags.Add(FName(FluidTag));
			A->Tags.Add(FName(*GroupTag));
			A->Tags.Add(FName(*(TEXT("RTFluidPreset:") + PresetName)));
			// the interaction volume keeps its 5 m default: cover the whole simulation so anything crossing it disturbs the fluid
			if (UActorComponent* C = SimComponent(A))
			{
				FVector E(0.0);
				if (FStructProperty* EP = CastField<FStructProperty>(Prop(C, TEXT("ExtentsXYZ"))))
				{
					E = *EP->ContainerPtrToValuePtr<FVector>(C);
				}
				for (UBoxComponent* Box : TInlineComponentArray<UBoxComponent*>(A))
				{
					if (Box->GetName() == TEXT("InteractionVolume") && E.X > 0.0)
					{
						Box->SetBoxExtent(FVector(E.X * 50.0, E.Y * 50.0, FMath::Max(E.Z * 50.0, 150.0)), true);
					}
				}
			}
			if (bSurface)
			{
				A->Tags.Add(FName(*(TEXT("RTFluidSurface:") + SurfaceTag)));
			}
			return A;
		});
	if (!Sim)
	{
		return {};
	}
	// the pack's landscape driver (wet sand, footprints) reads this simulation: link any driver in the level to it and restart it
	bool bDriveLandscape = false;
	if (FX.IsValid() && FX->TryGetBoolField(TEXT("drive_landscape"), bDriveLandscape) && bDriveLandscape)
	{
		for (TActorIterator<AActor> It(Sim->GetWorld()); It; ++It)
		{
			if (!It->GetClass()->GetName().StartsWith(TEXT("DriveExternalSystemsWithSimData")))
			{
				continue;
			}
			if (FObjectProperty* P = CastField<FObjectProperty>(Prop(*It, TEXT("GetNinjaActorDirectly"))))
			{
				P->SetObjectPropertyValue_InContainer(*It, Sim);
			}
			if (UFunction* F = It->FindFunction(TEXT("RePlay")); F && F->NumParms == 0)
			{
				It->ProcessEvent(F, nullptr);
			}
		}
	}
	if (bTerrainTrails)
	{
		FVector Ext(1.0);
		Ext.InitFromString(Get(SimComponent(Sim), TEXT("ExtentsXYZ")));
		URuntimeFluidTrailFeed* Feed = NewObject<URuntimeFluidTrailFeed>(Sim, TEXT("TrailFeed"));
		Feed->Sim = SimComponent(Sim);
		Feed->Size = FVector2D(Ext.X, Ext.Y) * 100.0;
		Sim->AddInstanceComponent(Feed);
		Feed->RegisterComponent();
	}
	if (bFollowPlayer)
	{
		APlayerController* PC = Sim->GetWorld()->GetFirstPlayerController();
		APawn* Pawn = PC ? PC->GetPawn() : nullptr;
		if (!Pawn)
		{
			RuntimeToolCall::Fail(ERuntimeToolError::WrongState, TEXT("follow_player needs a player character"),
				TEXT("Create it first (character.third_person or character.create), then add the fluid"));
			return {};
		}
		Sim->AttachToActor(Pawn, FAttachmentTransformRules::KeepWorldTransform);
		Sim->Tags.AddUnique(TEXT("RTFluidFollowsPlayer"));
	}
	return Info(Sim);
}

FRuntimeFluidList URuntimeFluidToolset::Remove(const FString& Id)
{
	using namespace RtFluid;
	AActor* A = nullptr;
	for (AActor* F : Fluids(RuntimeToolCall::GetWorld()))
	{
		if (Ref(F) == Id || F->GetName() == Id)
		{
			A = F;
		}
	}
	if (!A)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::NotFound, FString::Printf(TEXT("'%s' is not a fluid simulation"), *Id), TEXT("See fluid.list"));
		return {};
	}
	TArray<AActor*> Gone = {A};
	for (const FName& T : A->Tags)
	{
		const FString S = T.ToString();
		if (S.StartsWith(TEXT("RTFluidGroup:")))   // the effect's sources, sparks and output surfaces
		{
			for (TActorIterator<AActor> It(A->GetWorld()); It; ++It)
			{
				if (*It != A && It->Tags.Contains(T) && !It->IsHidden())
				{
					Gone.AddUnique(*It);
				}
			}
		}
		if (S.StartsWith(TEXT("RTFluidSurface:")))
		{
			for (TActorIterator<ANiagaraActor> It(A->GetWorld()); It; ++It)
			{
				if (It->Tags.Contains(FName(*S.Mid(15))))
				{
					Gone.Add(*It);
				}
			}
		}
	}
	if (!RuntimeToolCall::RunCommand(MakeUnique<FSoftDeleteActorsCommand>(TEXT("remove fluid"), Gone)))
	{
		return {};
	}
	return List();
}

namespace RtFluid
{
	static FRuntimeFluidPart PlaceSystem(const FKind* Kinds, int32 Count, const FString& Kind, const FTransform& T, const TCHAR* What)
	{
		if (!ActorClass())
		{
			RuntimeToolCall::Fail(ERuntimeToolError::NotFound, TEXT("FluidNinja LIVE is not in this project"),
				TEXT("Add the FluidNinja LIVE pack to the project (Content/FluidNinjaLive)"));
			return {};
		}
		const FKind* Found = nullptr;
		TArray<FString> Names;
		for (int32 i = 0; i < Count; ++i)
		{
			Names.Add(Kinds[i].Name);
			if (Kind.Equals(Kinds[i].Name, ESearchCase::IgnoreCase))
			{
				Found = &Kinds[i];
			}
		}
		if (!Found)
		{
			RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("unknown %s '%s' (%s)"), What, *Kind, *JoinOrNone(Names)));
			return {};
		}
		const FString Path = SystemPath(Found->System);
		UNiagaraSystem* Sys = LoadObject<UNiagaraSystem>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
		if (!Sys)
		{
			RuntimeToolCall::Fail(ERuntimeToolError::NotFound, FString::Printf(TEXT("%s system %s not found"), What, *Path));
			return {};
		}
		TStrongObjectPtr<UNiagaraSystem> Keep(Sys);
		AActor* A = SpawnNode(FString(TEXT("Fluid_")) + Found->Name, TEXT("vfx:") + Path, [Keep, T](UWorld* W, FString& Err) -> AActor*
		{
			FActorSpawnParameters P;
			P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			ANiagaraActor* N = W->SpawnActor<ANiagaraActor>(ANiagaraActor::StaticClass(), T, P);
			if (!N)
			{
				Err = TEXT("could not spawn the effect");
				return nullptr;
			}
			N->GetNiagaraComponent()->SetAsset(Keep.Get());
			N->GetNiagaraComponent()->Activate(true);
			return N;
		});
		FRuntimeFluidPart Out;
		if (A)
		{
			Out.Id = Ref(A);
			Out.Kind = Found->Name;
			Out.System = Path;
		}
		return Out;
	}
}

FRuntimeFluidPart URuntimeFluidToolset::AddSource(const FString& Kind, const FVector& Location, const FRotator& Rotation)
{
	using namespace RtFluid;
	return PlaceSystem(SourceKinds, UE_ARRAY_COUNT(SourceKinds), Kind, FTransform(Rotation, Location), TEXT("flow source"));
}

FRuntimeFluidPart URuntimeFluidToolset::Ambient(const FString& Kind, const FVector& Location, const float& Scale)
{
	using namespace RtFluid;
	const float S = Passed(TEXT("scale")) ? FMath::Clamp(Scale, 0.05f, 50.f) : 1.f;
	return PlaceSystem(AmbientKinds, UE_ARRAY_COUNT(AmbientKinds), Kind, FTransform(FRotator::ZeroRotator, Location, FVector(S)), TEXT("ambient effect"));
}

namespace RtFluid
{
	static const TCHAR* const MainSettings[] = {TEXT("SimSpeed"), TEXT("BrushStrength"), TEXT("GlobalBrushSize"), TEXT("SkeletalMeshBrushSize"),
		TEXT("PrimitiveObjBrushSize"), TEXT("VehicleBrushSize"), TEXT("BrushHardness"), TEXT("DensityAccumulation"),
		TEXT("PersistencyOfFieldAndPointData"), TEXT("VelocityFeedback"), TEXT("VelocityClamp"), TEXT("PressureFeedback"),
		TEXT("VeloFromBrushMotion"), TEXT("DensityNoiseAmount"), TEXT("BrushDensityNoise")};

	static AActor* FindFluid(const FString& Id)
	{
		for (AActor* F : Fluids(RuntimeToolCall::GetWorld()))
		{
			if (Ref(F) == Id || F->GetName() == Id)
			{
				return F;
			}
		}
		RuntimeToolCall::Fail(ERuntimeToolError::NotFound, FString::Printf(TEXT("no fluid simulation '%s'"), *Id), TEXT("Use fluid.list for ids"));
		return nullptr;
	}

	/** The simulation's live output materials (what its surface / volumes are drawn with). */
	static TArray<UMaterialInstanceDynamic*> OutputMaterials(UActorComponent* C)
	{
		TArray<UMaterialInstanceDynamic*> Out;
		for (const TCHAR* Name : {TEXT("MI_Output"), TEXT("MI_SecondaryOutput")})
		{
			if (FObjectProperty* P = CastField<FObjectProperty>(Prop(C, Name)))
			{
				if (UMaterialInstanceDynamic* M = Cast<UMaterialInstanceDynamic>(P->GetObjectPropertyValue_InContainer(C)))
				{
					Out.AddUnique(M);
				}
			}
		}
		return Out;
	}

	static bool HasParam(const TArray<UMaterialInstanceDynamic*>& Mats, const FString& Name, bool bVector)
	{
		for (UMaterialInstanceDynamic* M : Mats)
		{
			TArray<FMaterialParameterInfo> Infos;
			TArray<FGuid> Ids;
			bVector ? M->GetAllVectorParameterInfo(Infos, Ids) : M->GetAllScalarParameterInfo(Infos, Ids);
			if (Infos.ContainsByPredicate([&Name](const FMaterialParameterInfo& I) { return I.Name.ToString() == Name; }))
			{
				return true;
			}
		}
		return false;
	}

	static FRuntimeFluidSettings Settings(AActor* A)
	{
		FRuntimeFluidSettings Out;
		Out.Id = Ref(A);
		UActorComponent* C = SimComponent(A);
		if (!C)
		{
			return Out;
		}
		for (const TCHAR* N : MainSettings)
		{
			if (Prop(C, N))
			{
				Out.Simulation.Add(N, Get(C, N));
			}
		}
		for (UMaterialInstanceDynamic* M : OutputMaterials(C))
		{
			Out.SurfaceMaterials.Add(M->Parent ? M->Parent->GetPathName() : M->GetPathName());
			TArray<FMaterialParameterInfo> Infos;
			TArray<FGuid> Ids;
			M->GetAllScalarParameterInfo(Infos, Ids);
			for (const FMaterialParameterInfo& I : Infos)
			{
				float V = 0.f;
				M->GetScalarParameterValue(I, V);
				Out.Surface.Add(I.Name.ToString(), FString::SanitizeFloat(V));
			}
			Infos.Reset();
			Ids.Reset();
			M->GetAllVectorParameterInfo(Infos, Ids);
			for (const FMaterialParameterInfo& I : Infos)
			{
				FLinearColor V;
				M->GetVectorParameterValue(I, V);
				Out.Surface.Add(I.Name.ToString(), V.ToString());
			}
		}
		return Out;
	}
}

FRuntimeFluidSettings URuntimeFluidToolset::GetSettings(const FString& Id)
{
	AActor* A = RtFluid::FindFluid(Id);
	return A ? RtFluid::Settings(A) : FRuntimeFluidSettings();
}

FRuntimeFluidSettings URuntimeFluidToolset::SetSettings(const FString& Id, const TMap<FString, float>& Simulation, const TMap<FString, float>& Surface,
	const TMap<FString, FString>& Colors)
{
	AActor* A = RtFluid::FindFluid(Id);
	UActorComponent* C = A ? RtFluid::SimComponent(A) : nullptr;
	if (!C)
	{
		return {};
	}
	for (const TPair<FString, float>& S : Simulation)
	{
		FProperty* P = RtFluid::Prop(C, *S.Key);
		if (!P || !(P->IsA<FNumericProperty>() || P->IsA<FBoolProperty>()))
		{
			RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("'%s' is not a numeric or on/off simulation setting"), *S.Key),
				TEXT("Use fluid.get_settings for names"));
			return {};
		}
		const FString Text = P->IsA<FBoolProperty>() ? FString(S.Value != 0.f ? TEXT("True") : TEXT("False"))
			: P->IsA<FIntProperty>() ? FString::FromInt(FMath::RoundToInt(S.Value)) : FString::SanitizeFloat(S.Value);
		RtFluid::Set(C, *S.Key, Text);
	}
	const TArray<UMaterialInstanceDynamic*> Mats = RtFluid::OutputMaterials(C);
	for (const TPair<FString, float>& S : Surface)
	{
		if (!RtFluid::HasParam(Mats, S.Key, false))
		{
			RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("the surface has no number '%s'"), *S.Key),
				TEXT("Use fluid.get_settings for names"));
			return {};
		}
		for (UMaterialInstanceDynamic* M : Mats)
		{
			M->SetScalarParameterValue(FName(*S.Key), S.Value);
		}
	}
	for (const TPair<FString, FString>& S : Colors)
	{
		if (!RtFluid::HasParam(Mats, S.Key, true))
		{
			RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("the surface has no colour '%s'"), *S.Key),
				TEXT("Use fluid.get_settings for names"));
			return {};
		}
		const FLinearColor Col = FLinearColor(FColor::FromHex(S.Value));
		for (UMaterialInstanceDynamic* M : Mats)
		{
			M->SetVectorParameterValue(FName(*S.Key), Col);
		}
	}
	return RtFluid::Settings(A);
}

FRuntimeFluidFloatResult URuntimeFluidToolset::Float(const TArray<AActor*>& Targets, const float& WaterLevel, float Buoyancy, float Drag)
{
	using namespace RtFluid;
	FRuntimeFluidFloatResult Out;
	if (Targets.Num() == 0)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, TEXT("targets is empty"), TEXT("Pass the objects to float, e.g. [\"n12\"]"));
		return Out;
	}
	float Level = WaterLevel;
	if (!Passed(TEXT("water_level")))
	{
		// the nearest water simulation's surface
		double Best = TNumericLimits<double>::Max();
		bool bFound = false;
		for (AActor* F : Fluids(RuntimeToolCall::GetWorld()))
		{
			const double D = FVector::DistSquared2D(F->GetActorLocation(), Targets[0] ? Targets[0]->GetActorLocation() : FVector::ZeroVector);
			if (D < Best)
			{
				Best = D;
				Level = F->GetActorLocation().Z;
				for (const FName& T : F->Tags)   // coast water knows its surface
				{
					FString Z;
					if (T.ToString().Split(TEXT("RTFluidSea:"), nullptr, &Z))
					{
						Level = FCString::Atof(*Z);
					}
				}
				bFound = true;
			}
		}
		if (!bFound)
		{
			RuntimeToolCall::Fail(ERuntimeToolError::NotFound, TEXT("no water simulation to float on"),
				TEXT("Add water first (fluid.add) or pass water_level"));
			return Out;
		}
	}
	for (AActor* A : Targets)
	{
		UPrimitiveComponent* P = A ? Cast<UPrimitiveComponent>(A->GetRootComponent()) : nullptr;
		if (!P || P->GetCollisionEnabled() == ECollisionEnabled::NoCollision)
		{
			RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("%s has no collision, so it cannot float"), A ? *Ref(A) : TEXT("target")),
				TEXT("Give it collision first (e.g. build.place_mesh collision:true)"));
			return Out;
		}
		P->SetMobility(EComponentMobility::Movable);
		P->SetSimulatePhysics(true);
		URuntimeFluidFloat* C = A->FindComponentByClass<URuntimeFluidFloat>();
		if (!C)
		{
			C = NewObject<URuntimeFluidFloat>(A, TEXT("Float"));
			A->AddInstanceComponent(C);
			C->RegisterComponent();
		}
		C->WaterLevel = Level;
		C->Buoyancy = FMath::Clamp(Buoyancy, 0.f, 20.f);
		C->Drag = FMath::Clamp(Drag, 0.f, 20.f);
		A->Tags.AddUnique(TEXT("RTFloating"));
		Out.Floating.Add(Ref(A));
	}
	Out.WaterLevel = Level;
	return Out;
}
