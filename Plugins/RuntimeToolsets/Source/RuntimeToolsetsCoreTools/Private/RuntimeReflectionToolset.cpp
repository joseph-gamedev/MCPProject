#include "RuntimeReflectionToolset.h"
#include "Helpers/FunctionInvoker.h"
#include "Helpers/ReflectionPolicy.h"
#include "Helpers/ToolsCommon.h"
#include "RuntimePropertyPath.h"
#include "UObject/EnumProperty.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"

using namespace RuntimeTools;

namespace
{
	bool CheckAccess(const UObject* Object)
	{
		FString Reason;
		if (!RuntimeReflectionPolicy::CanAccessObject(Object, Reason))
		{
			RuntimeToolCall::Fail(ERuntimeToolError::NotAllowed, Reason, TEXT("Project settings (AllowedObjectRoots, DeniedClasses) restrict reflection"));
			return false;
		}
		return true;
	}

	/** Property names of the object's class close to Path, for NotFound hints. */
	FString PathHint(const UObject* Object, const FString& Path)
	{
		FString First = Path;
		Path.Split(TEXT("."), &First, nullptr);
		First.Split(TEXT("["), &First, nullptr);
		TArray<FString> Close;
		for (TFieldIterator<FProperty> It(Object->GetClass(), EFieldIteratorFlags::IncludeSuper); It && Close.Num() < 8; ++It)
		{
			const FString N = It->GetName();
			if (RuntimeReflectionPolicy::IsVisibleProperty(*It) && (N.Contains(First) || First.Contains(N) || N.Left(3).Equals(First.Left(3), ESearchCase::IgnoreCase)))
			{
				Close.Add(N);
			}
		}
		return Close.Num() > 0 ? FString::Printf(TEXT("Similar properties: %s (see reflection.list_properties)"), *FString::Join(Close, TEXT(", ")))
			: FString(TEXT("See reflection.list_properties"));
	}

	/** Resolves a path for reading; fails the call (NotFound / NotAllowed) on error. */
	bool ResolvePath(UObject* Object, const FString& Path, RuntimePropertyPath::FResolved& Out)
	{
		FString Err;
		if (!RuntimePropertyPath::Resolve(Object, Path, Out, Err))
		{
			RuntimeToolCall::Fail(ERuntimeToolError::NotFound, FString::Printf(TEXT("%s: %s"), *Path, *Err), PathHint(Object, Path));
			return false;
		}
		return CheckAccess(Out.OwnerObject);
	}

	/** Resolves a path for writing; fails the call when it is missing or read-only. */
	bool ResolveWritable(UObject* Object, const FString& Path)
	{
		RuntimePropertyPath::FResolved R;
		if (!ResolvePath(Object, Path, R))
		{
			return false;
		}
		FString Reason;
		if (!RuntimeReflectionPolicy::CanWriteProperty(R.Property, Reason))
		{
			RuntimeToolCall::Fail(ERuntimeToolError::NotAllowed, Reason, TEXT("Look for a setter with reflection.list_functions {\"filter\":\"Set*\"}"));
			return false;
		}
		return true;
	}

	TSharedPtr<FJsonValue> ReadBack(UObject* Object, const FString& Path)
	{
		FString Err;
		const TSharedPtr<FJsonValue> V = RuntimePropertyPath::Get(Object, Path, Err);
		return V.IsValid() ? V : MakeShared<FJsonValueNull>();
	}

	const UEnum* EnumOf(const FProperty* P)
	{
		if (const FEnumProperty* EP = CastField<FEnumProperty>(P))
		{
			return EP->GetEnum();
		}
		if (const FByteProperty* BP = CastField<FByteProperty>(P))
		{
			return BP->Enum;
		}
		return nullptr;
	}

	TArray<FString> EnumValueNames(const UEnum* Enum)
	{
		TArray<FString> Out;
		for (int32 i = 0; i < Enum->NumEnums(); ++i)
		{
#if WITH_METADATA
			if (Enum->HasMetaData(TEXT("Hidden"), i))
			{
				continue;
			}
#endif
			const FString N = Enum->GetNameStringByIndex(i);
			if (!N.EndsWith(TEXT("_MAX")))
			{
				Out.Add(N);
			}
		}
		return Out;
	}

