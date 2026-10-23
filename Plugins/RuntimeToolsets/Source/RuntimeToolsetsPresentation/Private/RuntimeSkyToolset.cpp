#include "RuntimeSkyToolset.h"
#include "PresentationToolCall.h"
#include "RuntimeCommand.h"
#include "RuntimeCommonCommands.h"
#include "RuntimeSceneGraph.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Engine/PostProcessVolume.h"
#include "Components/LightComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/VolumetricCloudComponent.h"
#include "Engine/World.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "EngineUtils.h"
#include "Containers/Ticker.h"
#include "UObject/UnrealType.h"

using namespace PresentationTool;

// Ultra Dynamic Sky is a content pack (Blueprints): everything goes through reflection on its own property and function
// names, so the plugin builds and runs without it.
namespace RtSky
{
	static const TCHAR* SkyClassPath = TEXT("/Game/UltraDynamicSky/Blueprints/Ultra_Dynamic_Sky.Ultra_Dynamic_Sky_C");
	static const TCHAR* WeatherClassPath = TEXT("/Game/UltraDynamicSky/Blueprints/Ultra_Dynamic_Weather.Ultra_Dynamic_Weather_C");
	static const TCHAR* PresetFolder = TEXT("/Game/UltraDynamicSky/Blueprints/Weather_Effects/Weather_Presets/");

	struct FPresetName
	{
		const TCHAR* Name;
		const TCHAR* Asset;
	};
	static const FPresetName Presets[] = {{TEXT("clear"), TEXT("Clear_Skies")}, {TEXT("partly_cloudy"), TEXT("Partly_Cloudy")}, {TEXT("cloudy"), TEXT("Cloudy")},
		{TEXT("overcast"), TEXT("Overcast")}, {TEXT("foggy"), TEXT("Foggy")}, {TEXT("light_rain"), TEXT("Rain_Light")}, {TEXT("rain"), TEXT("Rain")},
		{TEXT("thunderstorm"), TEXT("Rain_Thunderstorm")}, {TEXT("light_snow"), TEXT("Snow_Light")}, {TEXT("snow"), TEXT("Snow")},
		{TEXT("blizzard"), TEXT("Snow_Blizzard")}, {TEXT("dust"), TEXT("Sand_Dust_Calm")}, {TEXT("sandstorm"), TEXT("Sand_Dust_Storm")}};
	static const TCHAR* SkyModes[] = {TEXT("volumetric"), TEXT("static"), TEXT("2d"), TEXT("none"), TEXT("aurora"), TEXT("space"), TEXT("voxel")};
	// weather value -> Ultra Dynamic Weather property (each has a "<name> - Manual Override" switch)
	static const TPair<const TCHAR*, const TCHAR*> WeatherValues[] = {{TEXT("cloud_coverage"), TEXT("Cloud Coverage")}, {TEXT("rain"), TEXT("Rain")},
		{TEXT("snow"), TEXT("Snow")}, {TEXT("thunder"), TEXT("Thunder/Lightning")}, {TEXT("wind"), TEXT("Wind Intensity")}, {TEXT("fog"), TEXT("Fog")},
		{TEXT("dust"), TEXT("Dust")}};

