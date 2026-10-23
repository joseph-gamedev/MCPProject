#include "RuntimeLightingToolset.h"
#include "HAL/IConsoleManager.h"
#include "PresentationToolCall.h"
#include "RuntimeSkyToolset.h"
#include "RuntimeCommand.h"
#include "RuntimeCommonCommands.h"
#include "RuntimeJson.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/VolumetricCloudComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/SkyLight.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/StrongObjectPtr.h"

namespace RtLight
{
	using namespace PresentationTool;

	template <typename T>
	T* FirstActor(UWorld* World)
	{
		for (TActorIterator<T> It(World); It; ++It)
		{
			if (!It->IsHidden())
			{
				return *It;
			}
		}
		return nullptr;
	}

	/** First component of a class on a visible actor (works for project subclasses and blueprint skies). */
	template <typename T>
	T* FirstComponent(UWorld* World)
	{
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (!It->IsHidden())
			{
				if (T* C = It->FindComponentByClass<T>())
				{
					return C;
				}
			}
		}
		return nullptr;
	}

	APostProcessVolume* FindUnboundVolume(UWorld* World)
	{
		// Volumes are always hidden in game, so visibility is not a filter. Highest priority wins; the style volume is the style toolset's.
		APostProcessVolume* Best = nullptr;
		for (TActorIterator<APostProcessVolume> It(World); It; ++It)
		{
			if (It->bUnbound && It->bEnabled && !It->Tags.Contains(FName(TEXT("RuntimeToolsetsStyle"))) && (!Best || It->Priority > Best->Priority))
			{
				Best = *It;
			}
		}
		return Best;
	}

	/** Sun elevation for a 24h clock: 6 = sunrise (0), 12 = noon (90), 18 = sunset, night below the horizon. */
	double SunElevation(double Hour)
	{
		return 90.0 * FMath::Sin(PI * (Hour - 6.0) / 12.0);
	}

	/** Camera exposure (EV100) that renders a scene lit by this much sun correctly (Sunny 16: 100000 lux ~ EV 15). */
	float EvForLux(float Lux)
	{
		return FMath::Clamp(FMath::Log2(FMath::Max(Lux, 0.01f) / 2.5f), 1.f, 16.f);
	}

	/**
	 * Auto exposure min/max brightness are EV100 only with the project setting "Extend default luminance range";
	 * otherwise they are scene luminance in cd/m2 (EV100 = log2(L * 100 / 12.5)). Tools always speak EV100.
	 */
	bool ExposureInEv()
	{
		static IConsoleVariable* V = IConsoleManager::Get().FindConsoleVariable(TEXT("r.DefaultFeature.AutoExposure.ExtendDefaultLuminanceRange"));
		return !V || V->GetInt() != 0;
	}

	float EvToSetting(float Ev)
	{
		return ExposureInEv() ? Ev : 0.125f * FMath::Pow(2.f, Ev);
	}

	float SettingToEv(float Value)
	{
		return ExposureInEv() ? Value : FMath::Log2(FMath::Max(Value, 1e-6f) / 0.125f);
	}

	/** Locks the exposure at one EV100 (auto exposure with min = max), so frames never pump or blow out. */
	void LockExposure(FPostProcessSettings& S, float Ev)
	{
		S.bOverride_AutoExposureMethod = true;
		S.AutoExposureMethod = AEM_Histogram;
		S.bOverride_AutoExposureMinBrightness = true;
		S.AutoExposureMinBrightness = EvToSetting(Ev);
		S.bOverride_AutoExposureMaxBrightness = true;
		S.AutoExposureMaxBrightness = EvToSetting(Ev);
	}

	FLinearColor Rgb(const FLinearColor& C)
	{
		return FLinearColor(C.R, C.G, C.B, 1.f);
	}

	// ---- snapshots: everything a tool may change, so undo restores exactly ----

	struct FSunState
	{
		FRotator Rotation;
		float Intensity = 0.f;
		FLinearColor Color;
		float Temperature = 6500.f;
		bool bUseTemperature = false;
		float SourceAngle = 0.5f;
		float SoftSourceAngle = 0.f;
		bool bCastCloudShadows = false;
		float CloudShadowStrength = 1.f;
		float ContactShadowLength = 0.f;

		static FSunState Read(const ADirectionalLight* Sun)
		{
			const UDirectionalLightComponent* C = CastChecked<UDirectionalLightComponent>(Sun->GetLightComponent());
			FSunState S;
			S.Rotation = Sun->GetActorRotation();
			S.Intensity = C->Intensity;
			S.Color = C->GetLightColor();
			S.Temperature = C->Temperature;
			S.bUseTemperature = C->bUseTemperature;
			S.SourceAngle = C->LightSourceAngle;
			S.SoftSourceAngle = C->LightSourceSoftAngle;
			S.bCastCloudShadows = C->bCastCloudShadows;
			S.CloudShadowStrength = C->CloudShadowOnSurfaceStrength;
			S.ContactShadowLength = C->ContactShadowLength;
			return S;
		}

		void Apply(ADirectionalLight* Sun) const
		{
			UDirectionalLightComponent* C = CastChecked<UDirectionalLightComponent>(Sun->GetLightComponent());
			if (USceneComponent* Root = Sun->GetRootComponent(); Root && Root->Mobility != EComponentMobility::Movable)
			{
				Root->SetMobility(EComponentMobility::Movable);
			}
			Sun->SetActorRotation(Rotation);
			C->SetIntensity(Intensity);
			C->SetLightColor(Color);
			C->SetTemperature(Temperature);
			C->SetUseTemperature(bUseTemperature);
			C->SetLightSourceAngle(SourceAngle);
			C->SetLightSourceSoftAngle(SoftSourceAngle);
			if (C->ContactShadowLength != ContactShadowLength)
			{
				C->ContactShadowLength = ContactShadowLength;
				C->MarkRenderStateDirty();
			}
			if (C->bCastCloudShadows != bCastCloudShadows || C->CloudShadowOnSurfaceStrength != CloudShadowStrength)
			{
				C->bCastCloudShadows = bCastCloudShadows;
				C->CloudShadowOnSurfaceStrength = CloudShadowStrength;
				C->CloudShadowStrength = CloudShadowStrength;
				C->MarkRenderStateDirty();
			}
		}
	};

	struct FSkyState
	{
		float Intensity = 1.f;
		FLinearColor Color;

		static FSkyState Read(const ASkyLight* Sky)
		{
			const USkyLightComponent* C = Sky->GetLightComponent();
			return {C->Intensity, C->GetLightColor()};
		}

		void Apply(ASkyLight* Sky) const
		{
			USkyLightComponent* C = Sky->GetLightComponent();
			C->SetIntensity(Intensity);
			C->SetLightColor(Color);
			if (!C->bRealTimeCapture)
			{
				C->RecaptureSky();
			}
		}
	};

	struct FFogState
	{
		float Density = 0.f;
		float HeightFalloff = 0.f;
		FLinearColor Color;
		float StartDistance = 0.f;
		bool bVolumetric = false;

		static FFogState Read(const AExponentialHeightFog* Fog)
		{
			const UExponentialHeightFogComponent* C = Fog->GetComponent();
			FFogState S;
			S.Density = C->FogDensity;
			S.HeightFalloff = C->FogHeightFalloff;
			S.Color = C->FogInscatteringLuminance;
			S.StartDistance = C->StartDistance;
			S.bVolumetric = C->bEnableVolumetricFog;
			return S;
		}

		void Apply(AExponentialHeightFog* Fog) const
		{
			UExponentialHeightFogComponent* C = Fog->GetComponent();
			C->SetFogDensity(Density);
			C->SetFogHeightFalloff(HeightFalloff);
			C->SetFogInscatteringColor(Color);
			C->SetStartDistance(StartDistance);
			C->SetVolumetricFog(bVolumetric);
		}
	};

	struct FAtmosphereState
	{
		FLinearColor Rayleigh;
		float RayleighScale = 0.f;
		float MieScale = 0.f;
		float MieAnisotropy = 0.f;
		float MultiScattering = 1.f;

		static FAtmosphereState Read(const USkyAtmosphereComponent* C)
		{
			FAtmosphereState S;
			S.Rayleigh = C->RayleighScattering;
			S.RayleighScale = C->RayleighScatteringScale;
			S.MieScale = C->MieScatteringScale;
			S.MieAnisotropy = C->MieAnisotropy;
			S.MultiScattering = C->MultiScatteringFactor;
			return S;
		}

		void Apply(USkyAtmosphereComponent* C) const
		{
			C->SetRayleighScattering(Rayleigh);
			C->SetRayleighScatteringScale(RayleighScale);
			C->SetMieScatteringScale(MieScale);
			C->SetMieAnisotropy(MieAnisotropy);
			C->SetMultiScatteringFactor(MultiScattering);
		}
	};

	/** Coverage / density parameter names of a cloud material (first scalar whose name contains the word). */
	FString FindCloudParam(const UMaterialInterface* M, const TCHAR* Word)
	{
		if (!M)
		{
			return FString();
		}
		TArray<FMaterialParameterInfo> Infos;
		TArray<FGuid> Ids;
		M->GetAllScalarParameterInfo(Infos, Ids);
		for (const FMaterialParameterInfo& I : Infos)
		{
			if (I.Name.ToString().Contains(Word))
			{
				return I.Name.ToString();
			}
		}
		return FString();
	}

	TArray<FString> ScalarNames(const UMaterialInterface* M)
	{
		TArray<FString> Out;
		if (M)
		{
			TArray<FMaterialParameterInfo> Infos;
			TArray<FGuid> Ids;
			M->GetAllScalarParameterInfo(Infos, Ids);
			for (const FMaterialParameterInfo& I : Infos)
			{
				Out.AddUnique(I.Name.ToString());
			}
		}
		Out.Sort();
		return Out;
	}

	UMaterialInterface* CloudMaterial(const UVolumetricCloudComponent* C)
	{
		UMaterialInterface* M = C->GetMaterial();
		return M ? M : C->Material.LoadSynchronous();
	}

	struct FCloudState
	{
		float BottomAltitude = 0.f;
		float LayerHeight = 0.f;
		float SkyLightOcclusion = 0.f;
		TStrongObjectPtr<UMaterialInterface> Material;
		TOptional<float> Coverage;
		TOptional<float> Density;
		FString CoverageParam;
		FString DensityParam;

		static FCloudState Read(const UVolumetricCloudComponent* C)
		{
			FCloudState S;
			S.BottomAltitude = C->LayerBottomAltitude;
			S.LayerHeight = C->LayerHeight;
			S.SkyLightOcclusion = C->SkyLightCloudBottomOcclusion;
			UMaterialInterface* M = CloudMaterial(C);
			S.Material.Reset(M);
			S.CoverageParam = FindCloudParam(M, TEXT("Coverage"));
			S.DensityParam = FindCloudParam(M, TEXT("Density"));
			float V = 0.f;
			if (!S.CoverageParam.IsEmpty() && M->GetScalarParameterValue(FHashedMaterialParameterInfo(FName(*S.CoverageParam)), V))
			{
				S.Coverage = V;
			}
			if (!S.DensityParam.IsEmpty() && M->GetScalarParameterValue(FHashedMaterialParameterInfo(FName(*S.DensityParam)), V))
			{
				S.Density = V;
			}
			return S;
		}

		/** Applies the layer settings; material values go to a dynamic instance (NewMid) when given. */
		void Apply(UVolumetricCloudComponent* C, UMaterialInstanceDynamic* Mid) const
		{
			C->SetLayerBottomAltitude(BottomAltitude);
			C->SetLayerHeight(LayerHeight);
			C->SetSkyLightCloudBottomOcclusion(SkyLightOcclusion);
			UMaterialInterface* Target = Mid ? static_cast<UMaterialInterface*>(Mid) : Material.Get();
			if (CloudMaterial(C) != Target)
			{
				C->SetMaterial(Target);
			}
			if (Mid)
			{
				if (Coverage.IsSet() && !CoverageParam.IsEmpty())
				{
					Mid->SetScalarParameterValue(FName(*CoverageParam), Coverage.GetValue());
				}
				if (Density.IsSet() && !DensityParam.IsEmpty())
				{
					Mid->SetScalarParameterValue(FName(*DensityParam), Density.GetValue());
				}
			}
		}
	};

	/** Generic "snapshot before, apply after" command for one object. */
	template <typename TObj, typename TState>
	TUniquePtr<IRuntimeCommand> MakeStateCommand(const FString& Label, TObj* Object, const TState& Old, const TState& New, TSharedPtr<FJsonObject> Delta = nullptr)
	{
		TWeakObjectPtr<TObj> Weak = Object;
		return MakeUnique<FLambdaCommand>(Label,
			[Weak, New](FRuntimeCommandContext&, FString& Err)
			{
				if (!Weak.IsValid())
				{
					Err = TEXT("lighting actor no longer exists");
					return false;
				}
				New.Apply(Weak.Get());
				return true;
			},
			[Weak, Old](FRuntimeCommandContext&)
			{
				if (!Weak.IsValid())
				{
					return false;
				}
				Old.Apply(Weak.Get());
				return true;
			},
			Delta);
	}

	template <typename TObj, typename TState>
	TUniquePtr<IRuntimeCommand> MakeStateCommand(const FString& Label, TObj* Object, const TState& New)
	{
		return MakeStateCommand(Label, Object, TState::Read(Object), New);
	}

	/** Cloud change: undo restores the layer and the original material; the dynamic instance is reused on redo. */
	TUniquePtr<IRuntimeCommand> MakeCloudCommand(UVolumetricCloudComponent* C, const FCloudState& New, bool bNeedsMid)
	{
		TWeakObjectPtr<UVolumetricCloudComponent> Weak = C;
		const TSharedRef<FCloudState> Old = MakeShared<FCloudState>(FCloudState::Read(C));
		const TSharedRef<FCloudState> NewState = MakeShared<FCloudState>(New);
		TSharedRef<TStrongObjectPtr<UMaterialInstanceDynamic>> Mid = MakeShared<TStrongObjectPtr<UMaterialInstanceDynamic>>();
		if (bNeedsMid)
		{
			if (UMaterialInstanceDynamic* Existing = Cast<UMaterialInstanceDynamic>(Old->Material.Get()))
			{
				Mid->Reset(Existing); // already ours (or a project instance): edit in place
			}
		}
		return MakeUnique<FLambdaCommand>(TEXT("set clouds"),
			[Weak, NewState, Mid, bNeedsMid](FRuntimeCommandContext&, FString& Err)
			{
				if (!Weak.IsValid())
				{
					Err = TEXT("volumetric cloud no longer exists");
					return false;
				}
				if (bNeedsMid && !Mid->IsValid())
				{
					Mid->Reset(UMaterialInstanceDynamic::Create(NewState->Material.Get(), Weak.Get()));
					if (!Mid->IsValid())
					{
						Err = TEXT("could not create a dynamic instance of the cloud material");
						return false;
					}
				}
				NewState->Apply(Weak.Get(), bNeedsMid ? Mid->Get() : nullptr);
				return true;
			},
			[Weak, Old, Mid, bNeedsMid](FRuntimeCommandContext&)
			{
				if (!Weak.IsValid())
				{
					return false;
				}
				// Same instance as before the change: restore its values; otherwise put the original material back.
				const bool bSameMid = bNeedsMid && Mid->Get() == Old->Material.Get();
				Old->Apply(Weak.Get(), bSameMid ? Mid->Get() : nullptr);
				return true;
			});
	}

	ADirectionalLight* ResolveSun(AActor* Target)
	{
		if (Target)
		{
			ADirectionalLight* Sun = Cast<ADirectionalLight>(Target);
			if (!Sun)
			{
				RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("%s is a %s, not a DirectionalLight"), *Ref(Target), *Target->GetClass()->GetName()));
			}
			return Sun;
		}
		ADirectionalLight* Sun = FirstActor<ADirectionalLight>(RuntimeToolCall::GetWorld());
		if (!Sun)
		{
			RuntimeToolCall::Fail(ERuntimeToolError::NotFound, TEXT("no directional light in the level"), TEXT("Spawn one: actors.spawn_actor class /Script/Engine.DirectionalLight"));
		}
		return Sun;
	}

	FRuntimeSunInfo SunInfo(const ADirectionalLight* Sun)
	{
		FRuntimeSunInfo J;
		if (!Sun)
		{
			return J;
		}
		const FSunState S = FSunState::Read(Sun);
		J.Actor = Ref(Sun);
		J.Rotation = S.Rotation;
		J.Elevation = RuntimeJson::Round(-S.Rotation.Pitch);
		J.Intensity = S.Intensity;
		J.Color = Rgb(S.Color);
		J.Temperature = S.Temperature;
		J.bUseTemperature = S.bUseTemperature;
		J.SourceAngle = S.SourceAngle;
		J.SoftSourceAngle = S.SoftSourceAngle;
		J.bCastCloudShadows = S.bCastCloudShadows;
		return J;
	}

	FRuntimeSkyLightInfo SkyInfo(const ASkyLight* Sky)
	{
		FRuntimeSkyLightInfo J;
		if (Sky)
		{
			const FSkyState S = FSkyState::Read(Sky);
			J.Actor = Ref(Sky);
			J.Intensity = S.Intensity;
			J.Color = Rgb(S.Color);
			J.bRealtimeCapture = Sky->GetLightComponent()->bRealTimeCapture;
		}
		return J;
	}

	FRuntimeFogInfo FogInfo(const AExponentialHeightFog* Fog)
	{
		FRuntimeFogInfo J;
		if (Fog)
		{
			const FFogState S = FFogState::Read(Fog);
			J.Actor = Ref(Fog);
			J.Density = S.Density;
			J.HeightFalloff = S.HeightFalloff;
			J.StartDistance = S.StartDistance;
			J.Color = Rgb(S.Color);
			J.bVolumetric = S.bVolumetric;
		}
		return J;
	}

	FRuntimeAtmosphereInfo AtmosphereInfo(const USkyAtmosphereComponent* C)
	{
		FRuntimeAtmosphereInfo J;
		if (C)
		{
			const FAtmosphereState S = FAtmosphereState::Read(C);
			J.Actor = Ref(C->GetOwner());
			J.RayleighScattering = Rgb(S.Rayleigh);
			J.RayleighScatteringScale = S.RayleighScale;
			J.MieScatteringScale = S.MieScale;
			J.MieAnisotropy = S.MieAnisotropy;
			J.MultiScatteringFactor = S.MultiScattering;
		}
		return J;
	}

	FRuntimeCloudInfo CloudInfo(const UVolumetricCloudComponent* C, const ADirectionalLight* Sun)
	{
		FRuntimeCloudInfo J;
		if (C)
		{
			const FCloudState S = FCloudState::Read(C);
			J.Actor = Ref(C->GetOwner());
			J.BottomAltitudeKm = S.BottomAltitude;
			J.LayerHeightKm = S.LayerHeight;
			J.SkyLightOcclusion = S.SkyLightOcclusion;
			J.Material = S.Material.IsValid() ? S.Material->GetPathName() : FString();
			J.CoverageParameter = S.Coverage.IsSet() ? S.CoverageParam : FString();
			J.Coverage = S.Coverage.Get(0.f);
			J.DensityParameter = S.Density.IsSet() ? S.DensityParam : FString();
			J.Density = S.Density.Get(0.f);
		}
		if (Sun)
		{
			const FSunState S = FSunState::Read(Sun);
			J.bCastShadows = S.bCastCloudShadows;
			J.ShadowStrength = S.CloudShadowStrength;
		}
		return J;
	}

	FRuntimePostProcessInfo PostInfo(const APostProcessVolume* Volume)
	{
		FRuntimePostProcessInfo J;
		if (!Volume)
		{
			return J;
		}
		const FPostProcessSettings& S = Volume->Settings;
		J.Actor = Ref(Volume);
		J.ExposureBias = S.AutoExposureBias;
		J.ExposureMin = SettingToEv(S.AutoExposureMinBrightness);
		J.ExposureMax = SettingToEv(S.AutoExposureMaxBrightness);
		J.Bloom = S.BloomIntensity;
		J.Saturation = S.ColorSaturation.X;
		J.Contrast = S.ColorContrast.X;
		J.Vignette = S.VignetteIntensity;
		J.WhiteBalance = S.WhiteTemp;
		auto Add = [&J](bool bOn, const TCHAR* Name) { if (bOn) { J.Overridden.Add(Name); } };
		Add(S.bOverride_AutoExposureBias, TEXT("exposure_bias"));
		Add(S.bOverride_AutoExposureMinBrightness, TEXT("exposure_min"));
		Add(S.bOverride_AutoExposureMaxBrightness, TEXT("exposure_max"));
		Add(S.bOverride_BloomIntensity, TEXT("bloom"));
		Add(S.bOverride_ColorSaturation, TEXT("saturation"));
		Add(S.bOverride_ColorContrast, TEXT("contrast"));
		Add(S.bOverride_VignetteIntensity, TEXT("vignette"));
		Add(S.bOverride_WhiteTemp, TEXT("white_balance"));
		if (S.bOverride_AutoExposureMethod && S.AutoExposureMethod == AEM_Manual)
		{
			J.ExposureMode = ERuntimeExposureMode::Manual;
		}
		else if (S.bOverride_AutoExposureMinBrightness && S.bOverride_AutoExposureMaxBrightness)
		{
			J.ExposureMode = ERuntimeExposureMode::Auto;
		}
		else if (J.Overridden.Num() > 0 || S.bOverride_AutoExposureMethod)
		{
			J.ExposureMode = ERuntimeExposureMode::Manual;
		}
		else
		{
			J.ExposureMode = ERuntimeExposureMode::ProjectDefault;
		}
		return J;
	}

	/** Sets an unbound post-process volume's settings; spawns one (as a scene node) when the level has none. */
	void AddPostProcess(UWorld* World, TFunctionRef<void(FPostProcessSettings&)> Edit, FCompositeCommand& Out, bool& bOutCreated)
	{
		APostProcessVolume* Volume = FindUnboundVolume(World);
		TSharedRef<TWeakObjectPtr<APostProcessVolume>> Target = MakeShared<TWeakObjectPtr<APostProcessVolume>>(Volume);
		FPostProcessSettings New = Volume ? Volume->Settings : FPostProcessSettings();
		Edit(New);
		bOutCreated = Volume == nullptr;
		if (!Volume)
		{
			auto Spawn = [Target](FRuntimeCommandContext& Ctx, FString& Err) -> AActor*
			{
				FActorSpawnParameters P;
				P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
				APostProcessVolume* V = Ctx.World->SpawnActor<APostProcessVolume>(APostProcessVolume::StaticClass(), FTransform::Identity, P);
				if (V)
				{
					V->bUnbound = true;
					V->Priority = 10.f;
				}
				*Target = V;
				return V;
			};
			Out.Add(MakeUnique<FCreateNodeCommand>(TEXT("post process volume"), Spawn, TEXT("PostProcess"), FString(), ERuntimeNodeKind::Actor,
				TEXT("class:/Script/Engine.PostProcessVolume"), TArray<FName>()));
		}
		const FPostProcessSettings Old = Volume ? Volume->Settings : FPostProcessSettings();
		Out.Add(MakeUnique<FLambdaCommand>(TEXT("post process"),
			[Target, New](FRuntimeCommandContext&, FString& Err)
			{
				if (!Target->IsValid())
				{
					Err = TEXT("post process volume missing");
					return false;
				}
				(*Target)->Settings = New;
				return true;
			},
			[Target, Old](FRuntimeCommandContext&)
			{
				if (Target->IsValid())
				{
					(*Target)->Settings = Old;
				}
				return true;
			}));
	}

	bool InRange(const TCHAR* Name, float Value, float Min, float Max)
	{
		if (Passed(Name) && (Value < Min || Value > Max))
		{
			RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("%s %g is outside %g..%g"), Name, Value, Min, Max));
			return false;
		}
		return true;
	}

	/** Adds the sky light recapture (non-realtime sky lights must recapture to see a new sky). */
	void AddSkyRecapture(UWorld* World, FCompositeCommand& Cmd)
	{
		if (ASkyLight* Sky = FirstActor<ASkyLight>(World))
		{
			Cmd.Add(MakeStateCommand(TEXT("sky recapture"), Sky, FSkyState::Read(Sky)));
		}
	}
}