	/** Runs FPropertyCommands for Path -> value as one undo step and returns the values read back. */
	TMap<FString, FRuntimeToolJson> ApplyValues(UObject* Object, const TArray<TPair<FString, TSharedPtr<FJsonValue>>>& Values, const FString& Label)
	{
		TMap<FString, FRuntimeToolJson> Out;
		for (const TPair<FString, TSharedPtr<FJsonValue>>& V : Values)
		{
			if (!ResolveWritable(Object, V.Key))
			{
				return Out;
			}
		}
		const FString Ref = RuntimeToolCall::MakeReference(Object);
		TUniquePtr<FCompositeCommand> Cmd = MakeUnique<FCompositeCommand>(FString::Printf(TEXT("%s %s"), *Label, *Ref));
		for (const TPair<FString, TSharedPtr<FJsonValue>>& V : Values)
		{
			Cmd->Add(MakeUnique<FPropertyCommand>(Object, V.Key, V.Value, Ref));
		}
		if (!RuntimeToolCall::RunCommand(MoveTemp(Cmd)))
		{
			return {};
		}
		for (const TPair<FString, TSharedPtr<FJsonValue>>& V : Values)
		{
			Out.Add(V.Key, FRuntimeToolJson::Make(ReadBack(Object, V.Key)));
		}
		return Out;
	}
}

FRuntimePropertyList URuntimeReflectionToolset::ListProperties(UObject* Target, const FString& Filter, bool bEditableOnly, bool bIncludeValues, int32 Limit)
{
	FRuntimePropertyList Out;
	if (!CheckAccess(Target))
	{
		return Out;
	}
	Out.Class = Target->GetClass()->GetName();
	const int32 MaxItems = URuntimeToolsetsSettings::Get().ClampLimit(Limit);
	for (TFieldIterator<FProperty> It(Target->GetClass(), EFieldIteratorFlags::IncludeSuper); It; ++It)
	{
		FProperty* P = *It;
		if ((bEditableOnly && !RuntimeReflectionPolicy::IsVisibleProperty(P)) || (!Filter.IsEmpty() && !P->GetName().MatchesWildcard(Filter)))
		{
			continue;
		}
		++Out.Total;
		if (Out.Properties.Num() >= MaxItems)
		{
			continue;
		}
		FRuntimePropertyInfo& I = Out.Properties.AddDefaulted_GetRef();
		I.Name = P->GetName();
		I.Type = RuntimePropertyPath::DescribeType(P);
		FString Ignored;
		I.bReadOnly = !RuntimeReflectionPolicy::CanWriteProperty(P, Ignored);
		if (const UEnum* E = EnumOf(P))
		{
			I.EnumValues = EnumValueNames(E);
		}
		if (bIncludeValues)
		{
			const TSharedPtr<FJsonValue> V = RuntimePropertyPath::ToJson(P, P->ContainerPtrToValuePtr<void>(Target));
			const FString Text = RuntimeJson::WriteValue(V);
			if (Text.Len() > 200)
			{
				I.ValuePreview = Text.Left(200) + TEXT("...");
			}
			else
			{
				I.Value = FRuntimeToolJson::Make(V);
			}
		}
	}
	return Out;
}

FRuntimePropertyValue URuntimeReflectionToolset::GetProperty(UObject* Target, const FString& Path)
{
	FRuntimePropertyValue Out;
	RuntimePropertyPath::FResolved R;
	if (!CheckAccess(Target) || !ResolvePath(Target, Path, R))
	{
		return Out;
	}
	Out.Path = Path;
	Out.Type = RuntimePropertyPath::DescribeType(R.Property);
	Out.Value = FRuntimeToolJson::Make(RuntimePropertyPath::ToJson(R.Property, R.ValuePtr));
	return Out;
}

