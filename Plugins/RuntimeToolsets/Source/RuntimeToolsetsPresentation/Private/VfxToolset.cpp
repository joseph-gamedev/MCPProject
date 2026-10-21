#include "VfxToolset.h"
#include "RuntimeToolHelpers.h"
#include "Engine/World.h"
#include "NiagaraActor.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "NiagaraTypes.h"
#include "UObject/UObjectIterator.h"

using namespace RuntimeTools;

namespace
{
	UNiagaraComponent* ResolveNiagara(FToolInvocation& Inv)
	{
		FResolvedTarget T;
		if (!Inv.ResolveTarget(TEXT("target"), T))
		{
			return nullptr;
		}
		UNiagaraComponent* Comp = Cast<UNiagaraComponent>(T.Component);
		if (!Comp && T.Actor)
		{
			Comp = T.Actor->FindComponentByClass<UNiagaraComponent>();
		}
		if (!Comp)
		{
			Inv.Fail(ERuntimeToolError::InvalidArguments, TEXT("target has no Niagara component"));
		}
		return Comp;
	}

	/** User parameters of a component as "Name" -> type definition (without the "User." prefix). */
	TMap<FString, FNiagaraTypeDefinition> UserParams(UNiagaraComponent* Comp)
	{
		TMap<FString, FNiagaraTypeDefinition> Out;
		TArray<FNiagaraVariable> Vars;
		Comp->GetOverrideParameters().GetUserParameters(Vars);
		for (const FNiagaraVariable& V : Vars)
		{
			FString Name = V.GetName().ToString();
			Name.RemoveFromStart(TEXT("User."));
			Out.Add(Name, V.GetType());
		}
		return Out;
	}

	FNiagaraVariable UserVar(const FNiagaraTypeDefinition& Type, const FString& Name)
	{
		return FNiagaraVariable(Type, FName(*(TEXT("User.") + Name)));
	}

	/** Reads the current value of a user parameter as JSON (float, int, bool, vec3, color). */
	TSharedPtr<FJsonValue> ReadParam(UNiagaraComponent* Comp, const FNiagaraTypeDefinition& Type, const FString& Name)
	{
		const FNiagaraParameterStore& Store = Comp->GetOverrideParameters();
		const FNiagaraVariable Var = UserVar(Type, Name);
		if (Type == FNiagaraTypeDefinition::GetFloatDef())
		{
			return MakeShared<FJsonValueNumber>(RuntimeJson::Round(Store.GetParameterValue<float>(Var)));
		}
		if (Type == FNiagaraTypeDefinition::GetIntDef())
		{
			return MakeShared<FJsonValueNumber>(Store.GetParameterValue<int32>(Var));
		}
		if (Type == FNiagaraTypeDefinition::GetBoolDef())
		{
			return MakeShared<FJsonValueBoolean>(Store.GetParameterValue<FNiagaraBool>(Var).GetValue());
		}
		if (Type == FNiagaraTypeDefinition::GetVec3Def() || Type == FNiagaraTypeDefinition::GetPositionDef())
		{
			const FVector3f V = Store.GetParameterValue<FVector3f>(Var);
			return RuntimeJson::Vec(FVector(V));
		}
		if (Type == FNiagaraTypeDefinition::GetColorDef())
		{
			const FLinearColor C = Store.GetParameterValue<FLinearColor>(Var);
			return MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>{MakeShared<FJsonValueNumber>(C.R), MakeShared<FJsonValueNumber>(C.G),
				MakeShared<FJsonValueNumber>(C.B), MakeShared<FJsonValueNumber>(C.A)});
		}
		return nullptr;
	}

	/** Writes a JSON value into a typed user parameter. */
	bool WriteParam(UNiagaraComponent* Comp, const FNiagaraTypeDefinition& Type, const FString& Name, const TSharedPtr<FJsonValue>& Value, FString& OutError)
	{
		const FName VarName(*Name);
		if (Type == FNiagaraTypeDefinition::GetFloatDef() && Value->Type == EJson::Number)
		{
			Comp->SetVariableFloat(VarName, static_cast<float>(Value->AsNumber()));
			return true;
		}
		if (Type == FNiagaraTypeDefinition::GetIntDef() && Value->Type == EJson::Number)
		{
			Comp->SetVariableInt(VarName, static_cast<int32>(Value->AsNumber()));
			return true;
		}
		if (Type == FNiagaraTypeDefinition::GetBoolDef() && Value->Type == EJson::Boolean)
		{
			Comp->SetVariableBool(VarName, Value->AsBool());
			return true;
		}
		if (Type == FNiagaraTypeDefinition::GetVec3Def() || Type == FNiagaraTypeDefinition::GetPositionDef())
		{
			FVector V;
			if (RuntimeJson::ToVector(Value, V))
			{
				Comp->SetVariableVec3(VarName, V);
				return true;
			}
		}
		if (Type == FNiagaraTypeDefinition::GetColorDef() && Value->Type == EJson::Array && (Value->AsArray().Num() == 3 || Value->AsArray().Num() == 4))
		{
			const TArray<TSharedPtr<FJsonValue>>& A = Value->AsArray();
			Comp->SetVariableLinearColor(VarName, FLinearColor(A[0]->AsNumber(), A[1]->AsNumber(), A[2]->AsNumber(), A.Num() == 4 ? A[3]->AsNumber() : 1.0));
			return true;
		}
		OutError = FString::Printf(TEXT("value does not fit parameter type %s"), *Type.GetName());
		return false;
	}
}

