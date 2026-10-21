#pragma once

#include "CoreMinimal.h"
#include "RuntimeToolCall.h"
#include "RuntimeToolsetDefinition.h"
#include "RuntimeSplineToolset.generated.h"

class AActor;

/** Mesh swept along a spline. */
UENUM()
enum class ERuntimeSplineProfileKind : uint8
{
	/** No mesh: an invisible path (for pcg tools or scripts). */
	None,
	/** Flat slab: roads, paths, rivers. */
	Ribbon,
	/** Upright slab: walls, hedges, barriers. */
	Wall,
	/** Round pipe. */
	Tube,
};

USTRUCT()
struct FRuntimeSplineProfile
{
	GENERATED_BODY()

	/** What to sweep along the spline. */
	UPROPERTY()
	ERuntimeSplineProfileKind Kind = ERuntimeSplineProfileKind::None;

	/** Width in cm (ribbon and wall width, tube diameter). */
	UPROPERTY()
	float Width = 400.f;

	/** Height in cm: ribbon thickness (default 8), wall height (default 300), tube lift above the path (default 0). Negative = default. */
	UPROPERTY()
	float Height = -1.f;

	/** Distance between mesh rings in cm (smaller = smoother curves). */
	UPROPERTY()
	float Step = 100.f;

	/** Raise the mesh above the path (cm), e.g. to avoid z-fighting with the ground. */
	UPROPERTY()
	float ZOffset = 2.f;

	/** Builder material fields: {"material": "asphalt", "color": [r,g,b], "roughness": 0.8, ...} (same as build.create_assembly parts). */
	UPROPERTY()
	FRuntimeToolJson Material;
};

USTRUCT()
struct FRuntimeSplineInfo
{
	GENERATED_BODY()

	/** The spline node. Pass it to sample_spline, pcg tools or scene tools. */
	UPROPERTY()
	TObjectPtr<AActor> Spline = nullptr;

	/** Length along the curve in cm. */
	UPROPERTY()
	float LengthCm = 0.f;

	/** Whether the spline is a closed loop (an area). */
	UPROPERTY()
	bool bClosed = false;
};

USTRUCT()
struct FRuntimeSplineSample
{
	GENERATED_BODY()

	/** World position. */
	UPROPERTY()
	FVector Location = FVector::ZeroVector;

	/** Unit direction of travel. */
	UPROPERTY()
	FVector Direction = FVector::ForwardVector;

	/** Yaw of the direction in degrees (rotate placed objects by this to face along the path). */
	UPROPERTY()
	float Yaw = 0.f;

	/** Distance from the start in cm. */
	UPROPERTY()
	float Distance = 0.f;
};

USTRUCT()
struct FRuntimeSplineSamples
{
	GENERATED_BODY()

	UPROPERTY()
	float LengthCm = 0.f;

	UPROPERTY()
	bool bClosed = false;

	/** Samples from the start, `spacing` apart. */
	UPROPERTY()
	TArray<FRuntimeSplineSample> Points;
};

/**
 * Splines: smooth or straight paths through points, optionally shown as a swept mesh (roads, rivers, walls, pipes).
 * Closed splines are areas for pcg.scatter (mode inside) and PCG graphs; open ones are paths.
 */
UCLASS()
class URuntimeSplineToolset : public URuntimeToolsetDefinition
{
	GENERATED_BODY()

public:
	virtual FString GetToolsetVersion() const override { return TEXT("2.0.0"); }

	/**
	 * Creates a spline through world points, optionally shown as a mesh swept along it. Undoable.
	 *
	 * @param Points - World positions [x,y,z] in cm, at least 2 (e.g. [[0,0,0],[5000,2000,0],[9000,0,0]]).
	 * @param Name - Node name.
	 * @param bClosed - Closed loop: an area (forest, field, lake shore) instead of a path.
	 * @param bLinear - Straight segments between points instead of a smooth curve (fences, walls, buildings).
	 * @param bFollowGround - Drop each point onto the ground below before building the curve.
	 * @param Profile - Mesh along the spline: {kind: ribbon|wall|tube|none, width, height, step, z_offset, material:{...}}.
	 * @param Parent - Optional parent node.
	 * @return The spline node, its length and whether it is closed.
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld, AutoCreateRefTerm = "Profile"), Category = "Spline")
	static FRuntimeSplineInfo CreateSpline(const TArray<FVector>& Points, const FRuntimeSplineProfile& Profile, const FString& Name = TEXT("Spline"),
		bool bClosed = false, bool bLinear = false, bool bFollowGround = false, AActor* Parent = nullptr);

	/**
	 * Samples points along a spline, for placing things along a path in scripts.
	 *
	 * @param Spline - The spline node.
	 * @param Spacing - Distance between samples in cm.
	 * @param Limit - Maximum number of samples returned.
	 * @return Length, closed flag and the samples (location, direction, yaw, distance).
	 */
	UFUNCTION(meta = (AICallable, RequiresWorld), Category = "Spline")
	static FRuntimeSplineSamples SampleSpline(AActor* Spline, float Spacing = 500.f, int32 Limit = 200);
};
