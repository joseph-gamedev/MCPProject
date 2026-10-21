#pragma once

#include "CoreMinimal.h"
#include "RuntimeToolCall.h"
#include "RuntimeToolsetDefinition.h"
#include "RuntimeGameToolset.generated.h"

class AActor;
class APawn;
class APlayerController;

USTRUCT()
struct FGameToolGameState
{
	GENERATED_BODY()

	/** Loaded map name (without the streaming prefix). */
	UPROPERTY()
	FString Map;

	/** Game time in seconds (affected by pause and time dilation). */
	UPROPERTY()
	float TimeSeconds = 0.f;

	/** Real time since the world started, in seconds. */
	UPROPERTY()
	float RealTimeSeconds = 0.f;

	UPROPERTY()
	bool bPaused = false;

	/** Global time dilation (1 = normal speed). */
	UPROPERTY()
	float TimeDilation = 1.f;

	/** Game mode class (empty on clients or worlds without one). */
	UPROPERTY()
	FString GameMode;

	/** Pawn class the game mode spawns for players. */
	UPROPERTY()
	FString DefaultPawn;

	/** Game state class. */
	UPROPERTY()
	FString GameState;

	/** Player states known to the game state. */
	UPROPERTY()
	int32 PlayerStates = 0;

	/** Player controllers in the world. */
	UPROPERTY()
	int32 Players = 0;
};

USTRUCT()
struct FGameToolPlayerInfo
{
	GENERATED_BODY()

	/** The player controller. */
	UPROPERTY()
	TObjectPtr<APlayerController> Controller = nullptr;

	/** Whether the controller is local (has a viewport and input). */
	UPROPERTY()
	bool bLocal = false;

	/** Player name from the player state (empty without one). */
	UPROPERTY()
	FString Name;

	/** The possessed pawn (null when the player has none). */
	UPROPERTY()
	TObjectPtr<APawn> Pawn = nullptr;

	/** Pawn location in cm (zero without a pawn). */
	UPROPERTY()
	FVector Location = FVector::ZeroVector;

	/** Control rotation (where the player looks). */
	UPROPERTY()
	FRotator Rotation = FRotator::ZeroRotator;
};

USTRUCT()
struct FGameToolTeleportResult
{
	GENERATED_BODY()

	/** Reference of the moved actor (node id, else actor:Name). */
	UPROPERTY()
	FString Teleported;

	/** Name of the moved actor. */
	UPROPERTY()
	FString Name;

	/** Final world location in cm. */
	UPROPERTY()
	FVector Location = FVector::ZeroVector;

	/** True when snap_to_ground found ground and moved the actor onto it. */
	UPROPERTY()
	bool bSnappedToGround = false;
};

USTRUCT()
struct FGameToolPauseResult
{
	GENERATED_BODY()

	/** The requested pause state. */
	UPROPERTY()
	bool bPaused = false;

	/** Whether the world reports itself paused now. */
	UPROPERTY()
	bool bWorldPaused = false;
};

USTRUCT()
struct FGameToolTimeDilationResult
{
	GENERATED_BODY()

	/** Time dilation now in effect (the engine clamps to its global min/max). */
	UPROPERTY()
	float TimeDilation = 1.f;

	/** Time dilation before the call. */
	UPROPERTY()
	float Previous = 1.f;
};

/**
 * The running game: map, game mode, players, pause and time scale; move players or actors around.
 * Workflow: get_game_state / list_players to see what is running -> teleport (optionally snap_to_ground) ->
 * set_pause / set_time_dilation to freeze or slow the action while inspecting (tools keep working while paused).
 * Every change is undoable with history.undo.
 */
UCLASS()
class URuntimeGameToolset : public URuntimeToolsetDefinition
{
	GENERATED_BODY()

public:
	virtual FString GetToolsetVersion() const override { return TEXT("2.0.0"); }

	/**
	 * Returns a summary of the running game: map, game mode, game state, time, pause, time dilation and player count.
	 *
	 * @return {map, time_seconds, real_time_seconds, paused, time_dilation, game_mode, default_pawn, game_state, player_states, players}.
	 */
	UFUNCTION(meta = (AICallable, RequiresWorld), Category = "Game")
	static FGameToolGameState GetGameState();

	/**
	 * Lists player controllers with their pawns, locations and view rotations.
	 *
	 * @return One entry per player controller, in the world's controller order (the first is the local player in single-player games).
	 */
	UFUNCTION(meta = (AICallable, RequiresWorld), Category = "Game")
	static TArray<FGameToolPlayerInfo> ListPlayers();

	/**
	 * Moves an actor (default: the first player's pawn) to a location, optionally turning it. For pawns the controller's
	 * view rotation turns too. Undoable.
	 *
	 * @param Location - Destination [x,y,z] in cm, e.g. [0,0,200].
	 * @param Rotation - Optional new rotation [pitch,yaw,roll], e.g. [0,90,0].
	 * @param Target - Actor or pawn to move (node id, path, actor:Name, "pawn"); default the player pawn.
	 * @param bSnapToGround - Drop the actor so the bottom of its bounds rests on the ground below the location (Epic snap_to_ground).
	 * @return The moved actor's reference and name, its final location and whether it was snapped to the ground.
	 * Fails if no target is given and there is no player pawn.
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld, AutoCreateRefTerm = "Rotation"), Category = "Game")
	static FGameToolTeleportResult Teleport(const FVector& Location, const FRotator& Rotation, AActor* Target = nullptr, bool bSnapToGround = false);

	/**
	 * Pauses or unpauses the game. Tools keep working while paused. Undoable.
	 *
	 * @param bPaused - true to pause, false to resume.
	 * @return The requested state and whether the world reports itself paused.
	 * Fails if the world has no player controller (pausing goes through one).
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld), Category = "Game")
	static FGameToolPauseResult SetPause(bool bPaused);

	/**
	 * Slows down or speeds up game time. Undoable.
	 *
	 * @param Value - Time scale between 0.0001 and 20, e.g. 0.25 for slow motion (1 = normal).
	 * @return The time dilation now in effect (engine-clamped) and the previous one.
	 * Fails if the value is out of range.
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld), Category = "Game")
	static FGameToolTimeDilationResult SetTimeDilation(float Value);
};
