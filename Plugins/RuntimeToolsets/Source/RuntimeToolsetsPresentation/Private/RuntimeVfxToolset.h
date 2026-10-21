#pragma once

#include "CoreMinimal.h"
#include "RuntimeToolCall.h"
#include "RuntimeToolsetDefinition.h"
#include "RuntimeVfxToolset.generated.h"

class AActor;
class UNiagaraComponent;
class UNiagaraSystem;
class UWorld;

/** One user variable (the external interface of a Niagara system). */
USTRUCT()
struct FRuntimeVfxUserVariable
{
	GENERATED_BODY()

	/** Name without the "User." prefix, e.g. "SpawnRate". */
	UPROPERTY()
	FString Name;

	/** Niagara type: float, int32, bool, Vector, Position, LinearColor, ... */
	UPROPERTY()
	FString Type;

	/** Current value (number, bool, [x,y,z] or [r,g,b,a]); null for types the tools cannot read. */
	UPROPERTY()
	FRuntimeToolJson Value;
};

/** A Niagara effect node. */
USTRUCT()
struct FRuntimeVfxInfo
{
	GENERATED_BODY()

	/** Node id, e.g. "n12" (pass it as `target`). */
	UPROPERTY()
	FString Id;

	/** Node name. */
	UPROPERTY()
	FString Name;

	/** Niagara system asset path. */
	UPROPERTY()
	FString System;

	/** World location of the effect. */
	UPROPERTY()
	FVector Location = FVector::ZeroVector;

	UPROPERTY()
	bool bActive = false;

	/** Whether the location was moved down onto the ground. */
	UPROPERTY()
	bool bSnappedToGround = false;

	/** Names of the user variables (set them with set_user_variable). */
	UPROPERTY()
	TArray<FString> UserVariables;
};

/** A fire-and-forget effect from spawn_one_shot. */
USTRUCT()
struct FRuntimeVfxOneShot
{
	GENERATED_BODY()

	UPROPERTY()
	FString System;

	UPROPERTY()
	FVector Location = FVector::ZeroVector;

	UPROPERTY()
	bool bSnappedToGround = false;

	/** Whether the system loops forever (a looping system never finishes; prefer spawn_system for it). */
	UPROPERTY()
	bool bLooping = false;
};

/** Summary of one emitter. */
USTRUCT()
struct FRuntimeVfxEmitterSummary
{
	GENERATED_BODY()

	UPROPERTY()
	FString Name;

	UPROPERTY()
	bool bEnabled = true;

	/** CPU or GPU. */
	UPROPERTY()
	FString SimTarget;

	/** Renderer classes (de-duplicated), e.g. ["NiagaraSpriteRendererProperties"]. */
	UPROPERTY()
	TArray<FString> Renderers;
};

/** First-contact summary of an effect (Epic: GetSystemSummary). */
USTRUCT()
struct FRuntimeVfxSummary
{
	GENERATED_BODY()

	/** The effect (node id or actor:Name). */
	UPROPERTY()
	FString Target;

	UPROPERTY()
	FString System;

	UPROPERTY()
	bool bActive = false;

	/** Whether the system loops forever. */
	UPROPERTY()
	bool bLooping = false;

	UPROPERTY()
	FVector Location = FVector::ZeroVector;

	/** User variables with current values, sorted by name. */
	UPROPERTY()
	TArray<FRuntimeVfxUserVariable> UserVariables;

	/** Emitters in system order. */
	UPROPERTY()
	TArray<FRuntimeVfxEmitterSummary> Emitters;
};

/** State of an effect after activate / deactivate / set_system. */
USTRUCT()
struct FRuntimeVfxState
{
	GENERATED_BODY()

	UPROPERTY()
	FString Target;

	UPROPERTY()
	FString System;

	UPROPERTY()
	bool bActive = false;
};

/** An active effect. */
USTRUCT()
struct FRuntimeVfxActive
{
	GENERATED_BODY()

	UPROPERTY()
	FString Target;

	UPROPERTY()
	FString System;

	UPROPERTY()
	FVector Location = FVector::ZeroVector;
};

