#pragma once

#include "CoreMinimal.h"
#include "RuntimeToolCall.h"
#include "RuntimeToolsetDefinition.h"
#include "RuntimeInputToolset.generated.h"

/** What happens to the key. */
UENUM()
enum class EInputToolKeyEvent : uint8
{
	/** Press now, release after `duration` (at least one frame). */
	Tap,
	/** Press and keep it down (send Release later). */
	Press,
	/** Release a pressed key. */
	Release,
	/** One axis sample with `value` (mouse, sticks). */
	Axis,
};

USTRUCT()
struct FInputToolKeyResult
{
	GENERATED_BODY()

	/** Canonical key name, e.g. "SpaceBar". */
	UPROPERTY()
	FString Key;

	UPROPERTY()
	EInputToolKeyEvent Event = EInputToolKeyEvent::Tap;

	/** Whether the player's input stack consumed the (first) event. */
	UPROPERTY()
	bool bHandled = false;
};

USTRUCT()
struct FInputToolActionResult
{
	GENERATED_BODY()

	/** Path of the triggered input action. */
	UPROPERTY()
	FString Action;

	/** Frames the action value was injected on. */
	UPROPERTY()
	int32 Frames = 0;
};

USTRUCT()
struct FInputToolMappingResult
{
	GENERATED_BODY()

	/** Path of the mapping context. */
	UPROPERTY()
	FString Context;

	/** Whether the context is applied to the local player now. */
	UPROPERTY()
	bool bApplied = false;
};

/** Completes when a simulated key event is done (after the hold time for taps). */
UCLASS()
class URuntimeInputToolKeyAsyncResult : public URuntimeToolAsyncResult
{
	GENERATED_BODY()

public:
	bool SetValue(const FInputToolKeyResult& InValue)
	{
		Value = InValue;
		return Complete();
	}

	UPROPERTY()
	FInputToolKeyResult Value;
};

/** Completes when an injected action has been held for its duration. */
UCLASS()
class URuntimeInputToolActionAsyncResult : public URuntimeToolAsyncResult
{
	GENERATED_BODY()

public:
	bool SetValue(const FInputToolActionResult& InValue)
	{
		Value = InValue;
		return Complete();
	}

	UPROPERTY()
	FInputToolActionResult Value;
};

/**
 * Simulated player input for the local player: raw keys/buttons/axes and Enhanced Input actions and mapping contexts.
 * Workflow: inject_key for keys the game binds directly (W, SpaceBar, LeftMouseButton, Gamepad_FaceButton_Bottom);
 * inject_action to trigger an Enhanced Input action by asset path regardless of bindings; add_mapping_context /
 * remove_mapping_context to switch control schemes. Injected input cannot be undone; mapping changes can.
 * Needs a local player controller (a running game or PIE).
 */
UCLASS()
class URuntimeInputToolset : public URuntimeToolsetDefinition
{
	GENERATED_BODY()

public:
	virtual FString GetToolsetVersion() const override { return TEXT("2.0.0"); }

	/**
	 * Simulates a key, button or axis for the local player: tap (press, wait, release), press, release or axis.
	 * Completes after the release for taps.
	 *
	 * @param Key - UE key name, e.g. "W", "SpaceBar", "LeftMouseButton", "Gamepad_FaceButton_Bottom", "MouseX".
	 * @param Event - Tap, Press, Release or Axis.
	 * @param Duration - Seconds to hold for Tap (0..30, at least one frame), e.g. 0.5.
	 * @param Value - Axis value for Event = Axis, e.g. -1.
	 * @return The canonical key name, the event and whether the input stack handled it.
	 * Fails if there is no local player or the key name is unknown.
	 */
	UFUNCTION(meta = (AICallable, Irreversible, RequiresWorld), Category = "Input")
	static URuntimeInputToolKeyAsyncResult* InjectKey(const FString& Key, EInputToolKeyEvent Event = EInputToolKeyEvent::Tap, float Duration = 0.1f,
		float Value = 1.f);

	/**
	 * Triggers an Enhanced Input action with a value, re-injected every frame for the duration (one frame by default).
	 *
	 * @param Action - Input action asset path, e.g. "/Game/Input/IA_Jump.IA_Jump".
	 * @param Value - true, a number, [x,y] or [x,y,z] matching the action's value type (default true).
	 * @param Duration - Seconds to keep injecting (0..30), e.g. 1.5 to hold a move action.
	 * @return The action path and how many frames it was injected on.
	 * Fails if there is no local player, Enhanced Input is not active, the action is not found (cooked?) or the value has the wrong shape.
	 */
	UFUNCTION(meta = (AICallable, Irreversible, RequiresWorld, AutoCreateRefTerm = "Value"), Category = "Input")
	static URuntimeInputToolActionAsyncResult* InjectAction(const FString& Action, const FRuntimeToolJson& Value, float Duration = 0.f);

	/**
	 * Applies an Enhanced Input mapping context to the local player. Undoable.
	 *
	 * @param Context - Mapping context asset path, e.g. "/Game/Input/IMC_Vehicle.IMC_Vehicle".
	 * @param Priority - Higher priority contexts win conflicting keys (-1000..1000).
	 * @return The context path and whether it is applied.
	 * Fails if there is no local player, Enhanced Input is not active or the context is not found.
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld), Category = "Input")
	static FInputToolMappingResult AddMappingContext(const FString& Context, int32 Priority = 0);

	/**
	 * Removes an Enhanced Input mapping context from the local player. Undoable.
	 *
	 * @param Context - Mapping context asset path, e.g. "/Game/Input/IMC_Vehicle.IMC_Vehicle".
	 * @return The context path and applied = false.
	 * Fails if there is no local player or the context is not applied.
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld), Category = "Input")
	static FInputToolMappingResult RemoveMappingContext(const FString& Context);
};
