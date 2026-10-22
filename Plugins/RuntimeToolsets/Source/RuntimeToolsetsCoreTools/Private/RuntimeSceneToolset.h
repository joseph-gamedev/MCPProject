#pragma once

#include "CoreMinimal.h"
#include "RuntimeSceneTypes.h"
#include "RuntimeToolCall.h"
#include "RuntimeToolsetDefinition.h"
#include "RuntimeSceneToolset.generated.h"

class AActor;

/** Space for scene node moves and rotations. */
UENUM()
enum class ERuntimeNodeSpace : uint8
{
	/** World coordinates and world axes. */
	World,
	/** Relative to the parent node (absolute values) or along the node's own axes (offsets and deltas). */
	Local,
};

/** How set_node_tags applies the given tags. */
UENUM()
enum class ERuntimeTagMode : uint8
{
	/** Replace the node's tags. */
	Set,
	/** Add the tags, keeping existing ones. */
	Add,
	/** Remove the tags. */
	Remove,
};

/** Node kind filter for find_nodes. */
UENUM()
enum class ERuntimeNodeKindFilter : uint8
{
	Any,
	/** Empty group nodes (pivots for children). */
	Group,
	/** Actor nodes (shapes, classes, builder objects...). */
	Actor,
};

/** One node found by find_nodes. */
USTRUCT()
struct FRuntimeNodeSummary
{
	GENERATED_BODY()

	/** Node id, e.g. "n12". */
	UPROPERTY()
	FString Id;

	UPROPERTY()
	FString Name;

	/** Path from the root, e.g. "house/door". */
	UPROPERTY()
	FString Path;

	/** "group" or "actor". */
	UPROPERTY()
	FString Kind;

	UPROPERTY()
	TArray<FString> Tags;
};

USTRUCT()
struct FRuntimeNodeList
{
	GENERATED_BODY()

	/** Number of matching nodes (may exceed the list). */
	UPROPERTY()
	int32 Total = 0;

	/** Matches in tree order (pre-order), at most `limit`. */
	UPROPERTY()
	TArray<FRuntimeNodeSummary> Nodes;
};

/** World-space bounds of a node and its visible children. */
USTRUCT()
struct FRuntimeNodeBounds
{
	GENERATED_BODY()

	/** Box center [x, y, z] in cm. */
	UPROPERTY()
	FVector Center = FVector::ZeroVector;

	/** Box size [x, y, z] in cm. */
	UPROPERTY()
	FVector Size = FVector::ZeroVector;
};

/** Everything about one node. */
USTRUCT()
struct FRuntimeNodeDetails
{
	GENERATED_BODY()

	UPROPERTY()
	FString Id;

	UPROPERTY()
	FString Name;

	UPROPERTY()
	FString Path;

	/** "group" or "actor". */
	UPROPERTY()
	FString Kind;

	/** Parent node id (empty for roots). */
	UPROPERTY()
	FString Parent;

	/** Child node ids. */
	UPROPERTY()
	TArray<FString> Children;

	UPROPERTY()
	TArray<FString> Tags;

	/** Actor class name. */
	UPROPERTY()
	FString Class;

	/** What created it ("shape:cube", "class:/Script/Engine.PointLight", "group", builder recipes...). */
	UPROPERTY()
	FString Source;

	/** World location in cm. */
	UPROPERTY()
	FVector Location = FVector::ZeroVector;

	/** World rotation [pitch, yaw, roll] in degrees. */
	UPROPERTY()
	FRotator Rotation = FRotator::ZeroRotator;

	/** World scale. */
	UPROPERTY()
	FVector Scale = FVector::OneVector;

	/** Bounds including child nodes. */
	UPROPERTY()
	FRuntimeNodeBounds Bounds;

	/** Hidden in game. */
	UPROPERTY()
	bool bHidden = false;

	/** Shape color [r, g, b] set by spawn_actor (empty if none). */
	UPROPERTY()
	TArray<float> Color;
};

USTRUCT()
struct FRuntimeTreeResult
{
	GENERATED_BODY()

	/** Nested nodes [{id, <fields>, children:[...]}]; nodes at the depth limit show child_count instead of children. */
	UPROPERTY()
	FRuntimeToolJson Nodes;

	/** Live nodes in the whole scene graph. */
	UPROPERTY()
	int32 TotalNodes = 0;

	/** True when the node limit cut the tree short. */
	UPROPERTY()
	bool bTruncated = false;
};

USTRUCT()
struct FRuntimePivotResult
{
	GENERATED_BODY()

	/** The group node. */
	UPROPERTY()
	FString Pivot;

	/** New pivot location (world, cm). */
	UPROPERTY()
	FVector Location = FVector::ZeroVector;
};

USTRUCT()
struct FRuntimeTagsResult
{
	GENERATED_BODY()

