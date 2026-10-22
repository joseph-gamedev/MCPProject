#pragma once

#include "CoreMinimal.h"
#include "RuntimeAgent.h"
#include "RuntimeToolCall.h"
#include "RuntimeToolsetDefinition.h"
#include "RuntimeAiToolset.generated.h"

class AActor;
class UBehaviorTree;

USTRUCT()
struct FRuntimeNavigationStatus
{
	GENERATED_BODY()

	/** A navmesh exists in the world. */
	UPROPERTY()
	bool bHasNavMesh = false;

	/** Tiles are still being generated. */
	UPROPERTY()
	bool bBuilding = false;

	/** The navmesh rebuilds at runtime when geometry changes (RecastNavMesh RuntimeGeneration = Dynamic). */
	UPROPERTY()
	bool bDynamic = false;

	/** Area covered by registered navigation bounds. */
	UPROPERTY()
	FVector BoundsMin = FVector::ZeroVector;

	UPROPERTY()
	FVector BoundsMax = FVector::ZeroVector;

	/** What to do next when something is missing. */
	UPROPERTY()
	FString Hint;
};

USTRUCT()
struct FRuntimeNavPath
{
	GENERATED_BODY()

	/** A path (complete or partial) was found. */
	UPROPERTY()
	bool bFound = false;

	/** The path stops short of the goal (goal unreachable). */
	UPROPERTY()
	bool bPartial = false;

	/** Path length in cm. */
	UPROPERTY()
	float Length = 0.f;

	/** Path corners from start to end. */
	UPROPERTY()
	TArray<FVector> Points;
};

USTRUCT()
struct FRuntimeAgentInfo
{
	GENERATED_BODY()

	/** The agent pawn (pass it, or the object it carries, as `agent`). */
	UPROPERTY()
	TObjectPtr<AActor> Agent = nullptr;

	/** The object the agent carries (null for plain characters). */
	UPROPERTY()
	TObjectPtr<AActor> Body = nullptr;

	/** Node id of the carried object, when it is a scene node. */
	UPROPERTY()
	FString Id;

	UPROPERTY()
	FVector Location = FVector::ZeroVector;

	/** Idle, Waiting, Paused or Moving. */
	UPROPERTY()
	FString Movement;

	/** Behavior tree being run, if any. */
	UPROPERTY()
	FString BehaviorTree;

	UPROPERTY()
	FRuntimeAgentSettings Settings;
};

UENUM()
enum class ERuntimeMoveResult : uint8
{
	Success,
	/** Something blocked the agent on the way. */
	Blocked,
	/** Interrupted by another move or stop_movement. */
	Aborted,
	/** No path to the goal. */
	Unreachable,
	/** Timed out before arriving. */
	TimedOut,
};

USTRUCT()
struct FRuntimeMoveOutcome
{
	GENERATED_BODY()

	UPROPERTY()
	ERuntimeMoveResult Result = ERuntimeMoveResult::Success;

	/** Where the agent ended up. */
	UPROPERTY()
	FVector Location = FVector::ZeroVector;

	/** Seconds the move took. */
	UPROPERTY()
	float Seconds = 0.f;
};

UCLASS(BlueprintType)
class URuntimeMoveAsyncResult : public URuntimeToolAsyncResult
{
	GENERATED_BODY()

public:
	bool SetValue(const FRuntimeMoveOutcome& InValue)
	{
		Value = InValue;
		return Complete();
	}

	UPROPERTY()
	FRuntimeMoveOutcome Value;
};

UCLASS(BlueprintType)
class URuntimeNavBuildAsyncResult : public URuntimeToolAsyncResult
{
	GENERATED_BODY()

public:
	bool SetValue(const FRuntimeNavigationStatus& InValue)
	{
		Value = InValue;
		return Complete();
	}

	UPROPERTY()
	FRuntimeNavigationStatus Value;
};

USTRUCT()
struct FRuntimePerceivedActor
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<AActor> Actor = nullptr;

	/** Sight or Hearing. */
	UPROPERTY()
	FString Sense;

	/** Where the stimulus came from (for hearing: the noise location). */
	UPROPERTY()
	FVector Location = FVector::ZeroVector;

	/** Seconds since it was sensed. */
	UPROPERTY()
	float Age = 0.f;

	/** True while currently sensed (seen now), false when remembered. */
	UPROPERTY()
	bool bCurrentlySensed = false;

	/** Noise tag (hearing). */
	UPROPERTY()
	FString Tag;
};

USTRUCT()
struct FRuntimeBlackboardEntry
{
	GENERATED_BODY()

	UPROPERTY()
	FString Key;

	/** Bool, Int, Float, String, Name, Vector, Rotator, Object, Class, Enum. */
	UPROPERTY()
	FString Type;

	/** Current value as text ("" when the agent runs no tree). */
	UPROPERTY()
	FString Value;
};

USTRUCT()
struct FRuntimeBrainState
{
	GENERATED_BODY()

	/** A behavior tree is running. */
	UPROPERTY()
	bool bRunning = false;

	UPROPERTY()
	FString BehaviorTree;

