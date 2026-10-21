#pragma once

#include "CoreMinimal.h"
#include "RuntimeToolCall.h"
#include "RuntimeToolsetDefinition.h"
#include "RuntimeReflectionToolset.generated.h"

/** One reflected property. */
USTRUCT()
struct FRuntimePropertyInfo
{
	GENERATED_BODY()

	/** Property name, usable as a path segment ("Intensity", "RelativeLocation"). */
	UPROPERTY()
	FString Name;

	/** C++ type, e.g. "float", "FVector", "TArray<FName>", "EComponentMobility::Type". */
	UPROPERTY()
	FString Type;

	/** Not writable by set_property / set_properties. */
	UPROPERTY()
	bool bReadOnly = false;

	/** Allowed values for enum properties (pass one of these names). */
	UPROPERTY()
	TArray<FString> EnumValues;

	/** Current value (with include_values; null when the value was too long, see value_preview). */
	UPROPERTY()
	FRuntimeToolJson Value;

	/** First 200 characters of a long value (with include_values). */
	UPROPERTY()
	FString ValuePreview;
};

USTRUCT()
struct FRuntimePropertyList
{
	GENERATED_BODY()

	/** The object's class. */
	UPROPERTY()
	FString Class;

	/** Number of matching properties (may exceed the list). */
	UPROPERTY()
	int32 Total = 0;

	/** Properties in declaration order (subclass first), at most `limit`. */
	UPROPERTY()
	TArray<FRuntimePropertyInfo> Properties;
};

USTRUCT()
struct FRuntimePropertyValue
{
	GENERATED_BODY()

	/** The property path that was read. */
	UPROPERTY()
	FString Path;

	UPROPERTY()
	FString Type;

	/** Value as JSON: numbers, strings, [x,y,z] vectors, [r,g,b,a] colors, objects for structs, references for objects. */
	UPROPERTY()
	FRuntimeToolJson Value;
};

USTRUCT()
struct FRuntimePropertyChange
{
	GENERATED_BODY()

	/** The changed object. */
	UPROPERTY()
	FString Target;

	/** The property path. */
	UPROPERTY()
	FString Property;

	/** Value after the change (read back). */
	UPROPERTY()
	FRuntimeToolJson Value;
};

USTRUCT()
struct FRuntimeCallResult
{
	GENERATED_BODY()

	/** Return value (null for functions without one). */
	UPROPERTY()
	FRuntimeToolJson Return;

	/** Out parameters by name. */
	UPROPERTY()
	FRuntimeToolJson Outputs;

	/** Parameters that were not passed and used their zero/default value. */
	UPROPERTY()
	TArray<FString> Defaulted;

	/** False when the function may have changed state (only pure/const functions are side-effect free). */
	UPROPERTY()
	bool bUndoable = true;
};

USTRUCT()
struct FRuntimeFunctionList
{
	GENERATED_BODY()

	UPROPERTY()
	FString Class;

	UPROPERTY()
	int32 Total = 0;

	/** Signatures "Name(type Param, out type Param) -> type", at most `limit`. */
	UPROPERTY()
	TArray<FString> Functions;
};

USTRUCT()
struct FRuntimeClassInfo
{
	GENERATED_BODY()

	UPROPERTY()
	FString Name;

	/** Full path, e.g. "/Script/Engine.PointLight" (pass it to actors.spawn_actor class). */
	UPROPERTY()
	FString Path;

	/** Parent class name. */
	UPROPERTY()
	FString Parent;

	UPROPERTY()
	bool bAbstract = false;
};

USTRUCT()
struct FRuntimeClassList
{
	GENERATED_BODY()

	UPROPERTY()
	int32 Total = 0;

	/** Matching loaded classes, at most `limit`. */
	UPROPERTY()
	TArray<FRuntimeClassInfo> Classes;
};

USTRUCT()
struct FRuntimeSubsystemInfo
{
	GENERATED_BODY()

	/** Target string for other reflection tools, e.g. "subsystem:world/MySubsystem". */
	UPROPERTY()
	FString Target;

	/** Class path. */
	UPROPERTY()
	FString Class;

	/** Number of functions call_function may call. */
	UPROPERTY()
	int32 CallableFunctions = 0;
};

