#pragma once

#include "CoreMinimal.h"
#include "RuntimeToolsetDefinition.h"
#include "RuntimeFluidToolset.generated.h"

/** One fluid simulation in the level. */
USTRUCT()
struct FRuntimeFluidInfo
{
	GENERATED_BODY()

	/** The simulation (node id). */
	UPROPERTY()
	FString Id;

	/** The water surface it drives (node id); empty for presets that draw on their own plane. */
	UPROPERTY()
	FString Surface;

	/** Preset it was made from. */
	UPROPERTY()
	FString Preset;

	/** Centre of the simulated area (cm). */
	UPROPERTY()
	FVector Location = FVector::ZeroVector;

	/** Simulated area, metres [x, y]. */
	UPROPERTY()
	FVector2D SizeMeters = FVector2D::ZeroVector;

	/** Simulation grid, cells per side. */
	UPROPERTY()
	int32 Resolution = 0;

	/** Output material on the surface. */
	UPROPERTY()
	FString Material;
};

/** A flow source or ambient effect that was placed. */
USTRUCT()
struct FRuntimeFluidPart
{
	GENERATED_BODY()

	/** The node (id). */
	UPROPERTY()
	FString Id;

	UPROPERTY()
	FString Kind;

	/** Niagara system it runs. */
	UPROPERTY()
	FString System;
};

/** A simulation's tunable settings and its surface look. */
USTRUCT()
struct FRuntimeFluidSettings
{
	GENERATED_BODY()

	UPROPERTY()
	FString Id;

	/** Simulation behaviour, name -> value (the main ones; fluid.set_settings accepts any numeric or on/off setting by name). */
	UPROPERTY()
	TMap<FString, FString> Simulation;

	/** Surface look: the output materials' parameters, name -> value (numbers, or colours as (R=,G=,B=,A=)). */
	UPROPERTY()
	TMap<FString, FString> Surface;

	/** The output materials the surface values belong to. */
	UPROPERTY()
	TArray<FString> SurfaceMaterials;
};

/** Objects set floating. */
USTRUCT()
struct FRuntimeFluidFloatResult
{
	GENERATED_BODY()

	/** Ids now floating. */
	UPROPERTY()
	TArray<FString> Floating;

	/** Water surface height used (cm). */
	UPROPERTY()
	float WaterLevel = 0.f;
};

/** The fluid simulations in the level and the presets available. */
USTRUCT()
struct FRuntimeFluidList
{
	GENERATED_BODY()

	/** Whether the FluidNinja LIVE content is in the project (/Game/FluidNinjaLive). */
	UPROPERTY()
	bool bInstalled = false;

	UPROPERTY()
	TArray<FRuntimeFluidInfo> Fluids;

	/** Preset names fluid.add accepts (water). */
	UPROPERTY()
	TArray<FString> Presets;

	/** Effects fluid.add accepts, "name: what it is" (fire, smoke, mist, mud, snow / sand trails, lava ...). */
	UPROPERTY()
	TArray<FString> Effects;

	/** Flow sources fluid.add_source accepts (they push and feed nearby simulations). */
	UPROPERTY()
	TArray<FString> Sources;

	/** Ambient particle effects fluid.ambient accepts (no simulation needed). */
	UPROPERTY()
	TArray<FString> Ambient;

	/** Water materials (short names) the `material` argument accepts, e.g. "MI_Water_Quiet". */
	UPROPERTY()
	TArray<FString> WaterMaterials;
};

/**
 * Real-time fluid simulation (FluidNinja LIVE) on a 2D GPU grid: water (lakes, pools, seas) that ripples and wakes around
 * anything moving through it; fire (bonfire, campfire, candle, flamethrower), smoke (columns, heavy smoke, explosions),
 * fog / mist banks, mud and blood, snow and sand that keep tracks, drifting snow and sand, lava; plus flow sources (hoses,
 * jets, vortices, emitters) that push and feed simulations, and ambient particles (blizzard, sandstorm, rain, sparks).
 * Characters, vehicles and objects that generate overlap events disturb a simulation anywhere over its area (built
 * meshes do not by default); the fluid does not push objects back. Fog / mist turns on the level's volumetric fog.
 * Workflow: list (presets, effects, sources, ambient, what exists) -> add (a preset or effect at a place; size, yaw) ->
 * add_source to feed or stir it -> ambient for atmosphere -> screenshot -> remove. Pairs with sky.set_weather and
 * sky.set_ground.
 */
