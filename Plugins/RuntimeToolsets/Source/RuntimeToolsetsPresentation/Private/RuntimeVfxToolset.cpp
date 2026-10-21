#include "RuntimeVfxToolset.h"
#include "PresentationToolCall.h"
#include "RuntimeCommand.h"
#include "RuntimeCommonCommands.h"
#include "RuntimeJson.h"
#include "RuntimeSceneGraph.h"
#include "Engine/World.h"
#include "NiagaraActor.h"
#include "NiagaraComponent.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraRendererProperties.h"
#include "NiagaraSystem.h"
#include "NiagaraTypes.h"
#include "NiagaraUserRedirectionParameterStore.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectIterator.h"

namespace RtVfx
{
	using namespace PresentationTool;

	/** User variables of a parameter store as "Name" -> type (without the "User." prefix), sorted by name. */
	TMap<FString, FNiagaraTypeDefinition> UserParamsOf(const FNiagaraUserRedirectionParameterStore& Store)
	{
		TArray<FNiagaraVariable> Vars;
		Store.GetUserParameters(Vars);
		TMap<FString, FNiagaraTypeDefinition> Out;
		for (const FNiagaraVariable& V : Vars)
		{
			FString Name = V.GetName().ToString();
			Name.RemoveFromStart(TEXT("User."));
			Out.Add(Name, V.GetType());
		}
		Out.KeySort([](const FString& A, const FString& B) { return A < B; });
		return Out;
	}

	TMap<FString, FNiagaraTypeDefinition> UserParams(UNiagaraComponent* Comp)
	{
		return UserParamsOf(Comp->GetOverrideParameters());
	}

	TMap<FString, FNiagaraTypeDefinition> SystemUserParams(UNiagaraSystem* System)
	{
		return UserParamsOf(System->GetExposedParameters());
	}

	FString TypeName(const FNiagaraTypeDefinition& Type)
	{
		return Type.GetName();
	}

	/** The JSON a type expects, for error messages. */
	FString ExpectedJson(const FNiagaraTypeDefinition& Type)
	{
		if (Type == FNiagaraTypeDefinition::GetFloatDef() || Type == FNiagaraTypeDefinition::GetIntDef()) return TEXT("a number");
		if (Type == FNiagaraTypeDefinition::GetBoolDef()) return TEXT("true or false");
		if (Type == FNiagaraTypeDefinition::GetVec3Def() || Type == FNiagaraTypeDefinition::GetPositionDef()) return TEXT("[x, y, z]");
		if (Type == FNiagaraTypeDefinition::GetColorDef()) return TEXT("[r, g, b] or [r, g, b, a]");
		return FString::Printf(TEXT("(type %s is not supported by the tools: float, int, bool, vector, position, color)"), *Type.GetName());
	}

