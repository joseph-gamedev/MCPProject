#include "ReflectionToolset.h"
#include "Helpers/FunctionInvoker.h"
#include "Helpers/ReflectionPolicy.h"
#include "Helpers/ToolsCommon.h"
#include "RuntimePropertyPath.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"

using namespace RuntimeTools;

/** Resolves "target" and applies the access policy. */
static UObject* ResolveAllowedObject(FToolInvocation& Inv)
{
	FResolvedTarget Target;
	if (!Inv.ResolveTarget(TEXT("target"), Target))
	{
		return nullptr;
	}
	FString Reason;
	if (!RuntimeReflectionPolicy::CanAccessObject(Target.Object, Reason))
	{
		Inv.Fail(ERuntimeToolError::NotAllowed, Reason);
		return nullptr;
	}
	return Target.Object;
}

static FString TruncatedJson(const TSharedPtr<FJsonValue>& Value, int32 MaxChars, bool& bOutTruncated)
{
	FString S = RuntimeJson::WriteValue(Value);
	bOutTruncated = S.Len() > MaxChars;
	if (bOutTruncated)
	{
		S = S.Left(MaxChars) + TEXT("...");
	}
	return S;
}

static void ListProperties(FToolInvocation& Inv)
{
	UObject* Object = ResolveAllowedObject(Inv);
	if (!Object)
	{
		return;
	}
	const FString Filter = Inv.GetString(TEXT("filter"));
	const bool bEditableOnly = Inv.GetBool(TEXT("editable_only"), true);
	const bool bValues = Inv.GetBool(TEXT("include_values"), false);
	const int32 Limit = Inv.GetLimit();

	TArray<TSharedPtr<FJsonValue>> Items;
	int32 Total = 0;
	for (TFieldIterator<FProperty> It(Object->GetClass(), EFieldIteratorFlags::IncludeSuper); It; ++It)
	{
		FProperty* P = *It;
		if (bEditableOnly && !RuntimeReflectionPolicy::IsVisibleProperty(P))
		{
			continue;
		}
		if (!Filter.IsEmpty() && !P->GetName().MatchesWildcard(Filter))
		{
			continue;
		}
		++Total;
		if (Items.Num() >= Limit)
		{
			continue;
		}
		TSharedRef<FJsonObject> J = Obj();
		J->SetStringField(TEXT("name"), P->GetName());
		J->SetStringField(TEXT("type"), RuntimePropertyPath::DescribeType(P));
		FString Ignored;
		if (!RuntimeReflectionPolicy::CanWriteProperty(P, Ignored))
		{
			J->SetBoolField(TEXT("read_only"), true);
		}
		if (bValues)
		{
			bool bTruncated = false;
			const TSharedPtr<FJsonValue> V = RuntimePropertyPath::ToJson(P, P->ContainerPtrToValuePtr<void>(Object));
			const FString Text = TruncatedJson(V, 200, bTruncated);
			if (bTruncated)
			{
				J->SetStringField(TEXT("value_preview"), Text);
			}
			else
			{
				J->SetField(TEXT("value"), V);
			}
		}
		Items.Add(MakeShared<FJsonValueObject>(J));
	}
	TSharedRef<FJsonObject> Out = Obj();
	Out->SetStringField(TEXT("class"), Object->GetClass()->GetName());
	Out->SetNumberField(TEXT("total"), Total);
	Out->SetArrayField(TEXT("properties"), Items);
	Inv.Succeed(Out);
}

