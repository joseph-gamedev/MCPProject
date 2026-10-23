#pragma once

#include "CoreMinimal.h"
#include "RuntimeToolsetDefinition.h"
#include "RuntimeSkyToolset.generated.h"

class AActor;
class UWorld;

namespace RuntimeSky
{
	/** The Ultra Dynamic Sky actor in use (in the level and not deleted or hidden), or null. */
	AActor* ActiveSky(UWorld* World);
}

/** The Ultra Dynamic Sky and Weather state. */
USTRUCT()
struct FRuntimeSkyState
{
	GENERATED_BODY()

	/** Whether the Ultra Dynamic Sky content is in the project (/Game/UltraDynamicSky). */
	UPROPERTY()
	bool bInstalled = false;

	/** The sky actor (node id); empty before sky.setup. */
	UPROPERTY()
	FString Sky;

	/** The weather actor (node id); empty when there is none. */
	UPROPERTY()
	FString Weather;

	/** Time of day in hours, 0-24 (6.5 = 06:30). */
	UPROPERTY()
	float Hour = 0.f;

	/** Whether time passes on its own. */
	UPROPERTY()
	bool bAnimateTime = false;

	/** Real minutes a full day (dawn to dusk) lasts when animating. */
	UPROPERTY()
	float DayLengthMinutes = 0.f;

	/** Real minutes a night lasts when animating. */
	UPROPERTY()
	float NightLengthMinutes = 0.f;

	/** volumetric | static | 2d | none | aurora | space | voxel. */
	UPROPERTY()
	FString SkyMode;

	/** Weather preset in effect (or "manual" / "random"). */
	UPROPERTY()
	FString WeatherPreset;

	/** Current weather values, 0-10 each: cloud_coverage, rain, snow, thunder, wind, fog, dust. */
	UPROPERTY()
	TMap<FString, float> Values;

	/** Moon phase 0-1 (0 and 1 new moon, 0.5 full). */
	UPROPERTY()
	float MoonPhase = 0.f;

	/** Latitude in degrees (sun path and day length). */
	UPROPERTY()
	float Latitude = 0.f;

	/** Ground and surfaces, 0-1 each: wetness, puddles, snow, dust, mud. */
	UPROPERTY()
	TMap<FString, float> Ground;

	/** Weather presets set_weather accepts. */
	UPROPERTY()
	TArray<FString> Presets;
};

/**
 * Ultra Dynamic Sky (the project's sky and weather system): time of day with a real sun and moon path, volumetric
 * clouds, weather presets with rain, snow, thunder, wind, fog and dust particles, wet / snowy / dusty surfaces, stars and
 * auroras. Follow the outdoor_lighting skill for composition.
 * Workflow: setup once (spawns the sky and weather actors and hides the level's own sun, sky light, atmosphere, clouds and
 * fog; they come back when the sky is deleted or with remove) -> set_time -> set_weather (a preset with a transition) or set_weather_values (exact amounts) -> set_sky for the
 * cloud mode, moon, stars and auroras -> get_state. set_ground: wet ground, puddles, mud, snow and dust on surfaces.
 * Weather drives cloud coverage and fog; set them with
 * set_weather_values. The lighting toolset's sun and fog tools do not apply while this sky is in use.
 */
UCLASS()
class URuntimeSkyToolset : public URuntimeToolsetDefinition
{
	GENERATED_BODY()

public:
	virtual FString GetToolsetVersion() const override { return TEXT("1.0.0"); }

	/**
	 * Puts Ultra Dynamic Sky and Weather in the level (once; later calls return the existing ones) and hides the level's
	 * own directional lights, sky light, sky atmosphere, volumetric clouds and height fog so they do not double up.
	 *
	 * @param bWeather - Also add the weather actor (rain, snow, fog, dust, wet surfaces).
	 * @param Hour - Starting time of day, 0-24, e.g. 10.5.
	 * @param Preset - Starting weather preset (see get_state presets), e.g. "partly_cloudy".
	 * @return The sky state.
	 * Fails if the Ultra Dynamic Sky content is not in the project.
	 */
	UFUNCTION(meta = (AICallable, RequiresWorld, AutoCreateRefTerm = "Hour,Preset"), Category = "Sky")
	static FRuntimeSkyState Setup(bool bWeather = true, const float& Hour = 10.f, const FString& Preset = TEXT(""));

	/**
	 * Removes Ultra Dynamic Sky and Weather and brings back the level's own sun, sky light, atmosphere, clouds and fog
	 * (also happens on its own when the sky is deleted, e.g. by scene.clear_all). Undoable.
	 *
	 * @return The sky state (empty sky).
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld), Category = "Sky")
	static FRuntimeSkyState Remove();

	/**
	 * Reads the sky: time, animation, sky mode, weather preset and values, moon, latitude, and the presets available.
	 *
	 * @return The sky state; `sky` is empty before setup.
	 */
	UFUNCTION(meta = (AICallable, RequiresWorld), Category = "Sky")
	static FRuntimeSkyState GetState();