UCLASS()
class URuntimeFluidToolset : public URuntimeToolsetDefinition
{
	GENERATED_BODY()

public:
	virtual FString GetToolsetVersion() const override { return TEXT("1.0.0"); }

	/**
	 * Lists the fluid simulations in the level, the presets and the water materials.
	 *
	 * @return Simulations, presets and materials; `installed` false when the FluidNinja content is missing.
	 */
	UFUNCTION(meta = (AICallable, RequiresWorld), Category = "Fluid")
	static FRuntimeFluidList List();

	/**
	 * Adds a fluid simulation from a preset: the simulation plus the water surface it drives. Undoable.
	 *
	 * @param Preset - an effect: bonfire | fire | candle | smoke_column | heavy_smoke | explosion | flamethrower | magic | mist |
	 *                 mud | blood | snow_trails | snow_drift | sand_trails | sandstorm | lava (see list `effects`);
	 *                 shore water: coast_water (waves and whitewater rolling onto a beach; its sea level is always z = -480:
	 *                 build the ground with sea_level -480; reads the ground's height itself) | coast_sand (wet sand with footprints on the ground under it);
	 *                 or water: lake | pool | sea | sea_wavy | sea_200m | sea_wavy_200m | river | river_whitewater | river_whitewater_inverse |
	 *                 water_tank | tracks | default (or a preset asset path). Lakes, pools and seas look right anywhere (put ground
	 *                 below them, e.g. a terrain basin); the river presets are made to follow a spline over a landscape and
	 *                 look like a sandy bed with streaks on open ground.
	 * @param Location - Centre of the area on the water level (cm), e.g. [0, 0, 20].
	 * @param SizeMeters - Side of the simulated square in metres, e.g. 80 (the preset's own size when omitted).
	 * @param Resolution - Grid cells per side, 256-2048 (higher = finer ripples, more GPU), e.g. 1024.
	 * @param Material - Water material, a short name from list `water_materials` (e.g. "MI_Water_Quiet") or a path.
	 * @param Name - Node name, e.g. "Lake".
	 * @param Yaw - Turn the whole effect around its vertical axis, degrees (fire and smoke face the camera anyway).
	 * @param bFollowPlayer - The simulation travels with the player's character (a beach, sea or sand area that is always
	 *                        around them, like the pack's coast demo); create the character first.
	 * @param Ground - How the water reads the ground under it (shallows, shore waves): auto (a Landscape when the level has one,
	 *                 otherwise a top-down capture of whatever is there), landscape, capture, or flat.
	 * @return The new simulation.
	 * Fails if FluidNinja is not in the project, the preset or material is unknown, or a value is out of range.
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld, AutoCreateRefTerm = "Location,SizeMeters,Resolution,Material,Name,Yaw"), Category = "Fluid")
	static FRuntimeFluidInfo Add(const FString& Preset, const FVector& Location, const float& SizeMeters, const int32& Resolution, const FString& Material,
		const FString& Name, const float& Yaw, bool bFollowPlayer = false, const FString& Ground = TEXT("auto"));

	/**
	 * Places a flow source: it pushes fluid and feeds density into the simulations it is inside (or near). Undoable.
	 *
	 * @param Kind - water_hose | water_hose_mini | jet | vortex | avalanche | bonfire | smoke | smoke_light | burst | burst_radial |
	 *               radial | linear | turret | cauldron | grid.
	 * @param Location - Where (cm), e.g. [0, 0, 50].
	 * @param Rotation - Direction it points, [pitch, yaw, roll], e.g. [0, 90, 0].
	 * @return The source.
	 * Fails if FluidNinja is not in the project or the kind is unknown.
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld, AutoCreateRefTerm = "Location,Rotation"), Category = "Fluid")
	static FRuntimeFluidPart AddSource(const FString& Kind, const FVector& Location, const FRotator& Rotation);

	/**
	 * Places an ambient particle effect (no simulation): weather and atmosphere around a spot. Undoable.
	 *
	 * @param Kind - blizzard | sandstorm | rain | rain_translucent | sparks | sparks_burning | magic | plankton | plankton_soft.
	 * @param Location - Where (cm), e.g. [0, 0, 200].
	 * @param Scale - Size multiplier, e.g. 1.
	 * @return The effect.
	 * Fails if FluidNinja is not in the project or the kind is unknown.
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld, AutoCreateRefTerm = "Location,Scale"), Category = "Fluid")
	static FRuntimeFluidPart Ambient(const FString& Kind, const FVector& Location, const float& Scale);

	/**
	 * Removes a fluid simulation and its surface. Undoable.
	 *
	 * @param Id - The simulation (node id from add or list).
	 * @return The list after the removal.
	 * Fails if the id is not a fluid simulation.
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld), Category = "Fluid")
	static FRuntimeFluidList Remove(const FString& Id);

	/**
	 * A simulation's tunable settings and surface look, with current values.
	 * Main simulation settings: SimSpeed (how fast it moves; lower = thicker, slower), BrushStrength (how hard things push),
	 * GlobalBrushSize, SkeletalMeshBrushSize (characters), PrimitiveObjBrushSize (objects), VehicleBrushSize, BrushHardness
	 * (soft 0 .. sharp 1 marks), DensityAccumulation (how fast it fills back in), PersistencyOfFieldAndPointData (how long
	 * marks last), VelocityFeedback (how long motion carries on, 0..1), VelocityClamp, PressureFeedback, VeloFromBrushMotion
	 * (how much movement drags it along), DensityNoiseAmount and BrushDensityNoise (irregularity).
	 *
	 * @param Id - The simulation (id from list / add), e.g. "n4".
	 * @return Settings and surface parameters.
	 * Fails if there is no such simulation.
	 */
	UFUNCTION(meta = (AICallable, RequiresWorld), Category = "Fluid")
	static FRuntimeFluidSettings GetSettings(const FString& Id);