	/** The task or node currently executing. */
	UPROPERTY()
	FString ActiveNode;

	UPROPERTY()
	TArray<FRuntimeBlackboardEntry> Blackboard;
};

USTRUCT()
struct FRuntimeBtNode
{
	GENERATED_BODY()

	/** Execution index (depth-first). */
	UPROPERTY()
	int32 Index = 0;

	/** Depth in the tree (root = 0). */
	UPROPERTY()
	int32 Depth = 0;

	UPROPERTY()
	FString Name;

	/** Composite, Task, Decorator or Service. */
	UPROPERTY()
	FString Kind;

	UPROPERTY()
	FString Class;
};

USTRUCT()
struct FRuntimeBtDescription
{
	GENERATED_BODY()

	UPROPERTY()
	FString Name;

	UPROPERTY()
	TArray<FRuntimeBtNode> Nodes;

	/** Keys of the tree's blackboard. */
	UPROPERTY()
	TArray<FRuntimeBlackboardEntry> BlackboardKeys;
};

/**
 * Real Unreal AI at runtime: navigation meshes, AI-controlled agents that walk on them, AI perception (sight and
 * hearing), noise events, behavior trees and blackboards. Any builder object can become an agent: it is carried by an
 * AI-controlled character. Project characters with their own controllers work too.
 * Workflow: build_navigation (once the level geometry exists) -> create_agent -> move_to / run_behavior_tree ->
 * get_perception, report_noise, get_brain_state. Logic brains can use this with define_brain backend "ai".
 * See the ai_navigation_and_perception skill.
 */
UCLASS()
class URuntimeAiToolset : public URuntimeToolsetDefinition
{
	GENERATED_BODY()

public:
	virtual FString GetToolsetVersion() const override { return TEXT("1.0.0"); }

	// Navigation ---------------------------------------------------------------------------------------------------

	/**
	 * Builds a navmesh over an area so agents can find paths, and waits until it is ready. Call it after the walkable
	 * geometry exists; it updates by itself when things move later (dynamic generation).
	 *
	 * @param Bounds - Area {min:[x,y,z], max:[x,y,z]} in cm; default: everything in the scene plus a margin.
	 * @param Margin - Extra space around the automatic bounds (cm).
	 * @param TimeoutSeconds - Give up waiting after this long.
	 * @return Navigation status (navmesh present, building, bounds, a hint if something is missing).
	 */
	UFUNCTION(meta = (AICallable, RequiresWorld, AutoCreateRefTerm = "Bounds"), Category = "AI|Navigation")
	static URuntimeNavBuildAsyncResult* BuildNavigation(const FBox& Bounds, float Margin = 1000.f, float TimeoutSeconds = 60.f);

	/**
	 * Reports whether a navmesh exists, whether it is still building, and the registered bounds.
	 */
	UFUNCTION(meta = (AICallable, RequiresWorld), Category = "AI|Navigation")
	static FRuntimeNavigationStatus GetNavigationStatus();

	/**
	 * Finds a walkable path between two points.
	 *
	 * @param Start - [x,y,z] start (projected onto the navmesh).
	 * @param End - [x,y,z] goal.
	 * @return Found, partial, length (cm) and path corners. Fails if there is no navmesh.
	 */
	UFUNCTION(meta = (AICallable, RequiresWorld), Category = "AI|Navigation")
	static FRuntimeNavPath FindPath(const FVector& Start, const FVector& End);

	/**
	 * Snaps a point to the nearest walkable navmesh location.
	 *
	 * @param Location - [x,y,z].
	 * @param SearchExtent - How far to search in each axis (cm).
	 * @return The projected point; fails if nothing walkable is within reach.
	 */
	UFUNCTION(meta = (AICallable, RequiresWorld), Category = "AI|Navigation")
	static FVector ProjectToNavigation(const FVector& Location, float SearchExtent = 500.f);

	// Agents -------------------------------------------------------------------------------------------------------

	/**
	 * Makes an actor an AI agent: builder objects are carried by an AI-controlled character sized to them (they keep
	 * their node id); pawns get an AI controller. Configures movement speed and perception. Undoable.
	 *
	 * @param Target - Object or pawn to control.
	 * @param Settings - {max_speed, sight_radius, lose_sight_radius, sight_half_angle, hearing_range, memory_seconds, turn_rate}.
	 * @return The agent.
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld, AutoCreateRefTerm = "Settings"), Category = "AI|Agents")
	static FRuntimeAgentInfo CreateAgent(AActor* Target, const FRuntimeAgentSettings& Settings);

	/**
	 * Stops AI control of an object made into an agent with create_agent (it stays where it is). Undoable.
	 *
	 * @param Agent - The agent or the object it carries.
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld), Category = "AI|Agents")
	static bool RemoveAgent(AActor* Agent);

	/**
	 * Lists AI-controlled pawns in the world.
	 *
	 * @param Limit - Maximum number returned.
	 * @return Agents sorted by name.
	 */
	UFUNCTION(meta = (AICallable, RequiresWorld), Category = "AI|Agents")
	static TArray<FRuntimeAgentInfo> ListAgents(int32 Limit = 100);

