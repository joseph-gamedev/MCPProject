#include "RuntimePropertyPath.h"
#include "RuntimeJson.h"
#include "Components/ActorComponent.h"
#include "Components/SceneComponent.h"
#include "JsonObjectConverter.h"
#include "UObject/UnrealType.h"

namespace RuntimePropertyPath
{
	static bool ParseSegment(const FString& Segment, FString& OutName, int32& OutIndex, FString& OutError)
	{
		OutIndex = INDEX_NONE;
		int32 Bracket = INDEX_NONE;
		if (!Segment.FindChar(TEXT('['), Bracket))
		{
			OutName = Segment;
			return !OutName.IsEmpty();
		}
		OutName = Segment.Left(Bracket);
		const FString Rest = Segment.Mid(Bracket + 1);
		if (!Rest.EndsWith(TEXT("]")))
		{
			OutError = FString::Printf(TEXT("bad index in '%s'"), *Segment);
			return false;
		}
		const FString IndexText = Rest.LeftChop(1);
		if (!IndexText.IsNumeric())
		{
			OutError = FString::Printf(TEXT("bad index in '%s'"), *Segment);
			return false;
		}
		OutIndex = FCString::Atoi(*IndexText);
		return !OutName.IsEmpty();
	}

	static FProperty* FindPropertyCaseInsensitive(const UStruct* Struct, const FString& Name)
	{
		if (FProperty* P = Struct->FindPropertyByName(FName(*Name)))
		{
			return P;
		}
		for (TFieldIterator<FProperty> It(Struct); It; ++It)
		{
			if (It->GetName().Equals(Name, ESearchCase::IgnoreCase))
			{
				return *It;
			}
		}
		return nullptr;
	}