	static UClass* LoadClassAt(const TCHAR* Path)
	{
		return LoadObject<UClass>(nullptr, Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
	}

	static AActor* FindOf(UWorld* World, UClass* Class)
	{
		if (World && Class)
		{
			for (TActorIterator<AActor> It(World, Class); It; ++It)
			{
				if (IsValid(*It) && !It->IsActorBeingDestroyed() && !It->IsHidden())
				{
					return *It;
				}
			}
		}
		return nullptr;
	}

	/** A Blueprint variable by the name shown in the editor ("Time of Day"). */
	static FProperty* Prop(const UObject* Obj, const TCHAR* Name)
	{
		if (!Obj)
		{
			return nullptr;
		}
		for (TFieldIterator<FProperty> It(Obj->GetClass()); It; ++It)
		{
			if (It->GetAuthoredName() == Name || It->GetName() == Name)
			{
				return *It;
			}
		}
		return nullptr;
	}

	static bool SetNum(UObject* Obj, const TCHAR* Name, double V)
	{
		FProperty* P = Prop(Obj, Name);
		if (!P)
		{
			return false;
		}
		void* Ptr = P->ContainerPtrToValuePtr<void>(Obj);
		if (FNumericProperty* N = CastField<FNumericProperty>(P))
		{
			N->IsFloatingPoint() ? N->SetFloatingPointPropertyValue(Ptr, V) : N->SetIntPropertyValue(Ptr, static_cast<int64>(FMath::RoundToInt64(V)));
			return true;
		}
		if (FBoolProperty* B = CastField<FBoolProperty>(P))
		{
			B->SetPropertyValue(Ptr, V != 0.0);
			return true;
		}
		if (FEnumProperty* E = CastField<FEnumProperty>(P))
		{
			E->GetUnderlyingProperty()->SetIntPropertyValue(Ptr, static_cast<int64>(V));
			return true;
		}
		return false;
	}

	static double GetNum(const UObject* Obj, const TCHAR* Name, double Default = 0.0)
	{
		FProperty* P = Prop(Obj, Name);
		if (!P)
		{
			return Default;
		}
		const void* Ptr = P->ContainerPtrToValuePtr<void>(Obj);
		if (FNumericProperty* N = CastField<FNumericProperty>(P))
		{
			return N->IsFloatingPoint() ? N->GetFloatingPointPropertyValue(Ptr) : static_cast<double>(N->GetSignedIntPropertyValue(Ptr));
		}
		if (FBoolProperty* B = CastField<FBoolProperty>(P))
		{
			return B->GetPropertyValue(Ptr) ? 1.0 : 0.0;
		}
		if (FEnumProperty* E = CastField<FEnumProperty>(P))
		{
			return static_cast<double>(E->GetUnderlyingProperty()->GetSignedIntPropertyValue(Ptr));
		}
		return Default;
	}

	static UObject* GetObj(const UObject* Obj, const TCHAR* Name)
	{
		if (FObjectPropertyBase* P = CastField<FObjectPropertyBase>(Prop(Obj, Name)))
		{
			return P->GetObjectPropertyValue(P->ContainerPtrToValuePtr<void>(Obj));
		}
		return nullptr;
	}

	static bool SetObj(UObject* Obj, const TCHAR* Name, UObject* Value)
	{
		if (FObjectPropertyBase* P = CastField<FObjectPropertyBase>(Prop(Obj, Name)))
		{
			if (!Value || Value->IsA(P->PropertyClass))
			{
				P->SetObjectPropertyValue(P->ContainerPtrToValuePtr<void>(Obj), Value);
				return true;
			}
		}
		return false;
	}

	/** One input argument of a Blueprint function call: a number (also enum / bool) or an object. */
	struct FArg
	{
		double Num = 0.0;
		UObject* Obj = nullptr;
		bool bObj = false;
		static FArg N(double V) { FArg A; A.Num = V; return A; }
		static FArg O(UObject* V) { FArg A; A.Obj = V; A.bObj = true; return A; }
	};

	/** Calls a Blueprint function by its name with the input arguments in order; outputs are ignored. */
	static bool Call(UObject* Obj, const TCHAR* Function, const TArray<FArg>& Args)
	{
		UFunction* Fn = Obj ? Obj->FindFunction(FName(Function)) : nullptr;
		if (!Fn)
		{
			return false;
		}
		uint8* Parms = static_cast<uint8*>(FMemory_Alloca_Aligned(Fn->ParmsSize, Fn->GetMinAlignment()));
		FMemory::Memzero(Parms, Fn->ParmsSize);
		for (TFieldIterator<FProperty> It(Fn); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
		{
			It->InitializeValue_InContainer(Parms);
		}
		int32 i = 0;
		for (TFieldIterator<FProperty> It(Fn); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
		{
			if (It->HasAnyPropertyFlags(CPF_ReturnParm) || (It->HasAnyPropertyFlags(CPF_OutParm) && !It->HasAnyPropertyFlags(CPF_ReferenceParm)))
			{
				continue;
			}
			if (!Args.IsValidIndex(i))
			{
				break;
			}
			const FArg& A = Args[i++];
			void* Ptr = It->ContainerPtrToValuePtr<void>(Parms);
			if (A.bObj)
			{
				if (FObjectPropertyBase* P = CastField<FObjectPropertyBase>(*It))
				{
					P->SetObjectPropertyValue(Ptr, A.Obj);
				}
			}
			else if (FNumericProperty* N = CastField<FNumericProperty>(*It))
			{
				N->IsFloatingPoint() ? N->SetFloatingPointPropertyValue(Ptr, A.Num) : N->SetIntPropertyValue(Ptr, static_cast<int64>(A.Num));
			}
			else if (FBoolProperty* B = CastField<FBoolProperty>(*It))
			{
				B->SetPropertyValue(Ptr, A.Num != 0.0);
			}
			else if (FEnumProperty* E = CastField<FEnumProperty>(*It))
			{
				E->GetUnderlyingProperty()->SetIntPropertyValue(Ptr, static_cast<int64>(A.Num));
			}
		}
		Obj->ProcessEvent(Fn, Parms);
		for (TFieldIterator<FProperty> It(Fn); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
		{
			It->DestroyValue_InContainer(Parms);
		}
		return true;
	}

	static UObject* LoadPreset(const FString& Name)
	{
		for (const FPresetName& P : Presets)
		{
			if (Name.Equals(P.Name, ESearchCase::IgnoreCase) || Name.Equals(P.Asset, ESearchCase::IgnoreCase))
			{
				const FString Path = FString::Printf(TEXT("%s%s.%s"), PresetFolder, P.Asset, P.Asset);
				return LoadObject<UObject>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
			}
		}
		return nullptr;
	}

	static FString PresetNameOf(const UObject* Preset)
	{
		if (!Preset)
		{
			return TEXT("");
		}
		for (const FPresetName& P : Presets)
		{
			if (Preset->GetName() == P.Asset)
			{
				return P.Name;
			}
		}
		return Preset->GetName();
	}

	static TArray<FString> PresetList()
	{
		TArray<FString> Out;
		for (const FPresetName& P : Presets)
		{
			Out.Add(P.Name);
		}
		Out.Add(TEXT("random"));
		return Out;
	}

	static bool Installed()
	{
		return LoadClassAt(SkyClassPath) != nullptr;
	}

	static AActor* Sky(UWorld* World)
	{
		return FindOf(World, LoadClassAt(SkyClassPath));
	}

	static AActor* Weather(UWorld* World)
	{
		return FindOf(World, LoadClassAt(WeatherClassPath));
	}

	/** The sky, or a failed call explaining what is missing. */
	static AActor* RequireSky()
	{
		if (!Installed())
		{
			RuntimeToolCall::Fail(ERuntimeToolError::NotFound, TEXT("Ultra Dynamic Sky is not in this project"),
				TEXT("Add the Ultra Dynamic Sky pack to the project (Content/UltraDynamicSky), then call sky.setup"));
			return nullptr;
		}
		AActor* S = Sky(RuntimeToolCall::GetWorld());
		if (!S)
		{
			RuntimeToolCall::Fail(ERuntimeToolError::NotFound, TEXT("no Ultra Dynamic Sky in the level"), TEXT("Call sky.setup first"));
		}
		return S;
	}

	static AActor* RequireWeather()
	{
		if (!RequireSky())
		{
			return nullptr;
		}
		AActor* W = Weather(RuntimeToolCall::GetWorld());
		if (!W)
		{
			RuntimeToolCall::Fail(ERuntimeToolError::NotFound, TEXT("no Ultra Dynamic Weather in the level"), TEXT("Call sky.setup with weather true"));
		}
		return W;
	}

	static UMaterialParameterCollection* StyleCollection()
	{
		return LoadObject<UMaterialParameterCollection>(nullptr, TEXT("/RuntimeToolsets/Materials/MPC_RT_Style.MPC_RT_Style"), nullptr, LOAD_NoWarn | LOAD_Quiet);
	}

	static float MudAmount(UWorld* World)
	{
		UMaterialParameterCollection* C = StyleCollection();
		UMaterialParameterCollectionInstance* I = C && World ? World->GetParameterCollectionInstance(C) : nullptr;
		float V = 1.f;
		return I && I->GetScalarParameterValue(TEXT("Mud"), V) ? V : 1.f;
	}

	static FRuntimeSkyState State()
	{
		FRuntimeSkyState Out;
		Out.bInstalled = Installed();
		Out.Presets = PresetList();
		UWorld* World = RuntimeToolCall::GetWorld();
		AActor* S = Out.bInstalled ? Sky(World) : nullptr;
		if (!S)
		{
			return Out;
		}
		Out.Sky = Ref(S);
		Out.Hour = static_cast<float>(GetNum(S, TEXT("Time of Day")) / 100.0);
		Out.bAnimateTime = GetNum(S, TEXT("Animate Time of Day")) != 0.0;
		Out.DayLengthMinutes = static_cast<float>(GetNum(S, TEXT("Day Length")));
		Out.NightLengthMinutes = static_cast<float>(GetNum(S, TEXT("Night Length")));
		const int32 Mode = static_cast<int32>(GetNum(S, TEXT("Sky Mode")));
		Out.SkyMode = Mode >= 0 && Mode < UE_ARRAY_COUNT(SkyModes) ? SkyModes[Mode] : FString::FromInt(Mode);
		Out.MoonPhase = static_cast<float>(GetNum(S, TEXT("Moon Phase")));
		Out.Latitude = static_cast<float>(GetNum(S, TEXT("Latitude")));
		if (AActor* W = Weather(World))
		{
			Out.Weather = Ref(W);
			const bool bRandom = GetObj(W, TEXT("Random Weather Manager")) && GetNum(W, TEXT("Random Weather Variation")) != 0.0;
			UObject* Preset = GetObj(W, TEXT("Weather"));
			Out.WeatherPreset = bRandom ? FString(TEXT("random")) : (Preset ? PresetNameOf(Preset) : FString(TEXT("manual")));
			for (const auto& V : WeatherValues)
			{
				Out.Values.Add(V.Key, static_cast<float>(GetNum(W, V.Value)));
			}
			Out.Ground.Add(TEXT("wetness"), static_cast<float>(GetNum(W, TEXT("Material Wetness"))));
			Out.Ground.Add(TEXT("puddles"), static_cast<float>(GetNum(W, TEXT("Puddle Coverage"))));
			Out.Ground.Add(TEXT("snow"), static_cast<float>(GetNum(W, TEXT("Material Snow Coverage"))));
			Out.Ground.Add(TEXT("dust"), static_cast<float>(GetNum(W, TEXT("Material Dust Coverage"))));
			Out.Ground.Add(TEXT("mud"), MudAmount(World));
		}
		else
		{
			Out.Values.Add(TEXT("cloud_coverage"), static_cast<float>(GetNum(S, TEXT("Cloud Coverage"))));
			Out.Values.Add(TEXT("fog"), static_cast<float>(GetNum(S, TEXT("Fog"))));
		}
		return Out;
	}

	/**
	 * The level's own sun, sky light, atmosphere, clouds and fog are hidden while Ultra Dynamic Sky is in use and come back
	 * whenever it is not: deleted (scene.clear_all, scene delete, sky.remove), hidden or destroyed. Undo hides them again.
	 * A watcher checks the sky actor once per frame.
	 */
	/** A post-process volume's exposure overrides, switched off while Ultra Dynamic Sky drives exposure. */
	struct FExposureOverrides
	{
		TWeakObjectPtr<APostProcessVolume> Volume;
		uint8 Method : 1, Bias : 1, Min : 1, Max : 1, Physical : 1, SpeedUp : 1, SpeedDown : 1, LogMin : 1, LogMax : 1, Curve : 1, Meter : 1;

		static bool Any(const FPostProcessSettings& S)
		{
			return S.bOverride_AutoExposureMethod || S.bOverride_AutoExposureBias || S.bOverride_AutoExposureMinBrightness ||
				S.bOverride_AutoExposureMaxBrightness || S.bOverride_AutoExposureApplyPhysicalCameraExposure || S.bOverride_AutoExposureSpeedUp ||
				S.bOverride_AutoExposureSpeedDown || S.bOverride_HistogramLogMin || S.bOverride_HistogramLogMax ||
				S.bOverride_AutoExposureBiasCurve || S.bOverride_AutoExposureMeterMask;
		}
		void Save(const FPostProcessSettings& S)
		{
			Method = S.bOverride_AutoExposureMethod; Bias = S.bOverride_AutoExposureBias; Min = S.bOverride_AutoExposureMinBrightness;
			Max = S.bOverride_AutoExposureMaxBrightness; Physical = S.bOverride_AutoExposureApplyPhysicalCameraExposure;
			SpeedUp = S.bOverride_AutoExposureSpeedUp; SpeedDown = S.bOverride_AutoExposureSpeedDown; LogMin = S.bOverride_HistogramLogMin;
			LogMax = S.bOverride_HistogramLogMax; Curve = S.bOverride_AutoExposureBiasCurve; Meter = S.bOverride_AutoExposureMeterMask;
		}
		void Apply(FPostProcessSettings& S, bool bOn) const
		{
			S.bOverride_AutoExposureMethod = bOn && Method; S.bOverride_AutoExposureBias = bOn && Bias; S.bOverride_AutoExposureMinBrightness = bOn && Min;
			S.bOverride_AutoExposureMaxBrightness = bOn && Max; S.bOverride_AutoExposureApplyPhysicalCameraExposure = bOn && Physical;
			S.bOverride_AutoExposureSpeedUp = bOn && SpeedUp; S.bOverride_AutoExposureSpeedDown = bOn && SpeedDown; S.bOverride_HistogramLogMin = bOn && LogMin;
			S.bOverride_HistogramLogMax = bOn && LogMax; S.bOverride_AutoExposureBiasCurve = bOn && Curve; S.bOverride_AutoExposureMeterMask = bOn && Meter;
		}
	};

	struct FEngineSky
	{
		TWeakObjectPtr<AActor> Sky;
		TArray<TWeakObjectPtr<USceneComponent>> Comps;
		TArray<TWeakObjectPtr<AActor>> Actors;
		TArray<FExposureOverrides> Exposure;
		bool bHidden = false;
		FTSTicker::FDelegateHandle Ticker;
	};
	static FEngineSky GEngineSky;

	static void ApplyEngineSky(bool bHide)
	{
		for (const TWeakObjectPtr<USceneComponent>& C : GEngineSky.Comps)
		{
			if (C.IsValid())
			{
				C->SetVisibility(!bHide);
			}
		}
		for (const TWeakObjectPtr<AActor>& A : GEngineSky.Actors)
		{
			if (A.IsValid())
			{
				A->SetActorHiddenInGame(bHide);
			}
		}
		// exposure: Ultra Dynamic Sky sets it (day and night); grading, bloom, vignette etc. stay with the volumes
		for (const FExposureOverrides& E : GEngineSky.Exposure)
		{
			if (APostProcessVolume* V = E.Volume.Get())
			{
				E.Apply(V->Settings, !bHide);
			}
		}
		GEngineSky.bHidden = bHide;
	}

	static bool WatchSky(float)
	{
		AActor* S = GEngineSky.Sky.Get();
		const bool bActive = S && !S->IsActorBeingDestroyed() && !S->IsHidden();
		if (bActive != GEngineSky.bHidden)
		{
			ApplyEngineSky(bActive);
		}
		if (!S)
		{
			GEngineSky = FEngineSky();
			return false;  // the sky is gone for good: stop watching
		}
		return true;
	}

	/** Hides the level's own sky parts (remembering them) and starts the watcher. */
	static void HideEngineSky(UWorld* World, AActor* Sky, AActor* Weather)
	{
		if (GEngineSky.Sky.Get() != Sky)
		{
			if (GEngineSky.bHidden)
			{
				ApplyEngineSky(false);
			}
			GEngineSky.Comps.Reset();
			GEngineSky.Actors.Reset();
			GEngineSky.Exposure.Reset();
		}
		for (TActorIterator<APostProcessVolume> It(World); It; ++It)
		{
			if (FExposureOverrides::Any(It->Settings) && !GEngineSky.Exposure.ContainsByPredicate([&](const FExposureOverrides& E) { return E.Volume == *It; }))
			{
				FExposureOverrides E;
				E.Volume = *It;
				E.Save(It->Settings);
				GEngineSky.Exposure.Add(E);
			}
		}
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* A = *It;
			if (A == Sky || A == Weather || A->IsAttachedTo(Sky) || A->IsHidden())
			{
				continue;
			}
			bool bSky = false;
			TInlineComponentArray<USceneComponent*> Comps(A);
			for (USceneComponent* C : Comps)
			{
				if (((C->IsA<ULightComponent>() && Cast<ULightComponent>(C)->GetLightType() == LightType_Directional) || C->IsA<USkyLightComponent>() ||
						C->IsA<USkyAtmosphereComponent>() || C->IsA<UVolumetricCloudComponent>() || C->IsA<UExponentialHeightFogComponent>()) &&
					C->IsVisible())
				{
					GEngineSky.Comps.AddUnique(C);
					bSky = true;
				}
			}
			if (bSky)
			{
				GEngineSky.Actors.AddUnique(A);
			}
		}
		GEngineSky.Sky = Sky;
		ApplyEngineSky(true);
		if (!GEngineSky.Ticker.IsValid())
		{
			GEngineSky.Ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&WatchSky));
		}
	}

	static AActor* Spawn(const TCHAR* ClassPath, const FString& Name)
	{
		const FString Path = ClassPath;
		auto Make = [Path](FRuntimeCommandContext& Ctx, FString& Err) -> AActor*
		{
			UClass* Class = LoadClassAt(*Path);
			FActorSpawnParameters P;
			P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AActor* A = Class ? Ctx.World->SpawnActor<AActor>(Class, FTransform::Identity, P) : nullptr;
			if (!A)
			{
				Err = FString::Printf(TEXT("could not spawn %s"), *Path);
			}
			return A;
		};
		TUniquePtr<FCreateNodeCommand> Cmd = MakeUnique<FCreateNodeCommand>(FString::Printf(TEXT("spawn %s"), *Name), Make, Name, FString(),
			ERuntimeNodeKind::Actor, TEXT("class:") + Path, TArray<FName>());
		FCreateNodeCommand* Raw = Cmd.Get();
		if (!RuntimeToolCall::RunCommand(MoveTemp(Cmd)))
		{
			return nullptr;
		}
		const FRuntimeSceneNode* Node = RuntimeToolCall::GetGraph()->Find(Raw->GetNodeId());
		return Node ? Node->Actor.Get() : nullptr;
	}

	static bool InRange(const TCHAR* Name, double V, double Lo, double Hi)
	{
		if (V < Lo || V > Hi)
		{
			RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("%s %g is outside %g-%g"), Name, V, Lo, Hi));
			return false;
		}
		return true;
	}
}

