#pragma once

#include "CoreMinimal.h"

class UObject;
class UFunction;
class FProperty;

/** Allow/deny checks for reflection tools, driven by URuntimeToolsetsSettings. */
namespace RuntimeReflectionPolicy
{
	/** Object path must start with an AllowedObjectRoot and its class must not be denied. */
	bool CanAccessObject(const UObject* Object, FString& OutReason);

	/** Writable if editable/blueprint-writable (or the project allows non-editable writes). */
	bool CanWriteProperty(const FProperty* Property, FString& OutReason);

	/** Blueprint-callable, not denied, no latent parameters. */
	bool CanCallFunction(const UFunction* Function, FString& OutReason);

	/** True for properties shown by list_properties when editable_only is set. */
	bool IsVisibleProperty(const FProperty* Property);
}
