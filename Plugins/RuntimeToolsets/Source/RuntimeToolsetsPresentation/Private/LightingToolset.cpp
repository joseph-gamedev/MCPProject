#include "LightingToolset.h"
#include "RuntimeToolHelpers.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/SkyLightComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/SkyLight.h"
#include "Engine/World.h"
#include "EngineUtils.h"

using namespace RuntimeTools;

namespace
{
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

	APostProcessVolume* FindUnboundVolume(UWorld* World)
	{
		// Volumes are always hidden in game, so visibility is not a filter. Highest priority wins.
		APostProcessVolume* Best = nullptr;
		for (TActorIterator<APostProcessVolume> It(World); It; ++It)
		{
			if (It->bUnbound && It->bEnabled && (!Best || It->Priority > Best->Priority))
			{
				Best = *It;
			}
		}
		return Best;
	}

	/** Sun elevation for a 24h clock: 6 = sunrise (0°), 12 = noon (90°), 18 = sunset, night below the horizon. */
	double SunElevation(double Hour)
	{
		return 90.0 * FMath::Sin(PI * (Hour - 6.0) / 12.0);
	}

	// ---- snapshots: everything a tool may change, so undo restores exactly ----

	struct FSunState
	{
		FRotator Rotation;
		float Intensity = 0.f;
		FLinearColor Color;
		float Temperature = 6500.f;
		bool bUseTemperature = false;

