#include "FunctionInvoker.h"
#include "RuntimeJson.h"
#include "RuntimePropertyPath.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"
#include "UObject/Class.h"
#include "UObject/UnrealType.h"

namespace RuntimeFunctionInvoker
{
	UFunction* FindFunction(const UObject* Object, const FString& Name)
	{
		if (!Object)
		{
			return nullptr;
		}
		if (UFunction* F = Object->FindFunction(FName(*Name)))
		{
			return F;
		}
		for (TFieldIterator<UFunction> It(Object->GetClass(), EFieldIteratorFlags::IncludeSuper); It; ++It)
		{
			if (It->GetName().Equals(Name, ESearchCase::IgnoreCase))
			{
				return *It;
			}
		}
		return nullptr;
	}

	static bool IsWorldContextParam(const FProperty* P)
	{
		const FObjectPropertyBase* OP = CastField<FObjectPropertyBase>(P);
		if (!OP)
		{
			return false;
		}
		const FString Name = P->GetName();
		return Name.Equals(TEXT("WorldContextObject"), ESearchCase::IgnoreCase) || Name.Equals(TEXT("WorldContext"), ESearchCase::IgnoreCase);
	}

	static TSharedPtr<FJsonValue> FindArg(const TSharedPtr<FJsonObject>& Args, const FString& Name)
	{
		if (!Args.IsValid())
		{
			return nullptr;
		}
		for (const FString& Key : RuntimeJson::Keys(Args))
		{
			if (Key.Equals(Name, ESearchCase::IgnoreCase))
			{
				return Args->TryGetField(Key);
			}
		}
		return nullptr;
	}

	bool Invoke(UObject* Target, UFunction* Function, const TSharedPtr<FJsonObject>& Args, UWorld* World, TSharedPtr<FJsonObject>& OutResult,
		FString& OutError)
	{
		if (Function->HasAnyFunctionFlags(FUNC_Static))
		{
			Target = Function->GetOuterUClass()->GetDefaultObject();
		}
		if (!Target)
		{
			OutError = TEXT("no target object");
			return false;
		}

		const int32 Size = FMath::Max<int32>(Function->ParmsSize, 1);
		uint8* Params = static_cast<uint8*>(FMemory::Malloc(Size, FMath::Max<int32>(Function->GetMinAlignment(), 8)));
		FMemory::Memzero(Params, Size);
		for (TFieldIterator<FProperty> It(Function); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
		{
			It->InitializeValue_InContainer(Params);
		}
		ON_SCOPE_EXIT
		{
			for (TFieldIterator<FProperty> It(Function); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
			{
				It->DestroyValue_InContainer(Params);
			}
			FMemory::Free(Params);
		};

		TArray<FString> Defaulted;
		TArray<FString> Known;
		for (TFieldIterator<FProperty> It(Function); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
		{
			FProperty* P = *It;
			if (P->HasAnyPropertyFlags(CPF_ReturnParm))
			{
				continue;
			}
			const bool bOutOnly = P->HasAnyPropertyFlags(CPF_OutParm) && !P->HasAnyPropertyFlags(CPF_ReferenceParm | CPF_ConstParm);
			if (bOutOnly)
			{
				continue;
			}
			Known.Add(P->GetName());
			void* ValuePtr = P->ContainerPtrToValuePtr<void>(Params);
			const TSharedPtr<FJsonValue> Value = FindArg(Args, P->GetName());
			if (Value.IsValid() && !Value->IsNull())
			{
				FString Error;
				if (!RuntimePropertyPath::FromJson(Value, P, ValuePtr, Error))
				{
					OutError = FString::Printf(TEXT("argument %s: %s"), *P->GetName(), *Error);
					return false;
				}
			}
			else if (IsWorldContextParam(P))
			{
				CastField<FObjectPropertyBase>(P)->SetObjectPropertyValue(ValuePtr, World);
			}
			else
			{
				Defaulted.Add(P->GetName());
			}
		}

		// Unknown argument names are an error so typos do not silently default.
		if (Args.IsValid())
		{
			for (const FString& Key : RuntimeJson::Keys(Args))
			{
				const bool bKnown = Known.ContainsByPredicate([&Key](const FString& K) { return K.Equals(Key, ESearchCase::IgnoreCase); });
				if (!bKnown)
				{
					OutError = FString::Printf(TEXT("unknown argument '%s'; signature: %s"), *Key, *DescribeSignature(Function));
					return false;
				}
			}
		}

		Target->ProcessEvent(Function, Params);

		OutResult = MakeShared<FJsonObject>();
		for (TFieldIterator<FProperty> It(Function); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
		{
			FProperty* P = *It;
			if (P->HasAnyPropertyFlags(CPF_ReturnParm))
			{
				OutResult->SetField(TEXT("return"), RuntimePropertyPath::ToJson(P, P->ContainerPtrToValuePtr<void>(Params)));
			}
			else if (P->HasAnyPropertyFlags(CPF_OutParm) && !P->HasAnyPropertyFlags(CPF_ConstParm))
			{
				OutResult->SetField(P->GetName(), RuntimePropertyPath::ToJson(P, P->ContainerPtrToValuePtr<void>(Params)));
			}
		}
		if (Defaulted.Num() > 0)
		{
			OutResult->SetField(TEXT("defaulted"), RuntimeJson::Strings(Defaulted));
		}
		return true;
	}

	FString DescribeSignature(const UFunction* Function)
	{
		TArray<FString> Params;
		FString Ret;
		for (TFieldIterator<FProperty> It(Function); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
		{
			const FString Type = RuntimePropertyPath::DescribeType(*It);
			if (It->HasAnyPropertyFlags(CPF_ReturnParm))
			{
				Ret = Type;
			}
			else
			{
				const bool bOut = It->HasAnyPropertyFlags(CPF_OutParm) && !It->HasAnyPropertyFlags(CPF_ReferenceParm | CPF_ConstParm);
				Params.Add(FString::Printf(TEXT("%s%s %s"), bOut ? TEXT("out ") : TEXT(""), *Type, *It->GetName()));
			}
		}
		FString Sig = FString::Printf(TEXT("%s(%s)"), *Function->GetName(), *FString::Join(Params, TEXT(", ")));
		if (!Ret.IsEmpty())
		{
			Sig += TEXT(" -> ") + Ret;
		}
		return Sig;
	}
}
