#pragma once

#include "CoreMinimal.h"
#include "RuntimeToolsetDefinition.h"
#include "RuntimeLightingToolset.generated.h"

class AActor;

/** Directional light (sun). */
/** The level lighting parts set aside (or brought back). */
USTRUCT()
struct FRuntimeEnvironmentHidden
{
	GENERATED_BODY()

	/** Actors changed. */
	UPROPERTY()
	TArray<FString> Actors;

	UPROPERTY()
	bool bHidden = true;
};

USTRUCT()
struct FRuntimeSunInfo
{
	GENERATED_BODY()

	/** The light (node id or actor:Name); empty when the level has no directional light. */
	UPROPERTY()
	FString Actor;

	/** [pitch, yaw, roll]: pitch 0 = horizon, -90 = overhead; yaw 0 = north, 180 = south. */
	UPROPERTY()
	FRotator Rotation = FRotator::ZeroRotator;

	/** Elevation above the horizon in degrees (= -pitch). */
	UPROPERTY()
	float Elevation = 0.f;

	/** Intensity in lux (direct sun 80000-120000). */
	UPROPERTY()
	float Intensity = 0.f;

	UPROPERTY()
	FLinearColor Color = FLinearColor::White;

	/** Kelvin, used when bUseTemperature. */
	UPROPERTY()
	float Temperature = 6500.f;

	UPROPERTY()
	bool bUseTemperature = false;

	/** Angular diameter of the sun disc in degrees (soft shadows: larger). */
	UPROPERTY()
	float SourceAngle = 0.f;

	/** Soft source angle in degrees (overcast: 40-70). */
	UPROPERTY()
	float SoftSourceAngle = 0.f;

	/** Whether volumetric clouds cast shadows from this light. */
	UPROPERTY()
	bool bCastCloudShadows = false;
};

/** Sky light (ambient fill). */
USTRUCT()
struct FRuntimeSkyLightInfo
{
	GENERATED_BODY()

	/** Empty when the level has no sky light. */
	UPROPERTY()
	FString Actor;

	UPROPERTY()
	float Intensity = 0.f;

	UPROPERTY()
	FLinearColor Color = FLinearColor::White;

	/** Real-time capture follows sky changes; otherwise the tools recapture after changes. */
	UPROPERTY()
	bool bRealtimeCapture = false;
};

/** Exponential height fog. */
USTRUCT()
struct FRuntimeFogInfo
{
	GENERATED_BODY()

	/** Empty when the level has no height fog. */
	UPROPERTY()
	FString Actor;

	UPROPERTY()
	float Density = 0.f;

	UPROPERTY()
	float HeightFalloff = 0.f;

	/** Distance from the camera where fog starts, cm. */
	UPROPERTY()
	float StartDistance = 0.f;

	/** Inscattering color. */
	UPROPERTY()
	FLinearColor Color = FLinearColor::White;

	UPROPERTY()
	bool bVolumetric = false;
};

/** Sky atmosphere. */
USTRUCT()
struct FRuntimeAtmosphereInfo
{
	GENERATED_BODY()

	/** Empty when the level has no sky atmosphere. */
	UPROPERTY()
	FString Actor;

	/** Rayleigh scattering colour (clear sky about [0.175, 0.41, 1.0]). */
	UPROPERTY()
	FLinearColor RayleighScattering = FLinearColor::Black;

	UPROPERTY()
	float RayleighScatteringScale = 0.f;

	/** Mie (haze) scattering scale; overcast 0.01-0.05. */
	UPROPERTY()
	float MieScatteringScale = 0.f;

	UPROPERTY()
	float MieAnisotropy = 0.f;

	UPROPERTY()
	float MultiScatteringFactor = 0.f;
};

/** Volumetric cloud layer. */
USTRUCT()
struct FRuntimeCloudInfo
{
	GENERATED_BODY()

	/** Empty when the level has no volumetric cloud. */
	UPROPERTY()
	FString Actor;

	/** Bottom of the cloud layer in km. */
	UPROPERTY()
	float BottomAltitudeKm = 0.f;

	/** Thickness of the layer in km. */
	UPROPERTY()
	float LayerHeightKm = 0.f;

	/** How much clouds block sky light from below (0-1). */
	UPROPERTY()
	float SkyLightOcclusion = 0.f;

	/** Whether the sun casts cloud shadows (directional light setting). */
	UPROPERTY()
	bool bCastShadows = false;

	/** Cloud shadow strength on surfaces (directional light setting). */
	UPROPERTY()
	float ShadowStrength = 0.f;

	/** Cloud material path. Engine content (/Engine/...) is shared: changes go to a dynamic instance on this component. */
	UPROPERTY()
	FString Material;