	/**
	 * Sets the time of day, and whether and how fast time passes.
	 *
	 * @param Hour - 0-24, e.g. 6.25 sunrise light, 13 noon, 18.75 golden hour, 22 night.
	 * @param bAnimate - Let time pass on its own.
	 * @param DayLengthMinutes - Real minutes a day lasts when animating, e.g. 20.
	 * @param NightLengthMinutes - Real minutes a night lasts when animating, e.g. 8.
	 * @return The sky state.
	 * Fails before setup, or if hour is outside 0-24.
	 */
	UFUNCTION(meta = (AICallable, RequiresWorld, AutoCreateRefTerm = "bAnimate,DayLengthMinutes,NightLengthMinutes"), Category = "Sky")
	static FRuntimeSkyState SetTime(float Hour, const bool& bAnimate, const float& DayLengthMinutes, const float& NightLengthMinutes);

	/**
	 * Changes the weather to a preset with a smooth transition, or to random weather that keeps changing.
	 *
	 * @param Preset - clear | partly_cloudy | cloudy | overcast | foggy | light_rain | rain | thunderstorm | light_snow | snow |
	 *                 blizzard | dust | sandstorm | random.
	 * @param TransitionSeconds - How long the change takes, e.g. 10 (0 = instant).
	 * @return The sky state.
	 * Fails before setup (or without the weather actor) or for an unknown preset (the error lists them).
	 */
	UFUNCTION(meta = (AICallable, RequiresWorld), Category = "Sky")
	static FRuntimeSkyState SetWeather(const FString& Preset, float TransitionSeconds = 10.f);

	/**
	 * Sets exact weather amounts (0-10 each); values given stay fixed (manual override) until clear_overrides.
	 *
	 * @param CloudCoverage - 0 clear to 10 overcast.
	 * @param Rain - 0-10.
	 * @param Snow - 0-10.
	 * @param Thunder - Thunder and lightning, 0-10.
	 * @param Wind - 0-10.
	 * @param Fog - 0-10.
	 * @param Dust - 0-10.
	 * @param bClearOverrides - Release every manual value back to the preset.
	 * @return The sky state.
	 * Fails before setup (or without the weather actor) or for values outside 0-10.
	 */
	UFUNCTION(meta = (AICallable, RequiresWorld, AutoCreateRefTerm = "CloudCoverage,Rain,Snow,Thunder,Wind,Fog,Dust"), Category = "Sky")
	static FRuntimeSkyState SetWeatherValues(const float& CloudCoverage, const float& Rain, const float& Snow, const float& Thunder, const float& Wind,
		const float& Fog, const float& Dust, bool bClearOverrides = false);

	/**
	 * Sets the state of the ground and surfaces: wet ground, puddles in hollows (with rain ripples), snow and dust cover
	 * on terrain, buildings and props, and mud spreading over flat wet ground. Values given stay fixed until release;
	 * otherwise the weather drives them over time (rain wets and fills puddles, then they dry; snow settles and melts).
	 *
	 * @param Wetness - Wet, darker, shinier surfaces, 0-1.
	 * @param Puddles - Puddle coverage on the ground, 0-1 (0.3 scattered, 0.7 large pools).
	 * @param Snow - Snow cover on ground and tops of surfaces, 0-1.
	 * @param Dust - Dust / sand cover, 0-1.
	 * @param Mud - How strongly wet flat ground turns to mud, 0-2 (1 default, 0 no mud).
	 * @param bRelease - Hand every fixed value back to the weather.
	 * @return The sky state with `ground`.
	 * Fails before setup (or without the weather actor) or for values out of range.
	 */
	UFUNCTION(meta = (AICallable, RequiresWorld, AutoCreateRefTerm = "Wetness,Puddles,Snow,Dust,Mud"), Category = "Sky")
	static FRuntimeSkyState SetGround(const float& Wetness, const float& Puddles, const float& Snow, const float& Dust, const float& Mud, bool bRelease = false);

	/**
	 * Sets how the sky renders and the night sky.
	 *
	 * @param SkyMode - volumetric (3D clouds) | static (cloud texture, cheap) | 2d (panning 2D clouds) | none (no clouds) |
	 *                  aurora (volumetric aurora) | space (no atmosphere) | voxel (cubic clouds).
	 * @param MoonPhase - 0-1: 0 new moon, 0.25 first quarter, 0.5 full, 0.75 last quarter.
	 * @param bAuroras - Show auroras at night.
	 * @param StarsIntensity - Star brightness, e.g. 1 (0 hides them).
	 * @param Latitude - Degrees, -90 to 90 (sun path and day length), e.g. 40.7 for New York.
	 * @param Brightness - Overall sky and light intensity multiplier, e.g. 1.
	 * @return The sky state.
	 * Fails before setup or for an unknown sky mode.
	 */
	UFUNCTION(meta = (AICallable, RequiresWorld, AutoCreateRefTerm = "SkyMode,MoonPhase,bAuroras,StarsIntensity,Latitude,Brightness"), Category = "Sky")
	static FRuntimeSkyState SetSky(const FString& SkyMode, const float& MoonPhase, const bool& bAuroras, const float& StarsIntensity, const float& Latitude,
		const float& Brightness);
};
