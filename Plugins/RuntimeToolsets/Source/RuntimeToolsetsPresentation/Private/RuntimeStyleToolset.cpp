#include "RuntimeStyleToolset.h"
#include "RuntimeToolsetsSettings.h"
#include "PresentationToolCall.h"
#include "RuntimeCommand.h"
#include "RuntimeJson.h"
#include "RuntimeToolsetsCore.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Kismet/KismetMaterialLibrary.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialParameterCollection.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace RtStyle
{
	const TCHAR* MpcPath = TEXT("/RuntimeToolsets/Materials/MPC_RT_Style.MPC_RT_Style");
	const TCHAR* PostPath = TEXT("/RuntimeToolsets/Materials/M_PP_Style.M_PP_Style");
	const FName StyleTag(TEXT("RuntimeToolsetsStyle"));

	// Built-in presets. Every number is a style decision; edit or add presets in Config/RuntimeToolsets/StylePresets.json.
	const TCHAR* BuiltInPresets = TEXT(R"JSON({
"realistic": {"description": "Physically based: full surface detail, natural light, filmic bloom and ambient occlusion",
  "material": {"Weathering": 0.3, "Faceted": 0, "Pattern": 1, "Steps": 0, "Saturation": 1.0, "RimStrength": 0, "Tint": [1, 1, 1]},
  "post": {"Toon": 0, "Outline": 0, "Paint": 0, "PixelSize": 0, "Levels": 0, "Saturation": 1, "Contrast": 1, "Tint": [1, 1, 1]},
  "settings": {"bloom": 0.6, "vignette": 0.25, "ao": 0.8, "grain": 0.03, "chromatic": 0.0, "exposure_bias": 0.0, "temperature": 6500},
  "lighting": {"mood": "morning", "fog": {"density": 0.015}}},
"stylized": {"description": "Painterly and saturated: soft brush-like shapes, gentle light bands, coloured outlines, warm rim light",
  "material": {"Weathering": 0, "Faceted": 0, "Pattern": 0.35, "Steps": 0, "Saturation": 1.3, "RimStrength": 0.35, "RimColor": [1.0, 0.85, 0.6], "Tint": [1.03, 1.0, 0.95]},
  "post": {"Paint": 0.85, "PaintRadius": 3, "PaintScale": 1.2, "Toon": 0.35, "Bands": 4, "ShadowFloor": 0.35, "Outline": 0.5, "OutlineWidth": 1.0,
           "OutlineColor": [0.08, 0.05, 0.12], "PixelSize": 0, "Levels": 0, "Saturation": 1.15, "Contrast": 1.05, "Tint": [1, 1, 1]},
  "settings": {"bloom": 0.9, "vignette": 0.3, "ao": 0.6, "grain": 0.0, "chromatic": 0.0, "exposure_bias": 0.2, "temperature": 6000},
  "lighting": {"mood": "morning", "fog": {"density": 0.02, "color": [1.0, 0.9, 0.8]}}},
"lowpoly": {"description": "Low poly: flat-shaded facets, flat colours from a small palette, clean pastel light",
  "material": {"Weathering": 0, "Faceted": 1, "Pattern": 0, "Steps": 7, "Saturation": 1.15, "RimStrength": 0, "Tint": [1, 1, 1]},
  "post": {"Toon": 0, "Outline": 0, "Paint": 0, "PixelSize": 0, "Levels": 0, "Saturation": 1.1, "Contrast": 1.05, "Tint": [1, 1, 1]},
  "settings": {"bloom": 0.4, "vignette": 0.2, "ao": 1.0, "grain": 0.0, "chromatic": 0.0, "exposure_bias": 0.3, "temperature": 6500},
  "lighting": {"mood": "noon", "fog": {"density": 0.03, "color": [0.75, 0.85, 1.0]}}},
"toon": {"description": "Cel shaded cartoon: hard light bands, bold ink outlines, bright flat colours",
  "material": {"Weathering": 0, "Faceted": 0, "Pattern": 0.08, "Steps": 0, "Saturation": 1.35, "RimStrength": 0.25, "RimColor": [1, 1, 1], "Tint": [1, 1, 1]},
  "post": {"Toon": 1.0, "Bands": 3, "ShadowFloor": 0.35, "Outline": 1.0, "OutlineWidth": 1.5, "DepthSens": 10, "NormalSens": 2,
           "OutlineColor": [0.02, 0.02, 0.03], "Paint": 0, "PixelSize": 0, "Levels": 0, "Saturation": 1.1, "Contrast": 1.1, "Tint": [1, 1, 1]},
  "settings": {"bloom": 0.3, "vignette": 0.0, "ao": 0.3, "grain": 0.0, "chromatic": 0.0, "exposure_bias": 0.3, "temperature": 6500},
  "lighting": {"mood": "noon", "fog": {"density": 0.005}}},
"retro": {"description": "Retro pixel art: chunky pixels, flat facets and outlines",
  "material": {"Weathering": 0, "Faceted": 1, "Pattern": 0, "Steps": 6, "Saturation": 1.2, "RimStrength": 0, "Tint": [1, 1, 1]},
  "post": {"PixelSize": 4, "Levels": 0, "Outline": 0.6, "OutlineWidth": 1.0, "Toon": 0.6, "Bands": 3, "ShadowFloor": 0.3, "Paint": 0,
           "Saturation": 1.1, "Contrast": 1.05, "Tint": [1, 1, 1]},
  "settings": {"bloom": 0.2, "vignette": 0.2, "ao": 0.5, "grain": 0.0, "chromatic": 0.0, "exposure_bias": 0.2, "temperature": 6500},
  "lighting": {"mood": "noon", "fog": {"density": 0.01}}},
"noir": {"description": "Film noir: black and white, hard contrast, ink edges, heavy vignette and grain",
  "material": {"Weathering": 0, "Faceted": 0, "Pattern": 0.6, "Steps": 0, "Saturation": 0, "RimStrength": 0.2, "RimColor": [1, 1, 1], "Tint": [1, 1, 1]},
  "post": {"Saturation": 0, "Contrast": 1.9, "Toon": 0.6, "Bands": 4, "ShadowFloor": 0.04, "Outline": 0.5, "OutlineWidth": 1.0, "Paint": 0, "PixelSize": 0,
           "Levels": 0, "Tint": [1.0, 0.97, 0.9]},
  "settings": {"bloom": 0.5, "vignette": 0.7, "ao": 1.0, "grain": 0.4, "chromatic": 0.0, "exposure_bias": -0.4, "temperature": 6500},
  "lighting": {"mood": "sunset", "fog": {"density": 0.01}}}
})JSON");

	TMap<FString, TSharedPtr<FJsonObject>>& Presets()
	{
		static TMap<FString, TSharedPtr<FJsonObject>> Map;
		if (Map.Num() == 0)
		{
			auto Load = [](const FString& Text)
			{
				const TSharedPtr<FJsonObject> J = RuntimeJson::Parse(Text);
				if (J.IsValid())
				{
					for (const TPair<FString, TSharedPtr<FJsonValue>>& P : J->Values)
					{
						if (P.Value->Type == EJson::Object)
						{
							Map.Add(P.Key, P.Value->AsObject());
						}
					}
				}
			};
			Load(BuiltInPresets);
			FString File;
			if (FFileHelper::LoadFileToString(File, *(FPaths::ProjectConfigDir() / TEXT("RuntimeToolsets") / TEXT("StylePresets.json"))))
			{
				Load(File);
			}
		}
		return Map;
	}

	/** Current style per world (for get and undo). */
	TMap<TWeakObjectPtr<UWorld>, TSharedPtr<FJsonObject>>& Current()
	{
		static TMap<TWeakObjectPtr<UWorld>, TSharedPtr<FJsonObject>> Map;
		return Map;
	}

	TSharedRef<FJsonObject> Copy(const TSharedPtr<FJsonObject>& J)
	{
		return J.IsValid() ? RuntimeJson::Parse(RuntimeJson::Write(J.ToSharedRef())).ToSharedRef() : MakeShared<FJsonObject>();
	}

	/** Merges Over into Base (objects merge key by key, everything else replaces). */
	void Merge(const TSharedRef<FJsonObject>& Base, const TSharedPtr<FJsonObject>& Over)
	{
		if (!Over.IsValid())
		{
			return;
		}
		for (const TPair<FString, TSharedPtr<FJsonValue>>& P : Over->Values)
		{
			const TSharedPtr<FJsonObject>* Sub = nullptr;
			if (P.Value->Type == EJson::Object && Base->TryGetObjectField(P.Key, Sub))
			{
				TSharedRef<FJsonObject> M = Copy(*Sub);
				Merge(M, P.Value->AsObject());
				Base->SetObjectField(P.Key, M);
			}
			else
			{
				Base->SetField(P.Key, P.Value);
			}
		}
	}

	bool ToColor(const TSharedPtr<FJsonValue>& V, FLinearColor& Out)
	{
		const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
		if (!V.IsValid() || !V->TryGetArray(A) || A->Num() < 3)
		{
			return false;
		}
		Out = FLinearColor((*A)[0]->AsNumber(), (*A)[1]->AsNumber(), (*A)[2]->AsNumber(), A->Num() > 3 ? (*A)[3]->AsNumber() : 1.0);
		return true;
	}

	APostProcessVolume* StyleVolume(UWorld* W)
	{
		for (TActorIterator<APostProcessVolume> It(W); It; ++It)
		{
			if (It->Tags.Contains(StyleTag))
			{
				return *It;
			}
		}
		FActorSpawnParameters P;
		P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		P.ObjectFlags |= RF_Transient;
		APostProcessVolume* V = W->SpawnActor<APostProcessVolume>(APostProcessVolume::StaticClass(), FTransform::Identity, P);
		if (V)
		{
			V->bUnbound = true;
			V->Priority = 50.f;
			V->Tags.Add(StyleTag);
		}
		return V;
	}

	/** Applies the material, post and settings sections (lighting is applied by the tool through the lighting toolset). */
	bool ApplyLayers(UWorld* W, const TSharedRef<FJsonObject>& Style, FString& OutError)
	{
		UMaterialParameterCollection* Mpc = LoadObject<UMaterialParameterCollection>(nullptr, MpcPath);
		UMaterialInterface* PostBase = LoadObject<UMaterialInterface>(nullptr, PostPath);
		if (!Mpc || !PostBase)
		{
			OutError = TEXT("style assets are missing (run Scripts/create_materials.py and cook /RuntimeToolsets/Materials)");
			return false;
		}
		const TSharedPtr<FJsonObject>* Mat = nullptr;
		if (Style->TryGetObjectField(TEXT("material"), Mat))
		{
			for (const TPair<FString, TSharedPtr<FJsonValue>>& P : (*Mat)->Values)
			{
				FLinearColor C;
				if (P.Value->Type == EJson::Number)
				{
					UKismetMaterialLibrary::SetScalarParameterValue(W, Mpc, FName(*P.Key), static_cast<float>(P.Value->AsNumber()));
				}
				else if (ToColor(P.Value, C))
				{
					UKismetMaterialLibrary::SetVectorParameterValue(W, Mpc, FName(*P.Key), C);
				}
			}
		}
		APostProcessVolume* Vol = StyleVolume(W);
		if (!Vol)
		{
			OutError = TEXT("could not create the style post-process volume");
			return false;
		}
		UMaterialInstanceDynamic* Mid = nullptr;
		for (FWeightedBlendable& B : Vol->Settings.WeightedBlendables.Array)
		{
			if (UMaterialInstanceDynamic* M = Cast<UMaterialInstanceDynamic>(B.Object); M && M->Parent == PostBase)
			{
				Mid = M;
			}
		}
		if (!Mid)
		{
			Mid = UMaterialInstanceDynamic::Create(PostBase, Vol);
			Vol->Settings.WeightedBlendables.Array.Add(FWeightedBlendable(0.f, Mid));
		}
		bool bActive = false;
		const TSharedPtr<FJsonObject>* Post = nullptr;
		if (Style->TryGetObjectField(TEXT("post"), Post))
		{
			for (const TPair<FString, TSharedPtr<FJsonValue>>& P : (*Post)->Values)
			{
				FLinearColor C;
				if (P.Value->Type == EJson::Number)
				{
					const double V = P.Value->AsNumber();
					Mid->SetScalarParameterValue(FName(*P.Key), static_cast<float>(V));
					const bool bEffect = (P.Key == TEXT("Toon") || P.Key == TEXT("Outline") || P.Key == TEXT("Paint")) ? V > 0.01
						: (P.Key == TEXT("PixelSize") || P.Key == TEXT("Levels")) ? V > 1.5
						: (P.Key == TEXT("Saturation") || P.Key == TEXT("Contrast")) ? !FMath::IsNearlyEqual(V, 1.0, 0.01) : false;
					bActive |= bEffect;
				}
				else if (ToColor(P.Value, C))
				{
					Mid->SetVectorParameterValue(FName(*P.Key), C);
					bActive |= P.Key == TEXT("Tint") && !C.Equals(FLinearColor::White, 0.01f);
				}
			}
		}
		for (FWeightedBlendable& B : Vol->Settings.WeightedBlendables.Array)
		{
			if (B.Object == Mid)
			{
				B.Weight = bActive ? 1.f : 0.f; // nothing to do: skip the pass entirely
			}
		}
		const TSharedPtr<FJsonObject>* Set = nullptr;
		if (Style->TryGetObjectField(TEXT("settings"), Set))
		{
			FPostProcessSettings& S = Vol->Settings;
			double V;
			if ((*Set)->TryGetNumberField(TEXT("bloom"), V)) { S.bOverride_BloomIntensity = true; S.BloomIntensity = V; }
			if ((*Set)->TryGetNumberField(TEXT("vignette"), V)) { S.bOverride_VignetteIntensity = true; S.VignetteIntensity = V; }
			if ((*Set)->TryGetNumberField(TEXT("ao"), V)) { S.bOverride_AmbientOcclusionIntensity = true; S.AmbientOcclusionIntensity = V; }
			if ((*Set)->TryGetNumberField(TEXT("grain"), V)) { S.bOverride_FilmGrainIntensity = true; S.FilmGrainIntensity = V; }
			if ((*Set)->TryGetNumberField(TEXT("chromatic"), V)) { S.bOverride_SceneFringeIntensity = true; S.SceneFringeIntensity = V; }
			if ((*Set)->TryGetNumberField(TEXT("exposure_bias"), V)) { S.bOverride_AutoExposureBias = true; S.AutoExposureBias = V; }
			if ((*Set)->TryGetNumberField(TEXT("temperature"), V)) { S.bOverride_WhiteTemp = true; S.WhiteTemp = V; }
		}
		Current().Add(W, Copy(Style));
		return true;
	}


	/** Lighting through the lighting toolset (its own undo entries). */
	void ApplyLighting(const TSharedRef<FJsonObject>& Style, TArray<FString>& Notes)
	{
		const TSharedPtr<FJsonObject>* L = nullptr;
		if (!Style->TryGetObjectField(TEXT("lighting"), L))
		{
			return;
		}
		FToolInvocation& Inv = PresentationTool::Inv();
		auto Call = [&](const TCHAR* Tool, const TSharedRef<FJsonObject>& Args)
		{
			const FToolResult R = IRuntimeToolsetsCore::Get().ExecuteInline(Tool, Args, Inv.GetCaller(), Inv.GetDepth() + 1);
			if (!R.IsOk())
			{
				Notes.Add(FString::Printf(TEXT("%s: %s"), Tool, *R.Message));
			}
		};
		FString Mood;
		if ((*L)->TryGetStringField(TEXT("mood"), Mood))
		{
			TSharedRef<FJsonObject> A = MakeShared<FJsonObject>();
			A->SetStringField(TEXT("mood"), Mood);
			Call(TEXT("lighting.apply_mood"), A);
		}
		for (const TPair<const TCHAR*, const TCHAR*>& P : {TPair<const TCHAR*, const TCHAR*>(TEXT("sun"), TEXT("lighting.set_sun")),
				 TPair<const TCHAR*, const TCHAR*>(TEXT("sky"), TEXT("lighting.set_sky")), TPair<const TCHAR*, const TCHAR*>(TEXT("fog"), TEXT("lighting.set_fog"))})
		{
			const TSharedPtr<FJsonObject>* Section = nullptr;
			if ((*L)->TryGetObjectField(P.Key, Section))
			{
				Call(P.Value, Copy(*Section));
			}
		}
	}

	/** Merges the layer overrides given to a tool into a style. */
	bool MergeLayers(const TSharedRef<FJsonObject>& Style, const TArray<TPair<const TCHAR*, const FRuntimeToolJson*>>& Layers)
	{
		for (const TPair<const TCHAR*, const FRuntimeToolJson*>& Layer : Layers)
		{
			if (!Layer.Value->IsSet())
			{
				continue;
			}
			const TSharedPtr<FJsonObject> O = Layer.Value->AsObject();
			if (!O.IsValid())
			{
				RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("%s must be an object of parameters, e.g. {\"Saturation\": 1.2}"), Layer.Key));
				return false;
			}
			TSharedRef<FJsonObject> Over = MakeShared<FJsonObject>();
			Over->SetObjectField(Layer.Key, O);
			Merge(Style, Over);
		}
		return true;
	}

	FString PresetNames()
	{
		TArray<FString> Names;
		Presets().GetKeys(Names);
		Names.Sort();
		return FString::Join(Names, TEXT(", "));
	}
}