static void SpawnSystem(FToolInvocation& Inv)
{
	const FString Path = Inv.GetString(TEXT("system"));
	UNiagaraSystem* System = LoadObject<UNiagaraSystem>(nullptr, *Path);
	if (!System)
	{
		Inv.Fail(ERuntimeToolError::NotFound, FString::Printf(TEXT("Niagara system '%s' not found (is it cooked?)"), *Path),
			TEXT("Use the full object path, e.g. /Game/FX/NS_Fire.NS_Fire"));
		return;
	}
	FString ParentId;
	if (!Inv.GetString(TEXT("parent")).IsEmpty())
	{
		FResolvedTarget Parent;
		if (!Inv.ResolveTarget(TEXT("parent"), Parent))
		{
			return;
		}
		if (!Parent.Node)
		{
			Inv.Fail(ERuntimeToolError::InvalidArguments, TEXT("parent must be a scene node"));
			return;
		}
		ParentId = Parent.Node->Id;
	}
	const FTransform Transform = ReadTransform(Inv);
	const FString Name = Inv.GetString(TEXT("name"), System->GetName());
	TSharedPtr<FJsonObject> Params = Inv.GetObject(TEXT("parameters"));
	TStrongObjectPtr<UNiagaraSystem> Keep(System);
	auto Spawn = [Keep, Transform, Params](FRuntimeCommandContext& Ctx, FString& Error) -> AActor*
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ANiagaraActor* Actor = Ctx.World->SpawnActor<ANiagaraActor>(ANiagaraActor::StaticClass(), Transform, SpawnParams);
		if (!Actor)
		{
			Error = TEXT("could not spawn a Niagara actor");
			return nullptr;
		}
		UNiagaraComponent* Comp = Actor->GetNiagaraComponent();
		Comp->SetAsset(Keep.Get());
		if (Params.IsValid())
		{
			const TMap<FString, FNiagaraTypeDefinition> Known = UserParams(Comp);
			for (const FString& Key : RuntimeJson::Keys(Params))
			{
				if (const FNiagaraTypeDefinition* Type = Known.Find(Key))
				{
					FString Ignored;
					WriteParam(Comp, *Type, Key, Params->TryGetField(Key), Ignored);
				}
			}
		}
		Comp->Activate(true);
		return Actor;
	};
	TUniquePtr<FCreateNodeCommand> Cmd = MakeUnique<FCreateNodeCommand>(FString::Printf(TEXT("spawn vfx %s"), *Name), Spawn, Name, ParentId,
		ERuntimeNodeKind::Actor, FString::Printf(TEXT("vfx:%s"), *System->GetPathName()), ToNames(Inv.GetStringArray(TEXT("tags"))));
	FCreateNodeCommand* Raw = Cmd.Get();
	if (!Inv.RunCommand(MoveTemp(Cmd)))
	{
		return;
	}
	TSharedRef<FJsonObject> Out = Obj();
	Out->SetStringField(TEXT("id"), Raw->GetNodeId());
	Out->SetStringField(TEXT("system"), System->GetPathName());
	if (const FRuntimeSceneNode* Node = Inv.GetGraph()->Find(Raw->GetNodeId()))
	{
		if (AActor* A = Node->Actor.Get())
		{
			TArray<FString> Names;
			UserParams(A->FindComponentByClass<UNiagaraComponent>()).GetKeys(Names);
			Out->SetField(TEXT("user_parameters"), RuntimeJson::Strings(Names));
		}
	}
	Inv.Succeed(Out);
}

