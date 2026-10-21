#pragma once

#include "CoreMinimal.h"
#include "RuntimeToolCall.h"
#include "RuntimeToolsetDefinition.h"
#include "RuntimePhysicsToolset.generated.h"

class AActor;
class UPrimitiveComponent;

/** Overlap query shape. */
UENUM()
enum class EPhysicsToolOverlapShape : uint8
{
	Sphere,
	Box,
};

USTRUCT()
struct FPhysicsToolHit
{
	GENERATED_BODY()

	/** Reference of the hit actor (node id, else actor:Name). */
	UPROPERTY()
	FString Actor;

	/** Name of the hit component. */
	UPROPERTY()
	FString Component;

	/** Impact point in cm. */
	UPROPERTY()
	FVector Location = FVector::ZeroVector;

	/** Surface normal at the impact. */
	UPROPERTY()
	FVector Normal = FVector::ZeroVector;

	/** Distance from the start in cm. */
	UPROPERTY()
	float Distance = 0.f;
};

USTRUCT()
struct FPhysicsToolTraceResult
{
	GENERATED_BODY()

	UPROPERTY()
	bool bHit = false;

	/** Hits nearest first (one entry unless multi). */
	UPROPERTY()
	TArray<FPhysicsToolHit> Hits;
};

USTRUCT()
struct FPhysicsToolOverlapResult
{
	GENERATED_BODY()

	/** Overlapping actors before the limit. */
	UPROPERTY()
	int32 Total = 0;

	/** True when actors were cut off by the limit. */
	UPROPERTY()
	bool bTruncated = false;

	/** References of the overlapping actors (node ids, else actor:Name). */
	UPROPERTY()
	TArray<FString> Actors;
};

USTRUCT()
struct FPhysicsToolChannelInfo
{
	GENERATED_BODY()

	/** Channel name as configured in the project, e.g. "Visibility", "WorldStatic", "Pawn". Pass it as `channel`. */
	UPROPERTY()
	FString Name;

	/** Usable as a trace channel (Visibility, Camera, custom trace channels). */
	UPROPERTY()
	bool bTrace = false;

	/** An object type (WorldStatic, WorldDynamic, Pawn, PhysicsBody, ...). */
	UPROPERTY()
	bool bObjectType = false;
};

USTRUCT()
struct FPhysicsToolCollisionChannels
{
	GENERATED_BODY()

	/** Trace channels and object types, in engine channel order. */
	UPROPERTY()
	TArray<FPhysicsToolChannelInfo> Channels;

	/** Collision profile names (BlockAll, OverlapAll, Pawn, Ragdoll, ...), sorted. */
	UPROPERTY()
	TArray<FString> Profiles;
};

USTRUCT()
struct FPhysicsToolBodyInfo
{
	GENERATED_BODY()

	/** Bone the body belongs to. */
	UPROPERTY()
	FString Bone;

	UPROPERTY()
	bool bSimulating = false;

	/** Mass in kg. */
	UPROPERTY()
	float MassKg = 0.f;
};

USTRUCT()
struct FPhysicsToolState
{
	GENERATED_BODY()

	/** The inspected primitive component. */
	UPROPERTY()
	FString Component;

	UPROPERTY()
	bool bSimulating = false;

	UPROPERTY()
	bool bGravity = false;

	/** Collision profile name, e.g. "BlockAll". */
	UPROPERTY()
	FString CollisionProfile;

	/** Whether collision is enabled at all. */
	UPROPERTY()
	bool bCollision = false;

	/** Static, Stationary or Movable. Only Movable components simulate. */
	UPROPERTY()
	FString Mobility;

	/** Mass in kg (0 without a physics body). */
	UPROPERTY()
	float MassKg = 0.f;

	/** Linear velocity in cm/s (zero unless simulating). */
	UPROPERTY()
	FVector Velocity = FVector::ZeroVector;