FRuntimeStylePresetList URuntimeStyleToolset::ListPresets()
{
	using namespace RtStyle;
	FRuntimeStylePresetList Out;
	for (const TPair<FString, TSharedPtr<FJsonObject>>& P : Presets())
	{
		FRuntimeStylePresetInfo& I = Out.Presets.AddDefaulted_GetRef();
		I.Name = P.Key;
		P.Value->TryGetStringField(TEXT("description"), I.Description);
	}
	Out.Presets.Sort([](const FRuntimeStylePresetInfo& A, const FRuntimeStylePresetInfo& B) { return A.Name < B.Name; });
	Out.Layers = TEXT("material: Faceted, Pattern, Steps, Saturation, RimStrength, RimColor, Tint | post: Toon, Bands, ShadowFloor, Outline, OutlineWidth, "
		"OutlineColor, DepthSens, NormalSens, Paint, PaintRadius, PaintScale, PixelSize, Levels, Saturation, Contrast, Tint | settings: bloom, vignette, ao, grain, "
		"chromatic, exposure_bias, temperature | lighting: mood, sun{}, sky{}, fog{}");
	return Out;
}

FRuntimeStyleInfo URuntimeStyleToolset::Apply(const FRuntimeToolJson& Material, const FRuntimeToolJson& Post, const FRuntimeToolJson& Settings, const FRuntimeToolJson& Lighting,
	const FString& Preset, bool bLightingEnabled)
{
	using namespace RtStyle;
	if (URuntimeToolsetsSettings::Get().bProtectLevel)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::NotAllowed, TEXT("this level is protected: its look (lighting, grading, materials) stays as designed"),
			TEXT("Style only the objects you add (build.set_material) (bProtectLevel in the project settings)"));
		return {};
	}
	FRuntimeStyleInfo Out;
	UWorld* W = RuntimeToolCall::GetWorld();
	TSharedRef<FJsonObject> Style = MakeShared<FJsonObject>();
	if (!Preset.IsEmpty())
	{
		const TSharedPtr<FJsonObject>* P = Presets().Find(Preset);
		if (!P)
		{
			RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("unknown preset '%s'"), *Preset), TEXT("Presets: ") + PresetNames());
			return Out;
		}
		Style = Copy(*P);
	}
	else if (const TSharedPtr<FJsonObject>* Cur = Current().Find(W))
	{
		Style = Copy(*Cur); // tweak the current style
	}
	if (!MergeLayers(Style, {{TEXT("material"), &Material}, {TEXT("post"), &Post}, {TEXT("settings"), &Settings}, {TEXT("lighting"), &Lighting}}))
	{
		return Out;
	}
	if (!bLightingEnabled)
	{
		Style->RemoveField(TEXT("lighting"));
	}
	Style->SetStringField(TEXT("name"), Preset.IsEmpty() ? TEXT("custom") : *Preset);
	const TSharedPtr<FJsonObject>* Prev = Current().Find(W);
	const TSharedRef<FJsonObject> Before = Prev ? Copy(*Prev) : Copy(*Presets().Find(TEXT("realistic")));
	TWeakObjectPtr<UWorld> WeakW = W;
	auto Do = [WeakW, Style](FRuntimeCommandContext&, FString& Err) { return WeakW.IsValid() && ApplyLayers(WeakW.Get(), Style, Err); };
	auto Undo = [WeakW, Before](FRuntimeCommandContext&) { FString E; return WeakW.IsValid() && ApplyLayers(WeakW.Get(), Before, E); };
	if (!RuntimeToolCall::RunCommand(MakeUnique<FLambdaCommand>(TEXT("art style"), Do, Undo)))
	{
		return Out;
	}
	TArray<FString> Notes;
	ApplyLighting(Style, Notes);
	Out.bApplied = true;
	Out.Style = FRuntimeToolJson::Make(Style);
	Out.LightingNotes = FString::Join(Notes, TEXT("; "));
	return Out;
}