namespace RtLight
{
	/** True (and the call fails with a pointer to the sky tools) when Ultra Dynamic Sky drives this part of the lighting. */
	static bool SkyOwns(const TCHAR* What, const TCHAR* Instead)
	{
		if (RuntimeSky::ActiveSky(RuntimeToolCall::GetWorld()))
		{
			RuntimeToolCall::Fail(ERuntimeToolError::WrongState, FString::Printf(TEXT("Ultra Dynamic Sky controls the %s in this level"), What),
				FString::Printf(TEXT("Use %s (or sky.remove to go back to the level's own lighting)"), Instead));
			return true;
		}
		return false;
	}
}

FRuntimeEnvironment URuntimeLightingToolset::GetEnvironment()
{
	using namespace RtLight;
	UWorld* World = RuntimeToolCall::GetWorld();
	FRuntimeEnvironment Out;
	ADirectionalLight* Sun = FirstActor<ADirectionalLight>(World);
	Out.Sun = SunInfo(Sun);
	Out.SkyLight = SkyInfo(FirstActor<ASkyLight>(World));
	Out.Atmosphere = AtmosphereInfo(FirstComponent<USkyAtmosphereComponent>(World));
	Out.Clouds = CloudInfo(FirstComponent<UVolumetricCloudComponent>(World), Sun);
	Out.Fog = FogInfo(FirstActor<AExponentialHeightFog>(World));
	Out.PostProcess = PostInfo(FindUnboundVolume(World));
	return Out;
}