/** list_active result. */
USTRUCT()
struct FRuntimeVfxActiveList
{
	GENERATED_BODY()

	/** All active effects (the list is cut at `limit`). */
	UPROPERTY()
	int32 Total = 0;

	/** Sorted by target. */
	UPROPERTY()
	TArray<FRuntimeVfxActive> Active;
};

/** Result of set_user_variable. */
USTRUCT()
struct FRuntimeVfxVariableChange
{
	GENERATED_BODY()

	UPROPERTY()
	FString Target;

	UPROPERTY()
	FRuntimeVfxUserVariable Variable;

	/** Value before the change. */
	UPROPERTY()
	FRuntimeToolJson Previous;
};

/**
 * Niagara effects at runtime. Effects are scene nodes (spawn_system) or fire-and-forget bursts (spawn_one_shot).
 * Workflow: spawn_system (snap_to_ground for effects that sit on the floor) -> get_summary (user variables, emitters) ->
 * set_user_variable to tune it -> activate / deactivate, or set_system to swap the effect on the same node.
 * Read the vfx_placement skill before placing effects the user asked for.
 */
UCLASS()
class URuntimeVfxToolset : public URuntimeToolsetDefinition
{
	GENERATED_BODY()

public:
	virtual FString GetToolsetVersion() const override { return TEXT("2.0.0"); }

	/**
	 * Spawns a Niagara system as a scene node, optionally setting user variables. Undoable (undo removes it).
	 *
	 * @param System - Niagara system asset, e.g. "/Niagara/DefaultAssets/DefaultSystem.DefaultSystem".
	 * @param Location - World position [x, y, z] in cm.
	 * @param Rotation - [pitch, yaw, roll].
	 * @param Scale - [x, y, z], default [1, 1, 1].
	 * @param UserVariables - User variables by name, e.g. {"SpawnRate": 50, "Color": [1, 0.3, 0, 1]}.
	 * @param Tags - Node tags.
	 * @param Name - Node name (default: the system name).
	 * @param Parent - Optional parent node.
	 * @param bSnapToGround - Move the location down onto the first surface below it (fires, smoke, puddles).
	 * @return The effect node: id, name, system, location, active flag and its user variable names.
	 * Fails if the system is not found, a user variable does not exist or does not fit its type, or the parent is not a node.
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld, AutoCreateRefTerm = "Location,Rotation,Scale,UserVariables,Tags"), Category = "VFX")
	static FRuntimeVfxInfo SpawnSystem(UNiagaraSystem* System, const FVector& Location, const FRotator& Rotation, const FVector& Scale,
		const FRuntimeToolJson& UserVariables, const TArray<FName>& Tags, const FString& Name = TEXT(""), AActor* Parent = nullptr, bool bSnapToGround = false);

	/**
	 * Plays a system once at a location and lets it destroy itself when it finishes (explosions, impacts, sparks).
	 * Not a scene node and not undoable: it removes itself. Use spawn_system for effects that should stay.
	 *
	 * @param System - Niagara system asset, e.g. "/Game/FX/NS_Explosion.NS_Explosion".
	 * @param Location - World position [x, y, z] in cm.
	 * @param Rotation - [pitch, yaw, roll].
	 * @param Scale - [x, y, z], default [1, 1, 1].
	 * @param UserVariables - User variables by name, e.g. {"Size": 2}.
	 * @param bSnapToGround - Move the location down onto the first surface below it.
	 * @return The system, the final location and whether it loops (a looping system never ends).
	 * Fails if the system is not found or a user variable does not exist or fit.
	 */
	UFUNCTION(meta = (AICallable, RequiresWorld, AutoCreateRefTerm = "Location,Rotation,Scale,UserVariables"), Category = "VFX")
	static FRuntimeVfxOneShot SpawnOneShot(UNiagaraSystem* System, const FVector& Location, const FRotator& Rotation, const FVector& Scale,
		const FRuntimeToolJson& UserVariables, bool bSnapToGround = false);