	UPROPERTY()
	FString Target;

	/** The node's tags after the change. */
	UPROPERTY()
	TArray<FString> Tags;
};

USTRUCT()
struct FRuntimeDuplicateResult
{
	GENERATED_BODY()

	/** Id of the copy's root node. */
	UPROPERTY()
	FString Id;

	UPROPERTY()
	FString Name;

	UPROPERTY()
	FString Path;

	/** Nodes copied (the subtree size). */
	UPROPERTY()
	int32 Nodes = 0;
};

USTRUCT()
struct FRuntimeDeleteNodeResult
{
	GENERATED_BODY()

	/** Id of the deleted node. */
	UPROPERTY()
	FString Deleted;

	/** Nodes deleted (1 with promote_children, else the subtree size). */
	UPROPERTY()
	int32 Nodes = 0;
};

USTRUCT()
struct FRuntimeClearResult
{
	GENERATED_BODY()

	/** Nodes deleted. */
	UPROPERTY()
	int32 Deleted = 0;

	/** Checkpoint created first: history.rollback {"name": checkpoint} restores everything. Empty when nothing was deleted. */
	UPROPERTY()
	FString Checkpoint;
};

USTRUCT()
struct FRuntimeImportResult
{
	GENERATED_BODY()

	/** Ids of the imported root nodes, in spec order. */
	UPROPERTY()
	TArray<FString> Roots;

	/** Nodes created. */
	UPROPERTY()
	int32 Nodes = 0;

	/** Spec id -> new node id (for specs that carry ids, up to 200 entries). */
	UPROPERTY()
	TMap<FString, FString> IdMap;
};

/**
 * Scene graph: the hierarchy of nodes (actors and groups) created by the tools. Nodes have stable ids ("n12") and paths
 * ("house/door"); children follow their parent, and every change is undoable through the history toolset.
 * Workflow: create_group for each object or area -> actors.spawn_actor / build tools with parent -> list_tree or find_nodes
 * to see what exists -> get_node for transforms and bounds -> move/rotate/scale/reparent/duplicate nodes ->
 * export_tree / import_tree to save or copy layouts. clear_scope and clear_all checkpoint first, so they can be rolled back.
 */
UCLASS()
class URuntimeSceneToolset : public URuntimeToolsetDefinition
{
	GENERATED_BODY()

public:
	virtual FString GetToolsetVersion() const override { return TEXT("2.0.0"); }

	/**
	 * Creates an empty group node: a pivot that children move, rotate and scale with. Undoable.
	 * The transform is relative to the parent when one is given.
	 *
	 * @param Name - Group name (made unique among siblings), e.g. "house".
	 * @param Location - [x, y, z] in cm.
	 * @param Rotation - [pitch, yaw, roll] in degrees.
	 * @param Scale - [x, y, z] (default [1, 1, 1]).
	 * @param Tags - Node tags, e.g. ["building"].
	 * @param Parent - Parent node.
	 * @return The new node: {id, name, path, actor}.
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld, AutoCreateRefTerm = "Location,Rotation,Scale,Tags"), Category = "Scene")
	static FRuntimeNodeInfo CreateGroup(const FString& Name, const FVector& Location, const FRotator& Rotation, const FVector& Scale,
		const TArray<FString>& Tags, AActor* Parent = nullptr);

	/**
	 * Returns the node hierarchy, depth-limited. Nodes at the depth limit report child_count instead of children.
	 *
	 * @param Root - Start node (default: all root nodes).
	 * @param Depth - Levels to expand, 1-32.
	 * @param Limit - Maximum nodes returned (0 = project default).
	 * @param Fields - Node fields to include besides id: name, path, kind, parent, tags, class, source, location, rotation,
	 *        scale, bounds (default name and kind).
	 * @return Nested nodes in tree order, the total live node count and whether the limit cut the tree.
	 */
	UFUNCTION(meta = (AICallable, RequiresWorld, AutoCreateRefTerm = "Fields"), Category = "Scene")
	static FRuntimeTreeResult ListTree(const TArray<FString>& Fields, AActor* Root = nullptr, int32 Depth = 2, int32 Limit = 25);

	/**
	 * Finds nodes by name, tag or kind, optionally below one node.
	 *
	 * @param Pattern - Name filter: a wildcard ("door*") or a case-insensitive substring.
	 * @param Tag - Only nodes with this tag.
	 * @param Kind - Any, Group or Actor.
	 * @param Under - Search only below this node.
	 * @param Limit - Maximum nodes returned (0 = project default).
	 * @return Total match count and the nodes {id, name, path, kind, tags} in tree order.
	 */
	UFUNCTION(meta = (AICallable, RequiresWorld), Category = "Scene")
	static FRuntimeNodeList FindNodes(const FString& Pattern = TEXT(""), const FString& Tag = TEXT(""),
		ERuntimeNodeKindFilter Kind = ERuntimeNodeKindFilter::Any, AActor* Under = nullptr, int32 Limit = 25);

