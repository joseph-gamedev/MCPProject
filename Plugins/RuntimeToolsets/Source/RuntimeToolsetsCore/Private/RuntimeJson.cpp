#include "RuntimeJson.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace RuntimeJson
{
	double Round(double Value)
	{
		return FMath::RoundToDouble(Value * 1000.0) / 1000.0;
	}

	TSharedRef<FJsonValue> Vec(const FVector& V)
	{
		TArray<TSharedPtr<FJsonValue>> A;
		A.Add(MakeShared<FJsonValueNumber>(Round(V.X)));
		A.Add(MakeShared<FJsonValueNumber>(Round(V.Y)));
		A.Add(MakeShared<FJsonValueNumber>(Round(V.Z)));
		return MakeShared<FJsonValueArray>(A);
	}

	TSharedRef<FJsonValue> Rot(const FRotator& R)
	{
		TArray<TSharedPtr<FJsonValue>> A;
		A.Add(MakeShared<FJsonValueNumber>(Round(R.Pitch)));
		A.Add(MakeShared<FJsonValueNumber>(Round(R.Yaw)));
		A.Add(MakeShared<FJsonValueNumber>(Round(R.Roll)));
		return MakeShared<FJsonValueArray>(A);
	}

	TSharedRef<FJsonValue> Strings(const TArray<FString>& Values)
	{
		TArray<TSharedPtr<FJsonValue>> A;
		for (const FString& S : Values)
		{
			A.Add(MakeShared<FJsonValueString>(S));
		}
		return MakeShared<FJsonValueArray>(A);
	}

	TSharedRef<FJsonValue> Names(const TArray<FName>& Values)
	{
		TArray<TSharedPtr<FJsonValue>> A;
		for (const FName& N : Values)
		{
			A.Add(MakeShared<FJsonValueString>(N.ToString()));
		}
		return MakeShared<FJsonValueArray>(A);
	}

	static bool ToThree(const TSharedPtr<FJsonValue>& Value, double& A, double& B, double& C, const TCHAR* KA, const TCHAR* KB, const TCHAR* KC)
	{
		if (!Value.IsValid())
		{
			return false;
		}
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (Value->TryGetArray(Arr) && Arr && Arr->Num() == 3)
		{
			return (*Arr)[0]->TryGetNumber(A) && (*Arr)[1]->TryGetNumber(B) && (*Arr)[2]->TryGetNumber(C);
		}
		const TSharedPtr<FJsonObject>* Obj = nullptr;
		if (Value->TryGetObject(Obj) && Obj && Obj->IsValid())
		{
			return (*Obj)->TryGetNumberField(KA, A) && (*Obj)->TryGetNumberField(KB, B) && (*Obj)->TryGetNumberField(KC, C);
		}
		return false;
	}

	bool ToVector(const TSharedPtr<FJsonValue>& Value, FVector& Out)
	{
		double X, Y, Z;
		if (!ToThree(Value, X, Y, Z, TEXT("X"), TEXT("Y"), TEXT("Z")))
		{
			return false;
		}
		Out = FVector(X, Y, Z);
		return true;
	}

	bool ToRotator(const TSharedPtr<FJsonValue>& Value, FRotator& Out)
	{
		double P, Y, R;
		if (!ToThree(Value, P, Y, R, TEXT("Pitch"), TEXT("Yaw"), TEXT("Roll")))
		{
			return false;
		}
		Out = FRotator(P, Y, R);
		return true;
	}

	FString Write(const TSharedRef<FJsonObject>& Object)
	{
		FString Out;
		TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
		FJsonSerializer::Serialize(Object, Writer);
		return Out;
	}

	FString WriteValue(const TSharedPtr<FJsonValue>& Value)
	{
		if (!Value.IsValid())
		{
			return TEXT("null");
		}
		FString Out;
		TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
		FJsonSerializer::Serialize(Value, FString(), Writer);
		return Out;
	}

	TArray<FString> Keys(const TSharedPtr<FJsonObject>& Object)
	{
		TArray<FString> Out;
		if (Object.IsValid())
		{
			for (const auto& Pair : Object->Values)
			{
				Out.Add(FString(*Pair.Key));
			}
		}
		return Out;
	}

	TSharedPtr<FJsonValue> Field(const TSharedPtr<FJsonObject>& Object, FStringView Name)
	{
		return Object.IsValid() ? Object->TryGetField(Name) : nullptr;
	}

	bool HasValue(const TSharedPtr<FJsonObject>& Object, FStringView Name)
	{
		const TSharedPtr<FJsonValue> V = Field(Object, Name);
		return V.IsValid() && !V->IsNull();
	}

	static TSharedPtr<FJsonValue> SortValue(const TSharedPtr<FJsonValue>& Value);

	static TSharedRef<FJsonObject> SortObject(const TSharedRef<FJsonObject>& Object)
	{
		TArray<FString> SortedKeys = Keys(Object);
		SortedKeys.Sort();
		TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
		for (const FString& Key : SortedKeys)
		{
			Out->SetField(Key, SortValue(Object->TryGetField(Key)));
		}
		return Out;
	}

	static TSharedPtr<FJsonValue> SortValue(const TSharedPtr<FJsonValue>& Value)
	{
		if (!Value.IsValid())
		{
			return Value;
		}
		if (Value->Type == EJson::Object)
		{
			return MakeShared<FJsonValueObject>(SortObject(Value->AsObject().ToSharedRef()));
		}
		if (Value->Type == EJson::Array)
		{
			TArray<TSharedPtr<FJsonValue>> Items;
			for (const TSharedPtr<FJsonValue>& Item : Value->AsArray())
			{
				Items.Add(SortValue(Item));
			}
			return MakeShared<FJsonValueArray>(Items);
		}
		return Value;
	}

	FString WriteSorted(const TSharedRef<FJsonObject>& Object)
	{
		FString Out;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
		FJsonSerializer::Serialize(SortObject(Object), Writer);
		Out.ReplaceInline(TEXT("\r\n"), TEXT("\n"));
		return Out;
	}

	TSharedPtr<FJsonObject> Parse(const FString& Text)
	{
		TSharedPtr<FJsonObject> Out;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
		if (!FJsonSerializer::Deserialize(Reader, Out))
		{
			return nullptr;
		}
		return Out;
	}

	TSharedPtr<FJsonValue> ParseValue(const FString& Text)
	{
		TSharedPtr<FJsonValue> Out;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
		if (!FJsonSerializer::Deserialize(Reader, Out))
		{
			return nullptr;
		}
		return Out;
	}
}