TMap<FString, FRuntimeToolJson> URuntimeReflectionToolset::GetProperties(UObject* Target, const TArray<FString>& Properties)
{
	TMap<FString, FRuntimeToolJson> Out;
	if (!CheckAccess(Target))
	{
		return Out;
	}
	if (Properties.Num() == 0)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, TEXT("properties is empty"), TEXT("List names with reflection.list_properties"));
		return Out;
	}
	for (const FString& Path : Properties)
	{
		RuntimePropertyPath::FResolved R;
		if (!ResolvePath(Target, Path, R))
		{
			return {};
		}
		Out.Add(Path, FRuntimeToolJson::Make(RuntimePropertyPath::ToJson(R.Property, R.ValuePtr)));
	}
	return Out;
}

FRuntimePropertyChange URuntimeReflectionToolset::SetProperty(UObject* Target, const FString& Path, const FRuntimeToolJson& Value)
{
	FRuntimePropertyChange Out;
	if (!CheckAccess(Target))
	{
		return Out;
	}
	if (!Value.Value.IsValid())
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, TEXT("value is required"));
		return Out;
	}
	const TMap<FString, FRuntimeToolJson> Applied = ApplyValues(Target, {TPair<FString, TSharedPtr<FJsonValue>>(Path, Value.Value)}, TEXT("set"));
	if (RuntimeToolCall::HasFailed())
	{
		return Out;
	}
	Out.Target = RuntimeToolCall::MakeReference(Target);
	Out.Property = Path;
	Out.Value = Applied.FindRef(Path);
	return Out;
}

TMap<FString, FRuntimeToolJson> URuntimeReflectionToolset::SetProperties(UObject* Target, const FRuntimeToolJson& Values)
{
	if (!CheckAccess(Target))
	{
		return {};
	}
	const TSharedPtr<FJsonObject> Obj = Values.AsObject();
	if (!Obj.IsValid() || Obj->Values.Num() == 0)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, TEXT("values must be a non-empty object {path: value}"), TEXT("e.g. {\"Intensity\": 5000}"));
		return {};
	}
	TArray<TPair<FString, TSharedPtr<FJsonValue>>> List;
	for (const TPair<FString, TSharedPtr<FJsonValue>>& KV : Obj->Values)
	{
		List.Emplace(KV.Key, KV.Value);
	}
	return ApplyValues(Target, List, TEXT("set properties"));
}

TMap<FString, FRuntimeToolJson> URuntimeReflectionToolset::ResetProperties(UObject* Target, const TArray<FString>& Properties)
{
	if (!CheckAccess(Target))
	{
		return {};
	}
	if (Properties.Num() == 0)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, TEXT("properties is empty"));
		return {};
	}
	UObject* Archetype = Target->GetArchetype();
	if (!Archetype)
	{
		Archetype = Target->GetClass()->GetDefaultObject();
	}
	TArray<TPair<FString, TSharedPtr<FJsonValue>>> List;
	for (const FString& Path : Properties)
	{
		FString Err;
		const TSharedPtr<FJsonValue> Default = RuntimePropertyPath::Get(Archetype, Path, Err);
		if (!Default.IsValid())
		{
			RuntimeToolCall::Fail(ERuntimeToolError::NotFound, FString::Printf(TEXT("%s: %s"), *Path, *Err), PathHint(Target, Path));
			return {};
		}
		List.Emplace(Path, Default);
	}
	return ApplyValues(Target, List, TEXT("reset properties"));
}

