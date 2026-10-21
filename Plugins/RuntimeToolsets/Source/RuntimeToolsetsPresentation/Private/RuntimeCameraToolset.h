#pragma once

#include "CoreMinimal.h"
#include "RuntimeToolsetDefinition.h"
#include "RuntimeCameraToolset.generated.h"

class AActor;

/** How the view is driven after a camera tool. */
UENUM()
enum class ERuntimeCameraMode : uint8
{
	/** The player's own flying pawn was moved: the player keeps WASD/QE + mouse control. */
	PlayerPawn,
	/** A locked tool camera (no pawn to fly). */
	ToolCamera,
	/** Some other view target (a project camera, a possessed pawn...). */
	ViewTarget,
};

/** The player's view. */
USTRUCT()
struct FRuntimeCameraView
{
	GENERATED_BODY()

	/** View location [x, y, z] in cm. */
	UPROPERTY()
	FVector Location = FVector::ZeroVector;

	/** View rotation [pitch, yaw, roll]. */
	UPROPERTY()
	FRotator Rotation = FRotator::ZeroRotator;

	/** Horizontal field of view in degrees. */
	UPROPERTY()
	float Fov = 90.f;

	UPROPERTY()
	ERuntimeCameraMode Mode = ERuntimeCameraMode::ViewTarget;

	/** Whether the tool camera is the view target. */
	UPROPERTY()
	bool bToolCamera = false;

	/** The view target (node id, actor:Name). */
	UPROPERTY()
	FString ViewTarget;

	/** The player's pawn, empty if none. */
	UPROPERTY()
	FString Pawn;

	/** frame_nodes / orbit: the point looked at. */
	UPROPERTY()
	FVector Center = FVector::ZeroVector;

	/** frame_nodes / orbit: distance from the view to the center in cm. */
	UPROPERTY()
	float Distance = 0.f;

	/** What the player can do now. */
	UPROPERTY()
	FString Controls;
};

/** Fly mode state. */
USTRUCT()
struct FRuntimeFlyModeInfo
{
	GENERATED_BODY()

	UPROPERTY()
	bool bFlyMode = false;

	UPROPERTY()
	FString Controls;
};

UENUM()
enum class ERuntimeFlySpeedMode : uint8
{
	/** Keep the current mode (fixed when only `speed` is given). */
	Unchanged,
	/** Scales with the distance to the nearest surface: fast far away, slow up close. */
	Auto,
	/** Constant `speed`. */
	Fixed,
};

/** Fly speed settings. */
USTRUCT()
struct FRuntimeFlySpeedInfo
{
	GENERATED_BODY()

	/** Auto or Fixed. */
	UPROPERTY()
	ERuntimeFlySpeedMode Mode = ERuntimeFlySpeedMode::Auto;

	UPROPERTY()
	float Multiplier = 1.f;

	/** Auto mode: slowest speed, cm/s. */
	UPROPERTY()
	float MinSpeed = 0.f;

	/** Auto mode: fastest speed, cm/s. */
	UPROPERTY()
	float MaxSpeed = 0.f;

	/** Fixed mode speed, cm/s. */
	UPROPERTY()
	float FixedSpeed = 0.f;

	/** Speed right now, cm/s. */
	UPROPERTY()
	float CurrentSpeed = 0.f;
};

/** Result of possess. */
USTRUCT()
struct FRuntimePossessInfo
{
	GENERATED_BODY()

	/** The pawn now controlled. */
	UPROPERTY()
	FString Possessed;

	/** The pawn controlled before (empty if none); undo returns to it. */
	UPROPERTY()
	FString Previous;
};

/** Viewport render modes. Debug modes need r.ForceDebugViewModes=1 in cooked builds. */
UENUM()
enum class ERuntimeViewMode : uint8
{
	Lit,
	Unlit,
	Wireframe,
	LightingOnly,
	Reflections,
	ShaderComplexity,
	CollisionVisibility,
	CollisionPawn,
	LodColoration,
};

/** Result of set_view_mode. */
USTRUCT()
struct FRuntimeViewModeInfo
{
	GENERATED_BODY()

	UPROPERTY()
	ERuntimeViewMode ViewMode = ERuntimeViewMode::Lit;

	/** The mode before; undo restores it. */
	UPROPERTY()
	ERuntimeViewMode Previous = ERuntimeViewMode::Lit;
};

/**
 * Player view control. Camera moves keep the player in control: a flying pawn is moved directly, a walking pawn switches
 * to fly mode first (WASD/QE + mouse), and without any pawn a locked tool camera is used.
 * Workflow: get_camera -> set_camera (location + look_at) or frame_nodes (fit nodes on screen) -> orbit to circle around ->
 * reset_camera to give the view back. set_fly_mode / set_fly_speed tune free flight; possess switches pawns;
 * set_view_mode changes the render mode (lit, unlit, wireframe, ...). All moves are undoable.
 */
UCLASS()
class URuntimeCameraToolset : public URuntimeToolsetDefinition
{
	GENERATED_BODY()

public:
	virtual FString GetToolsetVersion() const override { return TEXT("2.0.0"); }

	/**
	 * Reads the current player view.
	 *
	 * @return Location, rotation, fov, how the view is driven (mode), the view target and the player's pawn.
	 * Fails if there is no local player controller.
	 */
	UFUNCTION(meta = (AICallable, RequiresWorld), Category = "Camera")
	static FRuntimeCameraView GetCamera();