	/** Reads the current value of a user variable as JSON (float, int, bool, vec3, color); null if unsupported. */
	TSharedPtr<FJsonValue> ReadParam(UNiagaraComponent* Comp, const FNiagaraTypeDefinition& Type, const FString& Name)
	{
		const FNiagaraParameterStore& Store = Comp->GetOverrideParameters();
		const FNiagaraVariable Var(Type, FName(*(TEXT("User.") + Name)));
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
			return MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>{MakeShared<FJsonValueNumber>(RuntimeJson::Round(C.R)),
				MakeShared<FJsonValueNumber>(RuntimeJson::Round(C.G)), MakeShared<FJsonValueNumber>(RuntimeJson::Round(C.B)),
				MakeShared<FJsonValueNumber>(RuntimeJson::Round(C.A))});
		}
		return nullptr;
	}

	/** Writes a JSON value into a typed user variable; with a null component only checks that it fits. */
	bool WriteParam(UNiagaraComponent* Comp, const FNiagaraTypeDefinition& Type, const FString& Name, const TSharedPtr<FJsonValue>& Value, FString& OutError)
	{
		const FName VarName(*Name);
		if (Value.IsValid())
		{
			if (Type == FNiagaraTypeDefinition::GetFloatDef() && Value->Type == EJson::Number)
			{
				if (Comp) { Comp->SetVariableFloat(VarName, static_cast<float>(Value->AsNumber())); }
				return true;
			}
			if (Type == FNiagaraTypeDefinition::GetIntDef() && Value->Type == EJson::Number)
			{
				if (Comp) { Comp->SetVariableInt(VarName, static_cast<int32>(Value->AsNumber())); }
				return true;
			}
			if (Type == FNiagaraTypeDefinition::GetBoolDef() && Value->Type == EJson::Boolean)
			{
				if (Comp) { Comp->SetVariableBool(VarName, Value->AsBool()); }
				return true;
			}
			if (Type == FNiagaraTypeDefinition::GetVec3Def() || Type == FNiagaraTypeDefinition::GetPositionDef())
			{
				FVector V;
				if (Value->Type == EJson::Array && RuntimeJson::ToVector(Value, V))
				{
					if (Comp) { Comp->SetVariableVec3(VarName, V); }
					return true;
				}
			}
			if (Type == FNiagaraTypeDefinition::GetColorDef() && Value->Type == EJson::Array && (Value->AsArray().Num() == 3 || Value->AsArray().Num() == 4))
			{
				const TArray<TSharedPtr<FJsonValue>>& A = Value->AsArray();
				if (Comp)
				{
					Comp->SetVariableLinearColor(VarName, FLinearColor(A[0]->AsNumber(), A[1]->AsNumber(), A[2]->AsNumber(), A.Num() == 4 ? A[3]->AsNumber() : 1.0));
				}
				return true;
			}
		}
		OutError = FString::Printf(TEXT("user variable '%s' is %s: expected %s"), *Name, *TypeName(Type), *ExpectedJson(Type));
		return false;
	}

	/** Checks a {name: value} object against a system's user variables. Fails the call on the first problem. */
	bool CheckUserValues(UNiagaraSystem* System, const FRuntimeToolJson& Values)
	{
		if (!Values.IsSet())
		{
			return true;
		}
		const TSharedPtr<FJsonObject> Obj = Values.AsObject();
		if (!Obj.IsValid())
		{
			RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, TEXT("user_variables must be an object {name: value}"));
			return false;
		}
		const TMap<FString, FNiagaraTypeDefinition> Known = SystemUserParams(System);
		for (const TPair<FString, TSharedPtr<FJsonValue>>& P : Obj->Values)
		{
			FString Name = P.Key;
			Name.RemoveFromStart(TEXT("User."));
			const FNiagaraTypeDefinition* Type = Known.Find(Name);
			if (!Type)
			{
				TArray<FString> Names;
				Known.GetKeys(Names);
				RuntimeToolCall::Fail(ERuntimeToolError::NotFound, FString::Printf(TEXT("%s has no user variable '%s' (has: %s)"), *System->GetName(), *Name, *JoinOrNone(Names)));
				return false;
			}
			FString Err;
			if (!WriteParam(nullptr, *Type, Name, P.Value, Err))
			{
				RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, Err);
				return false;
			}
		}
		return true;
	}

	void ApplyUserValues(UNiagaraComponent* Comp, const TSharedPtr<FJsonObject>& Obj)
	{
		if (!Obj.IsValid())
		{
			return;
		}
		const TMap<FString, FNiagaraTypeDefinition> Known = UserParams(Comp);
		for (const TPair<FString, TSharedPtr<FJsonValue>>& P : Obj->Values)
		{
			FString Name = P.Key;
			Name.RemoveFromStart(TEXT("User."));
			if (const FNiagaraTypeDefinition* Type = Known.Find(Name))
			{
				FString Ignored;
				WriteParam(Comp, *Type, Name, P.Value, Ignored);
			}
		}
	}

	/** Moves a location down onto the first surface below it. */
	bool SnapToGround(UWorld* World, FVector& InOut)
	{
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(RuntimeVfxSnap), true);
		if (World->LineTraceSingleByChannel(Hit, InOut + FVector(0, 0, 50), InOut - FVector(0, 0, 100000), ECC_Visibility, Params))
		{
			InOut = Hit.ImpactPoint;
			return true;
		}
		return false;
	}

	FRuntimeVfxUserVariable MakeVariable(UNiagaraComponent* Comp, const FString& Name, const FNiagaraTypeDefinition& Type)
	{
		FRuntimeVfxUserVariable V;
		V.Name = Name;
		V.Type = TypeName(Type);
		if (const TSharedPtr<FJsonValue> J = ReadParam(Comp, Type, Name))
		{
			V.Value = FRuntimeToolJson::Make(J);
		}
		return V;
	}

	const FNiagaraTypeDefinition* FindVariable(UNiagaraComponent* Comp, FString& InOutName, TMap<FString, FNiagaraTypeDefinition>& OutKnown)
	{
		InOutName.RemoveFromStart(TEXT("User."));
		OutKnown = UserParams(Comp);
		for (const TPair<FString, FNiagaraTypeDefinition>& P : OutKnown)
		{
			if (P.Key.Equals(InOutName, ESearchCase::IgnoreCase))
			{
				InOutName = P.Key;
				return &P.Value;
			}
		}
		TArray<FString> Names;
		OutKnown.GetKeys(Names);
		RuntimeToolCall::Fail(ERuntimeToolError::NotFound, FString::Printf(TEXT("no user variable '%s' (has: %s)"), *InOutName, *JoinOrNone(Names)),
			TEXT("See vfx.get_user_variables"));
		return nullptr;
	}

	FString SystemPath(const UNiagaraComponent* Comp)
	{
		return Comp->GetAsset() ? Comp->GetAsset()->GetPathName() : FString();
	}

	FString TargetRef(const UNiagaraComponent* Comp)
	{
		return Ref(Comp->GetOwner() ? static_cast<const UObject*>(Comp->GetOwner()) : static_cast<const UObject*>(Comp));
	}

	FRuntimeVfxState MakeState(UNiagaraComponent* Comp, bool bActive)
	{
		FRuntimeVfxState S;
		S.Target = TargetRef(Comp);
		S.System = SystemPath(Comp);
		S.bActive = bActive;
		return S;
	}

	FRuntimeVfxState SetActive(UNiagaraComponent* Comp, bool bActivate, bool bReset)
	{
		const bool bWas = Comp->IsActive();
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
		TSharedRef<FJsonObject> Delta = MakeShared<FJsonObject>();
		Delta->SetStringField(TEXT("target"), TargetRef(Comp));
		Delta->SetBoolField(TEXT("active"), bActivate);
		if (!RuntimeToolCall::RunCommand(MakeUnique<FLambdaCommand>(bActivate ? TEXT("activate vfx") : TEXT("deactivate vfx"),
			[Set, bActivate, bReset](FRuntimeCommandContext&, FString&) { return Set(bActivate, bReset); },
			[Set, bWas](FRuntimeCommandContext&) { return Set(bWas, false); }, Delta)))
		{
			return {};
		}
		return MakeState(Comp, bActivate);
	}
}

