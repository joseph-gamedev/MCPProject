#pragma once

#include "CoreMinimal.h"
#include "RuntimeToolsetDefinition.h"
#include "RuntimeToolCall.h"
#include "RuntimeLogicToolset.h"
#include "RuntimeCharacterToolset.generated.h"

UENUM()
enum class ELogicControlAction : uint8
{
	/** Walk along direction (world space) while held; several move keys add up. */
	Move,
	/** Jump on press (hold for higher with jump_hold); also leaves a climb. */
	Jump,
	/** Move at run_speed while held. */
	Run,
	/** Climb while inside a climbable object: direction z +1 up, -1 down. */
	Climb,
	/** Fire the equipped weapon (logic.equip owner "player"). */
	Fire,
	/** Fire the secondary weapon (set with logic.spawn_player secondary or player set). */
	FireSecondary
};

UENUM()
enum class ELogicFacing : uint8
{
	/** Turn toward the movement direction. */
	Movement,
	/** Face the mouse cursor on the ground plane. */
	Mouse,
	/** Keep the yaw given at creation. */
	Fixed
};

UENUM()
enum class ELogicCameraMode : uint8
{
	/** Stay at offset from the character (lock_axes keep some axes at location). */
	Follow,
	/** Stay at location, looking at look_at_point or the character. */
	Fixed
};

/** One key binding. */
USTRUCT()
struct FLogicControl
{
	GENERATED_BODY()

	/** Key name: A, D, W, S, Left, Right, Up, Down, SpaceBar, LeftShift, F, LeftMouseButton, Gamepad_FaceButton_Bottom, Gamepad_LeftX... */
	UPROPERTY()
	FString Key;

	/** What the key does. Any other key can still drive rules ({"on":"key","key":"E"}). */
	UPROPERTY()
	ELogicControlAction Action = ELogicControlAction::Move;

	/** Move: world direction, e.g. [1,0,0]; climb: [0,0,1] up or [0,0,-1] down. */
	UPROPERTY()
	FVector Direction = FVector::ZeroVector;
};

/** Character movement numbers (Unreal CharacterMovement). */
USTRUCT()
struct FLogicMovementSettings
{
	GENERATED_BODY()

	/** cm/s. */
	UPROPERTY()
	float WalkSpeed = 600.f;

	/** cm/s while a run key is held (default 1.6 x walk). */
	UPROPERTY()
	float RunSpeed = 0.f;

	/** cm/s^2. */
	UPROPERTY()
	float Acceleration = 3000.f;

	/** Stopping deceleration, cm/s^2. */
	UPROPERTY()
	float Braking = 2600.f;

	/** Jump launch speed cm/s (0 = cannot jump). */
	UPROPERTY()
	float JumpVelocity = 900.f;

	/** Seconds holding jump keeps rising. */
	UPROPERTY()
	float JumpHold = 0.2f;

	/** Gravity multiplier (2-3 feels arcade-snappy). */
	UPROPERTY()
	float GravityScale = 2.f;

	/** Steering in the air, 0-1. */
	UPROPERTY()
	float AirControl = 0.8f;

	/** Highest step walked up, cm. */
	UPROPERTY()
	float MaxStep = 40.f;

	/** Steepest walkable slope, degrees. */
	UPROPERTY()
	float MaxSlope = 46.f;

	/** cm/s on climbables. */
	UPROPERTY()
	float ClimbSpeed = 300.f;

	/** Degrees per second when turning to face. */
	UPROPERTY()
	float TurnRate = 1500.f;
};

/** Camera rig. */
USTRUCT()
struct FLogicCameraRig
{
	GENERATED_BODY()

	UPROPERTY()
	ELogicCameraMode Mode = ELogicCameraMode::Follow;

	/** Follow: camera position relative to the character, e.g. [0,-1800,250] = beside it (side view). */
	UPROPERTY()
	FVector Offset = FVector(0.0, -1600.0, 300.0);

	/** Fixed: camera position; Follow: the values used for lock_axes. */
	UPROPERTY()
	FVector Location = FVector::ZeroVector;