USTRUCT()
struct FRuntimeEnumEntry
{
	GENERATED_BODY()

	/** Name to pass as a value, e.g. "Movable". */
	UPROPERTY()
	FString Name;

	/** Numeric value. */
	UPROPERTY()
	int64 Value = 0;

	/** Tooltip, when available. */
	UPROPERTY()
	FString Description;
};

USTRUCT()
struct FRuntimeEnumInfo
{
	GENERATED_BODY()

	UPROPERTY()
	FString Name;

	/** Full path, e.g. "/Script/Engine.ECollisionChannel". */
	UPROPERTY()
	FString Path;

	/** Values combine as bit flags. */
	UPROPERTY()
	bool bBitflags = false;

	/** Values in declaration order (hidden and _MAX entries left out). */
	UPROPERTY()
	TArray<FRuntimeEnumEntry> Values;
};

/**
 * Reflection: inspect and change any object's properties and call its Blueprint-callable functions, like Epic's object
 * tools. Targets are node ids, actor:Name, components ("n12#LightComponent0", from actors.list_components), shortcuts
 * (pawn, game_mode, world_settings...), subsystem:<kind>/<Class> or object paths; project policy can deny classes.
 * Workflow: list_properties (types, enum values) -> get_properties -> set_properties (one undo step) or reset_properties;
 * describe_enum for enum names; list_functions -> call_function for behaviour; find_class to discover classes.
 */
UCLASS()
class URuntimeReflectionToolset : public URuntimeToolsetDefinition
{
	GENERATED_BODY()

public:
	virtual FString GetToolsetVersion() const override { return TEXT("2.0.0"); }

	/**
	 * Lists an object's properties with their types, enum values and (optionally) current values.
	 *
	 * @param Target - Object, e.g. "n12", "n12#PointLightComponent0" or "world_settings".
	 * @param Filter - Property name wildcard, e.g. "*Color*".
	 * @param bEditableOnly - Only editable or Blueprint-visible properties.
	 * @param bIncludeValues - Include current values (long values become a 200-character preview).
	 * @param Limit - Maximum properties returned (0 = project default).
	 * @return Class, total count and properties {name, type, read_only, enum_values, value, value_preview}.
	 */
	UFUNCTION(meta = (AICallable), Category = "Reflection")
	static FRuntimePropertyList ListProperties(UObject* Target, const FString& Filter = TEXT(""), bool bEditableOnly = true, bool bIncludeValues = false,
		int32 Limit = 25);

	/**
	 * Reads one property by path. Fails if the path does not exist (see list_properties).
	 *
	 * @param Target - Object to read.
	 * @param Path - Property path, e.g. "Intensity", "LightComponent.Intensity", "RelativeLocation.Z" or "Tags[0]".
	 * @return The path, the property type and the value.
	 */
	UFUNCTION(meta = (AICallable), Category = "Reflection")
	static FRuntimePropertyValue GetProperty(UObject* Target, const FString& Path);

	/**
	 * Reads several properties at once (Epic: get_properties).
	 *
	 * @param Target - Object to read.
	 * @param Properties - Property paths, e.g. ["Intensity", "LightColor", "AttenuationRadius"].
	 * @return Path -> value. Fails on the first unknown path.
	 */
	UFUNCTION(meta = (AICallable), Category = "Reflection")
	static TMap<FString, FRuntimeToolJson> GetProperties(UObject* Target, const TArray<FString>& Properties);

	/**
	 * Writes one property by path. Undoable. Vectors as [x,y,z], colors as [r,g,b,a], enums by name, objects by reference.
	 * Fails if the path does not exist or the property is read-only (try a setter with call_function).
	 *
	 * @param Target - Object to change.
	 * @param Path - Property path, e.g. "Intensity".
	 * @param Value - New value, e.g. 5000.
	 * @return The target, the path and the value read back.
	 */
	UFUNCTION(meta = (AICallable, Undoable), Category = "Reflection")
	static FRuntimePropertyChange SetProperty(UObject* Target, const FString& Path, const FRuntimeToolJson& Value);