		static FSunState Read(const ADirectionalLight* Sun)
		{
			const UDirectionalLightComponent* C = CastChecked<UDirectionalLightComponent>(Sun->GetLightComponent());
			FSunState S;
			S.Rotation = Sun->GetActorRotation();
			S.Intensity = C->Intensity;
			S.Color = C->GetLightColor();
			S.Temperature = C->Temperature;
			S.bUseTemperature = C->bUseTemperature;
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

	/** Generic "snapshot before, apply after" command for one actor. */
	template <typename TActor, typename TState>
	TUniquePtr<IRuntimeCommand> MakeStateCommand(const FString& Label, TActor* Actor, const TState& New, TSharedRef<FJsonObject> Delta)
	{
		TWeakObjectPtr<TActor> Weak = Actor;
		const TState Old = TState::Read(Actor);
		return MakeUnique<FLambdaCommand>(Label,
			[Weak, New](FRuntimeCommandContext&, FString& Error)
			{
				if (!Weak.IsValid())
				{
					Error = TEXT("actor no longer exists");
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

	ADirectionalLight* ResolveSun(FToolInvocation& Inv)
	{
		if (!Inv.GetString(TEXT("target")).IsEmpty())
		{
			ADirectionalLight* Sun = Cast<ADirectionalLight>(Inv.ResolveActor(TEXT("target")));
			if (!Sun && !Inv.IsComplete())
			{
				Inv.Fail(ERuntimeToolError::InvalidArguments, TEXT("target is not a directional light"));
			}
			return Sun;
		}
		ADirectionalLight* Sun = FirstActor<ADirectionalLight>(Inv.GetWorld());
		if (!Sun)
		{
			Inv.Fail(ERuntimeToolError::NotFound, TEXT("no directional light in the level"), TEXT("Spawn one: actors.spawn_actor class=/Script/Engine.DirectionalLight"));
		}
		return Sun;
	}

	/** Builds the new sun state from args (rotation/hour/intensity/color/temperature). */
	FSunState SunFromArgs(const FToolInvocation& Inv, const FSunState& Base)
	{
		FSunState S = Base;
		FRotator R;
		if (Inv.GetRotator(TEXT("rotation"), R))
		{
			S.Rotation = R;
		}
		if (Inv.Has(TEXT("hour")))
		{
			S.Rotation.Pitch = -SunElevation(Inv.GetNumber(TEXT("hour")));
			if (Inv.Has(TEXT("yaw")))
			{
				S.Rotation.Yaw = Inv.GetNumber(TEXT("yaw"));
			}
		}
		if (Inv.Has(TEXT("intensity")))
		{
			S.Intensity = static_cast<float>(Inv.GetNumber(TEXT("intensity")));
		}
		FVector C;
		if (Inv.GetVector(TEXT("color"), C))
		{
			S.Color = FLinearColor(C.X, C.Y, C.Z);
		}
		if (Inv.Has(TEXT("temperature")))
		{
			S.Temperature = static_cast<float>(Inv.GetNumber(TEXT("temperature")));
			S.bUseTemperature = true;
		}
		return S;
	}

	TSharedRef<FJsonObject> SunJson(const FSunState& S)
	{
		TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
		J->SetField(TEXT("rotation"), RuntimeJson::Rot(S.Rotation));
		J->SetNumberField(TEXT("elevation"), RuntimeJson::Round(-S.Rotation.Pitch));
		J->SetNumberField(TEXT("intensity"), RuntimeJson::Round(S.Intensity));
		J->SetField(TEXT("color"), RuntimeJson::Vec(FVector(S.Color.R, S.Color.G, S.Color.B)));
		if (S.bUseTemperature)
		{
			J->SetNumberField(TEXT("temperature"), RuntimeJson::Round(S.Temperature));
		}
		return J;
	}

	/** Post-process fields exposed by set_post_process, applied with their override flags. */
	void ApplyPostArgs(const FToolInvocation& Inv, FPostProcessSettings& S)
	{
		if (Inv.Has(TEXT("exposure_bias"))) { S.bOverride_AutoExposureBias = true; S.AutoExposureBias = static_cast<float>(Inv.GetNumber(TEXT("exposure_bias"))); }
		if (Inv.Has(TEXT("bloom"))) { S.bOverride_BloomIntensity = true; S.BloomIntensity = static_cast<float>(Inv.GetNumber(TEXT("bloom"))); }
		if (Inv.Has(TEXT("saturation"))) { const float V = static_cast<float>(Inv.GetNumber(TEXT("saturation"))); S.bOverride_ColorSaturation = true; S.ColorSaturation = FVector4(V, V, V, 1.f); }
		if (Inv.Has(TEXT("contrast"))) { const float V = static_cast<float>(Inv.GetNumber(TEXT("contrast"))); S.bOverride_ColorContrast = true; S.ColorContrast = FVector4(V, V, V, 1.f); }
		if (Inv.Has(TEXT("vignette"))) { S.bOverride_VignetteIntensity = true; S.VignetteIntensity = static_cast<float>(Inv.GetNumber(TEXT("vignette"))); }
		if (Inv.Has(TEXT("white_balance"))) { S.bOverride_WhiteTemp = true; S.WhiteTemp = static_cast<float>(Inv.GetNumber(TEXT("white_balance"))); }
	}

	/** Sets an unbound post-process volume's settings; spawns one (as a scene node) when the level has none. */
	bool RunPostProcess(FToolInvocation& Inv, TFunctionRef<void(FPostProcessSettings&)> Edit, FCompositeCommand& Out, FString& OutVolumeRef)
	{
		APostProcessVolume* Volume = FindUnboundVolume(Inv.GetWorld());
		TSharedRef<TWeakObjectPtr<APostProcessVolume>> Target = MakeShared<TWeakObjectPtr<APostProcessVolume>>(Volume);
		FPostProcessSettings New = Volume ? Volume->Settings : FPostProcessSettings();
		Edit(New);
		if (!Volume)
		{
			auto Spawn = [Target](FRuntimeCommandContext& Ctx, FString& Error) -> AActor*
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
			OutVolumeRef = TEXT("new");
		}
		else
		{
			OutVolumeRef = Ref(Inv, Volume);
		}
		const FPostProcessSettings Old = Volume ? Volume->Settings : FPostProcessSettings();
		Out.Add(MakeUnique<FLambdaCommand>(TEXT("post process"),
			[Target, New](FRuntimeCommandContext&, FString& Error)
			{
				if (!Target->IsValid())
				{
					Error = TEXT("post process volume missing");
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
		return true;
	}
}

static void GetEnvironment(FToolInvocation& Inv)
{
	UWorld* World = Inv.GetWorld();
	TSharedRef<FJsonObject> Out = Obj();
	if (ADirectionalLight* Sun = FirstActor<ADirectionalLight>(World))
	{
		TSharedRef<FJsonObject> J = SunJson(FSunState::Read(Sun));
		J->SetStringField(TEXT("actor"), Ref(Inv, Sun));
		Out->SetObjectField(TEXT("sun"), J);
	}
	if (ASkyLight* Sky = FirstActor<ASkyLight>(World))
	{
		const FSkyState S = FSkyState::Read(Sky);
		TSharedRef<FJsonObject> J = Obj();
		J->SetStringField(TEXT("actor"), Ref(Inv, Sky));
		J->SetNumberField(TEXT("intensity"), RuntimeJson::Round(S.Intensity));
		J->SetBoolField(TEXT("realtime_capture"), Sky->GetLightComponent()->bRealTimeCapture);
		Out->SetObjectField(TEXT("sky_light"), J);
	}
	if (AExponentialHeightFog* Fog = FirstActor<AExponentialHeightFog>(World))
	{
		const FFogState S = FFogState::Read(Fog);
		TSharedRef<FJsonObject> J = Obj();
		J->SetStringField(TEXT("actor"), Ref(Inv, Fog));
		J->SetNumberField(TEXT("density"), RuntimeJson::Round(S.Density));
		J->SetNumberField(TEXT("height_falloff"), RuntimeJson::Round(S.HeightFalloff));
		J->SetNumberField(TEXT("start_distance"), RuntimeJson::Round(S.StartDistance));
		J->SetBoolField(TEXT("volumetric"), S.bVolumetric);
		Out->SetObjectField(TEXT("fog"), J);
	}
	if (APostProcessVolume* Volume = FindUnboundVolume(World))
	{
		const FPostProcessSettings& S = Volume->Settings;
		TSharedRef<FJsonObject> J = Obj();
		J->SetStringField(TEXT("actor"), Ref(Inv, Volume));
		if (S.bOverride_AutoExposureBias) J->SetNumberField(TEXT("exposure_bias"), RuntimeJson::Round(S.AutoExposureBias));
		if (S.bOverride_BloomIntensity) J->SetNumberField(TEXT("bloom"), RuntimeJson::Round(S.BloomIntensity));
		if (S.bOverride_ColorSaturation) J->SetNumberField(TEXT("saturation"), RuntimeJson::Round(S.ColorSaturation.X));
		if (S.bOverride_ColorContrast) J->SetNumberField(TEXT("contrast"), RuntimeJson::Round(S.ColorContrast.X));
		if (S.bOverride_VignetteIntensity) J->SetNumberField(TEXT("vignette"), RuntimeJson::Round(S.VignetteIntensity));
		if (S.bOverride_WhiteTemp) J->SetNumberField(TEXT("white_balance"), RuntimeJson::Round(S.WhiteTemp));
		Out->SetObjectField(TEXT("post_process"), J);
	}
	Inv.Succeed(Out);
}

static void SetSun(FToolInvocation& Inv)
{
	ADirectionalLight* Sun = ResolveSun(Inv);
	if (!Sun)
	{
		return;
	}
	const FSunState New = SunFromArgs(Inv, FSunState::Read(Sun));
	TSharedRef<FJsonObject> Delta = SunJson(New);
	Delta->SetStringField(TEXT("sun"), Ref(Inv, Sun));
	if (Inv.RunCommand(MakeStateCommand(TEXT("set sun"), Sun, New, Delta)))
	{
		Inv.Succeed(Delta);
	}
}

static void SetSky(FToolInvocation& Inv)
{
	ASkyLight* Sky = FirstActor<ASkyLight>(Inv.GetWorld());
	if (!Sky)
	{
		Inv.Fail(ERuntimeToolError::NotFound, TEXT("no sky light in the level"));
		return;
	}
	FSkyState New = FSkyState::Read(Sky);
	if (Inv.Has(TEXT("intensity")))
	{
		New.Intensity = static_cast<float>(Inv.GetNumber(TEXT("intensity")));
	}
	FVector C;
	if (Inv.GetVector(TEXT("color"), C))
	{
		New.Color = FLinearColor(C.X, C.Y, C.Z);
	}
	TSharedRef<FJsonObject> Delta = Obj();
	Delta->SetStringField(TEXT("sky_light"), Ref(Inv, Sky));
	Delta->SetNumberField(TEXT("intensity"), RuntimeJson::Round(New.Intensity));
	if (Inv.RunCommand(MakeStateCommand(TEXT("set sky"), Sky, New, Delta)))
	{
		Inv.Succeed(Delta);
	}
}

static void SetFog(FToolInvocation& Inv)
{
	AExponentialHeightFog* Fog = FirstActor<AExponentialHeightFog>(Inv.GetWorld());
	if (!Fog)
	{
		Inv.Fail(ERuntimeToolError::NotFound, TEXT("no exponential height fog in the level"), TEXT("Spawn one: actors.spawn_actor class=/Script/Engine.ExponentialHeightFog"));
		return;
	}
	FFogState New = FFogState::Read(Fog);
	if (Inv.Has(TEXT("density"))) New.Density = static_cast<float>(Inv.GetNumber(TEXT("density")));
	if (Inv.Has(TEXT("height_falloff"))) New.HeightFalloff = static_cast<float>(Inv.GetNumber(TEXT("height_falloff")));
	if (Inv.Has(TEXT("start_distance"))) New.StartDistance = static_cast<float>(Inv.GetNumber(TEXT("start_distance")));
	if (Inv.Has(TEXT("volumetric"))) New.bVolumetric = Inv.GetBool(TEXT("volumetric"));
	FVector C;
	if (Inv.GetVector(TEXT("color"), C))
	{
		New.Color = FLinearColor(C.X, C.Y, C.Z);
	}
	TSharedRef<FJsonObject> Delta = Obj();
	Delta->SetStringField(TEXT("fog"), Ref(Inv, Fog));
	Delta->SetNumberField(TEXT("density"), RuntimeJson::Round(New.Density));
	Delta->SetBoolField(TEXT("volumetric"), New.bVolumetric);
	if (Inv.RunCommand(MakeStateCommand(TEXT("set fog"), Fog, New, Delta)))
	{
		Inv.Succeed(Delta);
	}
}

static void SetPostProcess(FToolInvocation& Inv)
{
	TUniquePtr<FCompositeCommand> Cmd = MakeUnique<FCompositeCommand>(TEXT("set post process"));
	FString VolumeRef;
	RunPostProcess(Inv, [&Inv](FPostProcessSettings& S) { ApplyPostArgs(Inv, S); }, *Cmd, VolumeRef);
	if (Inv.RunCommand(MoveTemp(Cmd)))
	{
		TSharedRef<FJsonObject> Out = Obj();
		Out->SetStringField(TEXT("volume"), VolumeRef == TEXT("new") ? Ref(Inv, FindUnboundVolume(Inv.GetWorld())) : VolumeRef);
		Out->SetBoolField(TEXT("created_volume"), VolumeRef == TEXT("new"));
		Inv.Succeed(Out);
	}
}

static void SetTimeOfDay(FToolInvocation& Inv)
{
	ADirectionalLight* Sun = ResolveSun(Inv);
	if (!Sun)
	{
		return;
	}
	const FSunState New = SunFromArgs(Inv, FSunState::Read(Sun));
	TUniquePtr<FCompositeCommand> Cmd = MakeUnique<FCompositeCommand>(TEXT("time of day"));
	TSharedRef<FJsonObject> Delta = SunJson(New);
	Delta->SetNumberField(TEXT("hour"), RuntimeJson::Round(Inv.GetNumber(TEXT("hour"))));
	Cmd->Add(MakeStateCommand(TEXT("sun"), Sun, New, Delta));
	// Non-realtime sky lights need a recapture to pick up the new sky.
	if (ASkyLight* Sky = FirstActor<ASkyLight>(Inv.GetWorld()))
	{
		Cmd->Add(MakeStateCommand(TEXT("sky recapture"), Sky, FSkyState::Read(Sky), Obj()));
	}
	if (Inv.RunCommand(MoveTemp(Cmd)))
	{
		Inv.Succeed(Delta);
	}
}

static void ApplyMood(FToolInvocation& Inv)
{
	const FString Mood = Inv.GetString(TEXT("mood"));
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
	};
	static const TMap<FString, FMood> Moods = {
		{TEXT("noon"), {12.0, 6500.f, 0.01f, false, 1.0f, 1.0f, 6500.f, 0.f}},
		{TEXT("morning"), {8.0, 5000.f, 0.03f, false, 1.05f, 1.0f, 6200.f, 0.f}},
		{TEXT("sunset"), {17.6, 3200.f, 0.03f, false, 1.15f, 1.05f, 5200.f, 0.f}},
		{TEXT("night"), {0.0, 4000.f, 0.02f, false, 0.7f, 1.05f, 7500.f, -0.5f}},
		{TEXT("overcast"), {13.0, 7500.f, 0.05f, false, 0.75f, 0.95f, 7000.f, 0.f}},
		{TEXT("foggy"), {10.0, 6000.f, 0.25f, true, 0.85f, 0.95f, 6800.f, 0.f}},
	};
	const FMood* M = Moods.Find(Mood);
	if (!M)
	{
		Inv.Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("unknown mood '%s'"), *Mood));
		return;
	}
	UWorld* World = Inv.GetWorld();
	TUniquePtr<FCompositeCommand> Cmd = MakeUnique<FCompositeCommand>(FString::Printf(TEXT("mood %s"), *Mood));
	TArray<FString> Changed;
	if (ADirectionalLight* Sun = FirstActor<ADirectionalLight>(World))
	{
		FSunState S = FSunState::Read(Sun);
		S.Rotation.Pitch = -SunElevation(M->Hour);
		S.Temperature = M->Temperature;
		S.bUseTemperature = true;
		Cmd->Add(MakeStateCommand(TEXT("sun"), Sun, S, Obj()));
		Changed.Add(TEXT("sun"));
	}
	if (AExponentialHeightFog* Fog = FirstActor<AExponentialHeightFog>(World))
	{
		FFogState F = FFogState::Read(Fog);
		F.Density = M->FogDensity;
		F.bVolumetric = M->bVolumetric;
		Cmd->Add(MakeStateCommand(TEXT("fog"), Fog, F, Obj()));
		Changed.Add(TEXT("fog"));
	}
	if (ASkyLight* Sky = FirstActor<ASkyLight>(World))
	{
		Cmd->Add(MakeStateCommand(TEXT("sky recapture"), Sky, FSkyState::Read(Sky), Obj()));
	}
	FString VolumeRef;
	RunPostProcess(Inv, [M](FPostProcessSettings& S)
	{
		S.bOverride_ColorSaturation = true;
		S.ColorSaturation = FVector4(M->Saturation, M->Saturation, M->Saturation, 1.f);
		S.bOverride_ColorContrast = true;
		S.ColorContrast = FVector4(M->Contrast, M->Contrast, M->Contrast, 1.f);
		S.bOverride_WhiteTemp = true;
		S.WhiteTemp = M->WhiteBalance;
		S.bOverride_AutoExposureBias = true;
		S.AutoExposureBias = M->ExposureBias;
	}, *Cmd, VolumeRef);
	Changed.Add(TEXT("post_process"));
	if (Inv.RunCommand(MoveTemp(Cmd)))
	{
		TSharedRef<FJsonObject> Out = Obj();
		Out->SetStringField(TEXT("mood"), Mood);
		Out->SetField(TEXT("changed"), RuntimeJson::Strings(Changed));
		Inv.Succeed(Out);
	}
}

void FLightingToolset::RegisterTools(FRuntimeToolsetBuilder& B)
{
	const auto Color = []() { return FToolParam::Vec3(TEXT("[r,g,b] 0-1")); };

	B.Tool(TEXT("get_environment"), TEXT("Sun, sky light, fog and post-process settings of the level."))
		.RequiresWorld()
		.Handle(&GetEnvironment);

	B.Tool(TEXT("set_sun"), TEXT("Set the directional light's rotation, intensity, color or temperature."))
		.Args(FToolSchema()
			.Add(TEXT("target"), FToolParam::Target(TEXT("Directional light (default: first in level)")))
			.Add(TEXT("rotation"), FToolParam::Rotator(TEXT("[pitch,yaw,roll]; pitch -90 = straight down")))
			.Add(TEXT("intensity"), FToolParam::Number(TEXT("Intensity (lux)"), 0.0))
			.Add(TEXT("color"), Color())
			.Add(TEXT("temperature"), FToolParam::Number(TEXT("Kelvin"), 1700.0, 12000.0)))
		.Undoable().RequiresWorld()
		.Handle(&SetSun);

	B.Tool(TEXT("set_sky"), TEXT("Set the sky light's intensity and color (recaptures if needed)."))
		.Args(FToolSchema().Add(TEXT("intensity"), FToolParam::Number(TEXT("Intensity"), 0.0)).Add(TEXT("color"), Color()))
		.Undoable().RequiresWorld()
		.Handle(&SetSky);

	B.Tool(TEXT("set_fog"), TEXT("Set exponential height fog density, falloff, color, start distance, volumetric."))
		.Args(FToolSchema()
			.Add(TEXT("density"), FToolParam::Number(TEXT("Density"), 0.0, 10.0))
			.Add(TEXT("height_falloff"), FToolParam::Number(TEXT("Height falloff"), 0.0, 10.0))
			.Add(TEXT("start_distance"), FToolParam::Number(TEXT("Start distance cm"), 0.0))
			.Add(TEXT("color"), Color())
			.Add(TEXT("volumetric"), FToolParam::Boolean(TEXT("Volumetric fog"))))
		.Undoable().RequiresWorld()
		.Handle(&SetFog);

	B.Tool(TEXT("set_post_process"), TEXT("Set global post process (exposure, bloom, saturation, contrast, vignette, white balance)."))
		.Args(FToolSchema()
			.Add(TEXT("exposure_bias"), FToolParam::Number(TEXT("Exposure compensation (stops)"), -15.0, 15.0))
			.Add(TEXT("bloom"), FToolParam::Number(TEXT("Bloom intensity"), 0.0, 8.0))
			.Add(TEXT("saturation"), FToolParam::Number(TEXT("Saturation (1 = neutral)"), 0.0, 2.0))
			.Add(TEXT("contrast"), FToolParam::Number(TEXT("Contrast (1 = neutral)"), 0.0, 2.0))
			.Add(TEXT("vignette"), FToolParam::Number(TEXT("Vignette intensity"), 0.0, 1.0))
			.Add(TEXT("white_balance"), FToolParam::Number(TEXT("White balance Kelvin"), 1500.0, 15000.0)))
		.Undoable().RequiresWorld()
		.Handle(&SetPostProcess);

	B.Tool(TEXT("set_time_of_day"), TEXT("Place the sun for a clock hour (6 sunrise, 12 noon, 18 sunset)."))
		.Args(FToolSchema()
			.Add(TEXT("hour"), FToolParam::Number(TEXT("Hour 0-24"), 0.0, 24.0).Required())
			.Add(TEXT("yaw"), FToolParam::Number(TEXT("Sun heading degrees")))
			.Add(TEXT("target"), FToolParam::Target(TEXT("Directional light (default: first in level)"))))
		.Undoable().RequiresWorld()
		.Handle(&SetTimeOfDay);

	B.Tool(TEXT("apply_mood"), TEXT("One-step look: noon, morning, sunset, night, overcast, foggy (sun, fog, grading)."))
		.Args(FToolSchema().Add(TEXT("mood"), FToolParam::Enum(TEXT("Preset"), {TEXT("noon"), TEXT("morning"), TEXT("sunset"), TEXT("night"), TEXT("overcast"), TEXT("foggy")}).Required()))
		.Undoable().RequiresWorld()
		.Handle(&ApplyMood);
}
