#pragma once

#include "CoreMinimal.h"
#include "RuntimeToolCall.h"
#include "RuntimeToolsetDefinition.h"
#include "RuntimeStateTreeToolset.generated.h"

class AActor;
class UStateTree;

/** A StateTree asset. */
USTRUCT()
struct FRuntimeStateTreeAsset
{
	GENERATED_BODY()

	/** Asset name, e.g. "ST_Guard". */
	UPROPERTY()
	FString Name;

	/** Object path, e.g. "/Game/AI/ST_Guard.ST_Guard" (pass to describe_state_tree or run_state_tree). */
	UPROPERTY()
	FString Path;
};

/** A task, evaluator or condition node of a compiled StateTree. */
USTRUCT()
struct FRuntimeStateTreeNode
{
	GENERATED_BODY()

	/** Node name given in the editor (may be empty). */
	UPROPERTY()
	FString Name;

	/** Node struct type, e.g. "StateTreeDelayTask". */
	UPROPERTY()
	FString Type;
};

/** A transition of a compiled state. */
USTRUCT()
struct FRuntimeStateTreeTransition
{
	GENERATED_BODY()

	/** When it is tried: OnStateCompleted, OnStateSucceeded, OnStateFailed, OnTick, OnEvent or OnDelegate. */
	UPROPERTY()
	FString Trigger;

	/** Event tag that triggers it (OnEvent), empty otherwise. Send it with statetree.send_event. */
	UPROPERTY()
	FString EventTag;

	/** Payload struct the event must carry (empty = none). */
	UPROPERTY()
	FString PayloadType;

	/** Target state path, or Succeeded / Failed / Stopped (tree completion), or "" for none. */
	UPROPERTY()
	FString Target;

	/** Low, Normal, Medium, High or Critical. */
	UPROPERTY()
	FString Priority;

	/** Delay in seconds before it fires (0 = immediate). */
	UPROPERTY()
	float Delay = 0.f;

	/** False if disabled in the asset. */
	UPROPERTY()
	bool bEnabled = true;

	/** Conditions that must pass. */
	UPROPERTY()
	TArray<FRuntimeStateTreeNode> Conditions;
};

/** A compiled state. */
USTRUCT()
struct FRuntimeStateTreeState
{
	GENERATED_BODY()

	/** State name. */
	UPROPERTY()
	FString Name;

	/** Path from the root, e.g. "Root/Combat/Attack" (names joined by /). */
	UPROPERTY()
	FString Path;

	/** State, Group, Linked, LinkedAsset or Subtree. */
	UPROPERTY()
	FString Type;

	/** How children are selected, e.g. TrySelectChildrenInOrder. */
	UPROPERTY()
	FString Selection;

	/** State gameplay tag (may be empty). */
	UPROPERTY()
	FString Tag;

	/** Event tag required to enter the state (may be empty). */
	UPROPERTY()
	FString RequiredEvent;

	/** Linked StateTree asset path for LinkedAsset states. */
	UPROPERTY()
	FString LinkedAsset;

	UPROPERTY()
	TArray<FRuntimeStateTreeNode> Tasks;

	UPROPERTY()
	TArray<FRuntimeStateTreeNode> EnterConditions;

	UPROPERTY()
	TArray<FRuntimeStateTreeTransition> Transitions;
};

/** Runtime-readable structure of a StateTree asset. */
USTRUCT()
struct FRuntimeStateTreeDescription
{
	GENERATED_BODY()

	/** Asset path. */
	UPROPERTY()
	FString Path;

	/** Schema class, e.g. "StateTreeComponentSchema" (statetree.run_state_tree needs this one or a child). */
	UPROPERTY()
	FString Schema;

	/** True if the asset is compiled and linked (it can run). */
	UPROPERTY()
	bool bReadyToRun = false;

	/** Global evaluators. */
	UPROPERTY()
	TArray<FRuntimeStateTreeNode> Evaluators;

	/** Global tasks. */
	UPROPERTY()
	TArray<FRuntimeStateTreeNode> GlobalTasks;

	/** States in tree order (parents before children). */
	UPROPERTY()
	TArray<FRuntimeStateTreeState> States;

	/** Event tags any transition or state listens for, sorted. */
	UPROPERTY()
	TArray<FString> Events;
};

/** Execution state of a StateTree on an actor. */
USTRUCT()
struct FRuntimeStateTreeStatus
{
	GENERATED_BODY()

	/** The actor's node id (or actor:Name). */
	UPROPERTY()
	FString Id;

	/** Actor name. */
	UPROPERTY()
	FString Name;

	/** StateTree asset path running on the actor. */
	UPROPERTY()
	FString Tree;

	/** True if a StateTree component was added by this call (run_state_tree only). */
	UPROPERTY()
	bool bAddedComponent = false;

	/** True while the logic runs. */
	UPROPERTY()
	bool bRunning = false;

	/** True if paused. */
	UPROPERTY()
	bool bPaused = false;

	/** Running, Stopped, Succeeded, Failed or Unset. */
	UPROPERTY()
	FString Status;

	/** Active state names, root first, leaf last (linked trees add their states). */
	UPROPERTY()
	TArray<FString> ActiveStates;

