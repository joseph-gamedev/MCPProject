#pragma once

#include "CoreMinimal.h"
#include "RuntimeToolCall.h"
#include "RuntimeToolsetDefinition.h"
#include "RuntimeStyleToolset.generated.h"

/** A named style preset. */
USTRUCT()
struct FRuntimeStylePresetInfo
{
	GENERATED_BODY()

	/** Preset name to pass to style.apply, e.g. "toon". */
	UPROPERTY()
	FString Name;

	/** What it looks like. */
	UPROPERTY()
	FString Description;
};

/** list_presets result. */
USTRUCT()
struct FRuntimeStylePresetList
{
	GENERATED_BODY()

	/** Presets sorted by name. */
	UPROPERTY()
	TArray<FRuntimeStylePresetInfo> Presets;

	/** Every adjustable parameter, per layer. */
	UPROPERTY()
	FString Layers;
};

/** A style (applied or defined). */
USTRUCT()
struct FRuntimeStyleInfo
{
	GENERATED_BODY()

	/** Whether a style is applied (get only; false = engine defaults). */
	UPROPERTY()
	bool bApplied = false;

	/** The style: {name, description, material:{...}, post:{...}, settings:{...}, lighting:{...}}. */
	UPROPERTY()
	FRuntimeToolJson Style;

	/** Problems while applying the lighting layer (missing sun or fog, ...), empty if none. */
	UPROPERTY()
	FString LightingNotes;
};

/**
 * Art style: one call restyles the whole scene (realistic, stylized, lowpoly, toon, retro, noir, ...).
 * A style drives three layers: global material switches for every builder material (MPC_RT_Style: flat shading,
 * procedural detail, colour steps, saturation, tint, rim light), a style post-process (M_PP_Style: toon bands, outlines,
 * painterly filter, pixelation, posterize, grading) plus post settings, and lighting (mood, sun, sky, fog).
 * Workflow: list_presets -> apply a preset -> screenshot -> apply again with only the layer to tweak -> define_preset to keep it.
 * Presets are data: built in, overridable/extendable from Config/RuntimeToolsets/StylePresets.json or define_preset.
 * See the art_style skill.
 */
UCLASS()
class URuntimeStyleToolset : public URuntimeToolsetDefinition
{
	GENERATED_BODY()

public:
	virtual FString GetToolsetVersion() const override { return TEXT("2.0.0"); }

	/**
	 * Lists the art style presets with descriptions, and every adjustable parameter per layer.
	 *
	 * @return Presets sorted by name, and the layer parameter list.
	 */
	UFUNCTION(meta = (AICallable), Category = "Style")
	static FRuntimeStylePresetList ListPresets();

	/**
	 * Restyles the whole scene: a preset and/or overrides of any layer, merged key by key. Without a preset, overrides
	 * tweak the style already applied. The material/post layer is one undo step; the lighting layer adds lighting undo steps.
	 *
	 * @param Preset - Preset name, e.g. "toon"; empty = tweak the current style.
	 * @param Material - {Faceted 0-1 flat shading, Pattern 0-1 procedural detail, Steps colour palette steps (0 off), Saturation, RimStrength, RimColor, Tint}, e.g. {"Saturation": 1.2}.
	 * @param Post - {Toon 0-1, Bands, ShadowFloor, Outline 0-1, OutlineWidth px, OutlineColor, DepthSens, NormalSens, Paint 0-1, PaintRadius, PaintScale, PixelSize px, Levels, Saturation, Contrast, Tint}, e.g. {"Outline": 0.6}.
	 * @param Settings - {bloom, vignette, ao, grain, chromatic, exposure_bias, temperature}.
	 * @param Lighting - {mood, sun:{rotation, intensity, temperature, color}, sky:{intensity, color}, fog:{density, color, height_falloff}}.
	 * @param bLightingEnabled - Also change the lighting.
	 * @return The applied style and any lighting notes.
	 * Fails if the preset is unknown (the hint lists the presets) or the style assets are missing.
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld, AutoCreateRefTerm = "Material,Post,Settings,Lighting"), Category = "Style")
	static FRuntimeStyleInfo Apply(const FRuntimeToolJson& Material, const FRuntimeToolJson& Post, const FRuntimeToolJson& Settings, const FRuntimeToolJson& Lighting,
		const FString& Preset = TEXT(""), bool bLightingEnabled = true);

	/**
	 * The style currently applied.
	 *
	 * @return applied = false (and a note in style) when no style was applied, otherwise the style object.
	 */
	UFUNCTION(meta = (AICallable, RequiresWorld), Category = "Style")
	static FRuntimeStyleInfo Get();

	/**
	 * Adds or replaces a named style preset, optionally starting from another preset. Lasts until the game restarts
	 * (put permanent presets in Config/RuntimeToolsets/StylePresets.json).
	 *
	 * @param Name - Preset name, e.g. "comic".
	 * @param BasedOn - Start from this preset, e.g. "toon".
	 * @param Description - What it looks like.
	 * @param Material - Material layer overrides.
	 * @param Post - Post layer overrides, e.g. {"OutlineWidth": 3}.
	 * @param Settings - Post settings overrides.
	 * @param Lighting - Lighting overrides.
	 * @return The new preset.
	 * Fails if the name is empty or based_on is not a preset.
	 */
	UFUNCTION(meta = (AICallable, AutoCreateRefTerm = "Material,Post,Settings,Lighting"), Category = "Style")
	static FRuntimeStyleInfo DefinePreset(const FString& Name, const FRuntimeToolJson& Material, const FRuntimeToolJson& Post, const FRuntimeToolJson& Settings,
		const FRuntimeToolJson& Lighting, const FString& BasedOn = TEXT(""), const FString& Description = TEXT("Custom style"));
};
