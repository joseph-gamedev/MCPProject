#include "RuntimeToolCall.h"
#include "RuntimeCommand.h"
#include "RuntimeObjectResolver.h"
#include "RuntimeSceneGraph.h"
#include "RuntimeToolInvocation.h"
#include "RuntimeToolReflection.h"
#include "RuntimeToolsetDefinition.h"
#include "GameFramework/Actor.h"

// ---- FRuntimeToolJson ---------------------------------------------------------------------------------------------------

TSharedPtr<FJsonObject> FRuntimeToolJson::AsObject() const
{
	const TSharedPtr<FJsonObject>* O = nullptr;
	return Value.IsValid() && Value->TryGetObject(O) ? *O : nullptr;
}

FRuntimeToolJson FRuntimeToolJson::Make(const TSharedPtr<FJsonValue>& InValue)
{
	FRuntimeToolJson J;
	J.Value = InValue;
	return J;
}

FRuntimeToolJson FRuntimeToolJson::Make(const TSharedPtr<FJsonObject>& InObject)
{
	return Make(InObject.IsValid() ? StaticCastSharedRef<FJsonValue>(MakeShared<FJsonValueObject>(InObject)) : TSharedPtr<FJsonValue>());
}

// ---- current call -------------------------------------------------------------------------------------------------------

namespace RuntimeToolCallPrivate
{
	static TArray<FToolInvocation*> Stack;

	void PushCurrent(FToolInvocation* Inv)
	{
		check(IsInGameThread());
		Stack.Push(Inv);
	}

	void PopCurrent()
	{
		Stack.Pop();
	}
}

namespace RuntimeToolCall
{
	FToolInvocation* Current()
	{
		return RuntimeToolCallPrivate::Stack.Num() > 0 ? RuntimeToolCallPrivate::Stack.Last() : nullptr;
	}

	UWorld* GetWorld()
	{
		FToolInvocation* I = Current();
		return I ? I->GetWorld() : nullptr;
	}

	FRuntimeSceneGraph* GetGraph()
	{
		FToolInvocation* I = Current();
		return I ? I->GetGraph() : nullptr;
	}

	void Fail(ERuntimeToolError Error, const FString& Message, const FString& Hint)
	{
		if (FToolInvocation* I = Current(); I && !I->IsComplete())
		{
			I->Fail(Error, Message, Hint);
		}
	}

	bool HasFailed()
	{
		FToolInvocation* I = Current();
		return I && I->IsComplete() && !I->GetResult().IsOk();
	}

	bool RunCommand(TUniquePtr<IRuntimeCommand> Command)
	{
		FToolInvocation* I = Current();
		return I && !I->IsComplete() && I->RunCommand(MoveTemp(Command));
	}

	UObject* Resolve(const FString& Reference, UClass* Expected)
	{
		FToolInvocation* I = Current();
		if (!I)
		{
			return nullptr;
		}
		RuntimeToolReflection::FConvertContext Ctx;
		Ctx.World = I->GetWorld();
		Ctx.Graph = I->GetGraph();
		FString Err;
		UObject* Out = RuntimeToolReflection::ResolveObject(Reference, Expected, Ctx, Err);
		if (!Out)
		{
			Fail(ERuntimeToolError::NotFound, Err);
		}
		return Out;
	}

	const FRuntimeSceneNode* NodeOf(const AActor* Actor)
	{
		FRuntimeSceneGraph* G = GetGraph();
		return G && Actor ? G->FindByActor(Actor) : nullptr;
	}

	FString MakeReference(const UObject* Object)
	{
		return RuntimeObjectResolver::MakeReference(Object, GetGraph());
	}

	void SetTimeout(double Seconds)
	{
		if (FToolInvocation* I = Current())
		{
			I->SetTimeout(Seconds);
		}
	}

	void ReportProgress(float Fraction, const FString& Message)
	{
		if (FToolInvocation* I = Current())
		{
			I->ReportProgress(Fraction, Message);
		}
	}
}

// ---- async results ------------------------------------------------------------------------------------------------------

bool URuntimeToolAsyncResult::SetError(const FString& InError, ERuntimeToolError Code)
{
	if (bIsComplete)
	{
		return false;
	}
	Error = InError.IsEmpty() ? TEXT("failed") : InError;
	ErrorCode = Code;
	bIsComplete = true;
	Finish();
	return true;
}

bool URuntimeToolAsyncResult::Complete()
{
	if (bIsComplete)
	{
		return false;
	}
	bIsComplete = true;
	Finish();
	return true;
}

void URuntimeToolAsyncResult::Bind(const TSharedRef<FToolInvocation>& InInvocation)
{
	Invocation = InInvocation;
	if (!IsRooted())
	{
		AddToRoot(); // kept alive until it completes
	}
	TWeakObjectPtr<URuntimeToolAsyncResult> Weak = this;
	InInvocation->SetCancelHandler([Weak]()
	{
		if (URuntimeToolAsyncResult* R = Weak.Get())
		{
			if (R->OnCancelled)
			{
				R->OnCancelled();
			}
			R->Invocation.Reset();
			if (R->IsRooted())
			{
				R->RemoveFromRoot();
			}
		}
	});
	if (bIsComplete)
	{
		Finish();
	}
}

void URuntimeToolAsyncResult::Finish()
{
	if (!Invocation.IsValid())
	{
		return; // not bound yet: Bind finishes it
	}
	TSharedPtr<FToolInvocation> Inv = Invocation;
	Invocation.Reset();
	if (IsRooted())
	{
		RemoveFromRoot();
	}
	if (Inv->IsComplete())
	{
		return;
	}
	if (!Error.IsEmpty())
	{
		Inv->Fail(ErrorCode, Error);
		return;
	}
	TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
	if (const FProperty* ValueProp = GetClass()->FindPropertyByName(TEXT("Value")))
	{
		Out->SetField(TEXT("returnValue"), RuntimeToolReflection::PropertyToJson(ValueProp, ValueProp->ContainerPtrToValuePtr<void>(this), Inv->GetGraph()));
	}
	Inv->Succeed(Out);
}

// ---- toolset definition -------------------------------------------------------------------------------------------------

FName URuntimeToolsetDefinition::GetToolsetName() const
{
	FString N = GetClass()->GetName();
	N.RemoveFromEnd(TEXT("Toolset"));
	N.RemoveFromStart(TEXT("Runtime"));
	return FName(*RuntimeToolReflection::ToSnakeCase(N));
}
