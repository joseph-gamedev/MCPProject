#include "RuntimeToolSchema.h"
#include "RuntimeJson.h"

// ---------------------------------------------------------------------------------------------------------------------
// FToolParam
// ---------------------------------------------------------------------------------------------------------------------

static FToolParam MakeParam(const TCHAR* Type, const FString& Description)
{
	FToolParam P;
	if (Type)
	{
		P.Json->SetStringField(TEXT("type"), Type);
	}
	if (!Description.IsEmpty())
	{
		P.Json->SetStringField(TEXT("description"), Description);
	}
	return P;
}

static TSharedRef<FJsonObject> NumberArraySchema(int32 Count)
{
	TSharedRef<FJsonObject> Items = MakeShared<FJsonObject>();
	Items->SetStringField(TEXT("type"), TEXT("number"));
	TSharedRef<FJsonObject> S = MakeShared<FJsonObject>();
	S->SetStringField(TEXT("type"), TEXT("array"));
	S->SetObjectField(TEXT("items"), Items);
	S->SetNumberField(TEXT("minItems"), Count);
	S->SetNumberField(TEXT("maxItems"), Count);
	return S;
}

FToolParam FToolParam::String(const FString& Description, int32 MaxLength)
{
	FToolParam P = MakeParam(TEXT("string"), Description);
	P.Json->SetNumberField(TEXT("maxLength"), MaxLength);
	return P;
}

FToolParam FToolParam::Integer(const FString& Description, TOptional<int64> Min, TOptional<int64> Max)
{
	FToolParam P = MakeParam(TEXT("integer"), Description);
	if (Min.IsSet())
	{
		P.Json->SetNumberField(TEXT("minimum"), static_cast<double>(Min.GetValue()));
	}
	if (Max.IsSet())
	{
		P.Json->SetNumberField(TEXT("maximum"), static_cast<double>(Max.GetValue()));
	}
	return P;
}

FToolParam FToolParam::Number(const FString& Description, TOptional<double> Min, TOptional<double> Max)
{
	FToolParam P = MakeParam(TEXT("number"), Description);
	if (Min.IsSet())
	{
		P.Json->SetNumberField(TEXT("minimum"), Min.GetValue());
	}
	if (Max.IsSet())
	{
		P.Json->SetNumberField(TEXT("maximum"), Max.GetValue());
	}
	return P;
}

FToolParam FToolParam::Boolean(const FString& Description)
{
	return MakeParam(TEXT("boolean"), Description);
}

FToolParam FToolParam::Enum(const FString& Description, const TArray<FString>& Values)
{
	FToolParam P = MakeParam(TEXT("string"), Description);
	TArray<TSharedPtr<FJsonValue>> Items;
	for (const FString& V : Values)
	{
		Items.Add(MakeShared<FJsonValueString>(V));
	}
	P.Json->SetArrayField(TEXT("enum"), Items);
	return P;
}

FToolParam FToolParam::Vec3(const FString& Description)
{
	FToolParam P;
	P.Json = NumberArraySchema(3);
	P.Json->SetStringField(TEXT("description"), Description);
	return P;
}

FToolParam FToolParam::Rotator(const FString& Description)
{
	return Vec3(Description.IsEmpty() ? TEXT("[pitch, yaw, roll] degrees") : Description);
}

FToolParam FToolParam::Array(const FString& Description, const FToolParam& Items, int32 MaxItems)
{
	FToolParam P = MakeParam(TEXT("array"), Description);
	P.Json->SetObjectField(TEXT("items"), Items.Json);
	P.Json->SetNumberField(TEXT("maxItems"), MaxItems);
	return P;
}

FToolParam FToolParam::StringArray(const FString& Description, int32 MaxItems)
{
	return Array(Description, String(FString(), 256), MaxItems);
}

FToolParam FToolParam::Object(const FString& Description)
{
	return MakeParam(TEXT("object"), Description);
}

FToolParam FToolParam::Any(const FString& Description)
{
	return MakeParam(nullptr, Description);
}

FToolParam FToolParam::Target(const FString& Description)
{
	return String(Description, 1024);
}

FToolParam FToolParam::Limit()
{
	return Integer(TEXT("Max results (default 25)"), 1, 1000);
}

FToolParam FToolParam::Fields(const TArray<FString>& Available)
{
	FToolParam P = Array(TEXT("Fields to include (default: all listed)"), Enum(FString(), Available), Available.Num());
	return P;
}

FToolParam FToolParam::Confirm()
{
	return Boolean(TEXT("Must be true to proceed")).Default(false);
}

FToolParam& FToolParam::Required()
{
	bRequired = true;
	return *this;
}

FToolParam& FToolParam::Default(bool Value)
{
	Json->SetBoolField(TEXT("default"), Value);
	return *this;
}

FToolParam& FToolParam::Default(int64 Value)
{
	Json->SetNumberField(TEXT("default"), static_cast<double>(Value));
	return *this;
}