static void GetProperty(FToolInvocation& Inv)
{
	UObject* Object = ResolveAllowedObject(Inv);
	if (!Object)
	{
		return;
	}
	const FString Path = Inv.GetString(TEXT("path"));
	RuntimePropertyPath::FResolved R;
	FString Error;
	if (!RuntimePropertyPath::Resolve(Object, Path, R, Error))
	{
		Inv.Fail(ERuntimeToolError::NotFound, Error, TEXT("Use reflection.list_properties"));
		return;
	}
	FString Reason;
	if (!RuntimeReflectionPolicy::CanAccessObject(R.OwnerObject, Reason))
	{
		Inv.Fail(ERuntimeToolError::NotAllowed, Reason);
		return;
	}
	TSharedRef<FJsonObject> Out = Obj();
	Out->SetStringField(TEXT("path"), Path);
	Out->SetStringField(TEXT("type"), RuntimePropertyPath::DescribeType(R.Property));
	Out->SetField(TEXT("value"), RuntimePropertyPath::ToJson(R.Property, R.ValuePtr));
	Inv.Succeed(Out);
}

static void SetProperty(FToolInvocation& Inv)
{
	UObject* Object = ResolveAllowedObject(Inv);
	if (!Object)
	{
		return;
	}
	const FString Path = Inv.GetString(TEXT("path"));
	RuntimePropertyPath::FResolved R;
	FString Error;
	if (!RuntimePropertyPath::Resolve(Object, Path, R, Error))
	{
		Inv.Fail(ERuntimeToolError::NotFound, Error, TEXT("Use reflection.list_properties"));
		return;
	}
	if (!RuntimeReflectionPolicy::CanAccessObject(R.OwnerObject, Error) || !RuntimeReflectionPolicy::CanWriteProperty(R.Property, Error))
	{
		Inv.Fail(ERuntimeToolError::NotAllowed, Error, TEXT("Try a setter via reflection.call_function"));
		return;
	}
	const FString Ref = RuntimeObjectResolver::MakeReference(Object, Inv.GetGraph());
	TSharedPtr<FJsonObject> Delta;
	if (Inv.RunCommand(MakeUnique<FPropertyCommand>(Object, Path, Inv.GetValue(TEXT("value")), Ref), &Delta))
	{
		Inv.Succeed(Delta);
	}
}

static void CallFunction(FToolInvocation& Inv)
{
	UObject* Object = ResolveAllowedObject(Inv);
	if (!Object)
	{
		return;
	}
	const FString Name = Inv.GetString(TEXT("function"));
	UFunction* Function = RuntimeFunctionInvoker::FindFunction(Object, Name);
	if (!Function)
	{
		Inv.Fail(ERuntimeToolError::NotFound, FString::Printf(TEXT("%s has no function '%s'"), *Object->GetClass()->GetName(), *Name),
			TEXT("Use reflection.list_functions"));
		return;
	}
	FString Reason;
	if (!RuntimeReflectionPolicy::CanCallFunction(Function, Reason))
	{
		Inv.Fail(ERuntimeToolError::NotAllowed, Reason);
		return;
	}
	TSharedPtr<FJsonObject> Result;
	FString Error;
	if (!RuntimeFunctionInvoker::Invoke(Object, Function, Inv.GetObject(TEXT("args")), Inv.GetWorld(), Result, Error))
	{
		Inv.Fail(ERuntimeToolError::InvalidArguments, Error);
		return;
	}
	const bool bPure = Function->HasAnyFunctionFlags(FUNC_BlueprintPure | FUNC_Const);
	if (!bPure)
	{
		Inv.RecordIrreversible(FString::Printf(TEXT("call %s.%s"), *Object->GetName(), *Function->GetName()));
		Result->SetBoolField(TEXT("undoable"), false);
	}
	Inv.Succeed(Result);
}

