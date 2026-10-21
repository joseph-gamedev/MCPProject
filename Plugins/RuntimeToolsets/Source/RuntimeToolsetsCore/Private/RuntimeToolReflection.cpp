#include "RuntimeToolReflection.h"
#include "RuntimeJson.h"
#include "RuntimeObjectResolver.h"
#include "RuntimeSceneGraph.h"
#include "RuntimeToolCall.h"
#include "RuntimeToolsetsLog.h"
#include "Components/ActorComponent.h"
#include "GameFramework/Actor.h"
#include "UObject/EnumProperty.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/TextProperty.h"
#include "UObject/UnrealType.h"

namespace RuntimeToolReflection
{
	// ---- names --------------------------------------------------------------------------------------------------------

	FString ToSnakeCase(const FString& In, bool bStripBoolPrefix)
	{
		FString N = In;
		if (bStripBoolPrefix && N.Len() > 1 && N[0] == TEXT('b') && FChar::IsUpper(N[1]))
		{
			N = N.Mid(1);
		}
		FString Out;
		Out.Reserve(N.Len() + 8);
		for (int32 i = 0; i < N.Len(); ++i)
		{
			const TCHAR C = N[i];
			if (FChar::IsUpper(C))
			{
				const bool bPrevLower = i > 0 && FChar::IsLower(N[i - 1]);
				const bool bPrevDigit = i > 0 && FChar::IsDigit(N[i - 1]);
				const bool bAcronymEnd = i > 0 && FChar::IsUpper(N[i - 1]) && i + 1 < N.Len() && FChar::IsLower(N[i + 1]);
				// "Scale3D" -> "scale3d", "Vector2D" -> "vector2d"
				const bool bDimension = bPrevDigit && C == TEXT('D') && (i + 1 == N.Len() || !FChar::IsLower(N[i + 1]));
				if ((bPrevLower || bAcronymEnd || (bPrevDigit && !bDimension)) && Out.Len() > 0 && Out[Out.Len() - 1] != TEXT('_'))
				{
					Out += TEXT('_');
				}
				Out += FChar::ToLower(C);
			}
			else if (C == TEXT('_') || C == TEXT(' '))
			{
				if (Out.Len() > 0 && Out[Out.Len() - 1] != TEXT('_'))
				{
					Out += TEXT('_');
				}
			}
			else
			{
				Out += C;
			}
		}
		return Out;
	}

	FString JsonName(const FProperty* Property)
	{
		return ToSnakeCase(Property->GetName(), CastField<FBoolProperty>(Property) != nullptr);
	}

	static FString Tooltip(const FField* Field)
	{
#if WITH_METADATA
		return Field->GetMetaData(TEXT("ToolTip"));
#else
		return FString();
#endif
	}

	// ---- special structs ----------------------------------------------------------------------------------------------

	enum class ESpecial : uint8 { None, Vector, Vector2D, Rotator, LinearColor, Color, Quat, Transform, IntPoint, IntVector, Guid, SoftObject, SoftClass, Json };

	static ESpecial SpecialOf(const UScriptStruct* S)
	{
		if (S == TBaseStructure<FVector>::Get()) return ESpecial::Vector;
		if (S == TBaseStructure<FVector2D>::Get()) return ESpecial::Vector2D;
		if (S == TBaseStructure<FRotator>::Get()) return ESpecial::Rotator;
		if (S == TBaseStructure<FLinearColor>::Get()) return ESpecial::LinearColor;
		if (S == TBaseStructure<FColor>::Get()) return ESpecial::Color;
		if (S == TBaseStructure<FQuat>::Get()) return ESpecial::Quat;
		if (S == TBaseStructure<FTransform>::Get()) return ESpecial::Transform;
		if (S == TBaseStructure<FIntPoint>::Get()) return ESpecial::IntPoint;
		if (S == TBaseStructure<FIntVector>::Get()) return ESpecial::IntVector;
		if (S == TBaseStructure<FGuid>::Get()) return ESpecial::Guid;
		if (S == TBaseStructure<FSoftObjectPath>::Get()) return ESpecial::SoftObject;
		if (S == TBaseStructure<FSoftClassPath>::Get()) return ESpecial::SoftClass;
		if (S == FRuntimeToolJson::StaticStruct()) return ESpecial::Json;
		return ESpecial::None;
	}

	static TSharedRef<FJsonObject> NumberArraySchema(int32 Min, int32 Max, const TCHAR* Description)
	{
		TSharedRef<FJsonObject> S = MakeShared<FJsonObject>();
		S->SetStringField(TEXT("type"), TEXT("array"));
		TSharedRef<FJsonObject> Items = MakeShared<FJsonObject>();
		Items->SetStringField(TEXT("type"), TEXT("number"));
		S->SetObjectField(TEXT("items"), Items);
		S->SetNumberField(TEXT("minItems"), Min);
		S->SetNumberField(TEXT("maxItems"), Max);
		S->SetStringField(TEXT("description"), Description);
		return S;
	}