FRuntimeSkyState URuntimeSkyToolset::Setup(bool bWeather, const float& Hour, const FString& Preset)
{
	using namespace RtSky;
	if (!Installed())
	{
		RequireSky();
		return {};
	}
	if (Passed(TEXT("hour")) && !InRange(TEXT("hour"), Hour, 0.0, 24.0))
	{
		return {};
	}
	UWorld* World = RuntimeToolCall::GetWorld();
	AActor* S = Sky(World);
	if (!S)
	{
		S = Spawn(SkyClassPath, TEXT("Ultra_Dynamic_Sky"));
		if (!S)
		{
			return {};
		}
	}
	AActor* W = Weather(World);
	if (bWeather && !W)
	{
		W = Spawn(WeatherClassPath, TEXT("Ultra_Dynamic_Weather"));
	}
	if (W)
	{
		SetObj(S, TEXT("Ultra Dynamic Weather"), W);
		SetObj(W, TEXT("UltraDynamicSky"), S);
		Call(S, TEXT("Start Up UDW If it Exists"), {});
	}
	HideEngineSky(World, S, W);
	SetNum(S, TEXT("Time of Day"), (Passed(TEXT("hour")) ? Hour : 10.f) * 100.0);
	if (W && !Preset.IsEmpty())
	{
		if (UObject* P = LoadPreset(Preset))
		{
			Call(W, TEXT("Change Weather"), {FArg::O(P), FArg::N(0.0)});
		}
	}
	return State();
}

