#include "ReflectionPolicy.h"
#include "RuntimeToolsetsSettings.h"
#include "Engine/LatentActionManager.h"
#include "UObject/Class.h"
#include "UObject/UnrealType.h"

namespace RuntimeReflectionPolicy
{
	bool CanAccessObject(const UObject* Object, FString& OutReason)
	{
		if (!Object)
		{
			OutReason = TEXT("null object");
			return false;
		}
		const URuntimeToolsetsSettings& S = URuntimeToolsetsSettings::Get();
		const FString Path = Object->GetPathName();
		bool bAllowed = S.AllowedObjectRoots.Num() == 0;
		for (const FString& Root : S.AllowedObjectRoots)
		{
			bAllowed |= Path.StartsWith(Root);
		}
		if (!bAllowed)
		{
			OutReason = FString::Printf(TEXT("'%s' is outside AllowedObjectRoots"), *Path);
			return false;
		}
		for (const UClass* C = Object->GetClass(); C; C = C->GetSuperClass())
		{
			for (const FString& Denied : S.DeniedClasses)
			{
				if (C->GetName() == Denied || C->GetPathName() == Denied)
				{
					OutReason = FString::Printf(TEXT("class %s is denied"), *C->GetName());
					return false;
				}
			}
		}
		return true;
	}

	bool IsVisibleProperty(const FProperty* Property)
	{
		return Property && Property->HasAnyPropertyFlags(CPF_Edit | CPF_BlueprintVisible);
	}

	bool CanWriteProperty(const FProperty* Property, FString& OutReason)
	{
		if (!Property)
		{
			OutReason = TEXT("no property");
			return false;
		}
		if (URuntimeToolsetsSettings::Get().bAllowWriteNonEditableProperties)
		{
			return true;
		}
		const bool bEditable = Property->HasAnyPropertyFlags(CPF_Edit) && !Property->HasAnyPropertyFlags(CPF_EditConst);
		const bool bBlueprintWritable = Property->HasAnyPropertyFlags(CPF_BlueprintVisible) && !Property->HasAnyPropertyFlags(CPF_BlueprintReadOnly);
		if (!bEditable && !bBlueprintWritable)
		{
			OutReason = FString::Printf(TEXT("'%s' is read-only (not editable / blueprint read-only)"), *Property->GetName());
			return false;
		}
		return true;
	}

	bool CanCallFunction(const UFunction* Function, FString& OutReason)
	{
		if (!Function)
		{
			OutReason = TEXT("no function");
			return false;
		}
		if (!Function->HasAnyFunctionFlags(FUNC_BlueprintCallable | FUNC_BlueprintPure))
		{
			OutReason = FString::Printf(TEXT("%s is not BlueprintCallable"), *Function->GetName());
			return false;
		}
		for (const FString& Denied : URuntimeToolsetsSettings::Get().DeniedFunctions)
		{
			if (Function->GetName().Equals(Denied, ESearchCase::IgnoreCase))
			{
				OutReason = FString::Printf(TEXT("%s is denied by project settings"), *Function->GetName());
				return false;
			}
		}
		for (TFieldIterator<FProperty> It(Function); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
		{
			if (const FStructProperty* SP = CastField<FStructProperty>(*It))
			{
				if (SP->Struct == FLatentActionInfo::StaticStruct())
				{
					OutReason = FString::Printf(TEXT("%s is latent and cannot be called"), *Function->GetName());
					return false;
				}
			}
		}
		return true;
	}
}