FRuntimeSunInfo URuntimeLightingToolset::SetSun(const FRotator& Rotation, const float& Intensity, const FLinearColor& Color, const float& Temperature,
	const float& SourceAngle, const float& SoftSourceAngle, AActor* Target)
{
	using namespace RtLight;
	if (SkyOwns(TEXT("sun"), TEXT("sky.set_time (sun position) and sky.set_sky (brightness)")))
	{
		return {};
	}
	if (!InRange(TEXT("intensity"), Intensity, 0.f, 1000000.f) || !InRange(TEXT("temperature"), Temperature, 1700.f, 12000.f)
		|| !InRange(TEXT("source_angle"), SourceAngle, 0.f, 90.f) || !InRange(TEXT("soft_source_angle"), SoftSourceAngle, 0.f, 90.f))
	{
		return {};
	}
	ADirectionalLight* Sun = ResolveSun(Target);
	if (!Sun)
	{
		return {};
	}
	FSunState New = FSunState::Read(Sun);
	if (Passed(TEXT("rotation"))) { New.Rotation = Rotation; }
	if (Passed(TEXT("intensity"))) { New.Intensity = Intensity; }
	if (Passed(TEXT("color"))) { New.Color = Rgb(Color); }
	if (Passed(TEXT("temperature"))) { New.Temperature = Temperature; New.bUseTemperature = true; }
	if (Passed(TEXT("source_angle"))) { New.SourceAngle = SourceAngle; }
	if (Passed(TEXT("soft_source_angle"))) { New.SoftSourceAngle = SoftSourceAngle; }
	TUniquePtr<FCompositeCommand> Cmd = MakeUnique<FCompositeCommand>(TEXT("set sun"));
	Cmd->Add(MakeStateCommand(TEXT("sun"), Sun, New));
	if (Passed(TEXT("intensity")))
	{
		// Keep the camera matched to the light: a physical sun with a fixed low exposure renders pure white.
		bool bCreated = false;
		const float Ev = EvForLux(Intensity);
		AddPostProcess(Sun->GetWorld(), [Ev](FPostProcessSettings& S) { LockExposure(S, Ev); }, *Cmd, bCreated);
	}
	if (!RuntimeToolCall::RunCommand(MoveTemp(Cmd)))
	{
		return {};
	}
	return SunInfo(Sun);
}

