#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

class IRuntimeToolset;
class URuntimeToolsetDefinition;
class UWorld;
class FRuntimeSceneGraph;

/**
 * Reflection layer for UFUNCTION toolsets (see URuntimeToolsetDefinition): JSON <-> FProperty conversion, schema
 * generation, compiled docs for packaged builds, and registration.
 */
namespace RuntimeToolReflection
{
	/** Context for resolving object references while converting JSON to properties. */
	struct FConvertContext
	{
		UWorld* World = nullptr;
		FRuntimeSceneGraph* Graph = nullptr;
		/** Outer for objects created during conversion. */
		UObject* Outer = nullptr;
	};

	/** "PCGVolume" -> "pcg_volume", "bClosedSpline" -> "closed_spline", "XPositionIdx" -> "x_position_idx". */
	RUNTIMETOOLSETSCORE_API FString ToSnakeCase(const FString& Name, bool bStripBoolPrefix = false);
	/** Snake-case JSON name of a property. */
	RUNTIMETOOLSETSCORE_API FString JsonName(const FProperty* Property);

	/** JSON schema of a property (type, enum, items, properties, description, minimum/maximum).
	 *  bOutput describes values as results return them (object references become {ref, name, class}). */
	RUNTIMETOOLSETSCORE_API TSharedRef<FJsonObject> PropertySchema(const FProperty* Property, bool bOutput = false);
	/** JSON value of a property. */
	RUNTIMETOOLSETSCORE_API TSharedPtr<FJsonValue> PropertyToJson(const FProperty* Property, const void* ValuePtr, FRuntimeSceneGraph* Graph = nullptr);
	/** Sets a property from JSON. On failure OutError says what was expected. */
	RUNTIMETOOLSETSCORE_API bool JsonToProperty(const TSharedPtr<FJsonValue>& Json, const FProperty* Property, void* ValuePtr, const FConvertContext& Context,
		FString& OutError);

	/** Resolves a reference (node id, node path, actor:Name, object or asset path) to an object of the expected class. */
	RUNTIMETOOLSETSCORE_API UObject* ResolveObject(const FString& Reference, UClass* Expected, const FConvertContext& Context, FString& OutError);

	/** Struct <-> JSON object (snake_case keys; missing keys keep the struct's defaults). */
	RUNTIMETOOLSETSCORE_API TSharedRef<FJsonObject> StructToJson(const UStruct* Struct, const void* Data, FRuntimeSceneGraph* Graph = nullptr);
	RUNTIMETOOLSETSCORE_API bool JsonToStruct(const TSharedPtr<FJsonObject>& Json, const UStruct* Struct, void* Data, const FConvertContext& Context, FString& OutError);

	/** Custom JSON handling for a struct type (Epic: FToolsetJsonConverter), e.g. PCG attribute selectors as "$Density". */
	struct FStructConverter
	{
		TFunction<TSharedPtr<FJsonValue>(const void* Data)> ToJson;
		TFunction<bool(const TSharedPtr<FJsonValue>& Json, void* Data, FString& OutError)> FromJson;
		TSharedPtr<FJsonObject> Schema;
	};
	/** Registers a converter for Struct and (by default) structs derived from it. */
	RUNTIMETOOLSETSCORE_API void RegisterStructConverter(const UScriptStruct* Struct, FStructConverter Converter, bool bIncludeChildren = true);
	RUNTIMETOOLSETSCORE_API void UnregisterStructConverter(const UScriptStruct* Struct);

	/** Wraps a toolset class as an IRuntimeToolset (tools from AICallable functions or compiled docs). */
	RUNTIMETOOLSETSCORE_API TSharedRef<IRuntimeToolset> MakeToolset(UClass* ToolsetClass);
	/** MakeToolset + IRuntimeToolsetsCore::RegisterToolset. Returns the toolset so the module can unregister it. */
	RUNTIMETOOLSETSCORE_API TSharedRef<IRuntimeToolset> RegisterToolsetClass(UClass* ToolsetClass);

	/** Docs generated from metadata for one toolset class (what gets compiled into packaged builds). Editor only. */
	RUNTIMETOOLSETSCORE_API TSharedPtr<FJsonObject> GenerateDocs(UClass* ToolsetClass);
	/** Registers compiled docs (JSON object: toolset name -> docs), concatenated from chunks. */
	RUNTIMETOOLSETSCORE_API void RegisterCompiledDocs(const TCHAR* const* Chunks, int32 NumChunks);
	/** Compiled docs for a toolset (null if none). */
	RUNTIMETOOLSETSCORE_API TSharedPtr<FJsonObject> FindCompiledDocs(FName Toolset);
	/** Writes Private/Generated/ToolDocs.gen.inl for every module with registered toolset classes (editor only).
	 *  Returns the number of files changed (or that would change, with bDryRun). */
	RUNTIMETOOLSETSCORE_API int32 WriteCompiledDocs(FString& OutReport, bool bDryRun = false);
}