FToolParam& FToolParam::Default(double Value)
{
	Json->SetNumberField(TEXT("default"), Value);
	return *this;
}

FToolParam& FToolParam::Default(const TCHAR* Value)
{
	Json->SetStringField(TEXT("default"), Value);
	return *this;
}

FToolParam& FToolParam::Default(const FString& Value)
{
	Json->SetStringField(TEXT("default"), Value);
	return *this;
}

FToolParam& FToolParam::Default(const TSharedPtr<FJsonValue>& Value)
{
	Json->SetField(TEXT("default"), Value);
	return *this;
}

// ---------------------------------------------------------------------------------------------------------------------
// FToolSchema
// ---------------------------------------------------------------------------------------------------------------------

FToolSchema::FToolSchema()
	: Properties(MakeShared<FJsonObject>())
{
}

FToolSchema& FToolSchema::Add(const FString& Name, const FToolParam& Param)
{
	Properties->SetObjectField(Name, Param.Json);
	if (Param.bRequired)
	{
		RequiredNames.AddUnique(Name);
	}
	return *this;
}

TSharedRef<FJsonObject> FToolSchema::Build() const
{
	TSharedRef<FJsonObject> Schema = MakeShared<FJsonObject>();
	Schema->SetStringField(TEXT("type"), TEXT("object"));
	Schema->SetObjectField(TEXT("properties"), Properties);
	if (RequiredNames.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> Req;
		for (const FString& N : RequiredNames)
		{
			Req.Add(MakeShared<FJsonValueString>(N));
		}
		Schema->SetArrayField(TEXT("required"), Req);
	}
	Schema->SetBoolField(TEXT("additionalProperties"), false);
	return Schema;
}

// ---------------------------------------------------------------------------------------------------------------------
// Validation
// ---------------------------------------------------------------------------------------------------------------------

namespace RuntimeToolSchema
{
	static const TCHAR* JsonTypeName(EJson Type)
	{
		switch (Type)
		{
		case EJson::None: return TEXT("none");
		case EJson::Null: return TEXT("null");
		case EJson::String: return TEXT("string");
		case EJson::Number: return TEXT("number");
		case EJson::Boolean: return TEXT("boolean");
		case EJson::Array: return TEXT("array");
		case EJson::Object: return TEXT("object");
		}
		return TEXT("unknown");
	}

	static bool ValidateObject(const TSharedPtr<FJsonObject>& Schema, const TSharedPtr<FJsonObject>& Object, const FString& Path, FString& OutError, bool bFillDefaults)
	{
		const TSharedPtr<FJsonObject>* PropsPtr = nullptr;
		const bool bHasProps = Schema->TryGetObjectField(TEXT("properties"), PropsPtr) && PropsPtr && PropsPtr->IsValid();

		const TArray<TSharedPtr<FJsonValue>>* RequiredArr = nullptr;
		if (Schema->TryGetArrayField(TEXT("required"), RequiredArr))
		{
			for (const TSharedPtr<FJsonValue>& R : *RequiredArr)
			{
				const FString Name = R->AsString();
				if (!RuntimeJson::HasValue(Object, Name))
				{
					OutError = FString::Printf(TEXT("%s.%s: required"), *Path, *Name);
					return false;
				}
			}
		}

		bool bAdditional = true;
		Schema->TryGetBoolField(TEXT("additionalProperties"), bAdditional);

		for (const FString& Key : RuntimeJson::Keys(Object))
		{
			const TSharedPtr<FJsonValue> Value = Object->TryGetField(Key);
			const TSharedPtr<FJsonObject>* PropSchema = nullptr;
			if (bHasProps && (*PropsPtr)->TryGetObjectField(Key, PropSchema))
			{
				if (Value.IsValid() && Value->IsNull())
				{
					continue; // null = "not provided"
				}
				if (!ValidateValue(*PropSchema, Value, Path + TEXT(".") + Key, OutError))
				{
					return false;
				}
			}
			else if (!bAdditional)
			{
				const TArray<FString> Known = bHasProps ? RuntimeJson::Keys(*PropsPtr) : TArray<FString>();
				OutError = FString::Printf(TEXT("%s.%s: unknown property (allowed: %s)"), *Path, *Key, *FString::Join(Known, TEXT(", ")));
				return false;
			}
		}

		if (bFillDefaults && bHasProps)
		{
			for (const FString& PropName : RuntimeJson::Keys(*PropsPtr))
			{
				const TSharedPtr<FJsonValue> PropValue = (*PropsPtr)->TryGetField(PropName);
				const TSharedPtr<FJsonObject> PropSchema = PropValue.IsValid() ? PropValue->AsObject() : nullptr;
				if (!PropSchema.IsValid())
				{
					continue;
				}
				const TSharedPtr<FJsonValue> Default = PropSchema->TryGetField(TEXT("default"));
				if (!RuntimeJson::HasValue(Object, PropName) && Default.IsValid())
				{
					Object->SetField(PropName, Default);
				}
			}
		}
		return true;
	}