	/** Angular velocity in degrees/s (zero unless simulating). */
	UPROPERTY()
	FVector AngularVelocity = FVector::ZeroVector;

	/** Per-bone bodies of a skeletal mesh (empty for other components). */
	UPROPERTY()
	TArray<FPhysicsToolBodyInfo> Bodies;
};

USTRUCT()
struct FPhysicsToolSimulateResult
{
	GENERATED_BODY()

	/** Reference of the owning actor. */
	UPROPERTY()
	FString Target;

	UPROPERTY()
	bool bSimulating = false;

	/** True when the component was Static/Stationary and was switched to Movable to simulate (undo restores it). */
	UPROPERTY()
	bool bMobilityChanged = false;

	/** Extra information (e.g. what changed besides simulation). */
	UPROPERTY()
	FString Note;
};

USTRUCT()
struct FPhysicsToolImpulseResult
{
	GENERATED_BODY()

	/** Reference of the pushed actor. */
	UPROPERTY()
	FString Target;

	/** Velocity right after the push in cm/s. */
	UPROPERTY()
	FVector Velocity = FVector::ZeroVector;
};

USTRUCT()
struct FPhysicsToolRagdollResult
{
	GENERATED_BODY()

	/** Reference of the actor. */
	UPROPERTY()
	FString Target;

	UPROPERTY()
	bool bRagdoll = false;

	/** Physics bodies of the skeletal mesh. */
	UPROPERTY()
	int32 Bodies = 0;
};

/**
 * Physics and collision queries in the running world: traces, overlaps, simulation state, pushes and ragdolls.
 * Workflow: get_collision_channels (which channel names exist) -> line_trace / overlap_test to probe the world ->
 * get_physics to inspect a body -> set_simulate (undo puts the object back) -> add_impulse to push it (not undoable) ->
 * ragdoll for skeletal meshes.
 */
UCLASS()
class URuntimePhysicsToolset : public URuntimeToolsetDefinition
{
	GENERATED_BODY()

public:
	virtual FString GetToolsetVersion() const override { return TEXT("2.0.0"); }

	/**
	 * Lists the collision channels (trace channels and object types) and collision profiles of this project, for use with
	 * line_trace / overlap_test `channel` (Epic scene.get_collision_channels).
	 *
	 * @return {channels:[{name, trace, object_type}] in engine order, profiles:[names] sorted}.
	 */
	UFUNCTION(meta = (AICallable), Category = "Physics|Query")
	static FPhysicsToolCollisionChannels GetCollisionChannels();

	/**
	 * Traces a ray through the world and returns what it hits.
	 *
	 * @param Start - Ray start [x,y,z] in cm, e.g. [0,0,500].
	 * @param End - Ray end [x,y,z]; or give direction (+ distance) instead.
	 * @param Direction - Ray direction, e.g. [0,0,-1] (used when end is omitted).
	 * @param Ignore - Actors the ray passes through (node ids, paths, actor:Name).
	 * @param Distance - Ray length in cm when using direction.
	 * @param Channel - Trace channel or object type name from get_collision_channels, e.g. "Visibility", "Camera", "WorldStatic", "Pawn".
	 * @param bMulti - Return every hit along the ray instead of the first blocking one.
	 * @param bComplex - Trace per-triangle collision instead of simple shapes.
	 * @param Limit - Maximum hits with multi (0 = default list limit).
	 * @return {hit, hits:[{actor, component, location, normal, distance}]} nearest first.
	 * Fails if neither end nor a non-zero direction is given, or the channel name is unknown (the channels are listed).
	 */
	UFUNCTION(meta = (AICallable, RequiresWorld, AutoCreateRefTerm = "End,Direction,Ignore"), Category = "Physics|Query")
	static FPhysicsToolTraceResult LineTrace(const FVector& Start, const FVector& End, const FVector& Direction, const TArray<AActor*>& Ignore,
		float Distance = 100000.f, const FString& Channel = TEXT("Visibility"), bool bMulti = false, bool bComplex = false, int32 Limit = 0);