	/**
	 * Writes several properties as one undo step (Epic: set_properties). All paths are checked first; nothing changes if any
	 * path is unknown or read-only.
	 *
	 * @param Target - Object to change.
	 * @param Values - Path -> value, e.g. {"Intensity": 5000, "LightColor": [255, 200, 150, 255]}.
	 * @return Path -> value read back after the change.
	 */
	UFUNCTION(meta = (AICallable, Undoable), Category = "Reflection")
	static TMap<FString, FRuntimeToolJson> SetProperties(UObject* Target, const FRuntimeToolJson& Values);

	/**
	 * Resets properties to their defaults (the object's archetype: class defaults, or the Blueprint/component template),
	 * as one undo step (Epic: reset_properties). Fails if a path is unknown or read-only.
	 *
	 * @param Target - Object to change.
	 * @param Properties - Property paths to reset, e.g. ["Intensity", "LightColor"].
	 * @return Path -> value after the reset.
	 */
	UFUNCTION(meta = (AICallable, Undoable), Category = "Reflection")
	static TMap<FString, FRuntimeToolJson> ResetProperties(UObject* Target, const TArray<FString>& Properties);

	/**
	 * Describes an enum: its value names (what enum properties and arguments accept) and numbers.
	 * Fails if no loaded enum has that name; similar names are suggested.
	 *
	 * @param Enum - Enum name or path, e.g. "ECollisionChannel", "EComponentMobility" or "/Script/Engine.ELightUnits".
	 * @return Name, path, whether it is a bit-flag enum, and its values in declaration order.
	 */
	UFUNCTION(meta = (AICallable), Category = "Reflection")
	static FRuntimeEnumInfo DescribeEnum(const FString& Enum);

	/**
	 * Calls a BlueprintCallable function with named arguments. Missing arguments use zero/default values (listed in
	 * defaulted); a WorldContextObject argument is filled in. Fails for unknown functions, unknown argument names, latent
	 * functions and functions denied by project settings.
	 *
	 * @param Target - Object, or class:/Script/Module.Class for static library functions.
	 * @param Function - Function name, e.g. "K2_GetActorLocation" (see list_functions).
	 * @param Args - Arguments by parameter name, e.g. {"NewLocation": [0, 0, 100]}.
	 * @return Return value, out parameters, defaulted parameter names and whether the call was side-effect free.
	 */
	UFUNCTION(meta = (AICallable, Irreversible, AutoCreateRefTerm = "Args"), Category = "Reflection")
	static FRuntimeCallResult CallFunction(UObject* Target, const FString& Function, const FRuntimeToolJson& Args);

	/**
	 * Lists the functions call_function may call on an object, with signatures.
	 *
	 * @param Target - Object to inspect.
	 * @param Filter - Name wildcard, e.g. "Set*".
	 * @param Limit - Maximum functions returned (0 = project default).
	 * @return Class, total count and signatures.
	 */
	UFUNCTION(meta = (AICallable), Category = "Reflection")
	static FRuntimeFunctionList ListFunctions(UObject* Target, const FString& Filter = TEXT(""), int32 Limit = 25);

	/**
	 * Finds loaded classes by name, optionally derived from a base class (Epic: search_subclasses).
	 * Fails if the base class does not exist.
	 *
	 * @param Pattern - Name wildcard, e.g. "*Light*".
	 * @param Base - Base class name, e.g. "Actor" or "ActorComponent".
	 * @param Limit - Maximum classes returned (0 = project default).
	 * @return Total count and classes {name, path, parent, abstract}.
	 */
	UFUNCTION(meta = (AICallable), Category = "Reflection")
	static FRuntimeClassList FindClass(const FString& Pattern = TEXT(""), const FString& Base = TEXT(""), int32 Limit = 25);

	/**
	 * Gets a subsystem as a target string for the other reflection tools. Fails if the class does not exist or the
	 * subsystem is not running.
	 *
	 * @param Kind - engine, game_instance, world or local_player.
	 * @param Class - Subsystem class name, e.g. "TimerSubsystem".
	 * @return The target string, class path and number of callable functions.
	 */
	UFUNCTION(meta = (AICallable), Category = "Reflection")
	static FRuntimeSubsystemInfo GetSubsystem(const FString& Kind, const FString& Class);
};