	static TArray<double> ReadNumbers(const TSharedPtr<FJsonValue>& J, const TArray<FString>& Keys)
	{
		TArray<double> Out;
		const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
		if (J->TryGetArray(A))
		{
			for (const TSharedPtr<FJsonValue>& V : *A)
			{
				double D = 0;
				if (!V->TryGetNumber(D))
				{
					return {};
				}
				Out.Add(D);
			}
			return Out;
		}
		const TSharedPtr<FJsonObject>* O = nullptr;
		if (J->TryGetObject(O))
		{
			// {"x":..,"y":..} (Epic's shape) is accepted too.
			for (const FString& K : Keys)
			{
				double D = 0;
				if (!(*O)->TryGetNumberField(K, D) && !(*O)->TryGetNumberField(K.ToUpper(), D))
				{
					break;
				}
				Out.Add(D);
			}
		}
		return Out;
	}

	static TSharedRef<FJsonValueArray> Numbers(std::initializer_list<double> Values)
	{
		TArray<TSharedPtr<FJsonValue>> A;
		for (double V : Values)
		{
			A.Add(MakeShared<FJsonValueNumber>(FMath::RoundToDouble(V * 10000.0) / 10000.0));
		}
		return MakeShared<FJsonValueArray>(A);
	}

	// ---- custom converters ---

	struct FConverterEntry
	{
		FStructConverter Converter;
		bool bIncludeChildren = true;
	};

	static TMap<const UScriptStruct*, FConverterEntry>& Converters()
	{
		static TMap<const UScriptStruct*, FConverterEntry> Map;
		return Map;
	}

	void RegisterStructConverter(const UScriptStruct* Struct, FStructConverter Converter, bool bIncludeChildren)
	{
		Converters().Add(Struct, {MoveTemp(Converter), bIncludeChildren});
	}

	void UnregisterStructConverter(const UScriptStruct* Struct)
	{
		Converters().Remove(Struct);
	}

	static const FStructConverter* FindConverter(const UStruct* Struct)
	{
		for (const UStruct* S = Struct; S; S = S->GetSuperStruct())
		{
			if (const FConverterEntry* E = Converters().Find(Cast<UScriptStruct>(S)); E && (S == Struct || E->bIncludeChildren))
			{
				return &E->Converter;
			}
		}
		return nullptr;
	}

	// ---- schema -------------------------------------------------------------------------------------------------------

	static TArray<FString> EnumNames(const UEnum* Enum)
	{
		TArray<FString> Names;
		for (int32 i = 0; i < Enum->NumEnums(); ++i)
		{
#if WITH_METADATA
			if (Enum->HasMetaData(TEXT("Hidden"), i))
			{
				continue;
			}
#endif
			const FString Name = Enum->GetNameStringByIndex(i);
			if (!Name.EndsWith(TEXT("_MAX")) && !Name.EndsWith(TEXT("MAX")))
			{
				Names.Add(Name);
			}
		}
		return Names;
	}

	static TSharedRef<FJsonObject> StructSchema(const UStruct* Struct, bool bOutput)
	{
		TSharedRef<FJsonObject> S = MakeShared<FJsonObject>();
		S->SetStringField(TEXT("type"), TEXT("object"));
		S->SetStringField(TEXT("title"), Struct->GetName());
		TSharedRef<FJsonObject> Props = MakeShared<FJsonObject>();
		for (TFieldIterator<FProperty> It(Struct); It; ++It)
		{
			if (It->HasAnyPropertyFlags(CPF_Deprecated | CPF_Transient))
			{
				continue;
			}
			Props->SetObjectField(JsonName(*It), PropertySchema(*It, bOutput));
		}
		S->SetObjectField(TEXT("properties"), Props);
		return S;
	}