FRuntimeSkyLightInfo URuntimeLightingToolset::SetSky(const float& Intensity, const FLinearColor& Color)
{
	using namespace RtLight;
	if (SkyOwns(TEXT("sky light"), TEXT("sky.set_sky brightness")))
	{
		return {};
	}
	if (!InRange(TEXT("intensity"), Intensity, 0.f, 1000.f))
	{
		return {};
	}
	ASkyLight* Sky = FirstActor<ASkyLight>(RuntimeToolCall::GetWorld());
	if (!Sky)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::NotFound, TEXT("no sky light in the level"), TEXT("Spawn one: actors.spawn_actor class /Script/Engine.SkyLight"));
		return {};
	}
	FSkyState New = FSkyState::Read(Sky);
	if (Passed(TEXT("intensity"))) { New.Intensity = Intensity; }
	if (Passed(TEXT("color"))) { New.Color = Rgb(Color); }
	if (!RuntimeToolCall::RunCommand(MakeStateCommand(TEXT("set sky"), Sky, New)))
	{
		return {};
	}
	return SkyInfo(Sky);
}

FRuntimeFogInfo URuntimeLightingToolset::SetFog(const float& Density, const float& HeightFalloff, const float& StartDistance, const FLinearColor& Color, const bool& bVolumetric)
{
	using namespace RtLight;
	if (SkyOwns(TEXT("fog"), TEXT("sky.set_weather_values fog or sky.set_weather foggy")))
	{
		return {};
	}
	if (!InRange(TEXT("density"), Density, 0.f, 10.f) || !InRange(TEXT("height_falloff"), HeightFalloff, 0.f, 10.f)
		|| !InRange(TEXT("start_distance"), StartDistance, 0.f, 10000000.f))
	{
		return {};
	}
	AExponentialHeightFog* Fog = FirstActor<AExponentialHeightFog>(RuntimeToolCall::GetWorld());
	if (!Fog)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::NotFound, TEXT("no exponential height fog in the level"), TEXT("Spawn one: actors.spawn_actor class /Script/Engine.ExponentialHeightFog"));
		return {};
	}
	FFogState New = FFogState::Read(Fog);
	if (Passed(TEXT("density"))) { New.Density = Density; }
	if (Passed(TEXT("height_falloff"))) { New.HeightFalloff = HeightFalloff; }
	if (Passed(TEXT("start_distance"))) { New.StartDistance = StartDistance; }
	if (Passed(TEXT("volumetric"))) { New.bVolumetric = bVolumetric; }
	if (Passed(TEXT("color"))) { New.Color = Rgb(Color); }
	if (!RuntimeToolCall::RunCommand(MakeStateCommand(TEXT("set fog"), Fog, New)))
	{
		return {};
	}
	return FogInfo(Fog);
}

