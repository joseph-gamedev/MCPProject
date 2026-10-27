#pragma once

#include "CoreMinimal.h"
#include "RuntimeToolCall.h"
#include "RuntimeToolInvocation.h"
#include "RuntimeToolsetsSettings.h"
#include "UObject/Package.h"

/** Small helpers shared by the presentation toolsets (UFUNCTION tools). */
namespace PresentationTool
{
	/** The running call (tools are only invoked through the dispatcher, so it is always set). */
	inline FToolInvocation& Inv()
	{
		return *RuntimeToolCall::Current();
	}

	/**
	 * Whether the caller passed an argument. Only meaningful for parameters without a schema default (the dispatcher fills
	 * defaults in): optional "keep the current value" parameters are const references listed in AutoCreateRefTerm.
	 */
	inline bool Passed(const TCHAR* JsonName)
	{
		const FToolInvocation* I = RuntimeToolCall::Current();
		return I && I->Has(JsonName);
	}

	inline FString Ref(const UObject* Object)
	{
		return Object ? RuntimeToolCall::MakeReference(Object) : FString();
	}

	/** "a, b, c" or "none". */
	inline FString JoinOrNone(const TArray<FString>& Items)
	{
		return Items.Num() ? FString::Join(Items, TEXT(", ")) : FString(TEXT("none"));
	}

	/** Loads an asset by path, enforcing the AllowedObjectRoots setting. Fails the call (NotAllowed / NotFound) and returns null on error. */
	template <typename T>
	T* LoadAllowed(const FString& Path, const TCHAR* What, const TCHAR* Example)
	{
		const URuntimeToolsetsSettings& S = URuntimeToolsetsSettings::Get();
		bool bAllowed = S.AllowedObjectRoots.Num() == 0;
		for (const FString& Root : S.AllowedObjectRoots)
		{
			bAllowed |= Path.StartsWith(Root);
		}
		if (!bAllowed)
		{
			RuntimeToolCall::Fail(ERuntimeToolError::NotAllowed, FString::Printf(TEXT("'%s' is outside AllowedObjectRoots (%s)"), *Path, *JoinOrNone(S.AllowedObjectRoots)),
				TEXT("Use an asset under one of the allowed roots"));
			return nullptr;
		}
		T* Asset = Path.IsEmpty() ? nullptr : LoadObject<T>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
		if (!Asset)
		{
			RuntimeToolCall::Fail(ERuntimeToolError::NotFound, FString::Printf(TEXT("%s '%s' not found (is it cooked?)"), What, *Path),
				FString::Printf(TEXT("Use the full object path, e.g. %s"), Example));
		}
		return Asset;
	}
}