	TSharedRef<FJsonObject> PropertySchema(const FProperty* P, bool bOutput)
	{
		TSharedRef<FJsonObject> S = MakeShared<FJsonObject>();
		FString Extra;
		if (CastField<FBoolProperty>(P))
		{
			S->SetStringField(TEXT("type"), TEXT("boolean"));
		}
		else if (const FEnumProperty* EP = CastField<FEnumProperty>(P))
		{
			S->SetStringField(TEXT("type"), TEXT("string"));
			S->SetField(TEXT("enum"), RuntimeJson::Strings(EnumNames(EP->GetEnum())));
		}
		else if (const FByteProperty* BP = CastField<FByteProperty>(P); BP && BP->Enum)
		{
			S->SetStringField(TEXT("type"), TEXT("string"));
			S->SetField(TEXT("enum"), RuntimeJson::Strings(EnumNames(BP->Enum)));
		}
		else if (const FNumericProperty* NP = CastField<FNumericProperty>(P))
		{
			S->SetStringField(TEXT("type"), NP->IsInteger() ? TEXT("integer") : TEXT("number"));
#if WITH_METADATA
			if (P->HasMetaData(TEXT("ClampMin")))
			{
				S->SetNumberField(TEXT("minimum"), FCString::Atod(*P->GetMetaData(TEXT("ClampMin"))));
			}
			if (P->HasMetaData(TEXT("ClampMax")))
			{
				S->SetNumberField(TEXT("maximum"), FCString::Atod(*P->GetMetaData(TEXT("ClampMax"))));
			}
#endif
		}
		else if (CastField<FStrProperty>(P) || CastField<FNameProperty>(P) || CastField<FTextProperty>(P))
		{
			S->SetStringField(TEXT("type"), TEXT("string"));
		}
		else if (const FStructProperty* ConvP = CastField<FStructProperty>(P); ConvP && FindConverter(ConvP->Struct))
		{
			const FStructConverter* C = FindConverter(ConvP->Struct);
			if (C->Schema.IsValid())
			{
				S = RuntimeJson::Parse(RuntimeJson::Write(C->Schema.ToSharedRef())).ToSharedRef();
			}
		}
		else if (const FStructProperty* SP = CastField<FStructProperty>(P))
		{
			switch (SpecialOf(SP->Struct))
			{
			case ESpecial::Vector: return NumberArraySchema(3, 3, TEXT("[x, y, z] (cm)"));
			case ESpecial::Vector2D: return NumberArraySchema(2, 2, TEXT("[x, y]"));
			case ESpecial::IntPoint: return NumberArraySchema(2, 2, TEXT("[x, y] integers"));
			case ESpecial::IntVector: return NumberArraySchema(3, 3, TEXT("[x, y, z] integers"));
			case ESpecial::Rotator: return NumberArraySchema(3, 3, TEXT("[pitch, yaw, roll] (degrees)"));
			case ESpecial::LinearColor: return NumberArraySchema(3, 4, TEXT("[r, g, b] or [r, g, b, a], linear 0-1"));
			case ESpecial::Color: return NumberArraySchema(3, 4, TEXT("[r, g, b] or [r, g, b, a], 0-255"));
			case ESpecial::Quat: return NumberArraySchema(4, 4, TEXT("[x, y, z, w]"));
			case ESpecial::Transform:
			{
				S->SetStringField(TEXT("type"), TEXT("object"));
				TSharedRef<FJsonObject> Props = MakeShared<FJsonObject>();
				Props->SetObjectField(TEXT("location"), NumberArraySchema(3, 3, TEXT("[x, y, z] (cm)")));
				Props->SetObjectField(TEXT("rotation"), NumberArraySchema(3, 3, TEXT("[pitch, yaw, roll] (degrees)")));
				Props->SetObjectField(TEXT("scale"), NumberArraySchema(3, 3, TEXT("[x, y, z]")));
				S->SetObjectField(TEXT("properties"), Props);
				break;
			}
			case ESpecial::Guid:
			case ESpecial::SoftObject:
				S->SetStringField(TEXT("type"), TEXT("string"));
				Extra = TEXT("Object path");
				break;
			case ESpecial::SoftClass:
				S->SetStringField(TEXT("type"), TEXT("string"));
				Extra = TEXT("Class name or path");
				break;
			case ESpecial::Json:
				Extra = TEXT("Any JSON value");
				break;
			default:
				S = StructSchema(SP->Struct, bOutput);
				break;
			}
		}
		else if (const FArrayProperty* AP = CastField<FArrayProperty>(P))
		{
			S->SetStringField(TEXT("type"), TEXT("array"));
			S->SetObjectField(TEXT("items"), PropertySchema(AP->Inner, bOutput));
		}
		else if (const FSetProperty* SetP = CastField<FSetProperty>(P))
		{
			S->SetStringField(TEXT("type"), TEXT("array"));
			S->SetObjectField(TEXT("items"), PropertySchema(SetP->ElementProp, bOutput));
		}
		else if (const FMapProperty* MP = CastField<FMapProperty>(P))
		{
			S->SetStringField(TEXT("type"), TEXT("object"));
			S->SetObjectField(TEXT("additionalProperties"), PropertySchema(MP->ValueProp, bOutput));
		}
		else if (const FClassProperty* CP = CastField<FClassProperty>(P))
		{
			S->SetStringField(TEXT("type"), TEXT("string"));
			Extra = FString::Printf(TEXT("Class name or path (a %s)"), CP->MetaClass ? *CP->MetaClass->GetName() : TEXT("class"));
		}
		else if (const FSoftClassProperty* SCP = CastField<FSoftClassProperty>(P))
		{
			S->SetStringField(TEXT("type"), TEXT("string"));
			Extra = FString::Printf(TEXT("Class name or path (a %s)"), SCP->MetaClass ? *SCP->MetaClass->GetName() : TEXT("class"));
		}
		else if (const FObjectPropertyBase* OutOP = CastField<FObjectPropertyBase>(P); OutOP && bOutput)
		{
			S->SetStringField(TEXT("type"), TEXT("object"));
			TSharedRef<FJsonObject> Props = MakeShared<FJsonObject>();
			TSharedRef<FJsonObject> RefS = MakeShared<FJsonObject>();
			RefS->SetStringField(TEXT("type"), TEXT("string"));
			RefS->SetStringField(TEXT("description"), TEXT("Pass this back to tools that take this object"));
			Props->SetObjectField(TEXT("ref"), RefS);
			TSharedRef<FJsonObject> Str = MakeShared<FJsonObject>();
			Str->SetStringField(TEXT("type"), TEXT("string"));
			Props->SetObjectField(TEXT("name"), Str);
			Props->SetObjectField(TEXT("class"), Str);
			S->SetObjectField(TEXT("properties"), Props);
			Extra = OutOP->PropertyClass->GetName() + TEXT(" (null if none)");
		}
		else if (const FObjectPropertyBase* OP = CastField<FObjectPropertyBase>(P))
		{
			S->SetStringField(TEXT("type"), TEXT("string"));
			Extra = FString::Printf(TEXT("Reference to %s: node id (n12), node path, actor:Name or object path"), *OP->PropertyClass->GetName());
		}
		FString Desc = Tooltip(P);
		if (!Extra.IsEmpty())
		{
			Desc = Desc.IsEmpty() ? Extra : Desc + TEXT(" (") + Extra + TEXT(")");
		}
		if (!Desc.IsEmpty())
		{
			S->SetStringField(TEXT("description"), Desc);
		}
		return S;
	}