FRuntimeAtmosphereInfo URuntimeLightingToolset::SetAtmosphere(const FLinearColor& RayleighScattering, const float& RayleighScatteringScale, const float& MieScatteringScale,
	const float& MieAnisotropy, const float& MultiScatteringFactor)
{
	using namespace RtLight;
	if (SkyOwns(TEXT("atmosphere"), TEXT("sky.set_time and sky.set_sky")))
	{
		return {};
	}
	if (!InRange(TEXT("rayleigh_scattering_scale"), RayleighScatteringScale, 0.f, 2.f) || !InRange(TEXT("mie_scattering_scale"), MieScatteringScale, 0.f, 5.f)
		|| !InRange(TEXT("mie_anisotropy"), MieAnisotropy, 0.f, 0.999f) || !InRange(TEXT("multi_scattering_factor"), MultiScatteringFactor, 0.f, 2.f))
	{
		return {};
	}
	USkyAtmosphereComponent* C = FirstComponent<USkyAtmosphereComponent>(RuntimeToolCall::GetWorld());
	if (!C)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::NotFound, TEXT("no sky atmosphere in the level"), TEXT("Spawn one: actors.spawn_actor class /Script/Engine.SkyAtmosphere"));
		return {};
	}
	FAtmosphereState New = FAtmosphereState::Read(C);
	if (Passed(TEXT("rayleigh_scattering"))) { New.Rayleigh = Rgb(RayleighScattering); }
	if (Passed(TEXT("rayleigh_scattering_scale"))) { New.RayleighScale = RayleighScatteringScale; }
	if (Passed(TEXT("mie_scattering_scale"))) { New.MieScale = MieScatteringScale; }
	if (Passed(TEXT("mie_anisotropy"))) { New.MieAnisotropy = MieAnisotropy; }
	if (Passed(TEXT("multi_scattering_factor"))) { New.MultiScattering = MultiScatteringFactor; }
	if (!RuntimeToolCall::RunCommand(MakeStateCommand(TEXT("set atmosphere"), C, New)))
	{
		return {};
	}
	return AtmosphereInfo(C);
}