FRuntimeVfxInfo URuntimeVfxToolset::SpawnSystem(UNiagaraSystem* System, const FVector& Location, const FRotator& Rotation, const FVector& Scale,
	const FRuntimeToolJson& UserVariables, const TArray<FName>& Tags, const FString& Name, AActor* Parent, bool bSnapToGround)
{
	using namespace RtVfx;
	FRuntimeVfxInfo Out;
	if (!System)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, TEXT("system is required"), TEXT("e.g. /Niagara/DefaultAssets/DefaultSystem.DefaultSystem"));
		return Out;
	}
	if (!CheckUserValues(System, UserVariables))
	{
		return Out;
	}
	FString ParentId;
	if (Parent)
	{
		const FRuntimeSceneNode* N = RuntimeToolCall::NodeOf(Parent);
		if (!N)
		{
			RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, TEXT("parent must be a scene node"), TEXT("Pass a node id from scene.find_nodes"));
			return Out;
		}
		ParentId = N->Id;
	}
	FVector Where = Passed(TEXT("location")) ? Location : FVector::ZeroVector;
	const bool bSnapped = bSnapToGround && SnapToGround(RuntimeToolCall::GetWorld(), Where);
	const FTransform Transform(Passed(TEXT("rotation")) ? Rotation : FRotator::ZeroRotator, Where, Passed(TEXT("scale")) ? Scale : FVector::OneVector);
	const FString NodeName = Name.IsEmpty() ? System->GetName() : Name;
	const TSharedPtr<FJsonObject> Values = UserVariables.AsObject();
	TStrongObjectPtr<UNiagaraSystem> Keep(System);
	auto Spawn = [Keep, Transform, Values](FRuntimeCommandContext& Ctx, FString& Err) -> AActor*
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ANiagaraActor* Actor = Ctx.World->SpawnActor<ANiagaraActor>(ANiagaraActor::StaticClass(), Transform, SpawnParams);
		if (!Actor)
		{
			Err = TEXT("could not spawn a Niagara actor");
			return nullptr;
		}
		UNiagaraComponent* Comp = Actor->GetNiagaraComponent();
		Comp->SetAsset(Keep.Get());
		ApplyUserValues(Comp, Values);
		Comp->Activate(true);
		return Actor;
	};
	TUniquePtr<FCreateNodeCommand> Cmd = MakeUnique<FCreateNodeCommand>(FString::Printf(TEXT("spawn vfx %s"), *NodeName), Spawn, NodeName, ParentId,
		ERuntimeNodeKind::Actor, FString::Printf(TEXT("vfx:%s"), *System->GetPathName()), Tags);
	FCreateNodeCommand* Raw = Cmd.Get();
	if (!RuntimeToolCall::RunCommand(MoveTemp(Cmd)))
	{
		return Out;
	}
	Out.Id = Raw->GetNodeId();
	Out.System = System->GetPathName();
	Out.bSnappedToGround = bSnapped;
	if (const FRuntimeSceneNode* Node = RuntimeToolCall::GetGraph()->Find(Raw->GetNodeId()))
	{
		Out.Name = Node->Name;
		if (AActor* A = Node->Actor.Get())
		{
			Out.Location = A->GetActorLocation();
			if (UNiagaraComponent* C = A->FindComponentByClass<UNiagaraComponent>())
			{
				Out.bActive = C->IsActive();
				UserParams(C).GetKeys(Out.UserVariables);
			}
		}
	}
	return Out;
}