	/**
	 * Moves the player's view to a pose; parts left out keep the current view. Undoable.
	 *
	 * @param Location - View position [x, y, z] in cm, e.g. [0, -1000, 500].
	 * @param Rotation - View rotation [pitch, yaw, roll]; ignored when look_at is given.
	 * @param LookAt - Point to look at [x, y, z], e.g. [0, 0, 0].
	 * @param Fov - Horizontal field of view in degrees (5-170), e.g. 60.
	 * @param BlendTime - Seconds to blend (tool camera only).
	 * @return The new view.
	 * Fails if there is no local player or look_at equals the location.
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld, AutoCreateRefTerm = "Location,Rotation,LookAt,Fov"), Category = "Camera")
	static FRuntimeCameraView SetCamera(const FVector& Location, const FRotator& Rotation, const FVector& LookAt, const float& Fov, float BlendTime = 0.f);

	/**
	 * Changes the field of view, keeping the current pose. Undoable.
	 *
	 * @param Fov - Horizontal field of view in degrees (5-170), e.g. 50.
	 * @param BlendTime - Seconds to blend (tool camera only).
	 * @return The new view.
	 * Fails if fov is outside 5-170 or there is no local player.
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld), Category = "Camera")
	static FRuntimeCameraView SetFov(float Fov, float BlendTime = 0.f);

	/**
	 * Points the view so all targets fit on screen. Undoable.
	 *
	 * @param Targets - Nodes or actors to frame, e.g. ["n3", "n7"].
	 * @param Direction - View direction [x, y, z] (default: the current one), e.g. [1, 0.5, -0.3].
	 * @param Padding - Margin factor (1 = tight, 1.15 default).
	 * @param Fov - Field of view in degrees for the framing (default: current).
	 * @param BlendTime - Seconds to blend (tool camera only).
	 * @return The new view, with center and distance.
	 * Fails if targets is empty, a target is not found, or the targets have no bounds.
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld, AutoCreateRefTerm = "Direction,Fov"), Category = "Camera")
	static FRuntimeCameraView FrameNodes(const TArray<AActor*>& Targets, const FVector& Direction, const float& Fov, float Padding = 1.15f, float BlendTime = 0.f);

	/**
	 * Orbits the view around a node's bounds centre or a point, by yaw/pitch degrees. Undoable.
	 *
	 * @param Target - Orbit around this node or actor, e.g. "n3".
	 * @param Center - Or around this point [x, y, z].
	 * @param Yaw - Degrees around the vertical axis, e.g. 90.
	 * @param Pitch - Degrees up (+) or down (-); clamped to +-89.
	 * @param Distance - New distance in cm (default: keep).
	 * @param BlendTime - Seconds to blend (tool camera only).
	 * @return The new view, with center and distance.
	 * Fails if neither target nor center is given.
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld, AutoCreateRefTerm = "Center,Distance"), Category = "Camera")
	static FRuntimeCameraView Orbit(const FVector& Center, const float& Distance, AActor* Target = nullptr, float Yaw = 0.f, float Pitch = 0.f, float BlendTime = 0.f);

	/**
	 * Gives the view back to the player's pawn (or controller) and leaves fly mode. Undoable.
	 *
	 * @param BlendTime - Seconds to blend.
	 * @return The view after the reset.
	 * Fails if there is no local player.
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld), Category = "Camera")
	static FRuntimeCameraView ResetCamera(float BlendTime = 0.f);

	/**
	 * Lets the player fly freely from the current view (WASD/QE + mouse), or returns them to their pawn. Undoable.
	 *
	 * @param bEnabled - true = fly mode on, false = back to the player's pawn.
	 * @return Whether fly mode is on and the controls.
	 * Fails if there is no local player.
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld), Category = "Camera")
	static FRuntimeFlyModeInfo SetFlyMode(bool bEnabled = true);

	/**
	 * Fly speed: auto (scales with distance to the nearest surface) or fixed. Arguments left out keep their value.
	 *
	 * @param Mode - Auto, Fixed or Unchanged (giving only `speed` switches to Fixed).
	 * @param Speed - Fixed speed in cm/s, e.g. 2000.
	 * @param Multiplier - Scales either mode, e.g. 2.
	 * @param MinSpeed - Auto mode: slowest cm/s.
	 * @param MaxSpeed - Auto mode: fastest cm/s.
	 * @return The settings after the change and the current speed.
	 * Fails if a speed or multiplier is not positive.
	 */
	UFUNCTION(meta = (AICallable, AutoCreateRefTerm = "Speed,Multiplier,MinSpeed,MaxSpeed"), Category = "Camera")
	static FRuntimeFlySpeedInfo SetFlySpeed(const float& Speed, const float& Multiplier, const float& MinSpeed, const float& MaxSpeed,
		ERuntimeFlySpeedMode Mode = ERuntimeFlySpeedMode::Unchanged);

	/**
	 * Makes the local player control a pawn. Undoable (returns to the previous pawn).
	 *
	 * @param Target - The pawn, e.g. "n5".
	 * @return The possessed pawn and the previous one.
	 * Fails if the target is not a pawn or is controlled by another controller.
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld), Category = "Camera")
	static FRuntimePossessInfo Possess(AActor* Target);

	/**
	 * Sets the viewport render mode. Undoable.
	 *
	 * @param Mode - Lit, Unlit, Wireframe, LightingOnly, Reflections, ShaderComplexity, CollisionVisibility, CollisionPawn, LodColoration.
	 * @return The new and the previous mode.
	 * Fails if there is no game viewport, or (Unsupported) the mode was stripped from this cooked build.
	 */
	UFUNCTION(meta = (AICallable, Undoable), Category = "Camera")
	static FRuntimeViewModeInfo SetViewMode(ERuntimeViewMode Mode);
};