	// ---- property -> JSON ---------------------------------------------------------------------------------------------

	static TSharedPtr<FJsonValue> ObjectToJson(const UObject* O, FRuntimeSceneGraph* Graph)
	{
		if (!O)
		{
			return MakeShared<FJsonValueNull>();
		}
		TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
		J->SetStringField(TEXT("ref"), RuntimeObjectResolver::MakeReference(O, Graph));
		if (const AActor* A = Cast<AActor>(O))
		{
#if WITH_EDITOR
			J->SetStringField(TEXT("name"), A->GetActorLabel());
#else
			J->SetStringField(TEXT("name"), A->GetName());
#endif
		}
		else
		{
			J->SetStringField(TEXT("name"), O->GetName());
		}
		J->SetStringField(TEXT("class"), O->GetClass()->GetName());
		return MakeShared<FJsonValueObject>(J);
	}

	TSharedRef<FJsonObject> StructToJson(const UStruct* Struct, const void* Data, FRuntimeSceneGraph* Graph)
	{
		TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
		for (TFieldIterator<FProperty> It(Struct); It; ++It)
		{
			if (!It->HasAnyPropertyFlags(CPF_Deprecated | CPF_Transient))
			{
				J->SetField(JsonName(*It), PropertyToJson(*It, It->ContainerPtrToValuePtr<void>(Data), Graph));
			}
		}
		return J;
	}