FRuntimeSkyState URuntimeSkyToolset::GetState()
{
	return RtSky::State();
}

FRuntimeSkyState URuntimeSkyToolset::SetTime(float Hour, const bool& bAnimate, const float& DayLengthMinutes, const float& NightLengthMinutes)
{
	using namespace RtSky;
	AActor* S = RequireSky();
	if (!S || !InRange(TEXT("hour"), Hour, 0.0, 24.0))
	{
		return {};
	}
	SetNum(S, TEXT("Time of Day"), Hour * 100.0);
	if (Passed(TEXT("animate")))
	{
		SetNum(S, TEXT("Animate Time of Day"), bAnimate ? 1.0 : 0.0);
	}
	if (Passed(TEXT("day_length_minutes")))
	{
		SetNum(S, TEXT("Day Length"), FMath::Max(0.1f, DayLengthMinutes));
	}
	if (Passed(TEXT("night_length_minutes")))
	{
		SetNum(S, TEXT("Night Length"), FMath::Max(0.1f, NightLengthMinutes));
	}
	return State();
}

FRuntimeSkyState URuntimeSkyToolset::SetWeather(const FString& Preset, float TransitionSeconds)
{
	using namespace RtSky;
	AActor* W = RequireWeather();
	if (!W)
	{
		return {};
	}
	const double T = FMath::Max(0.0, static_cast<double>(TransitionSeconds));
	if (Preset.Equals(TEXT("random"), ESearchCase::IgnoreCase))
	{
		Call(W, TEXT("Change to Random Weather Variation"), {FArg::N(T), FArg::N(0)});
		return State();
	}
	UObject* P = LoadPreset(Preset);
	if (!P)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("unknown weather preset '%s' (%s)"), *Preset, *JoinOrNone(PresetList())));
		return {};
	}
	Call(W, TEXT("Change Weather"), {FArg::O(P), FArg::N(T)});
	return State();
}