	/**
	 * Finds actors overlapping a sphere or box.
	 *
	 * @param Center - Shape centre [x,y,z] in cm, e.g. [0,0,100].
	 * @param Size - Box size [x,y,z] in cm (shape Box).
	 * @param Shape - Sphere or Box.
	 * @param Radius - Sphere radius in cm.
	 * @param Channel - Only bodies that overlap or block this channel (empty = every object type), e.g. "Pawn".
	 * @param Limit - Maximum actors returned (0 = default list limit).
	 * @return {total, truncated, actors:[refs]} in query order.
	 * Fails if a box has no positive size, the radius is not positive, or the channel name is unknown.
	 */
	UFUNCTION(meta = (AICallable, RequiresWorld, AutoCreateRefTerm = "Size"), Category = "Physics|Query")
	static FPhysicsToolOverlapResult OverlapTest(const FVector& Center, const FVector& Size, EPhysicsToolOverlapShape Shape = EPhysicsToolOverlapShape::Sphere,
		float Radius = 100.f, const FString& Channel = TEXT(""), int32 Limit = 0);

	/**
	 * Returns the simulation state, mass, velocity, collision and mobility of a body; for skeletal meshes also every bone body.
	 *
	 * @param Target - Actor (its first primitive component, usually the root) or "actor#Component", e.g. "n12".
	 * @return {component, simulating, gravity, collision_profile, collision, mobility, mass_kg, velocity, angular_velocity, bodies:[{bone, simulating, mass_kg}]}.
	 * Fails if the target has no primitive component.
	 */
	UFUNCTION(meta = (AICallable, RequiresWorld), Category = "Physics|Body")
	static FPhysicsToolState GetPhysics(UPrimitiveComponent* Target);

	/**
	 * Turns physics simulation on or off. A Static or Stationary component is switched to Movable first (reported in the
	 * result). Undoable: undo stops simulating and puts the object back where it was (re-attached).
	 *
	 * @param Target - Actor or "actor#Component", e.g. "n12".
	 * @param bEnabled - true to simulate, false to stop.
	 * @return {target, simulating, mobility_changed, note}.
	 * Fails if the component has no collision (physics cannot simulate without it).
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld), Category = "Physics|Body")
	static FPhysicsToolSimulateResult SetSimulate(UPrimitiveComponent* Target, bool bEnabled = true);

	/**
	 * Pushes a simulating object. Not undoable (the motion that follows cannot be reverted).
	 *
	 * @param Target - Simulating actor or "actor#Component", e.g. "n12".
	 * @param Impulse - [x,y,z]; a velocity change in cm/s with velocity_change, else an impulse in kg*cm/s, e.g. [0,0,500].
	 * @param At - Apply at this world point [x,y,z] (adds spin); default the centre of mass.
	 * @param bVelocityChange - Treat impulse as a velocity change (ignores mass).
	 * @return The pushed actor and its velocity right after the push.
	 * Fails if the target is not simulating physics (call set_simulate first).
	 */
	UFUNCTION(meta = (AICallable, Irreversible, RequiresWorld, AutoCreateRefTerm = "At"), Category = "Physics|Body")
	static FPhysicsToolImpulseResult AddImpulse(UPrimitiveComponent* Target, const FVector& Impulse, const FVector& At, bool bVelocityChange = true);

	/**
	 * Switches a skeletal mesh to ragdoll physics (or back). Undoable.
	 *
	 * @param Target - Actor with a skeletal mesh that has a physics asset, e.g. "pawn".
	 * @param bEnabled - true for ragdoll, false to stop simulating the bodies.
	 * @return {target, ragdoll, bodies}.
	 * Fails if the actor has no skeletal mesh with a physics asset.
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld), Category = "Physics|Body")
	static FPhysicsToolRagdollResult Ragdoll(AActor* Target, bool bEnabled = true);
};
