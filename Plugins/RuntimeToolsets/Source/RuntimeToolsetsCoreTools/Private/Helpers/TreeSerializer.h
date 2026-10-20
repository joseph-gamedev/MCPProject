#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "RuntimeCommand.h"

class FRuntimeSceneGraph;

/**
 * Converts scene-graph subtrees to/from a JSON spec:
 *   {"nodes":[{"id","name","source","location","rotation","scale","tags","color","children":[...]}]}
 * Transforms are relative to the parent node. Used by export_tree, import_tree and duplicate_node.
 */
namespace RuntimeTreeSerializer
{
	/** Exports one subtree, or all live roots when RootId is empty. */
	TSharedRef<FJsonObject> Export(FRuntimeSceneGraph& Graph, const FString& RootId);

	/**
	 * Builds one composite command that creates the spec's nodes under ParentId.
	 * @param RootNameOverride  Renames the first root (duplicate's "name").
	 * @param WorldOffset       Added to root positions (world space).
	 * @param OutRootIds        Ids the roots will get.
	 * @param OutIdMap          Optional spec id -> new id.
	 */
	TUniquePtr<FCompositeCommand> BuildImport(FRuntimeSceneGraph& Graph, const TSharedRef<FJsonObject>& Spec, const FString& ParentId,
		const FString& RootNameOverride, const FVector& WorldOffset, TArray<FString>& OutRootIds, TMap<FString, FString>* OutIdMap, FString& OutError);
}
