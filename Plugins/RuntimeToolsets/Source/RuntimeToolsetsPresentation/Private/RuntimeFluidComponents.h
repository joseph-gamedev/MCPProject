#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "RuntimeFluidComponents.generated.h"

/** Keeps a physics object afloat at a water level: lift by how deep it sits, water drag, and a gentle bob. */
UCLASS(ClassGroup = RuntimeToolsets)
class URuntimeFluidFloat : public UActorComponent
{
	GENERATED_BODY()

public:
	URuntimeFluidFloat();

	/** Water surface height (cm, world). */
	UPROPERTY()
	float WaterLevel = 0.f;

	/** Lift when fully under water, in multiples of its weight (above 1 it floats; higher rides higher). */
	UPROPERTY()
	float Buoyancy = 2.f;

	/** Water drag (slows drifting and spinning while in the water). */
	UPROPERTY()
	float Drag = 1.f;

	/** Wave bob height (cm). */
	UPROPERTY()
	float Bob = 12.f;

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	float Phase = 0.f;
};

/**
 * A top-down depth camera that hands a fluid simulation the height of the ground under it (FluidNinja's external height
 * data), so shore waves, shallows and downhill flow work on any geometry, not only Landscapes. It travels with the simulation
 * and leaves the water itself, its effects and characters out of the picture.
 */
UCLASS(ClassGroup = RuntimeToolsets)
class URuntimeFluidHeightCapture : public USceneCaptureComponent2D
{
	GENERATED_BODY()

public:
	URuntimeFluidHeightCapture();

	/** The simulation's NinjaLive component. */
	UPROPERTY()
	TObjectPtr<UActorComponent> Sim;

	/** Seconds between captures while it moves. */
	UPROPERTY()
	float Interval = 0.2f;

	/** Re-captures now (also tells the simulation the camera height). */
	void Refresh();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	float Since = 0.f;
	FVector Last = FVector(TNumericLimits<float>::Max());
	FQuat LastRot = FQuat::Identity;
};

/**
 * Hands a trail simulation's footprints (its painter texture) to the builder terrain under it: the terrain material darkens
 * and wets the sand / snow where things walked, following the simulation as it moves. No separate surface needed.
 */
UCLASS(ClassGroup = RuntimeToolsets)
class URuntimeFluidTrailFeed : public UActorComponent
{
	GENERATED_BODY()

public:
	URuntimeFluidTrailFeed();

	/** The simulation's NinjaLive component. */
	UPROPERTY()
	TObjectPtr<UActorComponent> Sim;

	/** Simulation size (cm). */
	UPROPERTY()
	FVector2D Size = FVector2D(3000.0);

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	/** Terrain materials that take trails (builder terrain), found in the level. */
	static void FindTerrainMaterials(UWorld* World, TArray<class UMaterialInstanceDynamic*>& Out);

private:
	TArray<TWeakObjectPtr<class UMaterialInstanceDynamic>> Targets;
	float Since = 10.f;
};