static void ListFunctions(FToolInvocation& Inv)
{
	UObject* Object = ResolveAllowedObject(Inv);
	if (!Object)
	{
		return;
	}
	const FString Filter = Inv.GetString(TEXT("filter"));
	const int32 Limit = Inv.GetLimit();
	TArray<TSharedPtr<FJsonValue>> Items;
	int32 Total = 0;
	for (TFieldIterator<UFunction> It(Object->GetClass(), EFieldIteratorFlags::IncludeSuper); It; ++It)
	{
		UFunction* F = *It;
		FString Ignored;
		if (!RuntimeReflectionPolicy::CanCallFunction(F, Ignored))
		{
			continue;
		}
		if (!Filter.IsEmpty() && !F->GetName().MatchesWildcard(Filter))
		{
			continue;
		}
		++Total;
		if (Items.Num() < Limit)
		{
			Items.Add(MakeShared<FJsonValueString>(RuntimeFunctionInvoker::DescribeSignature(F)));
		}
	}
	TSharedRef<FJsonObject> Out = Obj();
	Out->SetStringField(TEXT("class"), Object->GetClass()->GetName());
	Out->SetNumberField(TEXT("total"), Total);
	Out->SetArrayField(TEXT("functions"), Items);
	Inv.Succeed(Out);
}

static void FindClass(FToolInvocation& Inv)
{
	const FString Pattern = Inv.GetString(TEXT("pattern"));
	const FString BaseName = Inv.GetString(TEXT("base"));
	const int32 Limit = Inv.GetLimit();
	UClass* Base = nullptr;
	if (!BaseName.IsEmpty())
	{
		for (TObjectIterator<UClass> It; It; ++It)
		{
			if (It->GetName().Equals(BaseName, ESearchCase::IgnoreCase) || It->GetPathName() == BaseName)
			{
				Base = *It;
				break;
			}
		}
		if (!Base)
		{
			Inv.Fail(ERuntimeToolError::NotFound, FString::Printf(TEXT("base class '%s' not found"), *BaseName));
			return;
		}
	}
	TArray<TSharedPtr<FJsonValue>> Items;
	int32 Total = 0;
	for (TObjectIterator<UClass> It; It; ++It)
	{
		UClass* C = *It;
		const FString Name = C->GetName();
		if (Name.StartsWith(TEXT("SKEL_")) || Name.StartsWith(TEXT("REINST_")) || Name.StartsWith(TEXT("TRASHCLASS_")) ||
			C->HasAnyClassFlags(CLASS_Deprecated | CLASS_NewerVersionExists))
		{
			continue;
		}
		if (!Pattern.IsEmpty() && !Name.MatchesWildcard(Pattern))
		{
			continue;
		}
		if (Base && !C->IsChildOf(Base))
		{
			continue;
		}
		++Total;
		if (Items.Num() >= Limit)
		{
			continue;
		}
		TSharedRef<FJsonObject> J = Obj();
		J->SetStringField(TEXT("name"), Name);
		J->SetStringField(TEXT("path"), C->GetPathName());
		if (C->GetSuperClass())
		{
			J->SetStringField(TEXT("parent"), C->GetSuperClass()->GetName());
		}
		if (C->HasAnyClassFlags(CLASS_Abstract))
		{
			J->SetBoolField(TEXT("abstract"), true);
		}
		Items.Add(MakeShared<FJsonValueObject>(J));
	}
	TSharedRef<FJsonObject> Out = Obj();
	Out->SetNumberField(TEXT("total"), Total);
	Out->SetArrayField(TEXT("classes"), Items);
	Inv.Succeed(Out);
}

static void GetSubsystem(FToolInvocation& Inv)
{
	const FString Kind = Inv.GetString(TEXT("kind"));
	const FString ClassName = Inv.GetString(TEXT("class"));
	FString Message;
	UObject* Subsystem = RuntimeObjectResolver::ResolveSubsystem(Kind, ClassName, Inv.GetWorld(), Message);
	if (!Subsystem)
	{
		Inv.Fail(ERuntimeToolError::NotFound, Message.IsEmpty() ? FString::Printf(TEXT("subsystem %s/%s not available"), *Kind, *ClassName) : Message);
		return;
	}
	TSharedRef<FJsonObject> Out = Obj();
	Out->SetStringField(TEXT("target"), FString::Printf(TEXT("subsystem:%s/%s"), *Kind, *Subsystem->GetClass()->GetName()));
	Out->SetStringField(TEXT("class"), Subsystem->GetClass()->GetPathName());
	int32 Callable = 0;
	for (TFieldIterator<UFunction> It(Subsystem->GetClass(), EFieldIteratorFlags::IncludeSuper); It; ++It)
	{
		FString Ignored;
		Callable += RuntimeReflectionPolicy::CanCallFunction(*It, Ignored) ? 1 : 0;
	}
	Out->SetNumberField(TEXT("callable_functions"), Callable);
	Inv.Succeed(Out);
}