FRuntimeStyleInfo URuntimeStyleToolset::Get()
{
	using namespace RtStyle;
	FRuntimeStyleInfo Out;
	if (const TSharedPtr<FJsonObject>* Cur = Current().Find(RuntimeToolCall::GetWorld()))
	{
		Out.bApplied = true;
		Out.Style = FRuntimeToolJson::Make(*Cur);
	}
	else
	{
		Out.Style = FRuntimeToolJson::Make(MakeShared<FJsonValueString>(TEXT("none applied (engine defaults, equivalent to realistic materials)")));
	}
	return Out;
}

FRuntimeStyleInfo URuntimeStyleToolset::DefinePreset(const FString& Name, const FRuntimeToolJson& Material, const FRuntimeToolJson& Post, const FRuntimeToolJson& Settings,
	const FRuntimeToolJson& Lighting, const FString& BasedOn, const FString& Description)
{
	using namespace RtStyle;
	FRuntimeStyleInfo Out;
	if (Name.TrimStartAndEnd().IsEmpty())
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, TEXT("name is empty"), TEXT("e.g. \"comic\""));
		return Out;
	}
	TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
	if (!BasedOn.IsEmpty())
	{
		const TSharedPtr<FJsonObject>* Base = Presets().Find(BasedOn);
		if (!Base)
		{
			RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("based_on: unknown preset '%s'"), *BasedOn), TEXT("Presets: ") + PresetNames());
			return Out;
		}
		P = Copy(*Base);
	}
	if (!MergeLayers(P, {{TEXT("material"), &Material}, {TEXT("post"), &Post}, {TEXT("settings"), &Settings}, {TEXT("lighting"), &Lighting}}))
	{
		return Out;
	}
	P->SetStringField(TEXT("description"), Description);
	Presets().Add(Name, P);
	TSharedRef<FJsonObject> Shown = Copy(P);
	Shown->SetStringField(TEXT("name"), Name);
	Out.Style = FRuntimeToolJson::Make(Shown);
	return Out;
}
