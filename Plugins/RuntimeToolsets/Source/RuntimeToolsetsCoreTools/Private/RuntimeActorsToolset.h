#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "RuntimeSceneTypes.h"
#include "RuntimeToolsetDefinition.h"
#include "RuntimeActorsToolset.generated.h"

class AActor;

/** Coordinate space of a transform. */
UENUM()
enum class ERuntimeActorSpace : uint8
{
	/** World coordinates. */
	World,
	/** Relative to the attach parent (same as world for unattached actors). */
	Relative,
};

/** One actor found by find_actors. */
USTRUCT()
struct FRuntimeActorSummary
{
	GENERATED_BODY()

	/** Reference to pass to other tools: node id for tool-created actors, else actor:Name. */
	UPROPERTY()
	FString Ref;

	/** Node name for tool-created actors, else the engine object name. */
	UPROPERTY()
	FString Name;

	UPROPERTY()
	FString Class;

	/** World location in cm. */
	UPROPERTY()
	FVector Location = FVector::ZeroVector;

	/** World rotation [pitch, yaw, roll] in degrees. */
	UPROPERTY()
	FRotator Rotation = FRotator::ZeroRotator;

	UPROPERTY()
	TArray<FString> Tags;

	/** Hidden in game. */
	UPROPERTY()
	bool bHidden = false;
};

USTRUCT()
struct FRuntimeActorList
{
	GENERATED_BODY()

	/** Number of matching actors (may exceed the returned list). */
	UPROPERTY()
	int32 Total = 0;

	/** Matches in world iteration order, at most `limit`. */
	UPROPERTY()
	TArray<FRuntimeActorSummary> Actors;

	/** True when more actors matched than were returned. */
	UPROPERTY()
	bool bTruncated = false;
};

USTRUCT()
struct FRuntimeDestroyResult
{
	GENERATED_BODY()

	/** References of the deleted actors (the target and its node subtree). */
	UPROPERTY()
	TArray<FString> Deleted;
};

USTRUCT()
struct FRuntimeAttachResult
{
	GENERATED_BODY()

	/** The attached (or detached) actor. */
	UPROPERTY()
	FString Child;

	/** The new parent, empty when detached. */
	UPROPERTY()
	FString Parent;
};

USTRUCT()
struct FRuntimeVisibilityResult
{
	GENERATED_BODY()

	UPROPERTY()
	FString Target;

	UPROPERTY()
	bool bVisible = true;

	/** Actors changed (the target plus attached actors when propagating). */
	UPROPERTY()
	int32 Actors = 0;
};

/** One component of an actor. */
USTRUCT()
struct FRuntimeComponentInfo
{
	GENERATED_BODY()

	/** Component address ("n12#StaticMeshComponent0"): pass it as `target` to reflection tools or as a component argument. */
	UPROPERTY()
	FString Ref;

	UPROPERTY()
	FString Name;

	UPROPERTY()
	FString Class;

	/** Name of the scene component it is attached to (empty for the root and non-scene components). */
	UPROPERTY()
	FString Parent;

	/** The actor's root component. */
	UPROPERTY()
	bool bRoot = false;

	/** Scene components: visible flag (true for non-scene components). */
	UPROPERTY()
	bool bVisible = true;
};

USTRUCT()
struct FRuntimeComponentList
{
	GENERATED_BODY()

	/** The actor. */
	UPROPERTY()
	FString Actor;

	/** Number of matching components (may exceed the list). */
	UPROPERTY()
	int32 Total = 0;

	/** Components in the actor's order, at most `limit`. */
	UPROPERTY()
	TArray<FRuntimeComponentInfo> Components;
};

/**
 * Actors: spawn, find, move, attach, hide, inspect and destroy actors in the running world. Spawned actors become
 * scene-graph nodes (ids like "n12") that every other toolset accepts; level actors are addressed as actor:Name.
 * Workflow: find_actors (or spawn_actor) -> set_transform / attach / set_visibility -> list_components to reach a
 * component's properties with reflection tools. Use the scene toolset to organise nodes into groups.
 */
UCLASS()
class URuntimeActorsToolset : public URuntimeToolsetDefinition
{
	GENERATED_BODY()

public:
	virtual FString GetToolsetVersion() const override { return TEXT("2.0.0"); }