	TSharedPtr<FJsonValue> PropertyToJson(const FProperty* P, const void* V, FRuntimeSceneGraph* Graph)
	{
		if (const FBoolProperty* B = CastField<FBoolProperty>(P))
		{
			return MakeShared<FJsonValueBoolean>(B->GetPropertyValue(V));
		}
		if (const FEnumProperty* EP = CastField<FEnumProperty>(P))
		{
			const int64 Raw = EP->GetUnderlyingProperty()->GetSignedIntPropertyValue(V);
			return MakeShared<FJsonValueString>(EP->GetEnum()->GetNameStringByValue(Raw));
		}
		if (const FByteProperty* BP = CastField<FByteProperty>(P); BP && BP->Enum)
		{
			return MakeShared<FJsonValueString>(BP->Enum->GetNameStringByValue(BP->GetPropertyValue(V)));
		}
		if (const FNumericProperty* NP = CastField<FNumericProperty>(P))
		{
			return MakeShared<FJsonValueNumber>(NP->IsInteger() ? static_cast<double>(NP->GetSignedIntPropertyValue(V)) : NP->GetFloatingPointPropertyValue(V));
		}
		if (const FStrProperty* SP = CastField<FStrProperty>(P))
		{
			return MakeShared<FJsonValueString>(SP->GetPropertyValue(V));
		}
		if (const FNameProperty* NmP = CastField<FNameProperty>(P))
		{
			return MakeShared<FJsonValueString>(NmP->GetPropertyValue(V).ToString());
		}
		if (const FTextProperty* TP = CastField<FTextProperty>(P))
		{
			return MakeShared<FJsonValueString>(TP->GetPropertyValue(V).ToString());
		}
		if (const FStructProperty* StP = CastField<FStructProperty>(P); StP && FindConverter(StP->Struct))
		{
			return FindConverter(StP->Struct)->ToJson(V);
		}
		if (const FStructProperty* StP = CastField<FStructProperty>(P))
		{
			switch (SpecialOf(StP->Struct))
			{
			case ESpecial::Vector: { const FVector& X = *static_cast<const FVector*>(V); return Numbers({X.X, X.Y, X.Z}); }
			case ESpecial::Vector2D: { const FVector2D& X = *static_cast<const FVector2D*>(V); return Numbers({X.X, X.Y}); }
			case ESpecial::IntPoint: { const FIntPoint& X = *static_cast<const FIntPoint*>(V); return Numbers({double(X.X), double(X.Y)}); }
			case ESpecial::IntVector: { const FIntVector& X = *static_cast<const FIntVector*>(V); return Numbers({double(X.X), double(X.Y), double(X.Z)}); }
			case ESpecial::Rotator: { const FRotator& X = *static_cast<const FRotator*>(V); return Numbers({X.Pitch, X.Yaw, X.Roll}); }
			case ESpecial::LinearColor: { const FLinearColor& X = *static_cast<const FLinearColor*>(V); return Numbers({X.R, X.G, X.B, X.A}); }
			case ESpecial::Color: { const FColor& X = *static_cast<const FColor*>(V); return Numbers({double(X.R), double(X.G), double(X.B), double(X.A)}); }
			case ESpecial::Quat: { const FQuat& X = *static_cast<const FQuat*>(V); return Numbers({X.X, X.Y, X.Z, X.W}); }
			case ESpecial::Transform:
			{
				const FTransform& X = *static_cast<const FTransform*>(V);
				TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
				const FVector L = X.GetLocation(), Sc = X.GetScale3D();
				const FRotator R = X.Rotator();
				J->SetField(TEXT("location"), Numbers({L.X, L.Y, L.Z}));
				J->SetField(TEXT("rotation"), Numbers({R.Pitch, R.Yaw, R.Roll}));
				J->SetField(TEXT("scale"), Numbers({Sc.X, Sc.Y, Sc.Z}));
				return MakeShared<FJsonValueObject>(J);
			}
			case ESpecial::Guid: return MakeShared<FJsonValueString>(static_cast<const FGuid*>(V)->ToString());
			case ESpecial::SoftObject: return MakeShared<FJsonValueString>(static_cast<const FSoftObjectPath*>(V)->ToString());
			case ESpecial::SoftClass: return MakeShared<FJsonValueString>(static_cast<const FSoftClassPath*>(V)->ToString());
			case ESpecial::Json:
			{
				const FRuntimeToolJson& X = *static_cast<const FRuntimeToolJson*>(V);
				return X.Value.IsValid() ? X.Value : MakeShared<FJsonValueNull>();
			}
			default:
				return MakeShared<FJsonValueObject>(StructToJson(StP->Struct, V, Graph));
			}
		}
		if (const FArrayProperty* AP = CastField<FArrayProperty>(P))
		{
			FScriptArrayHelper H(AP, V);
			TArray<TSharedPtr<FJsonValue>> A;
			for (int32 i = 0; i < H.Num(); ++i)
			{
				A.Add(PropertyToJson(AP->Inner, H.GetRawPtr(i), Graph));
			}
			return MakeShared<FJsonValueArray>(A);
		}
		if (const FSetProperty* SetP = CastField<FSetProperty>(P))
		{
			FScriptSetHelper H(SetP, V);
			TArray<TSharedPtr<FJsonValue>> A;
			for (FScriptSetHelper::FIterator It(H); It; ++It)
			{
				A.Add(PropertyToJson(SetP->ElementProp, H.GetElementPtr(It), Graph));
			}
			return MakeShared<FJsonValueArray>(A);
		}
		if (const FMapProperty* MP = CastField<FMapProperty>(P))
		{
			FScriptMapHelper H(MP, V);
			TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
			for (FScriptMapHelper::FIterator It(H); It; ++It)
			{
				const TSharedPtr<FJsonValue> K = PropertyToJson(MP->KeyProp, H.GetKeyPtr(It), Graph);
				FString Key;
				if (!K->TryGetString(Key))
				{
					MP->KeyProp->ExportTextItem_Direct(Key, H.GetKeyPtr(It), nullptr, nullptr, PPF_None);
				}
				J->SetField(Key, PropertyToJson(MP->ValueProp, H.GetValuePtr(It), Graph));
			}
			return MakeShared<FJsonValueObject>(J);
		}
		if (const FClassProperty* CP = CastField<FClassProperty>(P))
		{
			const UObject* C = CP->GetObjectPropertyValue(V);
			return C ? StaticCastSharedRef<FJsonValue>(MakeShared<FJsonValueString>(C->GetPathName())) : MakeShared<FJsonValueNull>();
		}
		if (const FSoftObjectProperty* SOP = CastField<FSoftObjectProperty>(P))
		{
			return MakeShared<FJsonValueString>(SOP->GetPropertyValue(V).ToSoftObjectPath().ToString());
		}
		if (const FObjectPropertyBase* OP = CastField<FObjectPropertyBase>(P))
		{
			return ObjectToJson(OP->GetObjectPropertyValue(V), Graph);
		}
		FString Text;
		P->ExportTextItem_Direct(Text, V, nullptr, nullptr, PPF_None);
		return MakeShared<FJsonValueString>(Text);
	}

	// ---- JSON -> property ---------------------------------------------------------------------------------------------

	static bool SetEnum(const UEnum* Enum, const TSharedPtr<FJsonValue>& J, int64& Out, FString& OutError)
	{
		FString S;
		if (!J->TryGetString(S))
		{
			double D;
			if (J->TryGetNumber(D))
			{
				Out = static_cast<int64>(D);
				return true;
			}
			OutError = TEXT("expected an enum name");
			return false;
		}
		int64 V = Enum->GetValueByNameString(S);
		if (V == INDEX_NONE)
		{
			// Case-insensitive and snake_case tolerant ("on_interior" -> OnInterior).
			const FString Want = S.Replace(TEXT("_"), TEXT("")).ToLower();
			for (int32 i = 0; i < Enum->NumEnums(); ++i)
			{
				if (Enum->GetNameStringByIndex(i).ToLower() == Want)
				{
					V = Enum->GetValueByIndex(i);
					break;
				}
			}
		}
		if (V == INDEX_NONE)
		{
			OutError = FString::Printf(TEXT("'%s' is not one of %s"), *S, *FString::Join(EnumNames(Enum), TEXT(", ")));
			return false;
		}
		Out = V;
		return true;
	}

	static FString RefString(const TSharedPtr<FJsonValue>& J)
	{
		FString S;
		if (J->TryGetString(S))
		{
			return S;
		}
		const TSharedPtr<FJsonObject>* O = nullptr;
		if (J->TryGetObject(O))
		{
			for (const TCHAR* K : {TEXT("ref"), TEXT("ref_path"), TEXT("refPath"), TEXT("id"), TEXT("path")})
			{
				if ((*O)->TryGetStringField(K, S))
				{
					return S;
				}
			}
		}
		return FString();
	}