FRuntimeVfxOneShot URuntimeVfxToolset::SpawnOneShot(UNiagaraSystem* System, const FVector& Location, const FRotator& Rotation, const FVector& Scale,
	const FRuntimeToolJson& UserVariables, bool bSnapToGround)
{
	using namespace RtVfx;
	FRuntimeVfxOneShot Out;
	if (!System)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, TEXT("system is required"));
		return Out;
	}
	if (!CheckUserValues(System, UserVariables))
	{
		return Out;
	}
	UWorld* World = RuntimeToolCall::GetWorld();
	FVector Where = Passed(TEXT("location")) ? Location : FVector::ZeroVector;
	Out.bSnappedToGround = bSnapToGround && SnapToGround(World, Where);
	// Not pooled, no pre-cull (the agent asked for it to play), auto-destroyed when it finishes.
	UNiagaraComponent* Comp = UNiagaraFunctionLibrary::SpawnSystemAtLocation(World, System, Where, Passed(TEXT("rotation")) ? Rotation : FRotator::ZeroRotator,
		Passed(TEXT("scale")) ? Scale : FVector::OneVector, true, false, ENCPoolMethod::None, false);
	if (!Comp && World->GetWorldSettings())
	{
		// Worlds without an FX system setup (isolated worlds): host the component on the world settings actor.
		Comp = NewObject<UNiagaraComponent>(World->GetWorldSettings());
		Comp->SetAutoDestroy(true);
		Comp->bAutoActivate = false;
		Comp->SetAsset(System);
		Comp->SetWorldLocationAndRotation(Where, Passed(TEXT("rotation")) ? Rotation : FRotator::ZeroRotator);
		Comp->SetWorldScale3D(Passed(TEXT("scale")) ? Scale : FVector::OneVector);
		Comp->RegisterComponentWithWorld(World);
	}
	if (!Comp)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::Internal, TEXT("Niagara could not spawn the system"));
		return Out;
	}
	ApplyUserValues(Comp, UserVariables.AsObject());
	Comp->Activate(true);
	Out.System = System->GetPathName();
	Out.Location = Where;
	Out.bLooping = System->IsLooping();
	return Out;
}

FRuntimeVfxSummary URuntimeVfxToolset::GetSummary(UNiagaraComponent* Target)
{
	using namespace RtVfx;
	FRuntimeVfxSummary Out;
	if (!Target)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, TEXT("target is required"));
		return Out;
	}
	Out.Target = TargetRef(Target);
	Out.System = SystemPath(Target);
	Out.bActive = Target->IsActive();
	Out.Location = Target->GetComponentLocation();
	for (const TPair<FString, FNiagaraTypeDefinition>& P : UserParams(Target))
	{
		Out.UserVariables.Add(MakeVariable(Target, P.Key, P.Value));
	}
	if (UNiagaraSystem* System = Target->GetAsset())
	{
		Out.bLooping = System->IsLooping();
		for (const FNiagaraEmitterHandle& H : System->GetEmitterHandles())
		{
			FRuntimeVfxEmitterSummary& E = Out.Emitters.AddDefaulted_GetRef();
			E.Name = H.GetName().ToString();
			E.bEnabled = H.GetIsEnabled();
			if (const FVersionedNiagaraEmitterData* Data = H.GetEmitterData())
			{
				E.SimTarget = Data->SimTarget == ENiagaraSimTarget::GPUComputeSim ? TEXT("GPU") : TEXT("CPU");
				for (const UNiagaraRendererProperties* R : Data->GetRenderers())
				{
					if (R)
					{
						E.Renderers.AddUnique(R->GetClass()->GetName());
					}
				}
			}
		}
	}
	return Out;
}