FRuntimeEnumInfo URuntimeReflectionToolset::DescribeEnum(const FString& Enum)
{
	FRuntimeEnumInfo Out;
	const UEnum* Found = nullptr;
	if (Enum.StartsWith(TEXT("/")))
	{
		Found = FindObject<UEnum>(nullptr, *Enum);
	}
	if (!Found)
	{
		Found = FindFirstObject<UEnum>(*Enum, EFindFirstObjectOptions::NativeFirst);
	}
	if (!Found)
	{
		TArray<FString> Similar;
		const FString Want = Enum.StartsWith(TEXT("E")) ? Enum.Mid(1) : Enum;
		for (TObjectIterator<UEnum> It; It && Similar.Num() < 10; ++It)
		{
			if (It->GetName().Contains(Want))
			{
				Similar.Add(It->GetName());
			}
		}
		RuntimeToolCall::Fail(ERuntimeToolError::NotFound, FString::Printf(TEXT("no enum '%s'"), *Enum),
			Similar.Num() > 0 ? FString::Printf(TEXT("Similar: %s"), *FString::Join(Similar, TEXT(", "))) : FString(TEXT("Enum types start with E, e.g. ECollisionChannel")));
		return Out;
	}
	Out.Name = Found->GetName();
	Out.Path = Found->GetPathName();
#if WITH_METADATA
	Out.bBitflags = Found->HasMetaData(TEXT("Bitflags"));
#endif
	for (int32 i = 0; i < Found->NumEnums(); ++i)
	{
		const FString N = Found->GetNameStringByIndex(i);
#if WITH_METADATA
		if (Found->HasMetaData(TEXT("Hidden"), i))
		{
			continue;
		}
#endif
		if (N.EndsWith(TEXT("_MAX")))
		{
			continue;
		}
		FRuntimeEnumEntry& E = Out.Values.AddDefaulted_GetRef();
		E.Name = N;
		E.Value = Found->GetValueByIndex(i);
#if WITH_METADATA
		E.Description = Found->GetToolTipTextByIndex(i).ToString();
		if (E.Description == N)
		{
			E.Description.Reset();
		}
#endif
	}
	return Out;
}

FRuntimeCallResult URuntimeReflectionToolset::CallFunction(UObject* Target, const FString& Function, const FRuntimeToolJson& Args)
{
	FRuntimeCallResult Out;
	if (!CheckAccess(Target))
	{
		return Out;
	}
	UFunction* Fn = RuntimeFunctionInvoker::FindFunction(Target, Function);
	if (!Fn)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::NotFound, FString::Printf(TEXT("%s has no function '%s'"), *Target->GetClass()->GetName(), *Function),
			FString::Printf(TEXT("See reflection.list_functions {\"filter\":\"*%s*\"}"), *Function.Left(6)));
		return Out;
	}
	FString Reason;
	if (!RuntimeReflectionPolicy::CanCallFunction(Fn, Reason))
	{
		RuntimeToolCall::Fail(ERuntimeToolError::NotAllowed, Reason);
		return Out;
	}
	if (Args.IsSet() && !Args.AsObject().IsValid())
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, TEXT("args must be an object {ParamName: value}"), RuntimeFunctionInvoker::DescribeSignature(Fn));
		return Out;
	}
	TSharedPtr<FJsonObject> Result;
	FString Err;
	if (!RuntimeFunctionInvoker::Invoke(Target, Fn, Args.AsObject(), RuntimeToolCall::GetWorld(), Result, Err))
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, Err, RuntimeFunctionInvoker::DescribeSignature(Fn));
		return Out;
	}
	TSharedRef<FJsonObject> Outputs = MakeShared<FJsonObject>();
	for (const TPair<FString, TSharedPtr<FJsonValue>>& KV : Result->Values)
	{
		if (KV.Key == TEXT("return"))
		{
			Out.Return = FRuntimeToolJson::Make(KV.Value);
		}
		else if (KV.Key == TEXT("defaulted"))
		{
			for (const TSharedPtr<FJsonValue>& D : KV.Value->AsArray())
			{
				Out.Defaulted.Add(D->AsString());
			}
		}
		else
		{
			Outputs->SetField(KV.Key, KV.Value);
		}
	}
	Out.Outputs = FRuntimeToolJson::Make(Outputs);
	Out.bUndoable = Fn->HasAnyFunctionFlags(FUNC_BlueprintPure | FUNC_Const);
	if (!Out.bUndoable)
	{
		RuntimeToolCall::Current()->RecordIrreversible(FString::Printf(TEXT("call %s.%s"), *Target->GetName(), *Fn->GetName()));
	}
	return Out;
}

FRuntimeFunctionList URuntimeReflectionToolset::ListFunctions(UObject* Target, const FString& Filter, int32 Limit)
{
	FRuntimeFunctionList Out;
	if (!CheckAccess(Target))
	{
		return Out;
	}
	Out.Class = Target->GetClass()->GetName();
	const int32 MaxItems = URuntimeToolsetsSettings::Get().ClampLimit(Limit);
	for (TFieldIterator<UFunction> It(Target->GetClass(), EFieldIteratorFlags::IncludeSuper); It; ++It)
	{
		FString Ignored;
		if (!RuntimeReflectionPolicy::CanCallFunction(*It, Ignored) || (!Filter.IsEmpty() && !It->GetName().MatchesWildcard(Filter)))
		{
			continue;
		}
		++Out.Total;
		if (Out.Functions.Num() < MaxItems)
		{
			Out.Functions.Add(RuntimeFunctionInvoker::DescribeSignature(*It));
		}
	}
	return Out;
}