	/**
	 * Walks an agent to a location or to another actor along the navmesh, and waits until it arrives or fails.
	 *
	 * @param Agent - The agent or the object it carries.
	 * @param Location - [x,y,z] goal (ignored when target_actor is set).
	 * @param TargetActor - Follow to this actor instead.
	 * @param AcceptanceRadius - How close counts as arrived (cm).
	 * @param TimeoutSeconds - Stop waiting after this long (the agent keeps going).
	 * @return Success, Blocked, Aborted, Unreachable or TimedOut, with the final location.
	 */
	UFUNCTION(meta = (AICallable, RequiresWorld, AutoCreateRefTerm = "Location"), Category = "AI|Agents")
	static URuntimeMoveAsyncResult* MoveTo(AActor* Agent, const FVector& Location, AActor* TargetActor = nullptr, float AcceptanceRadius = 50.f,
		float TimeoutSeconds = 30.f);

	/**
	 * Stops an agent's current move.
	 *
	 * @param Agent - The agent or the object it carries.
	 */
	UFUNCTION(meta = (AICallable, RequiresWorld), Category = "AI|Agents")
	static bool StopMovement(AActor* Agent);

	// Perception ---------------------------------------------------------------------------------------------------

	/**
	 * What an agent currently sees and hears (and remembers for memory_seconds).
	 *
	 * @param Agent - The agent or the object it carries.
	 * @return Perceived actors with sense, location, age and whether they are sensed right now.
	 */
	UFUNCTION(meta = (AICallable, RequiresWorld), Category = "AI|Perception")
	static TArray<FRuntimePerceivedActor> GetPerception(AActor* Agent);

	/**
	 * Lets agents see an object that is not a pawn (pawns, including the player, are visible already). Undoable.
	 *
	 * @param Target - Object to make visible to AI sight.
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld), Category = "AI|Perception")
	static bool MakePerceivable(AActor* Target);

	/**
	 * Emits a noise that agents within their hearing range (and max_range) hear: gunshots, footsteps, alarms.
	 *
	 * @param Location - [x,y,z] where the noise happens.
	 * @param Loudness - Multiplies the agents' hearing range (1 = normal).
	 * @param MaxRange - Hard limit in cm (0 = no limit).
	 * @param Instigator - Who made the noise (agents remember it as the heard actor).
	 * @param Tag - Kind of noise, e.g. "Gunshot".
	 */
	UFUNCTION(meta = (AICallable, RequiresWorld), Category = "AI|Perception")
	static bool ReportNoise(const FVector& Location, float Loudness = 1.f, float MaxRange = 0.f, AActor* Instigator = nullptr, const FString& Tag = TEXT(""));

	// Behavior trees -----------------------------------------------------------------------------------------------

	/**
	 * Lists behavior tree assets packaged with the project.
	 *
	 * @return Asset paths, sorted.
	 */
	UFUNCTION(meta = (AICallable), Category = "AI|BehaviorTree")
	static TArray<FString> ListBehaviorTrees();

	/**
	 * Describes a behavior tree: every node with depth, kind and class, and its blackboard keys (Epic: list_nodes, get_blackboard).
	 *
	 * @param Tree - Behavior tree asset path.
	 */
	UFUNCTION(meta = (AICallable), Category = "AI|BehaviorTree")
	static FRuntimeBtDescription DescribeBehaviorTree(UBehaviorTree* Tree);

	/**
	 * Runs a behavior tree on an agent (replaces the one it runs).
	 *
	 * @param Agent - The agent or the object it carries.
	 * @param Tree - Behavior tree asset path.
	 */
	UFUNCTION(meta = (AICallable, RequiresWorld), Category = "AI|BehaviorTree")
	static FRuntimeBrainState RunBehaviorTree(AActor* Agent, UBehaviorTree* Tree);

	/**
	 * Stops the agent's behavior tree.
	 *
	 * @param Agent - The agent or the object it carries.
	 */
	UFUNCTION(meta = (AICallable, RequiresWorld), Category = "AI|BehaviorTree")
	static bool StopBehaviorTree(AActor* Agent);

	/**
	 * The agent's running tree, active node and blackboard values.
	 *
	 * @param Agent - The agent or the object it carries.
	 */
	UFUNCTION(meta = (AICallable, RequiresWorld), Category = "AI|BehaviorTree")
	static FRuntimeBrainState GetBrainState(AActor* Agent);

	/**
	 * Sets a blackboard value on an agent running a behavior tree.
	 *
	 * @param Agent - The agent or the object it carries.
	 * @param Key - Blackboard key name (see get_brain_state).
	 * @param Value - Value matching the key type: true, 3, 2.5, "text", [x,y,z], or an actor reference for Object keys.
	 * @return The entry after the change.
	 */
	UFUNCTION(meta = (AICallable, RequiresWorld), Category = "AI|BehaviorTree")
	static FRuntimeBlackboardEntry SetBlackboardValue(AActor* Agent, const FString& Key, const FRuntimeToolJson& Value);
};