static void ListUserParameters(FToolInvocation& Inv)
{
	UNiagaraComponent* Comp = ResolveNiagara(Inv);
	if (!Comp)
	{
		return;
	}
	TSharedRef<FJsonObject> Params = Obj();
	for (const TPair<FString, FNiagaraTypeDefinition>& P : UserParams(Comp))
	{
		TSharedRef<FJsonObject> J = Obj();
		J->SetStringField(TEXT("type"), P.Value.GetName());
		if (TSharedPtr<FJsonValue> V = ReadParam(Comp, P.Value, P.Key))
		{
			J->SetField(TEXT("value"), V);
		}
		Params->SetObjectField(P.Key, J);
	}
	TSharedRef<FJsonObject> Out = Obj();
	Out->SetStringField(TEXT("system"), Comp->GetAsset() ? Comp->GetAsset()->GetPathName() : TEXT(""));
	Out->SetBoolField(TEXT("active"), Comp->IsActive());
	Out->SetObjectField(TEXT("parameters"), Params);
	Inv.Succeed(Out);
}

static void SetUserVariable(FToolInvocation& Inv)
{
	UNiagaraComponent* Comp = ResolveNiagara(Inv);
	if (!Comp)
	{
		return;
	}
	FString Name = Inv.GetString(TEXT("name"));
	Name.RemoveFromStart(TEXT("User."));
	const TMap<FString, FNiagaraTypeDefinition> Known = UserParams(Comp);
	const FNiagaraTypeDefinition* Type = Known.Find(Name);
	if (!Type)
	{
		TArray<FString> Names;
		Known.GetKeys(Names);
		Inv.Fail(ERuntimeToolError::NotFound, FString::Printf(TEXT("no user parameter '%s' (has: %s)"), *Name, Names.Num() ? *FString::Join(Names, TEXT(", ")) : TEXT("none")));
		return;
	}
	const TSharedPtr<FJsonValue> New = Inv.GetValue(TEXT("value"));
	const TSharedPtr<FJsonValue> Old = ReadParam(Comp, *Type, Name);
	if (!Old.IsValid())
	{
		Inv.Fail(ERuntimeToolError::Unsupported, FString::Printf(TEXT("parameter type %s is not supported (float, int, bool, vector, color)"), *Type->GetName()));
		return;
	}
	TWeakObjectPtr<UNiagaraComponent> Weak = Comp;
	const FNiagaraTypeDefinition TypeCopy = *Type;
	TSharedRef<FJsonObject> Delta = Obj();
	Delta->SetStringField(TEXT("parameter"), Name);
	Delta->SetField(TEXT("value"), New);
	if (Inv.RunCommand(MakeUnique<FLambdaCommand>(FString::Printf(TEXT("vfx param %s"), *Name),
		[Weak, TypeCopy, Name, New](FRuntimeCommandContext&, FString& Error) { return Weak.IsValid() && WriteParam(Weak.Get(), TypeCopy, Name, New, Error); },
		[Weak, TypeCopy, Name, Old](FRuntimeCommandContext&) { FString E; return Weak.IsValid() && WriteParam(Weak.Get(), TypeCopy, Name, Old, E); }, Delta)))
	{
		Delta->SetField(TEXT("previous"), Old);
		Inv.Succeed(Delta);
	}
}

static void SetActive(FToolInvocation& Inv, bool bActivate)
{
	UNiagaraComponent* Comp = ResolveNiagara(Inv);
	if (!Comp)
	{
		return;
	}
	const bool bWas = Comp->IsActive();
	const bool bReset = Inv.GetBool(TEXT("reset"), false);
	TWeakObjectPtr<UNiagaraComponent> Weak = Comp;
	auto Set = [Weak](bool bOn, bool bInReset)
	{
		if (!Weak.IsValid())
		{
			return false;
		}
		if (bOn)
		{
			Weak->Activate(bInReset);
		}
		else
		{
			Weak->Deactivate();
		}
		return true;
	};
	TSharedRef<FJsonObject> Delta = Obj();
	Delta->SetStringField(TEXT("target"), Ref(Inv, Comp->GetOwner()));
	Delta->SetBoolField(TEXT("active"), bActivate);
	if (Inv.RunCommand(MakeUnique<FLambdaCommand>(bActivate ? TEXT("activate vfx") : TEXT("deactivate vfx"),
		[Set, bActivate, bReset](FRuntimeCommandContext&, FString& Error) { return Set(bActivate, bReset); },
		[Set, bWas](FRuntimeCommandContext&) { return Set(bWas, false); }, Delta)))
	{
		Inv.Succeed(Delta);
	}
}