FRuntimeCloudInfo URuntimeLightingToolset::SetClouds(const float& Coverage, const float& Density, const float& BottomAltitudeKm, const float& LayerHeightKm,
	const float& SkyLightOcclusion, const bool& bCastShadows, const float& ShadowStrength)
{
	using namespace RtLight;
	if (SkyOwns(TEXT("clouds"), TEXT("sky.set_weather_values cloud_coverage or sky.set_sky sky_mode")))
	{
		return {};
	}
	if (!InRange(TEXT("coverage"), Coverage, -1.f, 1.f) || !InRange(TEXT("density"), Density, 0.f, 10.f) || !InRange(TEXT("bottom_altitude_km"), BottomAltitudeKm, 0.f, 50.f)
		|| !InRange(TEXT("layer_height_km"), LayerHeightKm, 0.1f, 50.f) || !InRange(TEXT("sky_light_occlusion"), SkyLightOcclusion, 0.f, 1.f)
		|| !InRange(TEXT("shadow_strength"), ShadowStrength, 0.f, 1.f))
	{
		return {};
	}
	UWorld* World = RuntimeToolCall::GetWorld();
	UVolumetricCloudComponent* C = FirstComponent<UVolumetricCloudComponent>(World);
	if (!C)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::NotFound, TEXT("no volumetric cloud in the level"), TEXT("Spawn one: actors.spawn_actor class /Script/Engine.VolumetricCloud"));
		return {};
	}
	FCloudState New = FCloudState::Read(C);
	const bool bMaterial = Passed(TEXT("coverage")) || Passed(TEXT("density"));
	for (const TPair<const TCHAR*, const FString*>& P : {TPair<const TCHAR*, const FString*>(TEXT("coverage"), &New.CoverageParam), TPair<const TCHAR*, const FString*>(TEXT("density"), &New.DensityParam)})
	{
		if (Passed(P.Key) && P.Value->IsEmpty())
		{
			RuntimeToolCall::Fail(ERuntimeToolError::NotFound, FString::Printf(TEXT("cloud material %s has no %s parameter (scalars: %s)"),
				New.Material.IsValid() ? *New.Material->GetName() : TEXT("(none)"), P.Key, *JoinOrNone(ScalarNames(New.Material.Get()))),
				TEXT("Assign a cloud material with coverage/density parameters, or set them with materials tools on its instance"));
			return {};
		}
	}
	if (bMaterial && !New.Material.IsValid())
	{
		RuntimeToolCall::Fail(ERuntimeToolError::WrongState, TEXT("the volumetric cloud has no material"));
		return {};
	}
	if (Passed(TEXT("coverage"))) { New.Coverage = Coverage; }
	if (Passed(TEXT("density"))) { New.Density = Density; }
	if (Passed(TEXT("bottom_altitude_km"))) { New.BottomAltitude = BottomAltitudeKm; }
	if (Passed(TEXT("layer_height_km"))) { New.LayerHeight = LayerHeightKm; }
	if (Passed(TEXT("sky_light_occlusion"))) { New.SkyLightOcclusion = SkyLightOcclusion; }
	TUniquePtr<FCompositeCommand> Cmd = MakeUnique<FCompositeCommand>(TEXT("set clouds"));
	Cmd->Add(MakeCloudCommand(C, New, bMaterial));
	ADirectionalLight* Sun = FirstActor<ADirectionalLight>(World);
	if (Passed(TEXT("cast_shadows")) || Passed(TEXT("shadow_strength")))
	{
		if (!Sun)
		{
			RuntimeToolCall::Fail(ERuntimeToolError::NotFound, TEXT("cloud shadows are a sun setting and the level has no directional light"),
				TEXT("Spawn one: actors.spawn_actor class /Script/Engine.DirectionalLight"));
			return {};
		}
		FSunState S = FSunState::Read(Sun);
		if (Passed(TEXT("cast_shadows"))) { S.bCastCloudShadows = bCastShadows; }
		if (Passed(TEXT("shadow_strength"))) { S.CloudShadowStrength = ShadowStrength; }
		Cmd->Add(MakeStateCommand(TEXT("cloud shadows"), Sun, S));
	}
	AddSkyRecapture(World, *Cmd);
	if (!RuntimeToolCall::RunCommand(MoveTemp(Cmd)))
	{
		return {};
	}
	return CloudInfo(C, Sun);
}

