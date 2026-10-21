#include "StyleToolset.h"
#include "RuntimeCommand.h"
#include "RuntimeJson.h"
#include "RuntimeToolHelpers.h"
#include "RuntimeToolsetsCore.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Kismet/KismetMaterialLibrary.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialParameterCollection.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

using namespace RuntimeTools;

namespace
{
	const TCHAR* MpcPath = TEXT("/RuntimeToolsets/Materials/MPC_RT_Style.MPC_RT_Style");
	const TCHAR* PostPath = TEXT("/RuntimeToolsets/Materials/M_PP_Style.M_PP_Style");
	const FName StyleTag(TEXT("RuntimeToolsetsStyle"));

	// Built-in presets. Every number is a style decision; edit or add presets in Config/RuntimeToolsets/StylePresets.json.
	const TCHAR* BuiltInPresets = TEXT(R"JSON({
"realistic": {"description": "Physically based: full surface detail, natural light, filmic bloom and ambient occlusion",
  "material": {"Faceted": 0, "Pattern": 1, "Steps": 0, "Saturation": 1.0, "RimStrength": 0, "Tint": [1, 1, 1]},
  "post": {"Toon": 0, "Outline": 0, "Paint": 0, "PixelSize": 0, "Levels": 0, "Saturation": 1, "Contrast": 1, "Tint": [1, 1, 1]},
  "settings": {"bloom": 0.6, "vignette": 0.25, "ao": 0.8, "grain": 0.03, "chromatic": 0.0, "exposure_bias": 0.0, "temperature": 6500},
  "lighting": {"mood": "morning", "fog": {"density": 0.015}}},
"stylized": {"description": "Painterly and saturated: soft brush-like shapes, gentle light bands, coloured outlines, warm rim light",
  "material": {"Faceted": 0, "Pattern": 0.35, "Steps": 0, "Saturation": 1.3, "RimStrength": 0.35, "RimColor": [1.0, 0.85, 0.6], "Tint": [1.03, 1.0, 0.95]},
  "post": {"Paint": 0.85, "PaintRadius": 3, "PaintScale": 1.2, "Toon": 0.35, "Bands": 4, "ShadowFloor": 0.35, "Outline": 0.5, "OutlineWidth": 1.0,
           "OutlineColor": [0.08, 0.05, 0.12], "PixelSize": 0, "Levels": 0, "Saturation": 1.15, "Contrast": 1.05, "Tint": [1, 1, 1]},
  "settings": {"bloom": 0.9, "vignette": 0.3, "ao": 0.6, "grain": 0.0, "chromatic": 0.0, "exposure_bias": 0.2, "temperature": 6000},
  "lighting": {"mood": "morning", "fog": {"density": 0.02, "color": [1.0, 0.9, 0.8]}}},
"lowpoly": {"description": "Low poly: flat-shaded facets, flat colours from a small palette, clean pastel light",
  "material": {"Faceted": 1, "Pattern": 0, "Steps": 7, "Saturation": 1.15, "RimStrength": 0, "Tint": [1, 1, 1]},
  "post": {"Toon": 0, "Outline": 0, "Paint": 0, "PixelSize": 0, "Levels": 0, "Saturation": 1.1, "Contrast": 1.05, "Tint": [1, 1, 1]},
  "settings": {"bloom": 0.4, "vignette": 0.2, "ao": 1.0, "grain": 0.0, "chromatic": 0.0, "exposure_bias": 0.3, "temperature": 6500},
  "lighting": {"mood": "noon", "fog": {"density": 0.03, "color": [0.75, 0.85, 1.0]}}},
"toon": {"description": "Cel shaded cartoon: hard light bands, bold ink outlines, bright flat colours",
  "material": {"Faceted": 0, "Pattern": 0.08, "Steps": 0, "Saturation": 1.35, "RimStrength": 0.25, "RimColor": [1, 1, 1], "Tint": [1, 1, 1]},
  "post": {"Toon": 1.0, "Bands": 3, "ShadowFloor": 0.35, "Outline": 1.0, "OutlineWidth": 1.5, "DepthSens": 10, "NormalSens": 2,
           "OutlineColor": [0.02, 0.02, 0.03], "Paint": 0, "PixelSize": 0, "Levels": 0, "Saturation": 1.1, "Contrast": 1.1, "Tint": [1, 1, 1]},
  "settings": {"bloom": 0.3, "vignette": 0.0, "ao": 0.3, "grain": 0.0, "chromatic": 0.0, "exposure_bias": 0.3, "temperature": 6500},
  "lighting": {"mood": "noon", "fog": {"density": 0.005}}},
"retro": {"description": "Retro pixel art: chunky pixels, posterized colours, flat facets and outlines",
  "material": {"Faceted": 1, "Pattern": 0, "Steps": 6, "Saturation": 1.2, "RimStrength": 0, "Tint": [1, 1, 1]},
  "post": {"PixelSize": 4, "Levels": 10, "Outline": 0.6, "OutlineWidth": 1.0, "Toon": 0.6, "Bands": 3, "ShadowFloor": 0.3, "Paint": 0,
           "Saturation": 1.1, "Contrast": 1.05, "Tint": [1, 1, 1]},
  "settings": {"bloom": 0.2, "vignette": 0.2, "ao": 0.5, "grain": 0.0, "chromatic": 0.0, "exposure_bias": 0.2, "temperature": 6500},
  "lighting": {"mood": "noon", "fog": {"density": 0.01}}},
"noir": {"description": "Film noir: black and white, hard contrast, ink edges, heavy vignette and grain",
  "material": {"Faceted": 0, "Pattern": 0.6, "Steps": 0, "Saturation": 0, "RimStrength": 0.2, "RimColor": [1, 1, 1], "Tint": [1, 1, 1]},
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
	void ApplyLighting(FToolInvocation& Inv, const TSharedRef<FJsonObject>& Style, TArray<FString>& Notes)
	{
		const TSharedPtr<FJsonObject>* L = nullptr;
		if (!Style->TryGetObjectField(TEXT("lighting"), L))
		{
			return;
		}
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
}

static void ListPresets(FToolInvocation& Inv)
{
	TArray<TSharedPtr<FJsonValue>> Items;
	for (const TPair<FString, TSharedPtr<FJsonObject>>& P : Presets())
	{
		TSharedRef<FJsonObject> O = Obj();
		O->SetStringField(TEXT("name"), P.Key);
		FString D;
		P.Value->TryGetStringField(TEXT("description"), D);
		O->SetStringField(TEXT("description"), D);
		Items.Add(MakeShared<FJsonValueObject>(O));
	}
	TSharedRef<FJsonObject> Out = Obj();
	Out->SetArrayField(TEXT("presets"), Items);
	Out->SetStringField(TEXT("layers"), TEXT("material: Faceted, Pattern, Steps, Saturation, RimStrength, RimColor, Tint | post: Toon, Bands, ShadowFloor, Outline, OutlineWidth, "
		"OutlineColor, DepthSens, NormalSens, Paint, PaintRadius, PaintScale, PixelSize, Levels, Saturation, Contrast, Tint | settings: bloom, vignette, ao, grain, "
		"chromatic, exposure_bias, temperature | lighting: mood, sun{}, sky{}, fog{}"));
	Inv.Succeed(Out);
}

static void Apply(FToolInvocation& Inv)
{
	TSharedRef<FJsonObject> Style = MakeShared<FJsonObject>();
	const FString Preset = Inv.GetString(TEXT("preset"));
	if (!Preset.IsEmpty())
	{
		const TSharedPtr<FJsonObject>* P = Presets().Find(Preset);
		if (!P)
		{
			TArray<FString> Names;
			Presets().GetKeys(Names);
			Inv.Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("unknown preset '%s'"), *Preset), TEXT("Presets: ") + FString::Join(Names, TEXT(", ")));
			return;
		}
		Style = Copy(*P);
	}
	else if (const TSharedPtr<FJsonObject>* Cur = Current().Find(Inv.GetWorld()))
	{
		Style = Copy(*Cur); // tweak the current style
	}
	for (const TCHAR* Section : {TEXT("material"), TEXT("post"), TEXT("settings"), TEXT("lighting")})
	{
		if (const TSharedPtr<FJsonObject> O = Inv.GetObject(Section))
		{
			TSharedRef<FJsonObject> Over = MakeShared<FJsonObject>();
			Over->SetObjectField(Section, O);
			Merge(Style, Over);
		}
	}
	if (!Inv.GetBool(TEXT("lighting_enabled"), true))
	{
		Style->RemoveField(TEXT("lighting"));
	}
	Style->SetStringField(TEXT("name"), Preset.IsEmpty() ? TEXT("custom") : Preset);
	UWorld* W = Inv.GetWorld();
	const TSharedPtr<FJsonObject>* Prev = Current().Find(W);
	const TSharedRef<FJsonObject> Before = Prev ? Copy(*Prev) : Copy(*Presets().Find(TEXT("realistic")));
	TWeakObjectPtr<UWorld> WeakW = W;
	auto Do = [WeakW, Style](FRuntimeCommandContext&, FString& Err) { return WeakW.IsValid() && ApplyLayers(WeakW.Get(), Style, Err); };
	auto Undo = [WeakW, Before](FRuntimeCommandContext&) { FString E; return WeakW.IsValid() && ApplyLayers(WeakW.Get(), Before, E); };
	if (!Inv.RunCommand(MakeUnique<FLambdaCommand>(TEXT("art style"), Do, Undo)))
	{
		return;
	}
	TArray<FString> Notes;
	ApplyLighting(Inv, Style, Notes);
	TSharedRef<FJsonObject> Out = Obj();
	Out->SetObjectField(TEXT("style"), Style);
	if (Notes.Num() > 0)
	{
		Out->SetStringField(TEXT("lighting_notes"), FString::Join(Notes, TEXT("; ")));
	}
	Inv.Succeed(Out);
}

static void GetStyle(FToolInvocation& Inv)
{
	TSharedRef<FJsonObject> Out = Obj();
	if (const TSharedPtr<FJsonObject>* Cur = Current().Find(Inv.GetWorld()))
	{
		Out->SetObjectField(TEXT("style"), *Cur);
	}
	else
	{
		Out->SetStringField(TEXT("style"), TEXT("none applied (engine defaults, equivalent to realistic materials)"));
	}
	Inv.Succeed(Out);
}

static void DefinePreset(FToolInvocation& Inv)
{
	const FString Name = Inv.GetString(TEXT("name"));
	TSharedRef<FJsonObject> P = Inv.Has(TEXT("based_on")) && Presets().Contains(Inv.GetString(TEXT("based_on"))) ? Copy(Presets()[Inv.GetString(TEXT("based_on"))])
		: MakeShared<FJsonObject>();
	for (const TCHAR* Section : {TEXT("material"), TEXT("post"), TEXT("settings"), TEXT("lighting")})
	{
		if (const TSharedPtr<FJsonObject> O = Inv.GetObject(Section))
		{
			TSharedRef<FJsonObject> Over = MakeShared<FJsonObject>();
			Over->SetObjectField(Section, O);
			Merge(P, Over);
		}
	}
	P->SetStringField(TEXT("description"), Inv.GetString(TEXT("description"), TEXT("Custom style")));
	Presets().Add(Name, P);
	Inv.Succeed();
}

void FStyleToolset::RegisterTools(FRuntimeToolsetBuilder& B)
{
	B.Tool(TEXT("list_presets"), TEXT("Art style presets with descriptions, and every adjustable parameter."))
		.Args(FToolSchema())
		.Handle(&ListPresets);

	B.Tool(TEXT("apply"), TEXT("Restyle the whole scene: a preset (realistic, stylized, lowpoly, toon, retro, noir, ...) and/or overrides of any layer. Undoable."))
		.Args(FToolSchema()
			.Add(TEXT("preset"), FToolParam::String(TEXT("Preset name (empty = tweak the current style)"), 64))
			.Add(TEXT("material"), FToolParam::Object(TEXT("{Faceted 0-1 flat shading, Pattern 0-1 procedural detail, Steps colour palette steps (0 off), Saturation, RimStrength, RimColor, Tint}")))
			.Add(TEXT("post"), FToolParam::Object(TEXT("{Toon 0-1, Bands, ShadowFloor, Outline 0-1, OutlineWidth px, OutlineColor, DepthSens, NormalSens, Paint 0-1, PaintRadius, PaintScale, PixelSize px, Levels, Saturation, Contrast, Tint}")))
			.Add(TEXT("settings"), FToolParam::Object(TEXT("{bloom, vignette, ao, grain, chromatic, exposure_bias, temperature}")))
			.Add(TEXT("lighting"), FToolParam::Object(TEXT("{mood, sun:{rotation, intensity, temperature, color}, sky:{intensity, color}, fog:{density, color, height_falloff}}")))
			.Add(TEXT("lighting_enabled"), FToolParam::Boolean(TEXT("Also change the lighting (default true)")).Default(true)))
		.Undoable().RequiresWorld()
		.Handle(&Apply);

	B.Tool(TEXT("get"), TEXT("The style currently applied."))
		.Args(FToolSchema())
		.RequiresWorld()
		.Handle(&GetStyle);

	B.Tool(TEXT("define_preset"), TEXT("Add or replace a named style preset (optionally based on another)."))
		.Args(FToolSchema()
			.Add(TEXT("name"), FToolParam::String(TEXT("Preset name"), 64).Required())
			.Add(TEXT("based_on"), FToolParam::String(TEXT("Start from this preset"), 64))
			.Add(TEXT("description"), FToolParam::String(TEXT("What it looks like"), 256))
			.Add(TEXT("material"), FToolParam::Object(TEXT("Material layer")))
			.Add(TEXT("post"), FToolParam::Object(TEXT("Post layer")))
			.Add(TEXT("settings"), FToolParam::Object(TEXT("Post settings")))
			.Add(TEXT("lighting"), FToolParam::Object(TEXT("Lighting"))))
		.Handle(&DefinePreset);
}
