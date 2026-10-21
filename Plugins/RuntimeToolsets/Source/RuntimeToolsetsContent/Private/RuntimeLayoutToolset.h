#pragma once

#include "CoreMinimal.h"
#include "RuntimeToolCall.h"
#include "RuntimeToolsetDefinition.h"
#include "RuntimeLayoutToolset.generated.h"

class AActor;

UENUM()
enum class ELayoutToolAxis : uint8
{
	X,
	Y,
	Z,
};

/** Rotation of objects placed on a circle. */
UENUM()
enum class ELayoutToolFacing : uint8
{
	/** Keep the current rotation. */
	Keep,
	/** Face the centre. */
	Center,
	/** Face away from the centre. */
	Outward,
};

USTRUCT()
struct FLayoutToolMoveResult
{
	GENERATED_BODY()

	/** Objects that actually moved (unchanged ones are skipped). */
	UPROPERTY()
	int32 Moved = 0;

	/** References of the moved objects (at most 50). */
	UPROPERTY()
	TArray<FString> Nodes;
};

USTRUCT()
struct FLayoutToolAlignResult
{
	GENERATED_BODY()

	UPROPERTY()
	int32 Moved = 0;

	UPROPERTY()
	TArray<FString> Nodes;

	/** The coordinate everything was aligned to (cm). */
	UPROPERTY()
	float AlignedTo = 0.f;
};

USTRUCT()
struct FLayoutToolDropResult
{
	GENERATED_BODY()

	UPROPERTY()
	int32 Moved = 0;

	UPROPERTY()
	TArray<FString> Nodes;

	/** Targets with nothing below them within max_distance (not moved). */
	UPROPERTY()
	TArray<FString> NoGround;
};

USTRUCT()
struct FLayoutToolGridResult
{
	GENERATED_BODY()

	UPROPERTY()
	int32 Moved = 0;

	UPROPERTY()
	TArray<FString> Nodes;

	UPROPERTY()
	int32 Columns = 0;

	UPROPERTY()
	int32 Rows = 0;
};

USTRUCT()
struct FLayoutToolFreeSpace
{
	GENERATED_BODY()

	/** Whether a free spot was found within the radius. */
	UPROPERTY()
	bool bFound = false;

	/** Centre of the free box (cm). */
	UPROPERTY()
	FVector Center = FVector::ZeroVector;

	/** Bottom centre of the free box: pass it as the location of a base-pivot object. */
	UPROPERTY()
	FVector GroundLocation = FVector::ZeroVector;

	/** Candidate spots tested. */
	UPROPERTY()
	int32 Tested = 0;
};

/**
 * Layout: arrange many objects at once by their bounds (children included): align, distribute, snap to a grid, drop to
 * the ground, rows / grids / circles, seeded jitter, and finding free space for something new.
 * Every tool that moves objects is one undo step; targets are node ids, node paths, actor:Name or object paths.
 * Workflow: create objects (build.*, actors.spawn_actor) -> arrange_in_row / arrange_in_grid / arrange_in_circle or
 * align_nodes / distribute_nodes -> drop_to_ground -> randomize_transform for a natural look. Use find_free_space before
 * placing something new in a crowded scene.
 */
UCLASS()
class URuntimeLayoutToolset : public URuntimeToolsetDefinition
{
	GENERATED_BODY()

public:
	virtual FString GetToolsetVersion() const override { return TEXT("2.0.0"); }