FRuntimePostProcessInfo URuntimeLightingToolset::SetPostProcess(const float& ExposureBias, const float& ExposureMin, const float& ExposureMax, const float& Bloom,
	const float& Saturation, const float& Contrast, const float& Vignette, const float& WhiteBalance)
{
	using namespace RtLight;
	if ((Passed(TEXT("exposure_bias")) || Passed(TEXT("exposure_min")) || Passed(TEXT("exposure_max"))) &&
		SkyOwns(TEXT("exposure"), TEXT("sky.set_sky brightness (bloom, saturation, contrast, vignette and white balance still work here)")))
	{
		return {};
	}
	if (!InRange(TEXT("exposure_bias"), ExposureBias, -15.f, 15.f) || !InRange(TEXT("exposure_min"), ExposureMin, -15.f, 25.f) || !InRange(TEXT("exposure_max"), ExposureMax, -15.f, 25.f)
		|| !InRange(TEXT("bloom"), Bloom, 0.f, 8.f) || !InRange(TEXT("saturation"), Saturation, 0.f, 2.f) || !InRange(TEXT("contrast"), Contrast, 0.f, 2.f)
		|| !InRange(TEXT("vignette"), Vignette, 0.f, 1.f) || !InRange(TEXT("white_balance"), WhiteBalance, 1500.f, 15000.f))
	{
		return {};
	}
	if (Passed(TEXT("exposure_min")) && Passed(TEXT("exposure_max")) && ExposureMin > ExposureMax)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("exposure_min %g is above exposure_max %g"), ExposureMin, ExposureMax));
		return {};
	}
	UWorld* World = RuntimeToolCall::GetWorld();
	TUniquePtr<FCompositeCommand> Cmd = MakeUnique<FCompositeCommand>(TEXT("set post process"));
	bool bCreated = false;
	AddPostProcess(World, [&](FPostProcessSettings& S)
	{
		if (Passed(TEXT("exposure_bias"))) { S.bOverride_AutoExposureBias = true; S.AutoExposureBias = ExposureBias; }
		if (Passed(TEXT("exposure_min"))) { S.bOverride_AutoExposureMinBrightness = true; S.AutoExposureMinBrightness = EvToSetting(ExposureMin); }
		if (Passed(TEXT("exposure_max"))) { S.bOverride_AutoExposureMaxBrightness = true; S.AutoExposureMaxBrightness = EvToSetting(ExposureMax); }
		if (Passed(TEXT("bloom"))) { S.bOverride_BloomIntensity = true; S.BloomIntensity = Bloom; }
		if (Passed(TEXT("saturation"))) { S.bOverride_ColorSaturation = true; S.ColorSaturation = FVector4(Saturation, Saturation, Saturation, 1.f); }
		if (Passed(TEXT("contrast"))) { S.bOverride_ColorContrast = true; S.ColorContrast = FVector4(Contrast, Contrast, Contrast, 1.f); }
		if (Passed(TEXT("vignette"))) { S.bOverride_VignetteIntensity = true; S.VignetteIntensity = Vignette; }
		if (Passed(TEXT("white_balance"))) { S.bOverride_WhiteTemp = true; S.WhiteTemp = WhiteBalance; }
	}, *Cmd, bCreated);
	if (!RuntimeToolCall::RunCommand(MoveTemp(Cmd)))
	{
		return {};
	}
	FRuntimePostProcessInfo Out = PostInfo(FindUnboundVolume(World));
	Out.bCreatedVolume = bCreated;
	return Out;
}

FRuntimeTimeOfDay URuntimeLightingToolset::SetTimeOfDay(float Hour, const float& Yaw, AActor* Target)
{
	using namespace RtLight;
	if (SkyOwns(TEXT("time of day"), TEXT("sky.set_time")))
	{
		return {};
	}
	if (Hour < 0.f || Hour > 24.f)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("hour %g is outside 0-24"), Hour));
		return {};
	}
	ADirectionalLight* Sun = ResolveSun(Target);
	if (!Sun)
	{
		return {};
	}
	FSunState New = FSunState::Read(Sun);
	New.Rotation.Pitch = -SunElevation(Hour);
	if (Passed(TEXT("yaw")))
	{
		New.Rotation.Yaw = Yaw;
	}
	TUniquePtr<FCompositeCommand> Cmd = MakeUnique<FCompositeCommand>(TEXT("time of day"));
	Cmd->Add(MakeStateCommand(TEXT("sun"), Sun, New));
	AddSkyRecapture(RuntimeToolCall::GetWorld(), *Cmd);
	if (!RuntimeToolCall::RunCommand(MoveTemp(Cmd)))
	{
		return {};
	}
	FRuntimeTimeOfDay Out;
	Out.Hour = Hour;
	Out.Sun = SunInfo(Sun);
	return Out;
}

