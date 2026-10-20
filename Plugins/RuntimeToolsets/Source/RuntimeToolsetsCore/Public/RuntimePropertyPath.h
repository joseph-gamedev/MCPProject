#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"

class UObject;
class FProperty;

/**
 * Walks property paths such as "RootComponent.RelativeLocation", "Tags[0]" or "Settings.Bloom.Intensity".
 * Object references are followed; struct members and array indices are supported.
 */
namespace RuntimePropertyPath
{
	struct FResolved
	{
		FProperty* Property = nullptr;
		void* ValuePtr = nullptr;
		/** Object that owns the final property (for render-state refresh). */
		UObject* OwnerObject = nullptr;
	};

	RUNTIMETOOLSETSCORE_API bool Resolve(UObject* Root, const FString& Path, FResolved& Out, FString& OutError);

	RUNTIMETOOLSETSCORE_API TSharedPtr<FJsonValue> Get(UObject* Root, const FString& Path, FString& OutError);

	/**
	 * Converts and writes the value. Accepts [x,y,z] for vectors/rotators and [r,g,b,a] for colors.
	 * Refreshes render state / transforms of the owning component afterwards.
	 */
	RUNTIMETOOLSETSCORE_API bool Set(UObject* Root, const FString& Path, const TSharedPtr<FJsonValue>& Value, FString& OutError);

	/** Converts a property value to JSON (vectors become [x,y,z]). */
	RUNTIMETOOLSETSCORE_API TSharedPtr<FJsonValue> ToJson(FProperty* Property, const void* ValuePtr);

	/** Writes JSON into a property value (shared by set_property and call_function). */
	RUNTIMETOOLSETSCORE_API bool FromJson(const TSharedPtr<FJsonValue>& Value, FProperty* Property, void* ValuePtr, FString& OutError);

	/** Short type string: "float", "FVector", "TArray<FName>", "UStaticMeshComponent*". */
	RUNTIMETOOLSETSCORE_API FString DescribeType(const FProperty* Property);
}