	/** Follow: axes ("x","y","z") that stay at location instead of following, e.g. ["x","z"] for a fixed single screen. */
	UPROPERTY()
	TArray<FString> LockAxes;

	/** Aim at the character (or look_at_point for fixed); false = use rotation. */
	UPROPERTY()
	bool bLookAt = true;

	/** Fixed: point to look at. */
	UPROPERTY()
	FVector LookAtPoint = FVector::ZeroVector;

	/** Added to the look target, e.g. [0,0,100]. */
	UPROPERTY()
	FVector LookOffset = FVector::ZeroVector;

	/** Used when look_at is false. */
	UPROPERTY()
	FRotator Rotation = FRotator::ZeroRotator;

	/** Seconds the camera trails behind (0 = rigid). */
	UPROPERTY()
	float Lag = 0.12f;

	UPROPERTY()
	float Fov = 55.f;

	/** > 0 = orthographic view this many cm wide (flat 2D look, e.g. arcade screens). */
	UPROPERTY()
	float OrthoWidth = 0.f;
};

USTRUCT()
struct FLogicClimbableResult
{
	GENERATED_BODY()

	/** Node ids that are climbable now. */
	UPROPERTY()
	TArray<FString> Climbables;
};

/**
 * Generic playable character: any game's hero built from settings instead of a genre preset. Movement numbers, key
 * bindings, facing, a 2D plane lock, climbable objects and a camera rig are all data, so a side-scroller, a ladder game, a
 * top-down shooter or a third-person adventure are just different settings. The character is the logic "player": health,
 * weapons, touch/contact rules, pos('player') and {"player": ...} actions all apply. Undoable.
 */
UCLASS()
class URuntimeCharacterToolset : public URuntimeToolsetDefinition
{
	GENERATED_BODY()

public:
	/**
	 * Creates (or replaces) the playable character and possesses it. Undoable.
	 *
	 * @param Location - Feet position [x,y,z].
	 * @param Controls - Key bindings, e.g. [{"key":"D","action":"move","direction":[1,0,0]},{"key":"A","action":"move","direction":[-1,0,0]},{"key":"SpaceBar","action":"jump"},{"key":"W","action":"climb","direction":[0,0,1]},{"key":"S","action":"climb","direction":[0,0,-1]}].
	 * @param Movement - Movement numbers.
	 * @param Camera - Camera rig, e.g. side view {"mode":"follow","offset":[0,-2200,300],"lock_axes":["z"],"location":[0,0,900]} or one fixed screen {"mode":"fixed","location":[0,-3000,900],"look_at_point":[0,0,900],"ortho_width":3200}.
	 * @param Body - Your character model node (feet at z=0, facing +X); default: a simple hero.
	 * @param PlaneNormal - Keep the character in a plane, e.g. [0,1,0] = moves only in X/Z (2D side view).
	 * @param Facing - movement, mouse or fixed.
	 * @param Yaw - Start heading (and the heading for facing fixed), degrees.
	 * @param Radius - Collision radius cm.
	 * @param Height - Collision height cm.
	 * @return The player state. Fails if a control key is unknown or the body is not a scene node.
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld, AutoCreateRefTerm = "Movement,Camera,PlaneNormal"), Category = "Character")
	static FLogicPlayerState Create(const FVector& Location, const TArray<FLogicControl>& Controls, const FLogicMovementSettings& Movement,
		const FLogicCameraRig& Camera, const FVector& PlaneNormal, ELogicFacing Facing = ELogicFacing::Movement, float Yaw = 0.f,
		float Radius = 38.f, float Height = 180.f, AActor* Body = nullptr);

	/**
	 * Makes objects climbable (ladders, vines, ropes, walls): inside one, climb keys move the character up/down through
	 * floors, snapped to its center; leaving an end puts it back on its feet. Rules get on:"climb" filter "start"/"end". Undoable.
	 *
	 * @param Targets - Node ids, paths, names or "tag:ladder".
	 * @param bClimbable - false removes them.
	 * @return All climbable node ids. Fails without a character.create character or if a target matches no node.
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld), Category = "Character")
	static FLogicClimbableResult SetClimbable(const TArray<FString>& Targets, bool bClimbable = true);
};
