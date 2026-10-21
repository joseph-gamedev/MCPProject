#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "RuntimeToolsetDefinition.generated.h"

/**
 * Base class for toolsets declared the way Epic's ToolsetRegistry declares them (UToolsetDefinition), usable at runtime.
 *
 * Tools are static UFUNCTIONs marked meta=(AICallable). The class doc comment is the toolset description, the function doc
 * comment is the tool description ("@param Name - text" documents arguments, "@return" the result). Input and output JSON
 * schemas are generated from the signature: snake_case names, enums as strings, structs as objects, object pointers as
 * references (node id, name, actor:Name or object path), defaulted C++ parameters are optional.
 * The result is returned as {"returnValue": ...} (plus any out parameters).
 *
 * Extra metadata (ours, all optional):
 *   Undoable       - changes the world and records an undo step (call RuntimeToolCall::RunCommand inside).
 *   Irreversible   - changes the world and cannot be undone.
 *   RequiresWorld  - needs a running game/PIE world.
 * A tool returning a URuntimeToolAsyncResult subclass completes later (Epic's UToolCallAsyncResult pattern).
 * Inside a tool, report errors with RuntimeToolCall::Fail (Epic: RaiseScriptError) and return a default value.
 *
 * Doc comments are editor-only metadata: the generated docs (input/output schemas, descriptions, flags) are compiled into
 * each module (Private/Generated/ToolDocs.gen.inl, refreshed by RuntimeToolsets.UpdateSchemaSnapshots) for packaged builds.
 * Register a toolset class with RuntimeToolReflection::RegisterToolsetClass.
 */
UCLASS(Abstract, MinimalAPI)
class URuntimeToolsetDefinition : public UObject
{
	GENERATED_BODY()

public:
	/** Version of the toolset's public surface (Epic: GetToolsetVersion). */
	virtual FString GetToolsetVersion() const { return TEXT("1.0.0"); }

	/** Lower-case toolset name; by default the class name without the "Toolset" suffix ("UPcgToolset" -> "pcg"). */
	RUNTIMETOOLSETSCORE_API virtual FName GetToolsetName() const;
};