FRuntimeClassList URuntimeReflectionToolset::FindClass(const FString& Pattern, const FString& Base, int32 Limit)
{
	FRuntimeClassList Out;
	UClass* BaseClass = nullptr;
	if (!Base.IsEmpty())
	{
		for (TObjectIterator<UClass> It; It; ++It)
		{
			if (It->GetName().Equals(Base, ESearchCase::IgnoreCase) || It->GetPathName() == Base)
			{
				BaseClass = *It;
				break;
			}
		}
		if (!BaseClass)
		{
			RuntimeToolCall::Fail(ERuntimeToolError::NotFound, FString::Printf(TEXT("base class '%s' not found"), *Base),
				TEXT("Use the class name without its A/U prefix, e.g. Actor, ActorComponent, Pawn"));
			return Out;
		}
	}
	const int32 MaxItems = URuntimeToolsetsSettings::Get().ClampLimit(Limit);
	for (TObjectIterator<UClass> It; It; ++It)
	{
		UClass* C = *It;
		const FString Name = C->GetName();
		if (Name.StartsWith(TEXT("SKEL_")) || Name.StartsWith(TEXT("REINST_")) || Name.StartsWith(TEXT("TRASHCLASS_")) ||
			C->HasAnyClassFlags(CLASS_Deprecated | CLASS_NewerVersionExists))
		{
			continue;
		}
		if ((!Pattern.IsEmpty() && !Name.MatchesWildcard(Pattern)) || (BaseClass && !C->IsChildOf(BaseClass)))
		{
			continue;
		}
		++Out.Total;
		if (Out.Classes.Num() < MaxItems)
		{
			FRuntimeClassInfo& I = Out.Classes.AddDefaulted_GetRef();
			I.Name = Name;
			I.Path = C->GetPathName();
			I.Parent = C->GetSuperClass() ? C->GetSuperClass()->GetName() : FString();
			I.bAbstract = C->HasAnyClassFlags(CLASS_Abstract);
		}
	}
	return Out;
}

FRuntimeSubsystemInfo URuntimeReflectionToolset::GetSubsystem(const FString& Kind, const FString& Class)
{
	FRuntimeSubsystemInfo Out;
	static const TArray<FString> Kinds = {TEXT("engine"), TEXT("game_instance"), TEXT("world"), TEXT("local_player")};
	if (!Kinds.Contains(Kind))
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("kind '%s' is not one of %s"), *Kind, *FString::Join(Kinds, TEXT(", "))));
		return Out;
	}
	FString Message;
	UObject* Subsystem = RuntimeObjectResolver::ResolveSubsystem(Kind, Class, RuntimeToolCall::GetWorld(), Message);
	if (!Subsystem)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::NotFound, Message.IsEmpty() ? FString::Printf(TEXT("subsystem %s/%s is not running"), *Kind, *Class) : Message,
			FString::Printf(TEXT("Find subsystem classes with reflection.find_class {\"base\":\"%s\"}"),
				Kind == TEXT("engine") ? TEXT("EngineSubsystem") : Kind == TEXT("world") ? TEXT("WorldSubsystem") : Kind == TEXT("game_instance") ? TEXT("GameInstanceSubsystem") : TEXT("LocalPlayerSubsystem")));
		return Out;
	}
	Out.Target = FString::Printf(TEXT("subsystem:%s/%s"), *Kind, *Subsystem->GetClass()->GetName());
	Out.Class = Subsystem->GetClass()->GetPathName();
	for (TFieldIterator<UFunction> It(Subsystem->GetClass(), EFieldIteratorFlags::IncludeSuper); It; ++It)
	{
		FString Ignored;
		Out.CallableFunctions += RuntimeReflectionPolicy::CanCallFunction(*It, Ignored) ? 1 : 0;
	}
	return Out;
}