FRuntimeSkyState URuntimeSkyToolset::SetWeatherValues(const float& CloudCoverage, const float& Rain, const float& Snow, const float& Thunder, const float& Wind,
	const float& Fog, const float& Dust, bool bClearOverrides)
{
	using namespace RtSky;
	AActor* W = RequireWeather();
	if (!W)
	{
		return {};
	}
	const TPair<const TCHAR*, float> Given[] = {{TEXT("cloud_coverage"), CloudCoverage}, {TEXT("rain"), Rain}, {TEXT("snow"), Snow},
		{TEXT("thunder"), Thunder}, {TEXT("wind"), Wind}, {TEXT("fog"), Fog}, {TEXT("dust"), Dust}};
	for (const auto& G : Given)
	{
		if (Passed(G.Key) && !InRange(G.Key, G.Value, 0.0, 10.0))
		{
			return {};
		}
	}
	for (int32 i = 0; i < UE_ARRAY_COUNT(WeatherValues); ++i)
	{
		const FString Override = FString(WeatherValues[i].Value) + TEXT(" - Manual Override");
		if (bClearOverrides)
		{
			SetNum(W, *Override, 0.0);
		}
		if (Passed(Given[i].Key))
		{
			SetNum(W, *Override, 1.0);
			SetNum(W, WeatherValues[i].Value, Given[i].Value);
		}
	}
	return State();
}