	/** Active state paths per frame, e.g. ["Root/Patrol/MoveTo"]. */
	UPROPERTY()
	TArray<FString> ActivePaths;
};

/**
 * StateTree at runtime: list and inspect compiled StateTree assets, run them on any actor (a StateTreeComponent is added
 * when needed), send events and read the active states. Epic's StateTree toolset only inspects editor data; this one
 * reads the cooked runtime data (state names, tasks, conditions, transitions and events survive cooking; editor-only
 * details such as descriptions, colors, comments and property bindings do not).
 *
 * Workflow:
 * 1. statetree.list_state_trees, then statetree.describe_state_tree to learn the states and the events it listens for.
 * 2. statetree.run_state_tree on an actor (trees using StateTreeComponentSchema).
 * 3. statetree.send_event with the transition event tags; statetree.get_active_states to see where it is.
 * 4. statetree.stop_state_tree / statetree.restart_state_tree; undo of run_state_tree removes the added component.
 * StateTree assets cannot be authored at runtime (compilation is editor-only): build them in the editor.
 */
UCLASS()
class URuntimeStateTreeToolset : public URuntimeToolsetDefinition
{
	GENERATED_BODY()

public:
	virtual FString GetToolsetVersion() const override { return TEXT("1.0.0"); }
	virtual FName GetToolsetName() const override { return TEXT("statetree"); }

	/**
	 * Lists the StateTree assets in the project (asset registry; works in packaged builds).
	 *
	 * @param Filter - Optional case-insensitive substring of the path, e.g. "guard".
	 * @return Assets sorted by path.
	 */
	UFUNCTION(meta = (AICallable), Category = "StateTree")
	static TArray<FRuntimeStateTreeAsset> ListStateTrees(const FString& Filter = TEXT(""));

	/**
	 * Describes a compiled StateTree from its runtime data: schema, global evaluators and tasks, and every state with its
	 * tasks, enter conditions and transitions (trigger, event tag, target), plus the event tags it listens for.
	 *
	 * @param Asset - StateTree asset path, e.g. "/Game/AI/ST_Guard.ST_Guard".
	 * @return The structure. Fails if the asset is not a StateTree.
	 */
	UFUNCTION(meta = (AICallable), Category = "StateTree")
	static FRuntimeStateTreeDescription DescribeStateTree(UStateTree* Asset);

	/**
	 * Runs a StateTree on an actor: adds a StateTreeComponent if the actor has none, sets the tree and starts the logic.
	 * Undoable (stops it and removes the added component, or restores the previous tree).
	 *
	 * @param Target - The actor, e.g. "n12".
	 * @param Asset - StateTree asset path, e.g. "/Game/AI/ST_Guard.ST_Guard". Its schema must be StateTreeComponentSchema (or a child).
	 * @return Execution state after starting (status, active states). Fails if the tree is not compiled, uses another
	 *   schema, or does not start (missing context data).
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld), Category = "StateTree")
	static FRuntimeStateTreeStatus RunStateTree(AActor* Target, UStateTree* Asset);

	/**
	 * Stops the StateTree running on an actor.
	 *
	 * @param Target - Actor with a StateTreeComponent, e.g. "n12".
	 * @return Execution state afterwards. Fails if the actor has no StateTreeComponent.
	 */
	UFUNCTION(meta = (AICallable, RequiresWorld), Category = "StateTree")
	static FRuntimeStateTreeStatus StopStateTree(AActor* Target);

	/**
	 * Restarts the StateTree on an actor from its root (starts it if stopped).
	 *
	 * @param Target - Actor with a StateTreeComponent, e.g. "n12".
	 * @return Execution state afterwards. Fails if the actor has no StateTreeComponent or no tree.
	 */
	UFUNCTION(meta = (AICallable, RequiresWorld), Category = "StateTree")
	static FRuntimeStateTreeStatus RestartStateTree(AActor* Target);

	/**
	 * Sends an event to the StateTree running on an actor. Transitions listening for the tag fire on the next tree update.
	 *
	 * @param Target - Actor running a StateTree, e.g. "n12".
	 * @param Tag - Event gameplay tag, e.g. "AI.Event.Alert" (statetree.describe_state_tree lists the events a tree uses).
	 * @param Payload - Optional payload fields as JSON, converted to the payload struct the tree's transitions expect for
	 *   this tag, e.g. {"location": [100, 0, 0]}.
	 * @param Origin - Optional name of the sender.
	 * @return Execution state after sending. Fails if the tag is unknown (close matches), the tree is not running, or the
	 *   payload does not fit.
	 */
	UFUNCTION(meta = (AICallable, RequiresWorld, AutoCreateRefTerm = "Payload"), Category = "StateTree")
	static FRuntimeStateTreeStatus SendEvent(AActor* Target, const FString& Tag, const FRuntimeToolJson& Payload, const FString& Origin = TEXT(""));

	/**
	 * Returns the StateTree execution state on an actor: run status and active states.
	 *
	 * @param Target - Actor with a StateTreeComponent, e.g. "n12".
	 * @return Status, active state names (root to leaf) and paths. Fails if the actor has no StateTreeComponent.
	 */
	UFUNCTION(meta = (AICallable, RequiresWorld), Category = "StateTree")
	static FRuntimeStateTreeStatus GetActiveStates(AActor* Target);
};