	/**
	 * Returns one node: hierarchy, tags, class, source, world transform, bounds (including children) and shape color.
	 * Fails if the target is not a live scene node (deleted nodes come back with history.undo).
	 *
	 * @param Target - Node id or path, e.g. "n12" or "house/door".
	 */
	UFUNCTION(meta = (AICallable, RequiresWorld), Category = "Scene")
	static FRuntimeNodeDetails GetNode(AActor* Target);

	/**
	 * Moves a node to a location or by an offset (give exactly one). Children follow. Undoable.
	 *
	 * @param Target - Node to move.
	 * @param Location - New location [x, y, z] in cm.
	 * @param Offset - Offset to add [x, y, z] in cm.
	 * @param Space - World; or Local: location relative to the parent node, offset along the node's own axes.
	 * @return The node's resulting world transform.
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld, AutoCreateRefTerm = "Location,Offset"), Category = "Scene")
	static FRuntimeTransformResult MoveNode(AActor* Target, const FVector& Location, const FVector& Offset, ERuntimeNodeSpace Space = ERuntimeNodeSpace::World);

	/**
	 * Sets a node's rotation or applies a rotation delta (give exactly one). Children follow. Undoable.
	 *
	 * @param Target - Node to rotate.
	 * @param Rotation - New rotation [pitch, yaw, roll] in degrees.
	 * @param Delta - Rotation to apply, e.g. [0, 90, 0] turns 90 degrees around Z.
	 * @param Space - World; or Local: rotation relative to the parent node, delta about the node's own axes.
	 * @return The node's resulting world transform.
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld, AutoCreateRefTerm = "Rotation,Delta"), Category = "Scene")
	static FRuntimeTransformResult RotateNode(AActor* Target, const FRotator& Rotation, const FRotator& Delta, ERuntimeNodeSpace Space = ERuntimeNodeSpace::World);

	/**
	 * Sets a node's world scale or multiplies it by a factor (give one). Undoable. Prefer scaling leaves: non-uniform
	 * scale on a parent shears rotated children (the result then carries a warning). Fails on zero scale components.
	 *
	 * @param Target - Node to scale.
	 * @param Scale - New world scale [x, y, z].
	 * @param Factor - Multiply the current scale, e.g. [2, 2, 2].
	 * @return The node's resulting world transform.
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld, AutoCreateRefTerm = "Scale,Factor"), Category = "Scene")
	static FRuntimeTransformResult ScaleNode(AActor* Target, const FVector& Scale, const FVector& Factor);

	/**
	 * Moves a node under another node, or to the root when parent is omitted. Undoable.
	 * Fails if the new parent is the node itself or one of its descendants.
	 *
	 * @param Target - Node to move.
	 * @param Parent - New parent node; omit (or "") for the root.
	 * @param bKeepWorld - Keep the world transform (false keeps the relative transform).
	 * @return The node with its new path.
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld), Category = "Scene")
	static FRuntimeNodeInfo ReparentNode(AActor* Target, AActor* Parent = nullptr, bool bKeepWorld = true);

	/**
	 * Moves a group's origin while its children stay where they are. Undoable. Fails if the target is not a group.
	 *
	 * @param Target - Group node.
	 * @param Location - New pivot [x, y, z] in world cm (default: the center of the children's bounds).
	 * @return The group and its new pivot location.
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld, AutoCreateRefTerm = "Location"), Category = "Scene")
	static FRuntimePivotResult SetPivot(AActor* Target, const FVector& Location);

	/**
	 * Renames a node. The id stays the same; the path changes. Undoable. Fails if a sibling already has the name.
	 *
	 * @param Target - Node to rename.
	 * @param Name - New name, e.g. "front_door" ("/" is replaced by "_").
	 * @return The node with its new name and path.
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld), Category = "Scene")
	static FRuntimeNodeInfo RenameNode(AActor* Target, const FString& Name);

	/**
	 * Sets, adds or removes node tags (also applied as actor tags). Undoable.
	 *
	 * @param Target - Node to tag.
	 * @param Tags - Tags, e.g. ["wall", "stone"].
	 * @param Mode - Set (replace), Add or Remove.
	 * @return The node's tags after the change.
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld), Category = "Scene")
	static FRuntimeTagsResult SetNodeTags(AActor* Target, const TArray<FString>& Tags, ERuntimeTagMode Mode = ERuntimeTagMode::Set);

	/**
	 * Copies a node and its subtree (rebuilt from their spawn recipes) next to the original, as one undo step.
	 * Fails for subtrees over 2000 nodes.
	 *
	 * @param Target - Node to copy.
	 * @param Offset - World offset for the copy [x, y, z] in cm.
	 * @param Name - Name for the copy (default: the original's name, made unique).
	 * @return The copy's root node and the number of nodes copied.
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld, AutoCreateRefTerm = "Offset"), Category = "Scene")
	static FRuntimeDuplicateResult DuplicateNode(AActor* Target, const FVector& Offset, const FString& Name = TEXT(""));

	/**
	 * Deletes a node and its subtree, or only the node with its children moved up to its parent. Undoable.
	 *
	 * @param Target - Node to delete.
	 * @param bPromoteChildren - Keep the children and move them to the node's parent.
	 * @return The deleted node id and the number of nodes deleted.
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld), Category = "Scene")
	static FRuntimeDeleteNodeResult DeleteNode(AActor* Target, bool bPromoteChildren = false);

	/**
	 * Deletes everything this session created under a node, keeping the node itself. Creates a checkpoint first.
	 * Fails without confirm, or while a history transaction is open.
	 *
	 * @param Root - Scope node.
	 * @param bConfirm - Must be true.
	 * @return Nodes deleted and the checkpoint name (history.rollback restores them).
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld), Category = "Scene")
	static FRuntimeClearResult ClearScope(AActor* Root, bool bConfirm = false);

	/**
	 * Deletes every node this session created. Creates a checkpoint first.
	 * Fails without confirm, or while a history transaction is open.
	 *
	 * @param bConfirm - Must be true.
	 * @return Nodes deleted and the checkpoint name (history.rollback restores them).
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld), Category = "Scene")
	static FRuntimeClearResult ClearAll(bool bConfirm = false);

	/**
	 * Exports nodes as a JSON spec (transforms relative to the parent, spawn recipes, tags, colors) for import_tree.
	 *
	 * @param Root - Export only this subtree (default: all root nodes).
	 * @return {nodes:[{id, name, source, location, rotation, scale, tags, color, children:[...]}]}.
	 */
	UFUNCTION(meta = (AICallable, RequiresWorld), Category = "Scene")
	static FRuntimeToolJson ExportTree(AActor* Root = nullptr);