FRuntimeSkyState URuntimeSkyToolset::SetSky(const FString& SkyMode, const float& MoonPhase, const bool& bAuroras, const float& StarsIntensity,
	const float& Latitude, const float& Brightness)
{
	using namespace RtSky;
	AActor* S = RequireSky();
	if (!S)
	{
		return {};
	}
	if (Passed(TEXT("sky_mode")))
	{
		int32 Mode = INDEX_NONE;
		for (int32 i = 0; i < UE_ARRAY_COUNT(SkyModes); ++i)
		{
			if (SkyMode.Equals(SkyModes[i], ESearchCase::IgnoreCase))
			{
				Mode = i;
			}
		}
		if (Mode == INDEX_NONE)
		{
			RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("unknown sky mode '%s' (volumetric, static, 2d, none, aurora, space, voxel)"), *SkyMode));
			return {};
		}
		Call(S, TEXT("Change Sky Mode at Runtime"), {FArg::N(Mode)});
	}
	if (Passed(TEXT("moon_phase")))
	{
		if (!InRange(TEXT("moon_phase"), MoonPhase, 0.0, 1.0))
		{
			return {};
		}
		SetNum(S, TEXT("Moon Phase"), MoonPhase);
	}
	if (Passed(TEXT("auroras")))
	{
		SetNum(S, TEXT("Use Auroras"), bAuroras ? 1.0 : 0.0);
	}
	if (Passed(TEXT("stars_intensity")))
	{
		SetNum(S, TEXT("Stars Intensity"), FMath::Max(0.f, StarsIntensity));
	}
	if (Passed(TEXT("latitude")))
	{
		if (!InRange(TEXT("latitude"), Latitude, -90.0, 90.0))
		{
			return {};
		}
		SetNum(S, TEXT("Latitude"), Latitude);
	}
	if (Passed(TEXT("brightness")))
	{
		SetNum(S, TEXT("Overall Intensity"), FMath::Max(0.f, Brightness));
	}
	return State();
}