	/** Name of the material's coverage parameter, empty if it has none. */
	UPROPERTY()
	FString CoverageParameter;

	/** Global coverage (-1 clear .. 1 overcast; scattered clouds -0.2..0). Only valid when CoverageParameter is set. */
	UPROPERTY()
	float Coverage = 0.f;

	/** Name of the material's density parameter, empty if it has none. */
	UPROPERTY()
	FString DensityParameter;

	/** Global density (light clouds ~0.008, overcast 0.02-0.04). Only valid when DensityParameter is set. */
	UPROPERTY()
	float Density = 0.f;
};

/** How exposure behaves (decides the right exposure bias range). */
UENUM()
enum class ERuntimeExposureMode : uint8
{
	/** Auto exposure with min and max brightness set: bias is a small offset (-2..+2 EV). */
	Auto,
	/** Manual exposure, or no min/max: physical lux needs a large negative bias (sun -10..-14 EV). */
	Manual,
	/** No post process volume overrides: project defaults apply (usually auto exposure without limits). */
	ProjectDefault,
};

/** Global (unbound) post process volume. */
USTRUCT()
struct FRuntimePostProcessInfo
{
	GENERATED_BODY()

	/** Empty when the level has no unbound post process volume. */
	UPROPERTY()
	FString Actor;

	UPROPERTY()
	ERuntimeExposureMode ExposureMode = ERuntimeExposureMode::ProjectDefault;

	/** Exposure compensation in EV. */
	UPROPERTY()
	float ExposureBias = 0.f;

	/** Auto exposure minimum brightness (EV100). */
	UPROPERTY()
	float ExposureMin = 0.f;

	/** Auto exposure maximum brightness (EV100). */
	UPROPERTY()
	float ExposureMax = 0.f;

	UPROPERTY()
	float Bloom = 0.f;

	UPROPERTY()
	float Saturation = 1.f;

	UPROPERTY()
	float Contrast = 1.f;

	UPROPERTY()
	float Vignette = 0.f;

	/** White balance temperature in Kelvin. */
	UPROPERTY()
	float WhiteBalance = 6500.f;

	/** Settings this volume overrides (others use project defaults), e.g. ["exposure_bias", "bloom"]. */
	UPROPERTY()
	TArray<FString> Overridden;

	/** Whether set_post_process created the volume. */
	UPROPERTY()
	bool bCreatedVolume = false;
};

/** The whole outdoor lighting setup. Parts the level lacks have an empty `actor`. */
USTRUCT()
struct FRuntimeEnvironment
{
	GENERATED_BODY()

	UPROPERTY()
	FRuntimeSunInfo Sun;

	UPROPERTY()
	FRuntimeSkyLightInfo SkyLight;

	UPROPERTY()
	FRuntimeAtmosphereInfo Atmosphere;

	UPROPERTY()
	FRuntimeCloudInfo Clouds;

	UPROPERTY()
	FRuntimeFogInfo Fog;

	UPROPERTY()
	FRuntimePostProcessInfo PostProcess;
};

/** Lighting moods (one-step looks). */
UENUM()
enum class ERuntimeLightingMood : uint8
{
	Noon,
	Morning,
	Sunset,
	Night,
	Overcast,
	Foggy,
};

/** Result of apply_mood. */
USTRUCT()
struct FRuntimeMoodResult
{
	GENERATED_BODY()

	UPROPERTY()
	ERuntimeLightingMood Mood = ERuntimeLightingMood::Noon;

	/** Parts that changed: sun, fog, clouds, post_process. */
	UPROPERTY()
	TArray<FString> Changed;
};

/** Result of set_time_of_day. */
USTRUCT()
struct FRuntimeTimeOfDay
{
	GENERATED_BODY()

	UPROPERTY()
	float Hour = 12.f;

	/** The sun after the change. */
	UPROPERTY()
	FRuntimeSunInfo Sun;
};

/**
 * Outdoor lighting at runtime: sun (directional light), sky light, sky atmosphere, volumetric clouds, height fog and the
 * global post process (exposure, grading). Follow the outdoor_lighting skill.
 * Workflow: get_environment (what exists, exposure mode, cloud coverage) -> set_sun or set_time_of_day first (it drives
 * the sky) -> set_clouds / set_atmosphere -> set_fog -> set_post_process for exposure -> screenshot and refine.
 * apply_mood is a one-step starting point. Missing parts: spawn them with actors.spawn_actor (SkyAtmosphere,
 * VolumetricCloud, SkyLight, ExponentialHeightFog, DirectionalLight). Arguments left out keep their current value.
 */