	/**
	 * Summary of an effect: system, active and looping flags, user variables with values, and one entry per emitter
	 * (enabled, CPU/GPU, renderers). Call it first on an unfamiliar effect.
	 *
	 * @param Target - Effect node or actor (or actor#Component), e.g. "n12".
	 * @return The summary; user variables sorted by name, emitters in system order.
	 * Fails if the target has no Niagara component.
	 */
	UFUNCTION(meta = (AICallable, RequiresWorld), Category = "VFX")
	static FRuntimeVfxSummary GetSummary(UNiagaraComponent* Target);

	/**
	 * Lists the user variables of an effect with types and current values (Epic: GetUserVariables).
	 *
	 * @param Target - Effect node or actor, e.g. "n12".
	 * @return User variables sorted by name.
	 * Fails if the target has no Niagara component.
	 */
	UFUNCTION(meta = (AICallable, RequiresWorld), Category = "VFX|User Variables")
	static TArray<FRuntimeVfxUserVariable> GetUserVariables(UNiagaraComponent* Target);

	/**
	 * Reads one user variable.
	 *
	 * @param Target - Effect node or actor, e.g. "n12".
	 * @param Name - Variable name, "User." prefix optional, e.g. "SpawnRate".
	 * @return The variable with its type and current value.
	 * Fails if the variable does not exist (the error lists the available ones).
	 */
	UFUNCTION(meta = (AICallable, RequiresWorld), Category = "VFX|User Variables")
	static FRuntimeVfxUserVariable GetUserVariable(UNiagaraComponent* Target, const FString& Name);

	/**
	 * Sets a user variable on an effect: a number (float, int), true/false (bool), [x,y,z] (vector, position) or
	 * [r,g,b,a] (color). Undoable.
	 *
	 * @param Target - Effect node or actor, e.g. "n12".
	 * @param Name - Variable name, "User." prefix optional, e.g. "SpawnRate".
	 * @param Value - New value, e.g. 120.
	 * @return The variable after the change and its previous value.
	 * Fails if the variable does not exist, or the value does not fit its type (the error names the expected JSON).
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld), Category = "VFX|User Variables")
	static FRuntimeVfxVariableChange SetUserVariable(UNiagaraComponent* Target, const FString& Name, const FRuntimeToolJson& Value);

	/**
	 * Swaps the Niagara system of an effect, keeping its place in the scene (Epic: SetSystem). Undoable.
	 *
	 * @param Target - Effect node or actor, e.g. "n12".
	 * @param System - The new system, e.g. "/Game/FX/NS_Smoke.NS_Smoke".
	 * @param bResetUserVariables - true: user variables go back to the new system's defaults; false: overrides with matching names are kept.
	 * @return The effect with its new system.
	 * Fails if the target has no Niagara component or the system is not found.
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld), Category = "VFX")
	static FRuntimeVfxState SetSystem(UNiagaraComponent* Target, UNiagaraSystem* System, bool bResetUserVariables = false);

	/**
	 * Starts an effect, or restarts it from the beginning with reset. Undoable.
	 *
	 * @param Target - Effect node or actor, e.g. "n12".
	 * @param bReset - Restart from the beginning.
	 * @return The effect state.
	 * Fails if the target has no Niagara component.
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld), Category = "VFX")
	static FRuntimeVfxState Activate(UNiagaraComponent* Target, bool bReset = false);

	/**
	 * Stops spawning new particles; existing ones finish. Undoable.
	 *
	 * @param Target - Effect node or actor, e.g. "n12".
	 * @return The effect state.
	 * Fails if the target has no Niagara component.
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld), Category = "VFX")
	static FRuntimeVfxState Deactivate(UNiagaraComponent* Target);

	/**
	 * Lists the Niagara effects currently active in the world.
	 *
	 * @param Limit - Maximum entries returned.
	 * @return Total count and the effects (target, system name, location), sorted by target.
	 */
	UFUNCTION(meta = (AICallable, RequiresWorld), Category = "VFX")
	static FRuntimeVfxActiveList ListActive(int32 Limit = 100);

	/** Source spawner for "vfx:<system path>" nodes (duplicate / import / arrays). */
	static AActor* SpawnFromSource(UWorld* World, const FString& Source, const FTransform& Transform, const FString& Name, FString& OutError);
};