static void ListActive(FToolInvocation& Inv)
{
	const int32 Limit = Inv.GetLimit();
	TArray<TSharedPtr<FJsonValue>> Items;
	int32 Total = 0;
	for (TObjectIterator<UNiagaraComponent> It; It; ++It)
	{
		UNiagaraComponent* C = *It;
		if (!C || C->GetWorld() != Inv.GetWorld() || !C->IsActive() || C->HasAnyFlags(RF_ClassDefaultObject))
		{
			continue;
		}
		++Total;
		if (Items.Num() >= Limit)
		{
			continue;
		}
		TSharedRef<FJsonObject> J = Obj();
		J->SetStringField(TEXT("target"), C->GetOwner() ? Ref(Inv, C->GetOwner()) : C->GetPathName());
		J->SetStringField(TEXT("system"), C->GetAsset() ? C->GetAsset()->GetName() : TEXT(""));
		J->SetField(TEXT("location"), RuntimeJson::Vec(C->GetComponentLocation()));
		Items.Add(MakeShared<FJsonValueObject>(J));
	}
	TSharedRef<FJsonObject> Out = Obj();
	Out->SetNumberField(TEXT("total"), Total);
	Out->SetArrayField(TEXT("active"), Items);
	Inv.Succeed(Out);
}

AActor* FVfxToolset::SpawnFromSource(UWorld* World, const FString& Source, const FTransform& Transform, const FString& Name, FString& OutError)
{
	UNiagaraSystem* System = LoadObject<UNiagaraSystem>(nullptr, *Source.Mid(4));
	if (!System)
	{
		OutError = FString::Printf(TEXT("Niagara system '%s' not found"), *Source.Mid(4));
		return nullptr;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ANiagaraActor* Actor = World->SpawnActor<ANiagaraActor>(ANiagaraActor::StaticClass(), Transform, Params);
	if (!Actor)
	{
		OutError = TEXT("could not spawn a Niagara actor");
		return nullptr;
	}
	Actor->GetNiagaraComponent()->SetAsset(System);
	Actor->GetNiagaraComponent()->Activate(true);
	return Actor;
}

void FVfxToolset::RegisterTools(FRuntimeToolsetBuilder& B)
{
	B.Tool(TEXT("spawn_system"), TEXT("Spawn a Niagara system as a scene node, optionally with user parameters."))
		.Args(FToolSchema()
			.Add(TEXT("system"), FToolParam::String(TEXT("Niagara system path, e.g. /Game/FX/NS_Fire.NS_Fire"), 512).Required())
			.Add(TEXT("location"), FToolParam::Vec3(TEXT("[x,y,z] cm")))
			.Add(TEXT("rotation"), FToolParam::Rotator(TEXT("[pitch,yaw,roll]")))
			.Add(TEXT("scale"), FToolParam::Vec3(TEXT("[x,y,z]")))
			.Add(TEXT("parent"), FToolParam::Target(TEXT("Parent node")))
			.Add(TEXT("name"), FToolParam::String(TEXT("Node name"), 128))
			.Add(TEXT("parameters"), FToolParam::Object(TEXT("User parameters by name, e.g. {\"SpawnRate\":50}")))
			.Add(TEXT("tags"), FToolParam::StringArray(TEXT("Tags"), 16)))
		.Undoable().RequiresWorld()
		.Handle(&SpawnSystem);

	B.Tool(TEXT("list_user_parameters"), TEXT("User parameters of an effect with types and current values."))
		.Args(FToolSchema().Add(TEXT("target"), FToolParam::Target().Required()))
		.RequiresWorld()
		.Handle(&ListUserParameters);

	B.Tool(TEXT("set_user_variable"), TEXT("Set a user parameter (float, int, bool, [x,y,z], [r,g,b,a])."))
		.Args(FToolSchema()
			.Add(TEXT("target"), FToolParam::Target().Required())
			.Add(TEXT("name"), FToolParam::String(TEXT("Parameter name (User. prefix optional)"), 128).Required())
			.Add(TEXT("value"), FToolParam::Any(TEXT("New value")).Required()))
		.Undoable().RequiresWorld()
		.Handle(&SetUserVariable);

	B.Tool(TEXT("activate"), TEXT("Start (or restart with reset) an effect."))
		.Args(FToolSchema().Add(TEXT("target"), FToolParam::Target().Required()).Add(TEXT("reset"), FToolParam::Boolean(TEXT("Restart from the beginning")).Default(false)))
		.Undoable().RequiresWorld()
		.Handle([](FToolInvocation& Inv) { SetActive(Inv, true); });

	B.Tool(TEXT("deactivate"), TEXT("Stop an effect (existing particles finish)."))
		.Args(FToolSchema().Add(TEXT("target"), FToolParam::Target().Required()))
		.Undoable().RequiresWorld()
		.Handle([](FToolInvocation& Inv) { SetActive(Inv, false); });

	B.Tool(TEXT("list_active"), TEXT("Niagara effects currently active in the world."))
		.Args(FToolSchema().Add(TEXT("limit"), FToolParam::Limit()))
		.RequiresWorld()
		.Handle(&ListActive);
}