void FReflectionToolset::RegisterTools(FRuntimeToolsetBuilder& B)
{
	B.Tool(TEXT("list_properties"), TEXT("Properties of an object (name, type, read_only, optional values)."))
		.Args(FToolSchema()
			.Add(TEXT("target"), FToolParam::Target().Required())
			.Add(TEXT("filter"), FToolParam::String(TEXT("Name wildcard, e.g. *Color*"), 128))
			.Add(TEXT("editable_only"), FToolParam::Boolean(TEXT("Only editable/blueprint-visible")).Default(true))
			.Add(TEXT("include_values"), FToolParam::Boolean(TEXT("Include current values")).Default(false))
			.Add(TEXT("limit"), FToolParam::Limit()))
		.Handle(&ListProperties);

	B.Tool(TEXT("get_property"), TEXT("Read a property by path, e.g. LightComponent.Intensity or Tags[0]."))
		.Args(FToolSchema()
			.Add(TEXT("target"), FToolParam::Target().Required())
			.Add(TEXT("path"), FToolParam::String(TEXT("Property path"), 512).Required()))
		.Handle(&GetProperty);

	B.Tool(TEXT("set_property"), TEXT("Write a property by path. Vectors as [x,y,z], colors as [r,g,b,a]. Undoable."))
		.Args(FToolSchema()
			.Add(TEXT("target"), FToolParam::Target().Required())
			.Add(TEXT("path"), FToolParam::String(TEXT("Property path"), 512).Required())
			.Add(TEXT("value"), FToolParam::Any(TEXT("New value")).Required()))
		.Undoable()
		.Handle(&SetProperty);

	B.Tool(TEXT("call_function"), TEXT("Call a BlueprintCallable function with named args. Not undoable unless pure."))
		.Args(FToolSchema()
			.Add(TEXT("target"), FToolParam::Target(TEXT("Object, or class:/Script/Module.Class for static functions")).Required())
			.Add(TEXT("function"), FToolParam::String(TEXT("Function name"), 128).Required())
			.Add(TEXT("args"), FToolParam::Object(TEXT("Arguments by parameter name"))))
		.Irreversible()
		.Handle(&CallFunction);

	B.Tool(TEXT("list_functions"), TEXT("Callable functions of an object with signatures."))
		.Args(FToolSchema()
			.Add(TEXT("target"), FToolParam::Target().Required())
			.Add(TEXT("filter"), FToolParam::String(TEXT("Name wildcard, e.g. Set*"), 128))
			.Add(TEXT("limit"), FToolParam::Limit()))
		.Handle(&ListFunctions);

	B.Tool(TEXT("find_class"), TEXT("Find loaded classes by name wildcard, optionally derived from a base."))
		.Args(FToolSchema()
			.Add(TEXT("pattern"), FToolParam::String(TEXT("Name wildcard, e.g. *Light*"), 128))
			.Add(TEXT("base"), FToolParam::String(TEXT("Base class name, e.g. Actor"), 256))
			.Add(TEXT("limit"), FToolParam::Limit()))
		.Handle(&FindClass);

	B.Tool(TEXT("get_subsystem"), TEXT("Get a subsystem as a target string for other reflection tools."))
		.Args(FToolSchema()
			.Add(TEXT("kind"), FToolParam::Enum(TEXT("Subsystem kind"), {TEXT("engine"), TEXT("game_instance"), TEXT("world"), TEXT("local_player")}).Required())
			.Add(TEXT("class"), FToolParam::String(TEXT("Subsystem class name"), 256).Required()))
		.Handle(&GetSubsystem);
}