FRuntimeSkyState URuntimeSkyToolset::Remove()
{
	using namespace RtSky;
	UWorld* World = RuntimeToolCall::GetWorld();
	TArray<AActor*> Gone;
	if (AActor* W = Weather(World))
	{
		Gone.Add(W);
	}
	if (AActor* S = Sky(World))
	{
		Gone.Add(S);
	}
	if (Gone.Num() && !RuntimeToolCall::RunCommand(MakeUnique<FSoftDeleteActorsCommand>(TEXT("remove ultra dynamic sky"), Gone)))
	{
		return {};
	}
	ApplyEngineSky(false);  // right away (the watcher would do it next frame)
	return State();
}

AActor* RuntimeSky::ActiveSky(UWorld* World)
{
	return RtSky::Installed() ? RtSky::Sky(World) : nullptr;
}

FRuntimeSkyState URuntimeSkyToolset::SetGround(const float& Wetness, const float& Puddles, const float& Snow, const float& Dust, const float& Mud, bool bRelease)
{
	using namespace RtSky;
	AActor* W = RequireWeather();
	if (!W)
	{
		return {};
	}
	struct FGround
	{
		const TCHAR* Arg;
		float Value;
		const TCHAR* Prop;
	};
	const FGround Given[] = {{TEXT("wetness"), Wetness, TEXT("Material Wetness")}, {TEXT("snow"), Snow, TEXT("Material Snow Coverage")},
		{TEXT("dust"), Dust, TEXT("Material Dust Coverage")}};
	for (const FGround& G : Given)
	{
		if (Passed(G.Arg) && !InRange(G.Arg, G.Value, 0.0, 1.0))
		{
			return {};
		}
	}
	if ((Passed(TEXT("puddles")) && !InRange(TEXT("puddles"), Puddles, 0.0, 1.0)) || (Passed(TEXT("mud")) && !InRange(TEXT("mud"), Mud, 0.0, 2.0)))
	{
		return {};
	}
	for (const FGround& G : Given)
	{
		const FString Override = FString(G.Prop) + TEXT(" - Manual Override");
		if (bRelease)
		{
			SetNum(W, *Override, 0.0);
		}
		if (Passed(G.Arg))
		{
			SetNum(W, *Override, 1.0);
			SetNum(W, G.Prop, G.Value);
		}
	}
	if (Passed(TEXT("puddles")))
	{
		SetNum(W, TEXT("Puddle Coverage"), Puddles);
	}
	if (Passed(TEXT("mud")))
	{
		UWorld* World = RuntimeToolCall::GetWorld();
		if (UMaterialParameterCollection* C = StyleCollection())
		{
			if (UMaterialParameterCollectionInstance* I = World->GetParameterCollectionInstance(C))
			{
				I->SetScalarParameterValue(TEXT("Mud"), Mud);
			}
		}
	}
	return State();
}