	/**
	 * Makes objects float on water: boats, barrels, crates, debris bob at the surface, drift and settle (turns their physics on).
	 *
	 * @param Targets - Objects to float, e.g. ["n12", "n13"].
	 * @param WaterLevel - Water surface height (cm); default: the nearest water simulation's surface.
	 * @param Buoyancy - Lift when fully under water, in multiples of the object's weight (1.2 rides low, 3 rides high).
	 * @param Drag - Water drag (0.3 drifts freely, 3 calms quickly).
	 * @return The floating objects and the water level.
	 * Fails if a target has no collision (physics needs it).
	 */
	UFUNCTION(meta = (AICallable, RequiresWorld, AutoCreateRefTerm = "WaterLevel"), Category = "Fluid")
	static FRuntimeFluidFloatResult Float(const TArray<AActor*>& Targets, const float& WaterLevel, float Buoyancy = 2.f, float Drag = 1.f);

	/**
	 * Tunes a simulation live: behaviour settings and its surface look (see get_settings for names and current values).
	 *
	 * @param Id - The simulation, e.g. "n4".
	 * @param Simulation - Behaviour settings by name, e.g. {"SimSpeed": 2, "BrushStrength": 0.5}.
	 * @param Surface - Surface material numbers by name, e.g. {"Roughness": 0.3}.
	 * @param Colors - Surface material colours by name, as hex, e.g. {"Color": "#4a3222"}.
	 * @return The settings after the change.
	 * Fails if there is no such simulation or a name is unknown.
	 */
	UFUNCTION(meta = (AICallable, RequiresWorld, AutoCreateRefTerm = "Simulation,Surface,Colors"), Category = "Fluid")
	static FRuntimeFluidSettings SetSettings(const FString& Id, const TMap<FString, float>& Simulation, const TMap<FString, float>& Surface,
		const TMap<FString, FString>& Colors);
};