	/**
	 * Aligns the bounds (min, centre or max) of objects on one axis: to the selection's combined bounds, to another
	 * object's bounds, or to a coordinate. Undoable. Fails if targets is empty.
	 *
	 * @param Targets - Objects to align, e.g. ["n3","n4","n5"].
	 * @param Axis - Axis to align on.
	 * @param Mode - Which bounds point to align: "min", "center" or "max".
	 * @param To - Align to this object's bounds instead of the selection.
	 * @param Value - Align to this coordinate (a number in cm) instead, e.g. 0 for the floor.
	 * @return {moved, nodes, aligned_to}.
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld, AutoCreateRefTerm = "Value"), Category = "Layout")
	static FLayoutToolAlignResult AlignNodes(const TArray<AActor*>& Targets, const FRuntimeToolJson& Value, ELayoutToolAxis Axis = ELayoutToolAxis::X,
		const FString& Mode = TEXT("center"), AActor* To = nullptr);

	/**
	 * Spaces objects along an axis: even centres between the first and the last, or a fixed edge-to-edge gap starting at
	 * the first. Objects are ordered by their position on the axis. Undoable. Fails with fewer than 2 targets.
	 *
	 * @param Targets - Objects to distribute.
	 * @param Axis - Axis to distribute along.
	 * @param Gap - Edge-to-edge gap in cm, e.g. 50 (negative = even centres between the first and the last).
	 * @return {moved, nodes}.
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld), Category = "Layout")
	static FLayoutToolMoveResult DistributeNodes(const TArray<AActor*>& Targets, ELayoutToolAxis Axis = ELayoutToolAxis::X, float Gap = -1.f);

	/**
	 * Snaps locations (X and Y, optionally Z) and optionally rotations to a grid. Undoable. Fails if size is not positive.
	 *
	 * @param Targets - Objects to snap.
	 * @param Size - Grid size in cm, e.g. 100.
	 * @param bSnapZ - Also snap Z.
	 * @param RotationStep - Rotation snap in degrees, e.g. 45 (0 = rotations unchanged).
	 * @return {moved, nodes}.
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld), Category = "Layout")
	static FLayoutToolMoveResult SnapToGrid(const TArray<AActor*>& Targets, float Size = 100.f, bool bSnapZ = false, float RotationStep = 0.f);

	/**
	 * Moves objects down (or up) until the bottom of their bounds rests on what is below their centre (Epic:
	 * snap_to_ground). Targets ignore each other. Undoable. Fails if max_distance is not positive.
	 *
	 * @param Targets - Objects to drop.
	 * @param Offset - Extra height above the ground in cm.
	 * @param MaxDistance - How far down to look, in cm.
	 * @return {moved, nodes, no_ground} (no_ground: targets with nothing below them).
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld), Category = "Layout")
	static FLayoutToolDropResult DropToGround(const TArray<AActor*>& Targets, float Offset = 0.f, float MaxDistance = 100000.f);

	/**
	 * Lines objects up edge to edge along a direction, in the given order. Undoable. Fails if direction is zero.
	 *
	 * @param Targets - Objects in row order.
	 * @param Start - Centre of the first object [x,y,z] (default: its current centre).
	 * @param Direction - Row direction, e.g. [0,1,0] (default [1,0,0]).
	 * @param Gap - Gap between neighbours in cm.
	 * @return {moved, nodes}.
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld, AutoCreateRefTerm = "Start,Direction"), Category = "Layout")
	static FLayoutToolMoveResult ArrangeInRow(const TArray<AActor*>& Targets, const FVector& Start, const FVector& Direction, float Gap = 50.f);

	/**
	 * Places objects in a grid on the XY plane, row by row in the given order; each cell is the largest object plus the
	 * gap. Undoable. Fails if columns is negative.
	 *
	 * @param Targets - Objects in grid order.
	 * @param Origin - Centre of the first cell [x,y,z] (default: the first object's centre).
	 * @param Columns - Columns per row (0 = as square as possible).
	 * @param Gap - Gap between cells in cm.
	 * @return {moved, nodes, columns, rows}.
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld, AutoCreateRefTerm = "Origin"), Category = "Layout")
	static FLayoutToolGridResult ArrangeInGrid(const TArray<AActor*>& Targets, const FVector& Origin, int32 Columns = 0, float Gap = 50.f);

	/**
	 * Places objects evenly on a horizontal circle or arc (heights unchanged). A full circle spaces N objects N ways; a
	 * partial arc includes both ends. Undoable. Fails if radius is negative or arc is outside 1-360.
	 *
	 * @param Targets - Objects in order around the circle.
	 * @param Center - Circle centre [x,y,z] (default: the selection's centre).
	 * @param Radius - Radius in cm.
	 * @param StartAngle - Angle of the first object in degrees (0 = +X).
	 * @param Arc - Degrees covered, e.g. 180 for a half circle.
	 * @param Facing - Rotation of the objects.
	 * @return {moved, nodes}.
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld, AutoCreateRefTerm = "Center"), Category = "Layout")
	static FLayoutToolMoveResult ArrangeInCircle(const TArray<AActor*>& Targets, const FVector& Center, float Radius = 500.f, float StartAngle = 0.f,
		float Arc = 360.f, ELayoutToolFacing Facing = ELayoutToolFacing::Keep);

	/**
	 * Adds seeded random offset, yaw and uniform scale for a natural look; the same seed gives the same result.
	 * Undoable. Fails if scale_jitter is outside 0-0.95.
	 *
	 * @param Targets - Objects to jitter.
	 * @param LocationJitter - Maximum offset per axis [x,y,z] cm, e.g. [100,100,0].
	 * @param YawJitter - Maximum yaw change in degrees.
	 * @param ScaleJitter - Maximum uniform scale change as a fraction, e.g. 0.2.
	 * @param Seed - Random seed.
	 * @return {moved, nodes}.
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld, AutoCreateRefTerm = "LocationJitter"), Category = "Layout")
	static FLayoutToolMoveResult RandomizeTransform(const TArray<AActor*>& Targets, const FVector& LocationJitter, float YawJitter = 0.f,
		float ScaleJitter = 0.f, int32 Seed = 0);

	/**
	 * Finds the nearest spot where a box of the given size fits without overlapping anything, searching a square spiral
	 * outward from a point (optionally resting on the ground). Read-only. Fails if size is not positive.
	 *
	 * @param Size - Box size [x,y,z] cm, e.g. [400,400,300].
	 * @param Near - Search from here [x,y,z] (default the origin).
	 * @param Radius - Search radius in cm.
	 * @param Step - Grid step in cm (0 = the box's larger horizontal size).
	 * @param bOnGround - Rest the box on the ground (spots without ground are skipped).
	 * @return {found, center, ground_location, tested}.
	 */
	UFUNCTION(meta = (AICallable, RequiresWorld, AutoCreateRefTerm = "Near"), Category = "Layout")
	static FLayoutToolFreeSpace FindFreeSpace(const FVector& Size, const FVector& Near, float Radius = 3000.f, float Step = 0.f, bool bOnGround = true);
};
