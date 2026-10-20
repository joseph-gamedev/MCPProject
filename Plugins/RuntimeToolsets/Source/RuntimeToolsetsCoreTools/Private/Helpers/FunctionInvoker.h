#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class UObject;
class UFunction;
class UWorld;

/** Calls a UFunction with JSON arguments through ProcessEvent. */
namespace RuntimeFunctionInvoker
{
	/** Finds a function on the object's class (case-insensitive). */
	UFunction* FindFunction(const UObject* Object, const FString& Name);

	/**
	 * Fills parameters from Args (matched by name, case-insensitive), auto-fills WorldContext parameters,
	 * calls the function and returns {"return": ..., "<out param>": ...}.
	 * Missing input parameters are zero-initialized and listed in "defaulted".
	 */
	bool Invoke(UObject* Target, UFunction* Function, const TSharedPtr<FJsonObject>& Args, UWorld* World, TSharedPtr<FJsonObject>& OutResult,
		FString& OutError);

	/** "Name(Type A, Type B) -> Ret" */
	FString DescribeSignature(const UFunction* Function);
}