	UObject* ResolveObject(const FString& Ref, UClass* Expected, const FConvertContext& Ctx, FString& OutError)
	{
		FResolvedTarget T;
		ERuntimeToolError Err = ERuntimeToolError::NotFound;
		FString Msg;
		if (Ctx.World && RuntimeObjectResolver::Resolve(Ref, Ctx.World, Ctx.Graph, T, Err, Msg))
		{
			if (Expected && Expected->IsChildOf(UActorComponent::StaticClass()))
			{
				if (T.Component && T.Component->IsA(Expected))
				{
					return T.Component;
				}
				if (T.Actor)
				{
					if (UActorComponent* C = T.Actor->FindComponentByClass(Expected))
					{
						return C;
					}
				}
			}
			UObject* O = Expected && Expected->IsChildOf(AActor::StaticClass()) ? T.Actor : T.Object;
			if (O && (!Expected || O->IsA(Expected)))
			{
				return O;
			}
			if (T.Object && (!Expected || T.Object->IsA(Expected)))
			{
				return T.Object;
			}
		}
		// Assets and other loadable objects by path.
		if (Ref.Contains(TEXT("/")))
		{
			if (UObject* O = StaticLoadObject(Expected ? Expected : UObject::StaticClass(), nullptr, *Ref, nullptr, LOAD_NoWarn | LOAD_Quiet))
			{
				return O;
			}
		}
		OutError = Msg.IsEmpty() ? FString::Printf(TEXT("no %s '%s'"), Expected ? *Expected->GetName() : TEXT("object"), *Ref)
			: FString::Printf(TEXT("%s (expected a %s)"), *Msg, Expected ? *Expected->GetName() : TEXT("object"));
		return nullptr;
	}

	static UClass* ResolveClass(const FString& Name, UClass* Meta, FString& OutError)
	{
		UClass* C = nullptr;
		if (Name.Contains(TEXT("/")) || Name.Contains(TEXT(".")))
		{
			C = LoadClass<UObject>(nullptr, *Name, nullptr, LOAD_NoWarn | LOAD_Quiet);
			if (!C)
			{
				C = LoadClass<UObject>(nullptr, *(Name + TEXT("_C")), nullptr, LOAD_NoWarn | LOAD_Quiet);
			}
		}
		if (!C)
		{
			C = FindFirstObject<UClass>(*Name, EFindFirstObjectOptions::NativeFirst);
		}
		if (!C && Name.Len() > 1)
		{
			C = FindFirstObject<UClass>(*Name.Mid(1), EFindFirstObjectOptions::NativeFirst); // "AActor" -> "Actor"
		}
		if (!C)
		{
			OutError = FString::Printf(TEXT("no class '%s'"), *Name);
			return nullptr;
		}
		if (Meta && !C->IsChildOf(Meta))
		{
			OutError = FString::Printf(TEXT("%s is not a %s"), *C->GetName(), *Meta->GetName());
			return nullptr;
		}
		return C;
	}

	bool JsonToStruct(const TSharedPtr<FJsonObject>& Json, const UStruct* Struct, void* Data, const FConvertContext& Ctx, FString& OutError)
	{
		if (!Json.IsValid())
		{
			OutError = TEXT("expected an object");
			return false;
		}
		for (TFieldIterator<FProperty> It(Struct); It; ++It)
		{
			const FString Name = JsonName(*It);
			TSharedPtr<FJsonValue> V = Json->TryGetField(Name);
			if (!V.IsValid())
			{
				V = Json->TryGetField(It->GetName()); // PascalCase accepted too
			}
			if (V.IsValid() && !V->IsNull())
			{
				FString Err;
				if (!JsonToProperty(V, *It, It->ContainerPtrToValuePtr<void>(Data), Ctx, Err))
				{
					OutError = FString::Printf(TEXT(".%s: %s"), *Name, *Err);
					return false;
				}
			}
		}
		return true;
	}