	/**
	 * Spawns a basic shape or an actor class as a new scene node. Undoable. With a parent the transform is relative to it.
	 * Fails if both or neither of shape and class are given, the shape is unknown, or the class is missing or not allowed.
	 *
	 * @param Shape - Basic 100 cm shape: cube, sphere, cylinder, cone or plane.
	 * @param Class - Actor class path instead of a shape, e.g. "/Script/Engine.PointLight" or "/Game/BP_Door".
	 * @param Name - Node name (made unique among siblings), e.g. "crate". Defaults to the shape or class name.
	 * @param Parent - Parent node; the transform is then relative to it.
	 * @param Location - [x, y, z] in cm (default origin).
	 * @param Rotation - [pitch, yaw, roll] in degrees.
	 * @param Scale - [x, y, z] (default [1, 1, 1]).
	 * @param Color - Shape color [r, g, b] 0-1, e.g. [1, 0.2, 0.1].
	 * @param Tags - Actor tags, e.g. ["prop"].
	 * @param Properties - Class only: property values set before the actor is constructed, so Blueprints that build
	 *                     themselves in their construction script (roads, fences, generators) build from them, e.g.
	 *                     {"Spawn Spline Points": [[0,0,0],[5000,0,0]], "Sidewalk R": true}. Names as reflection lists them.
	 * @return The new node: {id, name, path, actor}.
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld, AutoCreateRefTerm = "Location,Rotation,Scale,Color,Tags,Properties"), Category = "Actors")
	static FRuntimeNodeInfo SpawnActor(const FVector& Location, const FRotator& Rotation, const FVector& Scale, const FLinearColor& Color,
		const TArray<FString>& Tags, const FRuntimeToolJson& Properties, const FString& Shape = TEXT(""), const FString& Class = TEXT(""),
		const FString& Name = TEXT(""), AActor* Parent = nullptr);

	/**
	 * Deletes an actor and its node subtree. Undoable (the actors are hidden until the step leaves history).
	 * Fails for level actors unless the project allows it (bAllowDestroyLevelActors); hide them with set_visibility instead.
	 *
	 * @param Target - Actor to delete, e.g. "n12" or "actor:PointLight_0".
	 * @return References of the deleted actors.
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld), Category = "Actors")
	static FRuntimeDestroyResult DestroyActor(AActor* Target);

	/**
	 * Searches the world for actors by name, class, tag, subtree and bounds (Epic's scene.find_actors).
	 *
	 * @param Name - Name filter: a wildcard ("Light*") or a case-insensitive substring of the actor or node name.
	 * @param Class - Class name wildcard, matched against the class and its parents, e.g. "*Light*" or "StaticMeshActor".
	 * @param Tag - Only actors with this tag.
	 * @param Root - Only this actor and the actors attached below it.
	 * @param Bounds - Only actors whose bounds overlap this world box {min:[x,y,z], max:[x,y,z]}.
	 * @param CollisionChannels - With bounds: use a physics overlap restricted to these object channels (e.g. ["ECC_WorldStatic",
	 *        "ECC_PhysicsBody"]); actors without collision are then ignored.
	 * @param bIncludeHidden - Include hidden level actors (hidden tool-created nodes are always included).
	 * @param Limit - Maximum actors returned (0 = project default).
	 * @return Total match count and the actors {ref, name, class, location, rotation, tags, hidden}, in world order.
	 */
	UFUNCTION(meta = (AICallable, RequiresWorld, AutoCreateRefTerm = "Bounds,CollisionChannels"), Category = "Actors")
	static FRuntimeActorList FindActors(const FRuntimeBox& Bounds, const TArray<TEnumAsByte<ECollisionChannel>>& CollisionChannels,
		const FString& Name = TEXT(""), const FString& Class = TEXT(""), const FString& Tag = TEXT(""), AActor* Root = nullptr,
		bool bIncludeHidden = false, int32 Limit = 25);