UCLASS()
class URuntimeLightingToolset : public URuntimeToolsetDefinition
{
	GENERATED_BODY()

public:
	virtual FString GetToolsetVersion() const override { return TEXT("2.0.0"); }

	/**
	 * Reads the whole lighting setup: sun, sky light, atmosphere, clouds (with material coverage/density), fog and post
	 * process (with the exposure mode, which decides the exposure bias range). Read it before changing lighting.
	 *
	 * @return One section per part; a part the level lacks has an empty `actor`.
	 */
	UFUNCTION(meta = (AICallable, RequiresWorld), Category = "Lighting")
	static FRuntimeEnvironment GetEnvironment();

	/**
	 * Sets the sun's rotation, intensity, colour, temperature and shadow softness. Undoable.
	 *
	 * @param Rotation - [pitch, yaw, roll]: pitch 0 = horizon, -90 = overhead, e.g. [-35, 120, 0].
	 * @param Intensity - Physical lux: 100000 noon sun, 20000 to 40000 low sun, 2000 to 8000 sunset, under 1 moonlight.
	 *                    The camera exposure is locked to match, so the frame never blows out; fine-tune with set_post_process exposure_bias.
	 * @param Color - Light colour [r, g, b], 0-1.
	 * @param Temperature - Kelvin (1700-12000), e.g. 6000; enables temperature.
	 * @param SourceAngle - Sun disc angle in degrees (2-5 overcast, 15-30 when the disc is hidden).
	 * @param SoftSourceAngle - Soft angle in degrees (overcast 40-70).
	 * @param Target - Directional light; the level's first one when omitted.
	 * @return The sun after the change.
	 * Fails if the level has no directional light or the target is not one.
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld, AutoCreateRefTerm = "Rotation,Intensity,Color,Temperature,SourceAngle,SoftSourceAngle"), Category = "Lighting")
	static FRuntimeSunInfo SetSun(const FRotator& Rotation, const float& Intensity, const FLinearColor& Color, const float& Temperature, const float& SourceAngle,
		const float& SoftSourceAngle, AActor* Target = nullptr);

	/**
	 * Sets the sky light's intensity and colour (recaptures when not real-time). Undoable.
	 *
	 * @param Intensity - Multiplier, e.g. 1.2.
	 * @param Color - [r, g, b], 0-1.
	 * @return The sky light after the change.
	 * Fails if the level has no sky light.
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld, AutoCreateRefTerm = "Intensity,Color"), Category = "Lighting")
	static FRuntimeSkyLightInfo SetSky(const float& Intensity, const FLinearColor& Color);

	/**
	 * Sets exponential height fog. Undoable.
	 *
	 * @param Density - Fog density (0-10), e.g. 0.02; sunset haze 0.000002-0.00002.
	 * @param HeightFalloff - Height falloff (0-10), e.g. 0.2.
	 * @param StartDistance - Distance in cm where fog starts, e.g. 3000.
	 * @param Color - Inscattering colour [r, g, b].
	 * @param bVolumetric - Volumetric fog on/off.
	 * @return The fog after the change.
	 * Fails if the level has no height fog or a value is out of range.
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld, AutoCreateRefTerm = "Density,HeightFalloff,StartDistance,Color,bVolumetric"), Category = "Lighting")
	static FRuntimeFogInfo SetFog(const float& Density, const float& HeightFalloff, const float& StartDistance, const FLinearColor& Color, const bool& bVolumetric);

	/**
	 * Sets the sky atmosphere's scattering (sky colour and haze). Undoable.
	 *
	 * @param RayleighScattering - Rayleigh colour [r, g, b]: clear [0.175, 0.41, 1.0], overcast about [0.45, 0.56, 0.75].
	 * @param RayleighScatteringScale - Rayleigh scale, default 0.0331.
	 * @param MieScatteringScale - Haze; clear 0.004, overcast 0.01-0.05.
	 * @param MieAnisotropy - Forward scattering of haze (0-0.999), default 0.8.
	 * @param MultiScatteringFactor - 0-2, default 1.
	 * @return The atmosphere after the change.
	 * Fails if the level has no sky atmosphere.
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld, AutoCreateRefTerm = "RayleighScattering,RayleighScatteringScale,MieScatteringScale,MieAnisotropy,MultiScatteringFactor"), Category = "Lighting")
	static FRuntimeAtmosphereInfo SetAtmosphere(const FLinearColor& RayleighScattering, const float& RayleighScatteringScale, const float& MieScatteringScale,
		const float& MieAnisotropy, const float& MultiScatteringFactor);

	/**
	 * Sets the volumetric cloud layer, cloud shadows and (through its material) coverage and density. Coverage and density
	 * go to a dynamic instance of the cloud material on this component, so engine assets are never modified. Undoable.
	 *
	 * @param Coverage - Global coverage -1 (clear) .. 1 (overcast); scattered clouds -0.2..0, overcast 0.5-0.75.
	 * @param Density - Global density; light 0.007-0.01, overcast 0.02-0.04.
	 * @param BottomAltitudeKm - Cloud base in km; 3-5 scattered, 0.5-2 overcast.
	 * @param LayerHeightKm - Layer thickness in km; 5-8 scattered, 8-14 overcast.
	 * @param SkyLightOcclusion - 0-1; overcast 0.75-0.9.
	 * @param bCastShadows - Cloud shadows from the sun (directional light setting).
	 * @param ShadowStrength - Cloud shadow strength on surfaces, 0-1 (overcast 0.8-1).
	 * @return The clouds after the change.
	 * Fails if the level has no volumetric cloud, or coverage/density is given but the cloud material has no such parameter
	 * (the error lists its scalar parameters).
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld, AutoCreateRefTerm = "Coverage,Density,BottomAltitudeKm,LayerHeightKm,SkyLightOcclusion,bCastShadows,ShadowStrength"), Category = "Lighting")
	static FRuntimeCloudInfo SetClouds(const float& Coverage, const float& Density, const float& BottomAltitudeKm, const float& LayerHeightKm,
		const float& SkyLightOcclusion, const bool& bCastShadows, const float& ShadowStrength);

	/**
	 * Sets the global post process: exposure (bias and auto exposure range), bloom and grading. Creates an unbound volume
	 * when the level has none. Undoable.
	 *
	 * @param ExposureBias - Exposure compensation in EV (-15..15). Auto exposure: -2..2; manual with physical lux: down to -14.
	 * @param ExposureMin - Auto exposure minimum brightness (EV100); with exposure_max it makes the exposure mode Auto.
	 * @param ExposureMax - Auto exposure maximum brightness (EV100).
	 * @param Bloom - Bloom intensity (0-8).
	 * @param Saturation - Saturation, 1 = neutral (0-2).
	 * @param Contrast - Contrast, 1 = neutral (0-2).
	 * @param Vignette - Vignette intensity (0-1).
	 * @param WhiteBalance - White balance temperature in Kelvin (1500-15000).
	 * @return The post process after the change, with the exposure mode.
	 * Fails if a value is out of range.
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld, AutoCreateRefTerm = "ExposureBias,ExposureMin,ExposureMax,Bloom,Saturation,Contrast,Vignette,WhiteBalance"), Category = "Lighting")
	static FRuntimePostProcessInfo SetPostProcess(const float& ExposureBias, const float& ExposureMin, const float& ExposureMax, const float& Bloom,
		const float& Saturation, const float& Contrast, const float& Vignette, const float& WhiteBalance);

	/**
	 * Places the sun for a clock hour on a sinusoidal day: 6 sunrise, 12 noon (overhead), 18 sunset, night below the horizon. Undoable.
	 *
	 * @param Hour - 0-24, e.g. 9.5.
	 * @param Yaw - Sun heading in degrees (default: keep), e.g. 120.
	 * @param Target - Directional light; the level's first one when omitted.
	 * @return The hour and the sun after the change.
	 * Fails if the hour is outside 0-24 or there is no directional light.
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld, AutoCreateRefTerm = "Yaw"), Category = "Lighting")
	static FRuntimeTimeOfDay SetTimeOfDay(float Hour, const float& Yaw, AActor* Target = nullptr);

	/**
	 * One-step look for the sun, fog, clouds and grading, as one undo step. A starting point: refine with the other tools.
	 *
	 * @param Mood - Noon, Morning, Sunset, Night, Overcast or Foggy. Each sets physical sun and sky light, a matching locked exposure,
	 *               fog, grading, ambient occlusion, contact shadows and reflections: the realistic starting point for outdoor scenes.
	 * @return The mood and the parts that changed (parts the level lacks are skipped).
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld), Category = "Lighting")
	static FRuntimeMoodResult ApplyMood(ERuntimeLightingMood Mood);

	/**
	 * Sets aside the level's own sun, sky light, atmosphere, clouds, fog and post process (the ones that came with the map),
	 * so a scene can bring its own lighting; tool-made ones are kept. Call again with hidden false to bring them back.
	 *
	 * @param bHidden - true sets them aside, false brings them back.
	 * @return The actors changed.
	 */
	UFUNCTION(meta = (AICallable, RequiresWorld), Category = "Lighting")
	static FRuntimeEnvironmentHidden HideEnvironment(bool bHidden = true);
};
