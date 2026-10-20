#include "RuntimeToolInvocation.h"
#include "RuntimeCommandHistory.h"
#include "RuntimeJson.h"
#include "RuntimeToolsetsSettings.h"

FToolInvocation::FToolInvocation(const TSharedRef<const FRuntimeToolDescriptor>& InTool, const TSharedRef<FJsonObject>& InArgs,
	const FToolCallerContext& InCaller, UWorld* InWorld, FRuntimeSceneGraph* InGraph, FRuntimeCommandHistory* InHistory, int32 InDepth)
	: Tool(InTool), Args(InArgs), Caller(InCaller), World(InWorld), Graph(InGraph), History(InHistory), Depth(InDepth)
{
	check(History);
}

FRuntimeCommandContext FToolInvocation::MakeCommandContext() const
{
	FRuntimeCommandContext C;
	C.World = World;
	C.Graph = Graph;
	C.SessionId = Caller.SessionId;
	return C;
}

bool FToolInvocation::Has(const TCHAR* Name) const
{
	return RuntimeJson::HasValue(Args, Name);
}

FString FToolInvocation::GetString(const TCHAR* Name, const FString& Default) const
{
	FString Out;
	return Args->TryGetStringField(Name, Out) ? Out : Default;
}

int64 FToolInvocation::GetInt(const TCHAR* Name, int64 Default) const
{
	double Out;
	return Args->TryGetNumberField(Name, Out) ? static_cast<int64>(Out) : Default;
}

double FToolInvocation::GetNumber(const TCHAR* Name, double Default) const
{
	double Out;
	return Args->TryGetNumberField(Name, Out) ? Out : Default;
}

bool FToolInvocation::GetBool(const TCHAR* Name, bool Default) const
{
	bool Out;
	return Args->TryGetBoolField(Name, Out) ? Out : Default;
}

bool FToolInvocation::GetVector(const TCHAR* Name, FVector& Out) const
{
	return Has(Name) && RuntimeJson::ToVector(Args->TryGetField(Name), Out);
}

bool FToolInvocation::GetRotator(const TCHAR* Name, FRotator& Out) const
{
	return Has(Name) && RuntimeJson::ToRotator(Args->TryGetField(Name), Out);
}

TArray<FString> FToolInvocation::GetStringArray(const TCHAR* Name) const
{
	TArray<FString> Out;
	const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
	if (Args->TryGetArrayField(Name, Arr))
	{
		for (const TSharedPtr<FJsonValue>& V : *Arr)
		{
			FString S;
			if (V.IsValid() && V->TryGetString(S))
			{
				Out.Add(S);
			}
		}
	}
	return Out;
}

TSharedPtr<FJsonObject> FToolInvocation::GetObject(const TCHAR* Name) const
{
	const TSharedPtr<FJsonObject>* Obj = nullptr;
	return Args->TryGetObjectField(Name, Obj) ? *Obj : nullptr;
}

TSharedPtr<FJsonValue> FToolInvocation::GetValue(const TCHAR* Name) const
{
	return Args->TryGetField(Name);
}

int32 FToolInvocation::GetLimit() const
{
	return URuntimeToolsetsSettings::Get().ClampLimit(GetInt(TEXT("limit"), 0));
}

TArray<FString> FToolInvocation::GetFields() const
{
	return GetStringArray(TEXT("fields"));
}

bool FToolInvocation::ResolveTarget(const TCHAR* ArgName, FResolvedTarget& Out)
{
	ERuntimeToolError Error = ERuntimeToolError::None;
	FString Message;
	if (!RuntimeObjectResolver::Resolve(GetString(ArgName), World, Graph, Out, Error, Message))
	{
		Fail(Error, FString::Printf(TEXT("%s: %s"), ArgName, *Message), TEXT("Use scene.find_nodes or actors.find_actors to get ids"));
		return false;
	}
	return true;
}

AActor* FToolInvocation::ResolveActor(const TCHAR* ArgName)
{
	ERuntimeToolError Error = ERuntimeToolError::None;
	FString Message;
	AActor* Actor = RuntimeObjectResolver::ResolveActor(GetString(ArgName), World, Graph, Error, Message);
	if (!Actor)
	{
		Fail(Error, FString::Printf(TEXT("%s: %s"), ArgName, *Message), TEXT("Use scene.find_nodes or actors.find_actors to get ids"));
	}
	return Actor;
}

bool FToolInvocation::RunCommand(TUniquePtr<IRuntimeCommand> Command, TSharedPtr<FJsonObject>* OutDelta)
{
	FRuntimeCommandContext Context = MakeCommandContext();
	FString Error;
	TSharedPtr<FJsonObject> Delta;
	const int64 EntryId = History->Execute(MoveTemp(Command), Context, Error, Delta);
	if (EntryId == INDEX_NONE)
	{
		Fail(ERuntimeToolError::WrongState, Error.IsEmpty() ? TEXT("command failed") : Error);
		return false;
	}
	Result.bUndoable = true;
	Result.HistoryEntryId = EntryId;
	if (OutDelta)
	{
		*OutDelta = Delta;
	}
	return true;
}

void FToolInvocation::RecordIrreversible(const FString& Label)
{
	FRuntimeCommandContext Context = MakeCommandContext();
	History->RecordIrreversible(Label, Context);
	Result.bUndoable = false;
}

void FToolInvocation::Succeed(TSharedPtr<FJsonObject> Payload)
{
	if (bComplete)
	{
		return;
	}
	Result.Error = ERuntimeToolError::None;
	Result.Payload = Payload;
	Finish();
}

void FToolInvocation::Fail(ERuntimeToolError Error, const FString& Message, const FString& Hint)
{
	if (bComplete)
	{
		return;
	}
	Result.Error = Error == ERuntimeToolError::None ? ERuntimeToolError::Internal : Error;
	Result.Message = Message;
	Result.Hint = Hint;
	Result.Payload.Reset();
	Result.Attachments.Reset();
	Finish();
}

void FToolInvocation::AddAttachment(FToolAttachment&& Attachment)
{
	Result.Attachments.Add(MoveTemp(Attachment));
}

void FToolInvocation::Defer()
{
	bDeferred = true;
}

void FToolInvocation::ReportProgress(float Fraction, const FString& Message)
{
	if (OnProgress && !bComplete)
	{
		OnProgress(FMath::Clamp(Fraction, 0.f, 1.f), Message);
	}
}

bool FToolInvocation::IsCancelled() const
{
	return CancelQuery ? CancelQuery() : false;
}

void FToolInvocation::NotifyCancelled()
{
	if (CancelHandler)
	{
		TFunction<void()> Handler = MoveTemp(CancelHandler);
		CancelHandler = nullptr;
		Handler();
	}
}

void FToolInvocation::Finish()
{
	bComplete = true;
	CancelHandler = nullptr;
	if (OnFinished)
	{
		TFunction<void(const FToolResult&)> Callback = MoveTemp(OnFinished);
		OnFinished = nullptr;
		Callback(Result);
	}
}