	bool Resolve(UObject* Root, const FString& Path, FResolved& Out, FString& OutError)
	{
		if (!Root)
		{
			OutError = TEXT("no object");
			return false;
		}
		TArray<FString> Segments;
		Path.ParseIntoArray(Segments, TEXT("."), true);
		if (Segments.Num() == 0)
		{
			OutError = TEXT("empty property path");
			return false;
		}

		const UStruct* Struct = Root->GetClass();
		void* Container = Root;
		UObject* Owner = Root;

		for (int32 i = 0; i < Segments.Num(); ++i)
		{
			FString Name;
			int32 Index;
			if (!ParseSegment(Segments[i], Name, Index, OutError))
			{
				if (OutError.IsEmpty())
				{
					OutError = FString::Printf(TEXT("bad path segment '%s'"), *Segments[i]);
				}
				return false;
			}
			FProperty* Prop = FindPropertyCaseInsensitive(Struct, Name);
			if (!Prop)
			{
				OutError = FString::Printf(TEXT("'%s' has no property '%s'"), *Struct->GetName(), *Name);
				return false;
			}
			void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Container);

			if (Index != INDEX_NONE)
			{
				if (FArrayProperty* ArrayProp = CastField<FArrayProperty>(Prop))
				{
					FScriptArrayHelper Helper(ArrayProp, ValuePtr);
					if (!Helper.IsValidIndex(Index))
					{
						OutError = FString::Printf(TEXT("%s[%d] out of range (size %d)"), *Name, Index, Helper.Num());
						return false;
					}
					ValuePtr = Helper.GetRawPtr(Index);
					Prop = ArrayProp->Inner;
				}
				else if (Prop->ArrayDim > 1)
				{
					if (Index < 0 || Index >= Prop->ArrayDim)
					{
						OutError = FString::Printf(TEXT("%s[%d] out of range (size %d)"), *Name, Index, Prop->ArrayDim);
						return false;
					}
					ValuePtr = Prop->ContainerPtrToValuePtr<void>(Container, Index);
				}
				else
				{
					OutError = FString::Printf(TEXT("'%s' is not an array"), *Name);
					return false;
				}
			}

			const bool bLast = i == Segments.Num() - 1;
			if (bLast)
			{
				Out.Property = Prop;
				Out.ValuePtr = ValuePtr;
				Out.OwnerObject = Owner;
				return true;
			}

			if (FStructProperty* StructProp = CastField<FStructProperty>(Prop))
			{
				Struct = StructProp->Struct;
				Container = ValuePtr;
			}
			else if (FObjectPropertyBase* ObjProp = CastField<FObjectPropertyBase>(Prop))
			{
				UObject* Next = ObjProp->GetObjectPropertyValue(ValuePtr);
				if (!Next)
				{
					OutError = FString::Printf(TEXT("'%s' is null"), *Name);
					return false;
				}
				Struct = Next->GetClass();
				Container = Next;
				Owner = Next;
			}
			else
			{
				OutError = FString::Printf(TEXT("cannot step into '%s' (%s)"), *Name, *DescribeType(Prop));
				return false;
			}
		}
		OutError = TEXT("unreachable");
		return false;
	}

	static bool IsVectorLike(const UScriptStruct* S)
	{
		return S == TBaseStructure<FVector>::Get() || S == TBaseStructure<FRotator>::Get();
	}

	TSharedPtr<FJsonValue> ToJson(FProperty* Property, const void* ValuePtr)
	{
		if (FStructProperty* SP = CastField<FStructProperty>(Property))
		{
			if (SP->Struct == TBaseStructure<FVector>::Get())
			{
				return RuntimeJson::Vec(*static_cast<const FVector*>(ValuePtr));
			}
			if (SP->Struct == TBaseStructure<FRotator>::Get())
			{
				return RuntimeJson::Rot(*static_cast<const FRotator*>(ValuePtr));
			}
		}
		if (FObjectPropertyBase* OP = CastField<FObjectPropertyBase>(Property))
		{
			const UObject* Obj = OP->GetObjectPropertyValue(ValuePtr);
			return Obj ? StaticCastSharedRef<FJsonValue>(MakeShared<FJsonValueString>(Obj->GetPathName())) : MakeShared<FJsonValueNull>();
		}
		return FJsonObjectConverter::UPropertyToJsonValue(Property, ValuePtr);
	}

	bool FromJson(const TSharedPtr<FJsonValue>& InValue, FProperty* Property, void* ValuePtr, FString& OutError)
	{
		TSharedPtr<FJsonValue> Value = InValue;
		if (FStructProperty* SP = CastField<FStructProperty>(Property))
		{
			// Accept [x,y,z] / [p,y,r] / [r,g,b,a] shorthand.
			if (Value.IsValid() && Value->Type == EJson::Array)
			{
				const TArray<TSharedPtr<FJsonValue>>& A = Value->AsArray();
				if (IsVectorLike(SP->Struct) && A.Num() == 3)
				{
					TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
					const bool bRot = SP->Struct == TBaseStructure<FRotator>::Get();
					O->SetNumberField(bRot ? TEXT("Pitch") : TEXT("X"), A[0]->AsNumber());
					O->SetNumberField(bRot ? TEXT("Yaw") : TEXT("Y"), A[1]->AsNumber());
					O->SetNumberField(bRot ? TEXT("Roll") : TEXT("Z"), A[2]->AsNumber());
					Value = MakeShared<FJsonValueObject>(O);
				}
				else if ((SP->Struct == TBaseStructure<FLinearColor>::Get() || SP->Struct == TBaseStructure<FColor>::Get()) && (A.Num() == 3 || A.Num() == 4))
				{
					TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
					const bool bByte = SP->Struct == TBaseStructure<FColor>::Get();
					O->SetNumberField(TEXT("R"), A[0]->AsNumber());
					O->SetNumberField(TEXT("G"), A[1]->AsNumber());
					O->SetNumberField(TEXT("B"), A[2]->AsNumber());
					O->SetNumberField(TEXT("A"), A.Num() == 4 ? A[3]->AsNumber() : (bByte ? 255.0 : 1.0));
					Value = MakeShared<FJsonValueObject>(O);
				}
			}
		}
		if (FObjectPropertyBase* OP = CastField<FObjectPropertyBase>(Property))
		{
			if (!Value.IsValid() || Value->IsNull())
			{
				OP->SetObjectPropertyValue(ValuePtr, nullptr);
				return true;
			}
			FString PathName;
			if (!Value->TryGetString(PathName))
			{
				OutError = TEXT("object properties take an object path string or null");
				return false;
			}
			UObject* Obj = StaticFindObject(OP->PropertyClass, nullptr, *PathName);
			if (!Obj && FPackageName::IsValidObjectPath(PathName))
			{
				Obj = StaticLoadObject(OP->PropertyClass, nullptr, *PathName);
			}
			if (!Obj)
			{
				OutError = FString::Printf(TEXT("'%s' not found or not a %s"), *PathName, *OP->PropertyClass->GetName());
				return false;
			}
			OP->SetObjectPropertyValue(ValuePtr, Obj);
			return true;
		}

		FText Reason;
		if (!FJsonObjectConverter::JsonValueToUProperty(Value, Property, ValuePtr, 0, 0, false, &Reason))
		{
			OutError = FString::Printf(TEXT("cannot convert value to %s%s%s"), *DescribeType(Property), Reason.IsEmpty() ? TEXT("") : TEXT(": "),
				*Reason.ToString());
			return false;
		}
		return true;
	}

	TSharedPtr<FJsonValue> Get(UObject* Root, const FString& Path, FString& OutError)
	{
		FResolved R;
		if (!Resolve(Root, Path, R, OutError))
		{
			return nullptr;
		}
		return ToJson(R.Property, R.ValuePtr);
	}

	bool Set(UObject* Root, const FString& Path, const TSharedPtr<FJsonValue>& Value, FString& OutError)
	{
		FResolved R;
		if (!Resolve(Root, Path, R, OutError))
		{
			return false;
		}
		if (!FromJson(Value, R.Property, R.ValuePtr, OutError))
		{
			return false;
		}
		if (USceneComponent* SceneComp = Cast<USceneComponent>(R.OwnerObject))
		{
			SceneComp->UpdateComponentToWorld();
		}
		if (UActorComponent* Comp = Cast<UActorComponent>(R.OwnerObject))
		{
			Comp->MarkRenderStateDirty();
		}
		return true;
	}

	FString DescribeType(const FProperty* Property)
	{
		if (!Property)
		{
			return TEXT("?");
		}
		FString Extended;
		const FString Type = Property->GetCPPType(&Extended);
		return Type + Extended;
	}
}