	bool JsonToProperty(const TSharedPtr<FJsonValue>& J, const FProperty* P, void* V, const FConvertContext& Ctx, FString& OutError)
	{
		if (const FBoolProperty* B = CastField<FBoolProperty>(P))
		{
			bool X;
			if (!J->TryGetBool(X))
			{
				OutError = TEXT("expected true/false");
				return false;
			}
			B->SetPropertyValue(V, X);
			return true;
		}
		if (const FEnumProperty* EP = CastField<FEnumProperty>(P))
		{
			int64 X;
			if (!SetEnum(EP->GetEnum(), J, X, OutError))
			{
				return false;
			}
			EP->GetUnderlyingProperty()->SetIntPropertyValue(V, X);
			return true;
		}
		if (const FByteProperty* BP = CastField<FByteProperty>(P); BP && BP->Enum)
		{
			int64 X;
			if (!SetEnum(BP->Enum, J, X, OutError))
			{
				return false;
			}
			BP->SetPropertyValue(V, static_cast<uint8>(X));
			return true;
		}
		if (const FNumericProperty* NP = CastField<FNumericProperty>(P))
		{
			double D;
			if (!J->TryGetNumber(D))
			{
				OutError = TEXT("expected a number");
				return false;
			}
			if (NP->IsInteger())
			{
				NP->SetIntPropertyValue(V, static_cast<int64>(FMath::RoundToDouble(D)));
			}
			else
			{
				NP->SetFloatingPointPropertyValue(V, D);
			}
			return true;
		}
		if (CastField<FStrProperty>(P) || CastField<FNameProperty>(P) || CastField<FTextProperty>(P))
		{
			FString S;
			if (!J->TryGetString(S))
			{
				if (J->Type == EJson::Number)
				{
					S = FString::SanitizeFloat(J->AsNumber());
				}
				else
				{
					OutError = TEXT("expected a string");
					return false;
				}
			}
			if (const FStrProperty* SP = CastField<FStrProperty>(P)) { SP->SetPropertyValue(V, S); }
			else if (const FNameProperty* NmP = CastField<FNameProperty>(P)) { NmP->SetPropertyValue(V, FName(*S)); }
			else { CastField<FTextProperty>(P)->SetPropertyValue(V, FText::FromString(S)); }
			return true;
		}
		if (const FStructProperty* StP = CastField<FStructProperty>(P); StP && FindConverter(StP->Struct))
		{
			return FindConverter(StP->Struct)->FromJson(J, V, OutError);
		}
		if (const FStructProperty* StP = CastField<FStructProperty>(P))
		{
			const ESpecial K = SpecialOf(StP->Struct);
			auto Need = [&](const TArray<double>& N, int32 Min) { if (N.Num() < Min) { OutError = FString::Printf(TEXT("expected %d numbers"), Min); return false; } return true; };
			switch (K)
			{
			case ESpecial::Vector: { const TArray<double> N = ReadNumbers(J, {TEXT("x"), TEXT("y"), TEXT("z")}); if (!Need(N, 3)) return false; *static_cast<FVector*>(V) = FVector(N[0], N[1], N[2]); return true; }
			case ESpecial::Vector2D: { const TArray<double> N = ReadNumbers(J, {TEXT("x"), TEXT("y")}); if (!Need(N, 2)) return false; *static_cast<FVector2D*>(V) = FVector2D(N[0], N[1]); return true; }
			case ESpecial::IntPoint: { const TArray<double> N = ReadNumbers(J, {TEXT("x"), TEXT("y")}); if (!Need(N, 2)) return false; *static_cast<FIntPoint*>(V) = FIntPoint(N[0], N[1]); return true; }
			case ESpecial::IntVector: { const TArray<double> N = ReadNumbers(J, {TEXT("x"), TEXT("y"), TEXT("z")}); if (!Need(N, 3)) return false; *static_cast<FIntVector*>(V) = FIntVector(N[0], N[1], N[2]); return true; }
			case ESpecial::Rotator: { const TArray<double> N = ReadNumbers(J, {TEXT("pitch"), TEXT("yaw"), TEXT("roll")}); if (!Need(N, 3)) return false; *static_cast<FRotator*>(V) = FRotator(N[0], N[1], N[2]); return true; }
			case ESpecial::LinearColor: { const TArray<double> N = ReadNumbers(J, {TEXT("r"), TEXT("g"), TEXT("b"), TEXT("a")}); if (!Need(N, 3)) return false; *static_cast<FLinearColor*>(V) = FLinearColor(N[0], N[1], N[2], N.Num() > 3 ? N[3] : 1.0); return true; }
			case ESpecial::Color: { const TArray<double> N = ReadNumbers(J, {TEXT("r"), TEXT("g"), TEXT("b"), TEXT("a")}); if (!Need(N, 3)) return false; *static_cast<FColor*>(V) = FColor(N[0], N[1], N[2], N.Num() > 3 ? N[3] : 255); return true; }
			case ESpecial::Quat: { const TArray<double> N = ReadNumbers(J, {TEXT("x"), TEXT("y"), TEXT("z"), TEXT("w")}); if (!Need(N, 4)) return false; *static_cast<FQuat*>(V) = FQuat(N[0], N[1], N[2], N[3]); return true; }
			case ESpecial::Transform:
			{
				const TSharedPtr<FJsonObject>* O = nullptr;
				if (!J->TryGetObject(O))
				{
					OutError = TEXT("expected {location, rotation, scale}");
					return false;
				}
				FTransform T = FTransform::Identity;
				auto Read = [&](const TCHAR* Key, const TCHAR* Alt, const TArray<FString>& Keys, TFunctionRef<void(const TArray<double>&)> Apply)
				{
					TSharedPtr<FJsonValue> F = (*O)->TryGetField(Key);
					if (!F.IsValid()) { F = (*O)->TryGetField(Alt); }
					if (F.IsValid()) { const TArray<double> N = ReadNumbers(F, Keys); if (N.Num() >= 3) { Apply(N); } }
				};
				Read(TEXT("location"), TEXT("translation"), {TEXT("x"), TEXT("y"), TEXT("z")}, [&](const TArray<double>& N) { T.SetLocation(FVector(N[0], N[1], N[2])); });
				Read(TEXT("rotation"), TEXT("rotator"), {TEXT("pitch"), TEXT("yaw"), TEXT("roll")}, [&](const TArray<double>& N) { T.SetRotation(FRotator(N[0], N[1], N[2]).Quaternion()); });
				Read(TEXT("scale"), TEXT("scale3d"), {TEXT("x"), TEXT("y"), TEXT("z")}, [&](const TArray<double>& N) { T.SetScale3D(FVector(N[0], N[1], N[2])); });
				*static_cast<FTransform*>(V) = T;
				return true;
			}
			case ESpecial::Guid:
			{
				FGuid G;
				if (!FGuid::Parse(J->AsString(), G)) { OutError = TEXT("expected a GUID"); return false; }
				*static_cast<FGuid*>(V) = G;
				return true;
			}
			case ESpecial::SoftObject: *static_cast<FSoftObjectPath*>(V) = FSoftObjectPath(RefString(J)); return true;
			case ESpecial::SoftClass: *static_cast<FSoftClassPath*>(V) = FSoftClassPath(RefString(J)); return true;
			case ESpecial::Json: static_cast<FRuntimeToolJson*>(V)->Value = J; return true;
			default:
			{
				const TSharedPtr<FJsonObject>* O = nullptr;
				if (!J->TryGetObject(O))
				{
					OutError = FString::Printf(TEXT("expected a %s object"), *StP->Struct->GetName());
					return false;
				}
				return JsonToStruct(*O, StP->Struct, V, Ctx, OutError);
			}
			}
		}
		if (const FArrayProperty* AP = CastField<FArrayProperty>(P))
		{
			const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
			if (!J->TryGetArray(A))
			{
				OutError = TEXT("expected an array");
				return false;
			}
			FScriptArrayHelper H(AP, V);
			H.Resize(A->Num());
			for (int32 i = 0; i < A->Num(); ++i)
			{
				FString Err;
				if (!JsonToProperty((*A)[i], AP->Inner, H.GetRawPtr(i), Ctx, Err))
				{
					OutError = FString::Printf(TEXT("[%d]: %s"), i, *Err);
					return false;
				}
			}
			return true;
		}
		if (const FSetProperty* SetP = CastField<FSetProperty>(P))
		{
			const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
			if (!J->TryGetArray(A))
			{
				OutError = TEXT("expected an array");
				return false;
			}
			FScriptSetHelper H(SetP, V);
			H.EmptyElements(A->Num());
			for (int32 i = 0; i < A->Num(); ++i)
			{
				const int32 Index = H.AddDefaultValue_Invalid_NeedsRehash();
				FString Err;
				if (!JsonToProperty((*A)[i], SetP->ElementProp, H.GetElementPtr(Index), Ctx, Err))
				{
					OutError = FString::Printf(TEXT("[%d]: %s"), i, *Err);
					return false;
				}
			}
			H.Rehash();
			return true;
		}
		if (const FMapProperty* MP = CastField<FMapProperty>(P))
		{
			const TSharedPtr<FJsonObject>* O = nullptr;
			if (!J->TryGetObject(O))
			{
				OutError = TEXT("expected an object");
				return false;
			}
			FScriptMapHelper H(MP, V);
			H.EmptyValues((*O)->Values.Num());
			for (const TPair<FString, TSharedPtr<FJsonValue>>& KV : (*O)->Values)
			{
				const int32 Index = H.AddDefaultValue_Invalid_NeedsRehash();
				FString Err;
				if (!JsonToProperty(MakeShared<FJsonValueString>(KV.Key), MP->KeyProp, H.GetKeyPtr(Index), Ctx, Err)
					|| !JsonToProperty(KV.Value, MP->ValueProp, H.GetValuePtr(Index), Ctx, Err))
				{
					OutError = FString::Printf(TEXT(".%s: %s"), *KV.Key, *Err);
					return false;
				}
			}
			H.Rehash();
			return true;
		}
		if (const FClassProperty* CP = CastField<FClassProperty>(P))
		{
			UClass* C = ResolveClass(RefString(J), CP->MetaClass, OutError);
			if (!C)
			{
				return false;
			}
			CP->SetObjectPropertyValue(V, C);
			return true;
		}
		if (const FSoftClassProperty* SCP = CastField<FSoftClassProperty>(P))
		{
			UClass* C = ResolveClass(RefString(J), SCP->MetaClass, OutError);
			if (!C)
			{
				return false;
			}
			SCP->SetPropertyValue(V, FSoftObjectPtr(C));
			return true;
		}
		if (const FSoftObjectProperty* SOP = CastField<FSoftObjectProperty>(P))
		{
			SOP->SetPropertyValue(V, FSoftObjectPtr(FSoftObjectPath(RefString(J))));
			return true;
		}
		if (const FObjectPropertyBase* OP = CastField<FObjectPropertyBase>(P))
		{
			const FString Ref = RefString(J);
			if (Ref.IsEmpty())
			{
				OutError = TEXT("expected a reference string");
				return false;
			}
			UObject* O = ResolveObject(Ref, OP->PropertyClass, Ctx, OutError);
			if (!O)
			{
				return false;
			}
			OP->SetObjectPropertyValue(V, O);
			return true;
		}
		FString S;
		if (J->TryGetString(S) && P->ImportText_Direct(*S, V, nullptr, PPF_None))
		{
			return true;
		}
		OutError = FString::Printf(TEXT("unsupported type %s"), *P->GetCPPType());
		return false;
	}
}