	/**
	 * Builds nodes from an export_tree spec as one undo step. Fails on invalid specs or more than 2000 nodes.
	 *
	 * @param Tree - Spec {nodes:[{name, source, location, rotation, scale, tags, color, children:[...]}]}, e.g. from export_tree.
	 * @param Parent - Attach the imported roots under this node.
	 * @return New root ids, the node count and spec id -> new id.
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld), Category = "Scene")
	static FRuntimeImportResult ImportTree(const FRuntimeToolJson& Tree, AActor* Parent = nullptr);

	/**
	 * Saves a node and everything under it as a named, reusable template (kept on this machine across sessions and
	 * games). Place copies with place_template. Use it for anything worth reusing: a generated house, a hand-built tree,
	 * a script-built bridge. Fails for an invalid name or more than 2000 nodes.
	 *
	 * @param Target - Root node to save (a group saves its whole subtree).
	 * @param Name - Template name: letters, digits, _ and -.
	 * @param Description - What it is and when to use it (shown by list_templates).
	 * @return {name, nodes, file}.
	 */
	UFUNCTION(meta = (AICallable, RequiresWorld), Category = "Scene|Templates")
	static FRuntimeToolJson SaveTemplate(AActor* Target, const FString& Name, const FString& Description);

	/**
	 * Places a copy of a saved or built-in template, as one undo step. Fails if the template does not exist.
	 *
	 * @param Name - Template name (list_templates).
	 * @param Location - Where its root goes, cm (relative to the parent when one is given).
	 * @param Yaw - Turn around Z, degrees.
	 * @param NewName - Name of the placed root (default: the template's).
	 * @param Parent - Parent group.
	 * @return {roots, nodes}.
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld, AutoCreateRefTerm = "Location"), Category = "Scene|Templates")
	static FRuntimeImportResult PlaceTemplate(const FString& Name, const FVector& Location, float Yaw = 0.f, const FString& NewName = TEXT(""),
		AActor* Parent = nullptr);

	/**
	 * Lists templates: saved ones and the ones shipped with the game in Content/RuntimeTemplates.
	 *
	 * @return {name: {description, nodes, builtin}}.
	 */
	UFUNCTION(meta = (AICallable), Category = "Scene|Templates")
	static FRuntimeToolJson ListTemplates();

	/**
	 * Deletes a saved template. Built-in templates cannot be deleted. Fails if it does not exist.
	 *
	 * @param Name - Template name.
	 * @return {deleted}.
	 */
	UFUNCTION(meta = (AICallable), Category = "Scene|Templates")
	static FRuntimeToolJson DeleteTemplate(const FString& Name);
};