TArray<FRuntimeVfxUserVariable> URuntimeVfxToolset::GetUserVariables(UNiagaraComponent* Target)
{
	using namespace RtVfx;
	TArray<FRuntimeVfxUserVariable> Out;
	if (!Target)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, TEXT("target is required"));
		return Out;
	}
	for (const TPair<FString, FNiagaraTypeDefinition>& P : UserParams(Target))
	{
		Out.Add(MakeVariable(Target, P.Key, P.Value));
	}
	return Out;
}

FRuntimeVfxUserVariable URuntimeVfxToolset::GetUserVariable(UNiagaraComponent* Target, const FString& Name)
{
	using namespace RtVfx;
	if (!Target)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, TEXT("target is required"));
		return {};
	}
	FString Exact = Name;
	TMap<FString, FNiagaraTypeDefinition> Known;
	const FNiagaraTypeDefinition* Type = FindVariable(Target, Exact, Known);
	return Type ? MakeVariable(Target, Exact, *Type) : FRuntimeVfxUserVariable();
}

FRuntimeVfxVariableChange URuntimeVfxToolset::SetUserVariable(UNiagaraComponent* Target, const FString& Name, const FRuntimeToolJson& Value)
{
	using namespace RtVfx;
	FRuntimeVfxVariableChange Out;
	if (!Target)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, TEXT("target is required"));
		return Out;
	}
	FString Exact = Name;
	TMap<FString, FNiagaraTypeDefinition> Known;
	const FNiagaraTypeDefinition* Type = FindVariable(Target, Exact, Known);
	if (!Type)
	{
		return Out;
	}
	const TSharedPtr<FJsonValue> Old = ReadParam(Target, *Type, Exact);
	if (!Old.IsValid())
	{
		RuntimeToolCall::Fail(ERuntimeToolError::Unsupported, FString::Printf(TEXT("user variable '%s' has type %s, which the tools cannot set (float, int, bool, vector, position, color)"),
			*Exact, *TypeName(*Type)));
		return Out;
	}
	FString Err;
	if (!WriteParam(nullptr, *Type, Exact, Value.Value, Err))
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, Err);
		return Out;
	}
	TWeakObjectPtr<UNiagaraComponent> Weak = Target;
	const FNiagaraTypeDefinition TypeCopy = *Type;
	const TSharedPtr<FJsonValue> New = Value.Value;
	TSharedRef<FJsonObject> Delta = MakeShared<FJsonObject>();
	Delta->SetStringField(TEXT("parameter"), Exact);
	Delta->SetField(TEXT("value"), New);
	if (!RuntimeToolCall::RunCommand(MakeUnique<FLambdaCommand>(FString::Printf(TEXT("vfx variable %s"), *Exact),
		[Weak, TypeCopy, Exact, New](FRuntimeCommandContext&, FString& E) { return Weak.IsValid() && WriteParam(Weak.Get(), TypeCopy, Exact, New, E); },
		[Weak, TypeCopy, Exact, Old](FRuntimeCommandContext&) { FString E; return Weak.IsValid() && WriteParam(Weak.Get(), TypeCopy, Exact, Old, E); }, Delta)))
	{
		return Out;
	}
	Out.Target = TargetRef(Target);
	Out.Variable = MakeVariable(Target, Exact, TypeCopy);
	Out.Previous = FRuntimeToolJson::Make(Old);
	return Out;
}