	/**
	 * Sets an actor's location, rotation and/or scale; parts you omit stay unchanged and attached children follow. Undoable.
	 *
	 * @param Target - Actor to move.
	 * @param Location - [x, y, z] in cm.
	 * @param Rotation - [pitch, yaw, roll] in degrees.
	 * @param Scale - [x, y, z].
	 * @param Space - World, or Relative to the attach parent.
	 * @return The actor's resulting world transform.
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld, AutoCreateRefTerm = "Location,Rotation,Scale"), Category = "Actors")
	static FRuntimeTransformResult SetTransform(AActor* Target, const FVector& Location, const FRotator& Rotation, const FVector& Scale,
		ERuntimeActorSpace Space = ERuntimeActorSpace::World);

	/**
	 * Attaches an actor to another actor, or detaches it when parent is omitted. Undoable. For tool-created nodes this also
	 * moves the node in the scene graph. Fails if the parent is the child or attached below it.
	 *
	 * @param Child - Actor to attach.
	 * @param Parent - New parent actor; omit (or "") to detach.
	 * @param Socket - Socket or bone name on the parent's root component, e.g. "hand_r".
	 * @param bKeepWorld - Keep the world transform (false keeps the relative transform).
	 * @return Child and new parent references.
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld), Category = "Actors")
	static FRuntimeAttachResult Attach(AActor* Child, AActor* Parent = nullptr, const FString& Socket = TEXT(""), bool bKeepWorld = true);

	/**
	 * Shows or hides an actor in game (and, by default, the actors attached to it). Undoable.
	 *
	 * @param Target - Actor to show or hide.
	 * @param bVisible - True to show, false to hide.
	 * @param bPropagate - Also apply to attached actors.
	 * @return The target, the new visibility and how many actors changed.
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld), Category = "Actors")
	static FRuntimeVisibilityResult SetVisibility(AActor* Target, bool bVisible, bool bPropagate = true);

	/**
	 * Lists an actor's components. Each ref ("n12#LightComponent0") can be passed directly as a target to reflection tools
	 * (list_properties, set_properties, call_function) or to any tool that takes a component.
	 *
	 * @param Target - Actor to inspect.
	 * @param Class - Only components of this class (wildcard, parents included), e.g. "*Light*".
	 * @param Limit - Maximum components returned (0 = project default).
	 * @return The actor reference, total count and components {ref, name, class, parent, root, visible}.
	 */
	UFUNCTION(meta = (AICallable, RequiresWorld), Category = "Actors")
	static FRuntimeComponentList ListComponents(AActor* Target, const FString& Class = TEXT(""), int32 Limit = 100);

	/**
	 * Adds any engine component to an actor and sets its properties: movement (ProjectileMovementComponent,
	 * RotatingMovementComponent, InterpToMovementComponent), shapes and triggers (SphereComponent, BoxComponent,
	 * CapsuleComponent), lights (PointLightComponent, SpotLightComponent), AudioComponent, PhysicsConstraintComponent,
	 * PhysicsThrusterComponent, RadialForceComponent... Scene components attach to the root (or attach_to) at location.
	 * Change it later with reflection.set_properties / call_function on the returned ref.
	 *
	 * @param Target - Actor or node id, e.g. "n12".
	 * @param Class - Component class name, e.g. "RotatingMovementComponent" (the U prefix is optional).
	 * @param Name - Component name (default: generated).
	 * @param Properties - Property values, e.g. {"RotationRate": {"Yaw": 90}} or {"InitialSpeed": 1200, "ProjectileGravityScale": 0}.
	 * @param Location - Relative location of a scene component.
	 * @param AttachTo - Name of the scene component to attach to (default: the root).
	 * @return The new component. Fails if the class is unknown, abstract or not a component, or a property does not apply.
	 */
	UFUNCTION(meta = (AICallable, RequiresWorld, AutoCreateRefTerm = "Name,Properties"), Category = "Actors")
	static FRuntimeComponentInfo AddComponent(AActor* Target, const FString& Class, const FString& Name, const FRuntimeToolJson& Properties,
		FVector Location = FVector::ZeroVector, const FString& AttachTo = TEXT(""));

	/**
	 * Removes a component (not the root).
	 *
	 * @param Target - Component ref "actor#Component", e.g. "n12#RotatingMovement".
	 * Fails if it is the actor's root component.
	 */
	UFUNCTION(meta = (AICallable, Irreversible, RequiresWorld), Category = "Actors")
	static void RemoveComponent(UActorComponent* Target);
};