	bool ValidateValue(const TSharedPtr<FJsonObject>& Schema, const TSharedPtr<FJsonValue>& Value, const FString& Path, FString& OutError)
	{
		if (!Schema.IsValid() || !Value.IsValid())
		{
			return true;
		}

		FString Type;
		Schema->TryGetStringField(TEXT("type"), Type);

		if (Type == TEXT("string"))
		{
			if (Value->Type != EJson::String)
			{
				OutError = FString::Printf(TEXT("%s: expected string, got %s"), *Path, JsonTypeName(Value->Type));
				return false;
			}
			const FString S = Value->AsString();
			double MaxLen = 0;
			if (Schema->TryGetNumberField(TEXT("maxLength"), MaxLen) && S.Len() > MaxLen)
			{
				OutError = FString::Printf(TEXT("%s: longer than %d characters"), *Path, static_cast<int32>(MaxLen));
				return false;
			}
			const TArray<TSharedPtr<FJsonValue>>* EnumArr = nullptr;
			if (Schema->TryGetArrayField(TEXT("enum"), EnumArr))
			{
				TArray<FString> Allowed;
				for (const TSharedPtr<FJsonValue>& E : *EnumArr)
				{
					Allowed.Add(E->AsString());
				}
				// Case- and underscore-insensitive, like reflected enums ("out_quad" matches "OutQuad").
				const FString Loose = S.Replace(TEXT("_"), TEXT(""));
				if (!Allowed.ContainsByPredicate([&Loose](const FString& A) { return A.Replace(TEXT("_"), TEXT("")).Equals(Loose, ESearchCase::IgnoreCase); }))
				{
					OutError = FString::Printf(TEXT("%s: '%s' is not one of [%s]"), *Path, *S, *FString::Join(Allowed, TEXT(", ")));
					return false;
				}
			}
			return true;
		}

		if (Type == TEXT("integer") || Type == TEXT("number"))
		{
			if (Value->Type != EJson::Number)
			{
				OutError = FString::Printf(TEXT("%s: expected %s, got %s"), *Path, *Type, JsonTypeName(Value->Type));
				return false;
			}
			const double N = Value->AsNumber();
			if (Type == TEXT("integer") && FMath::Frac(N) != 0.0)
			{
				OutError = FString::Printf(TEXT("%s: expected integer"), *Path);
				return false;
			}
			double Min = 0, Max = 0;
			if (Schema->TryGetNumberField(TEXT("minimum"), Min) && N < Min)
			{
				OutError = FString::Printf(TEXT("%s: must be >= %g"), *Path, Min);
				return false;
			}
			if (Schema->TryGetNumberField(TEXT("maximum"), Max) && N > Max)
			{
				OutError = FString::Printf(TEXT("%s: must be <= %g"), *Path, Max);
				return false;
			}
			return true;
		}

		if (Type == TEXT("boolean"))
		{
			if (Value->Type != EJson::Boolean)
			{
				OutError = FString::Printf(TEXT("%s: expected boolean, got %s"), *Path, JsonTypeName(Value->Type));
				return false;
			}
			return true;
		}

		if (Type == TEXT("array"))
		{
			if (Value->Type != EJson::Array)
			{
				OutError = FString::Printf(TEXT("%s: expected array, got %s"), *Path, JsonTypeName(Value->Type));
				return false;
			}
			const TArray<TSharedPtr<FJsonValue>>& Items = Value->AsArray();
			double MinItems = 0, MaxItems = 0;
			if (Schema->TryGetNumberField(TEXT("minItems"), MinItems) && Items.Num() < MinItems)
			{
				OutError = FString::Printf(TEXT("%s: needs at least %d items"), *Path, static_cast<int32>(MinItems));
				return false;
			}
			if (Schema->TryGetNumberField(TEXT("maxItems"), MaxItems) && Items.Num() > MaxItems)
			{
				OutError = FString::Printf(TEXT("%s: at most %d items"), *Path, static_cast<int32>(MaxItems));
				return false;
			}
			const TSharedPtr<FJsonObject>* ItemSchema = nullptr;
			if (Schema->TryGetObjectField(TEXT("items"), ItemSchema))
			{
				for (int32 i = 0; i < Items.Num(); ++i)
				{
					if (!ValidateValue(*ItemSchema, Items[i], FString::Printf(TEXT("%s[%d]"), *Path, i), OutError))
					{
						return false;
					}
				}
			}
			return true;
		}

		if (Type == TEXT("object"))
		{
			if (Value->Type != EJson::Object)
			{
				OutError = FString::Printf(TEXT("%s: expected object, got %s"), *Path, JsonTypeName(Value->Type));
				return false;
			}
			return ValidateObject(Schema, Value->AsObject(), Path, OutError, false);
		}

		return true; // untyped = any
	}

	bool ValidateAndFill(const TSharedRef<FJsonObject>& Schema, const TSharedRef<FJsonObject>& InOutArgs, FString& OutError)
	{
		return ValidateObject(Schema, InOutArgs, TEXT("$"), OutError, true);
	}
}