FRuntimeVfxState URuntimeVfxToolset::SetSystem(UNiagaraComponent* Target, UNiagaraSystem* System, bool bResetUserVariables)
{
	using namespace RtVfx;
	if (!Target || !System)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, TEXT("target and system are required"));
		return {};
	}
	TWeakObjectPtr<UNiagaraComponent> Weak = Target;
	TStrongObjectPtr<UNiagaraSystem> OldSystem(Target->GetAsset());
	TStrongObjectPtr<UNiagaraSystem> NewSystem(System);
	const FRuntimeSceneNode* Node = Target->GetOwner() ? RuntimeToolCall::NodeOf(Target->GetOwner()) : nullptr;
	const FString NodeId = Node ? Node->Id : FString();
	const FString OldSource = Node ? Node->Source : FString();
	const bool bWasActive = Target->IsActive();
	// Snapshot user overrides so undo can restore them.
	TSharedRef<FJsonObject> OldValues = MakeShared<FJsonObject>();
	for (const TPair<FString, FNiagaraTypeDefinition>& P : UserParams(Target))
	{
		if (const TSharedPtr<FJsonValue> V = ReadParam(Target, P.Value, P.Key))
		{
			OldValues->SetField(P.Key, V);
		}
	}
	auto Apply = [Weak, NodeId](FRuntimeCommandContext& Ctx, UNiagaraSystem* Asset, bool bReset, const FString& Source, const TSharedPtr<FJsonObject>& Values, bool bActive)
	{
		if (!Weak.IsValid())
		{
			return false;
		}
		Weak->SetAsset(Asset, bReset);
		ApplyUserValues(Weak.Get(), Values);
		if (bActive)
		{
			Weak->Activate(true);
		}
		if (FRuntimeSceneNode* N = Ctx.Graph && !NodeId.IsEmpty() ? Ctx.Graph->Find(NodeId) : nullptr; N && !Source.IsEmpty())
		{
			N->Source = Source;
		}
		return true;
	};
	const FString NewSource = NodeId.IsEmpty() ? FString() : TEXT("vfx:") + System->GetPathName();
	TSharedRef<FJsonObject> Delta = MakeShared<FJsonObject>();
	Delta->SetStringField(TEXT("target"), TargetRef(Target));
	Delta->SetStringField(TEXT("system"), System->GetPathName());
	if (!RuntimeToolCall::RunCommand(MakeUnique<FLambdaCommand>(TEXT("set vfx system"),
		[Apply, NewSystem, bResetUserVariables, NewSource, bWasActive](FRuntimeCommandContext& Ctx, FString&) { return Apply(Ctx, NewSystem.Get(), bResetUserVariables, NewSource, nullptr, bWasActive); },
		[Apply, OldSystem, OldSource, OldValues, bWasActive](FRuntimeCommandContext& Ctx) { return Apply(Ctx, OldSystem.Get(), true, OldSource, OldValues, bWasActive); }, Delta)))
	{
		return {};
	}
	return MakeState(Target, Target->IsActive());
}

FRuntimeVfxState URuntimeVfxToolset::Activate(UNiagaraComponent* Target, bool bReset)
{
	if (!Target)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, TEXT("target is required"));
		return {};
	}
	return RtVfx::SetActive(Target, true, bReset);
}

FRuntimeVfxState URuntimeVfxToolset::Deactivate(UNiagaraComponent* Target)
{
	if (!Target)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, TEXT("target is required"));
		return {};
	}
	return RtVfx::SetActive(Target, false, false);
}

FRuntimeVfxActiveList URuntimeVfxToolset::ListActive(int32 Limit)
{
	using namespace RtVfx;
	FRuntimeVfxActiveList Out;
	UWorld* World = RuntimeToolCall::GetWorld();
	TArray<FRuntimeVfxActive> All;
	for (TObjectIterator<UNiagaraComponent> It; It; ++It)
	{
		UNiagaraComponent* C = *It;
		if (!C || C->GetWorld() != World || !C->IsActive() || C->HasAnyFlags(RF_ClassDefaultObject))
		{
			continue;
		}
		FRuntimeVfxActive& A = All.AddDefaulted_GetRef();
		A.Target = TargetRef(C);
		A.System = C->GetAsset() ? C->GetAsset()->GetName() : FString();
		A.Location = C->GetComponentLocation();
	}
	All.Sort([](const FRuntimeVfxActive& A, const FRuntimeVfxActive& B) { return A.Target < B.Target; });
	Out.Total = All.Num();
	const int32 Max = URuntimeToolsetsSettings::Get().ClampLimit(Limit);
	for (int32 i = 0; i < All.Num() && i < Max; ++i)
	{
		Out.Active.Add(All[i]);
	}
	return Out;
}

AActor* URuntimeVfxToolset::SpawnFromSource(UWorld* World, const FString& Source, const FTransform& Transform, const FString& Name, FString& OutError)
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