FRuntimeMoodResult URuntimeLightingToolset::ApplyMood(ERuntimeLightingMood Mood)
{
	using namespace RtLight;
	if (SkyOwns(TEXT("lighting mood"), TEXT("sky.set_time and sky.set_weather")))
	{
		return {};
	}
	struct FMood
	{
		double Hour;
		float Temperature;
		float FogDensity;
		bool bVolumetric;
		float Saturation;
		float Contrast;
		float WhiteBalance;
		float ExposureBias;
		float SoftAngle;
		float CloudCoverage; // NaN-free sentinel: < -1 = leave clouds alone
		// Physical light and a locked camera exposure (EV100) that matches it.
		float SunLux;
		float SkyIntensity;
		float Ev;
	};
	// Values: Epic physical light units, Sunny-16 exposure (100000 lux ~ EV 15), photographic ranges per mood.
	static const TMap<ERuntimeLightingMood, FMood> Moods = {
		{ERuntimeLightingMood::Noon, {12.0, 6200.f, 0.01f, false, 1.0f, 1.0f, 6500.f, 0.f, 0.f, -2.f, 100000.f, 1.0f, 14.6f}},
		{ERuntimeLightingMood::Morning, {8.0, 5000.f, 0.02f, false, 1.02f, 1.0f, 6300.f, 0.f, 0.f, -2.f, 40000.f, 1.0f, 13.6f}},
		{ERuntimeLightingMood::Sunset, {17.4, 3400.f, 0.02f, false, 0.98f, 1.02f, 6200.f, 0.f, 0.f, -2.f, 9000.f, 1.0f, 11.8f}},
		{ERuntimeLightingMood::Night, {0.0, 4100.f, 0.02f, false, 0.8f, 1.0f, 7000.f, 0.f, 0.f, -2.f, 0.6f, 0.4f, 2.5f}},
		{ERuntimeLightingMood::Overcast, {13.0, 6800.f, 0.04f, false, 0.85f, 0.97f, 6800.f, 0.f, 55.f, 0.6f, 9000.f, 1.6f, 12.2f}},
		{ERuntimeLightingMood::Foggy, {10.0, 6000.f, 0.2f, true, 0.88f, 0.95f, 6600.f, 0.f, 30.f, -2.f, 25000.f, 1.3f, 13.0f}},
	};
	const FMood& M = Moods.FindChecked(Mood);
	UWorld* World = RuntimeToolCall::GetWorld();
	TUniquePtr<FCompositeCommand> Cmd = MakeUnique<FCompositeCommand>(TEXT("mood"));
	FRuntimeMoodResult Out;
	Out.Mood = Mood;
	if (ADirectionalLight* Sun = FirstActor<ADirectionalLight>(World))
	{
		FSunState S = FSunState::Read(Sun);
		S.Rotation.Pitch = -SunElevation(M.Hour);
		S.Temperature = M.Temperature;
		S.bUseTemperature = true;
		S.SoftSourceAngle = M.SoftAngle;
		S.Intensity = M.SunLux;
		S.ContactShadowLength = 0.03f; // grounds small objects (no floating look)
		Cmd->Add(MakeStateCommand(TEXT("sun"), Sun, S));
		Out.Changed.Add(TEXT("sun"));
	}
	if (ASkyLight* Sky = FirstActor<ASkyLight>(World))
	{
		FSkyState K = FSkyState::Read(Sky);
		K.Intensity = M.SkyIntensity;
		Cmd->Add(MakeStateCommand(TEXT("sky"), Sky, K));
		Out.Changed.Add(TEXT("sky"));
	}
	if (AExponentialHeightFog* Fog = FirstActor<AExponentialHeightFog>(World))
	{
		FFogState F = FFogState::Read(Fog);
		F.Density = M.FogDensity;
		F.bVolumetric = M.bVolumetric;
		Cmd->Add(MakeStateCommand(TEXT("fog"), Fog, F));
		Out.Changed.Add(TEXT("fog"));
	}
	if (UVolumetricCloudComponent* Clouds = FirstComponent<UVolumetricCloudComponent>(World); Clouds && M.CloudCoverage >= -1.f)
	{
		FCloudState CS = FCloudState::Read(Clouds);
		if (!CS.CoverageParam.IsEmpty() && CS.Material.IsValid())
		{
			CS.Coverage = M.CloudCoverage;
			Cmd->Add(MakeCloudCommand(Clouds, CS, true));
			Out.Changed.Add(TEXT("clouds"));
		}
	}
	AddSkyRecapture(World, *Cmd);
	bool bCreated = false;
	AddPostProcess(World, [&M](FPostProcessSettings& S)
	{
		S.bOverride_ColorSaturation = true;
		S.ColorSaturation = FVector4(M.Saturation, M.Saturation, M.Saturation, 1.f);
		S.bOverride_ColorContrast = true;
		S.ColorContrast = FVector4(M.Contrast, M.Contrast, M.Contrast, 1.f);
		S.bOverride_WhiteTemp = true;
		S.WhiteTemp = M.WhiteBalance;
		S.bOverride_AutoExposureBias = true;
		S.AutoExposureBias = M.ExposureBias;
		LockExposure(S, M.Ev);
		// Grounding and depth without baked lighting: ambient occlusion, sharp reflections, subtle lens.
		S.bOverride_AmbientOcclusionIntensity = true;
		S.AmbientOcclusionIntensity = 0.7f;
		S.bOverride_AmbientOcclusionRadius = true;
		S.AmbientOcclusionRadius = 150.f;
		S.bOverride_ScreenSpaceReflectionQuality = true;
		S.ScreenSpaceReflectionQuality = 100.f;
		S.bOverride_BloomIntensity = true;
		S.BloomIntensity = 0.5f;
		S.bOverride_VignetteIntensity = true;
		S.VignetteIntensity = 0.3f;
	}, *Cmd, bCreated);
	Out.Changed.Add(TEXT("post_process"));
	if (!RuntimeToolCall::RunCommand(MoveTemp(Cmd)))
	{
		return {};
	}
	return Out;
}
