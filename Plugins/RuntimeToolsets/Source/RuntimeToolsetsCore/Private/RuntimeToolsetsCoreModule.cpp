#include "RuntimeToolsetsCore.h"
#include "RuntimeCommandHistory.h"
#include "RuntimeJson.h"
#include "RuntimeLogCapture.h"
#include "RuntimeSceneGraph.h"
#include "RuntimeToolInvocation.h"
#include "RuntimeToolsetsLog.h"
#include "RuntimeToolsetsSettings.h"
#include "Async/Async.h"
#include "Containers/Queue.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CoreDelegates.h"
#include "Misc/OutputDeviceRedirector.h"
#include <atomic>

DEFINE_LOG_CATEGORY(LogRuntimeToolsetsCore);

static std::atomic<IRuntimeToolsetsCore*> GRuntimeToolsetsCoreInstance{nullptr};

IRuntimeToolsetsCore* GetRuntimeToolsetsCoreInstance()
{
	return GRuntimeToolsetsCoreInstance.load(std::memory_order_acquire);
}

namespace
{
	constexpr int32 MaxCallDepth = 4;

	struct FToolCall
	{
		FGuid Id = FGuid::NewGuid();
		FString Tool;
		TSharedPtr<FJsonObject> Args;
		FString ArgsError;
		FToolCallerContext Caller;
		FOnToolComplete OnComplete;
		FOnToolProgress OnProgress;
		std::atomic<bool> bCancelRequested{false};
		std::atomic<bool> bDone{false};
		double StartTime = 0.0;
	};

	class FToolCallHandle final : public IToolCallHandle
	{
	public:
		explicit FToolCallHandle(const TSharedRef<FToolCall>& InCall) : Call(InCall) {}
		virtual void Cancel() override { Call->bCancelRequested = true; }
		virtual bool IsDone() const override { return Call->bDone; }
		virtual FGuid GetCallId() const override { return Call->Id; }

	private:
		TSharedRef<FToolCall> Call;
	};

	struct FToolsetEntry
	{
		TSharedPtr<IRuntimeToolset> Toolset;
		TArray<TSharedRef<const FRuntimeToolDescriptor>> Tools;
	};
}

class FRuntimeToolsetsCoreModule final : public IRuntimeToolsetsCore
{
public:
	// ---- IModuleInterface ----

	virtual void StartupModule() override
	{
		const URuntimeToolsetsSettings& Settings = URuntimeToolsetsSettings::Get();
		LogCapture = MakeUnique<FRuntimeLogCapture>(Settings.LogRingLines);
		if (GLog)
		{
			GLog->AddOutputDevice(LogCapture.Get());
		}
		TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FRuntimeToolsetsCoreModule::Tick));
		WorldCleanupHandle = FWorldDelegates::OnWorldCleanup.AddRaw(this, &FRuntimeToolsetsCoreModule::OnWorldCleanup);

		CallCommand = IConsoleManager::Get().RegisterConsoleCommand(TEXT("RuntimeToolsets.Call"),
			TEXT("RuntimeToolsets.Call <toolset.tool> [json args] - runs a tool and logs the result"),
			FConsoleCommandWithArgsDelegate::CreateRaw(this, &FRuntimeToolsetsCoreModule::ConsoleCall), ECVF_Default);

		GRuntimeToolsetsCoreInstance.store(this, std::memory_order_release);
		UE_LOG(LogRuntimeToolsetsCore, Log, TEXT("RuntimeToolsetsCore started"));
	}

	virtual void ShutdownModule() override
	{
		GRuntimeToolsetsCoreInstance.store(nullptr, std::memory_order_release);
		if (CallCommand)
		{
			IConsoleManager::Get().UnregisterConsoleObject(CallCommand);
			CallCommand = nullptr;
		}
		FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
		FWorldDelegates::OnWorldCleanup.Remove(WorldCleanupHandle);

		// Fail everything still pending.
		TSharedPtr<FToolCall> Call;
		while (Queue.Dequeue(Call))
		{
			CompleteNow(Call.ToSharedRef(), FToolResult::Fail(ERuntimeToolError::Cancelled, TEXT("shutting down")));
		}
		if (ActiveInvocation.IsValid())
		{
			ActiveInvocation->NotifyCancelled();
			ActiveInvocation->Fail(ERuntimeToolError::Cancelled, TEXT("shutting down"));
		}

		Sessions.Empty();
		Graphs.Empty();

		if (GLog && LogCapture)
		{
			GLog->RemoveOutputDevice(LogCapture.Get());
		}
		LogCapture.Reset();
	}

	// ---- Catalogue ----

	virtual FString ListToolsets() const override
	{
		FReadScopeLock Lock(RegistryLock);
		TArray<FName> Names;
		Toolsets.GetKeys(Names);
		Names.Sort([](const FName& A, const FName& B) { return A.ToString().ToLower() < B.ToString().ToLower(); });

		TArray<TSharedPtr<FJsonValue>> Items;
		for (const FName& Name : Names)
		{
			const FToolsetEntry& Entry = Toolsets[Name];
			if (!IsToolsetEnabled(Name))
			{
				continue;
			}
			int32 Enabled = 0;
			for (const TSharedRef<const FRuntimeToolDescriptor>& T : Entry.Tools)
			{
				Enabled += IsToolEnabled(*T) ? 1 : 0;
			}
			TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
			J->SetStringField(TEXT("name"), Name.ToString().ToLower());
			J->SetStringField(TEXT("description"), Entry.Toolset->GetDescription());
			J->SetStringField(TEXT("version"), Entry.Toolset->GetVersion());
			J->SetNumberField(TEXT("tools"), Enabled);
			Items.Add(MakeShared<FJsonValueObject>(J));
		}
		TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
		Out->SetArrayField(TEXT("toolsets"), Items);
		return RuntimeJson::Write(Out);
	}

	virtual FString DescribeToolset(const FString& Name, bool bIncludeSchemas) const override
	{
		FReadScopeLock Lock(RegistryLock);
		const FToolsetEntry* Entry = Toolsets.Find(FName(*Name));
		if (!Entry || !IsToolsetEnabled(FName(*Name)))
		{
			return FString();
		}
		TArray<TSharedPtr<FJsonValue>> Tools;
		for (const TSharedRef<const FRuntimeToolDescriptor>& T : Entry->Tools)
		{
			if (IsToolEnabled(*T))
			{
				Tools.Add(MakeShared<FJsonValueObject>(T->ToJson(bIncludeSchemas)));
			}
		}
		TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
		Out->SetStringField(TEXT("name"), Entry->Toolset->GetName().ToString().ToLower());
		Out->SetStringField(TEXT("description"), Entry->Toolset->GetDescription());
		Out->SetStringField(TEXT("version"), Entry->Toolset->GetVersion());
		Out->SetArrayField(TEXT("tools"), Tools);
		return RuntimeJson::Write(Out);
	}

	virtual void RegisterToolset(const TSharedRef<IRuntimeToolset>& Toolset) override
	{
		FRuntimeToolsetBuilder Builder(Toolset->GetName());
		Toolset->RegisterTools(Builder);
		{
			FWriteScopeLock Lock(RegistryLock);
			UnregisterLocked(Toolset->GetName());
			FToolsetEntry& Entry = Toolsets.Add(Toolset->GetName());
			Entry.Toolset = Toolset;
			for (const TSharedRef<FRuntimeToolDescriptor>& T : Builder.GetTools())
			{
				if (!T->Handler)
				{
					UE_LOG(LogRuntimeToolsetsCore, Error, TEXT("Tool %s has no handler; skipped"), *T->QualifiedName);
					continue;
				}
				if (ToolsByName.Contains(T->QualifiedName))
				{
					UE_LOG(LogRuntimeToolsetsCore, Error, TEXT("Duplicate tool %s; skipped"), *T->QualifiedName);
					continue;
				}
				Entry.Tools.Add(T);
				ToolsByName.Add(T->QualifiedName, T);
			}
			UE_LOG(LogRuntimeToolsetsCore, Log, TEXT("Registered toolset '%s' (%d tools)"), *Toolset->GetName().ToString().ToLower(), Entry.Tools.Num());
		}
		BroadcastToolsetsChanged();
	}

	virtual void UnregisterToolset(FName Name) override
	{
		{
			FWriteScopeLock Lock(RegistryLock);
			UnregisterLocked(Name);
		}
		BroadcastToolsetsChanged();
	}

	virtual TArray<TSharedRef<const FRuntimeToolDescriptor>> GetAllTools() const override
	{
		FReadScopeLock Lock(RegistryLock);
		TArray<TSharedRef<const FRuntimeToolDescriptor>> Out;
		TArray<FName> Names;
		Toolsets.GetKeys(Names);
		Names.Sort([](const FName& A, const FName& B) { return A.ToString().ToLower() < B.ToString().ToLower(); });
		for (const FName& Name : Names)
		{
			if (!IsToolsetEnabled(Name))
			{
				continue;
			}
			for (const TSharedRef<const FRuntimeToolDescriptor>& T : Toolsets[Name].Tools)
			{
				if (IsToolEnabled(*T))
				{
					Out.Add(T);
				}
			}
		}
		return Out;
	}

	virtual TSharedPtr<const FRuntimeToolDescriptor> FindTool(const FString& QualifiedName) const override
	{
		FReadScopeLock Lock(RegistryLock);
		const TSharedRef<const FRuntimeToolDescriptor>* Found = ToolsByName.Find(QualifiedName);
		if (!Found && !QualifiedName.Contains(TEXT(".")))
		{
			// Accept "toolset_tool" (MCP-safe form): toolset names contain no underscore.
			int32 Underscore = INDEX_NONE;
			if (QualifiedName.FindChar(TEXT('_'), Underscore))
			{
				Found = ToolsByName.Find(QualifiedName.Left(Underscore) + TEXT(".") + QualifiedName.Mid(Underscore + 1));
			}
		}
		return Found ? TSharedPtr<const FRuntimeToolDescriptor>(*Found) : nullptr;
	}

	virtual bool IsToolEnabled(const FRuntimeToolDescriptor& Tool) const override
	{
		return IsToolsetEnabled(Tool.Toolset) && !URuntimeToolsetsSettings::Get().DisabledTools.Contains(Tool.QualifiedName);
	}

	// ---- Invocation ----

	virtual TSharedRef<IToolCallHandle> Invoke(const FString& Tool, const FString& JsonArguments, const FToolCallerContext& Caller,
		FOnToolComplete OnComplete, FOnToolProgress OnProgress) override
	{
		TSharedPtr<FJsonObject> Args;
		FString Error;
		const FString Trimmed = JsonArguments.TrimStartAndEnd();
		if (Trimmed.IsEmpty() || Trimmed == TEXT("null"))
		{
			Args = MakeShared<FJsonObject>();
		}
		else
		{
			Args = RuntimeJson::Parse(Trimmed);
			if (!Args.IsValid())
			{
				Error = TEXT("arguments are not a JSON object");
			}
		}
		return Enqueue(Tool, Args, Error, Caller, MoveTemp(OnComplete), MoveTemp(OnProgress));
	}

	virtual TSharedRef<IToolCallHandle> Invoke(const FString& Tool, const TSharedPtr<FJsonObject>& Arguments, const FToolCallerContext& Caller,
		FOnToolComplete OnComplete, FOnToolProgress OnProgress) override
	{
		// Deep copy: validation fills defaults and the caller may reuse its object.
		TSharedPtr<FJsonObject> Copy = Arguments.IsValid() ? RuntimeJson::Parse(RuntimeJson::Write(Arguments.ToSharedRef())) : MakeShared<FJsonObject>();
		return Enqueue(Tool, Copy, FString(), Caller, MoveTemp(OnComplete), MoveTemp(OnProgress));
	}

	virtual FToolResult ExecuteInline(const FString& Tool, const TSharedPtr<FJsonObject>& Arguments, const FToolCallerContext& Caller, int32 Depth) override
	{
		check(IsInGameThread());
		if (Depth > MaxCallDepth)
		{
			return FToolResult::Fail(ERuntimeToolError::NotAllowed, FString::Printf(TEXT("call depth limit (%d) reached"), MaxCallDepth));
		}
		TSharedPtr<FJsonObject> Args = Arguments.IsValid() ? RuntimeJson::Parse(RuntimeJson::Write(Arguments.ToSharedRef())) : MakeShared<FJsonObject>();
		FToolResult PrepareError;
		TSharedPtr<FToolInvocation> Inv = Prepare(Tool, Args, Caller, Depth, PrepareError);
		if (!Inv.IsValid())
		{
			return PrepareError;
		}
		if (Inv->GetTool().bAsync)
		{
			return FToolResult::Fail(ERuntimeToolError::Unsupported, FString::Printf(TEXT("%s is async and cannot be nested"), *Tool),
				TEXT("Call it directly instead of through call_tool"));
		}
		const double Start = FPlatformTime::Seconds();
		Inv->GetTool().Handler(*Inv);
		if (!Inv->IsComplete())
		{
			Inv->Fail(ERuntimeToolError::Internal, TEXT("tool returned without a result"));
		}
		const FToolResult& Result = Inv->GetResult();
		RecordUsage(Tool, Result, FPlatformTime::Seconds() - Start);
		return Result;
	}

	virtual TFunction<void()> ExecuteNested(const FString& Tool, const TSharedPtr<FJsonObject>& Arguments, const FToolCallerContext& Caller, int32 Depth,
		TFunction<void(const FToolResult&)> OnDone) override
	{
		check(IsInGameThread());
		if (Depth > MaxCallDepth)
		{
			OnDone(FToolResult::Fail(ERuntimeToolError::NotAllowed, FString::Printf(TEXT("call depth limit (%d) reached"), MaxCallDepth)));
			return []() {};
		}
		TSharedPtr<FJsonObject> Args = Arguments.IsValid() ? RuntimeJson::Parse(RuntimeJson::Write(Arguments.ToSharedRef())) : MakeShared<FJsonObject>();
		FToolResult PrepareError;
		TSharedPtr<FToolInvocation> Inv = Prepare(Tool, Args, Caller, Depth, PrepareError);
		if (!Inv.IsValid())
		{
			OnDone(PrepareError);
			return []() {};
		}
		const double Start = FPlatformTime::Seconds();
		Inv->OnFinished = [this, Tool, Start, OnDone](const FToolResult& Result)
		{
			RecordUsage(Tool, Result, FPlatformTime::Seconds() - Start);
			OnDone(Result);
		};
		Inv->GetTool().Handler(*Inv);
		if (!Inv->IsComplete() && !Inv->IsDeferred())
		{
			Inv->Fail(ERuntimeToolError::Internal, TEXT("tool returned without a result"));
		}
		// The returned function keeps the nested invocation alive while it is pending.
		return [Inv]()
		{
			if (!Inv->IsComplete())
			{
				Inv->NotifyCancelled();
				Inv->Fail(ERuntimeToolError::Cancelled, TEXT("cancelled"));
			}
		};
	}

	virtual FOnRuntimeToolEvent& OnEvent() override { return EventDelegate; }
	virtual FSimpleMulticastDelegate& OnToolsetsChanged() override { return ToolsetsChangedDelegate; }

	virtual void EndSession(const FString& SessionId) override
	{
		check(IsInGameThread());
		if (TUniquePtr<FRuntimeCommandHistory>* History = Sessions.Find(SessionId))
		{
			FRuntimeCommandContext Context = MakeContext(SessionId);
			(*History)->Clear(Context);
			Sessions.Remove(SessionId);
		}
		if (LogCapture)
		{
			LogCapture->RemoveSession(SessionId);
		}
	}

	// ---- Services ----

	virtual UWorld* FindWorld() const override
	{
		if (UWorld* Override = WorldOverride.Get())
		{
			return Override;
		}
		if (!GEngine)
		{
			return nullptr;
		}
		UWorld* Pie = nullptr;
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			UWorld* World = Context.World();
			if (!World)
			{
				continue;
			}
			if (Context.WorldType == EWorldType::Game)
			{
				return World;
			}
			if (Context.WorldType == EWorldType::PIE && !Pie)
			{
				Pie = World;
			}
		}
		return Pie;
	}

	virtual void SetWorldOverride(UWorld* World) override { WorldOverride = World; }

	virtual FRuntimeSceneGraph* GetSceneGraph(UWorld* World) override
	{
		if (!World)
		{
			return nullptr;
		}
		TUniquePtr<FRuntimeSceneGraph>& Graph = Graphs.FindOrAdd(World);
		if (!Graph.IsValid())
		{
			Graph = MakeUnique<FRuntimeSceneGraph>(World);
		}
		return Graph.Get();
	}

	virtual FRuntimeCommandHistory& GetHistory(const FString& SessionId) override
	{
		TUniquePtr<FRuntimeCommandHistory>& History = Sessions.FindOrAdd(SessionId);
		if (!History.IsValid())
		{
			History = MakeUnique<FRuntimeCommandHistory>(URuntimeToolsetsSettings::Get().MaxHistoryPerSession);
		}
		return *History;
	}

	virtual FRuntimeLogCapture& GetLogCapture() override { return *LogCapture; }

	virtual void BroadcastEvent(const FString& SessionId, const TSharedRef<FJsonObject>& Event) override
	{
		check(IsInGameThread());
		EventDelegate.Broadcast(SessionId, Event);
	}

	virtual TSharedRef<FJsonObject> GetUsageReport() const override
	{
		TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
		Out->SetNumberField(TEXT("calls"), static_cast<double>(UsageCalls));
		Out->SetNumberField(TEXT("errors"), static_cast<double>(UsageErrors));
		Out->SetNumberField(TEXT("result_bytes_total"), static_cast<double>(UsageBytes));
		Out->SetNumberField(TEXT("result_bytes_avg"), UsageCalls > 0 ? FMath::RoundToDouble(static_cast<double>(UsageBytes) / UsageCalls) : 0.0);
		Out->SetNumberField(TEXT("result_bytes_max"), static_cast<double>(UsageMaxBytes));
		Out->SetStringField(TEXT("largest_tool"), UsageMaxTool);
		TArray<TSharedPtr<FJsonValue>> Top;
		TArray<TPair<FString, int64>> Sorted;
		for (const TPair<FString, int64>& P : UsageBytesPerTool)
		{
			Sorted.Add(P);
		}
		Sorted.Sort([](const TPair<FString, int64>& A, const TPair<FString, int64>& B) { return A.Value > B.Value; });
		for (int32 i = 0; i < Sorted.Num() && i < 10; ++i)
		{
			TSharedRef<FJsonObject> T = MakeShared<FJsonObject>();
			T->SetStringField(TEXT("tool"), Sorted[i].Key);
			T->SetNumberField(TEXT("bytes"), static_cast<double>(Sorted[i].Value));
			T->SetNumberField(TEXT("calls"), static_cast<double>(UsageCallsPerTool.FindRef(Sorted[i].Key)));
			Top.Add(MakeShared<FJsonValueObject>(T));
		}
		Out->SetArrayField(TEXT("top_tools_by_bytes"), Top);
		return Out;
	}

private:
	// ---- Registry helpers ----

	static bool IsToolsetEnabled(FName Name)
	{
		const TArray<FString>& Enabled = URuntimeToolsetsSettings::Get().EnabledToolsets;
		return Name == TEXT("session") || Enabled.Num() == 0 || Enabled.Contains(Name.ToString());
	}

	void UnregisterLocked(FName Name)
	{
		if (FToolsetEntry* Existing = Toolsets.Find(Name))
		{
			for (const TSharedRef<const FRuntimeToolDescriptor>& T : Existing->Tools)
			{
				ToolsByName.Remove(T->QualifiedName);
			}
			Toolsets.Remove(Name);
		}
	}

	void BroadcastToolsetsChanged()
	{
		if (IsInGameThread())
		{
			ToolsetsChangedDelegate.Broadcast();
		}
		else
		{
			AsyncTask(ENamedThreads::GameThread, [this]() { ToolsetsChangedDelegate.Broadcast(); });
		}
	}

	// ---- Queue ----

	TSharedRef<IToolCallHandle> Enqueue(const FString& Tool, const TSharedPtr<FJsonObject>& Args, const FString& ArgsError, const FToolCallerContext& Caller,
		FOnToolComplete OnComplete, FOnToolProgress OnProgress)
	{
		TSharedRef<FToolCall> Call = MakeShared<FToolCall>();
		Call->Tool = Tool;
		Call->Args = Args;
		Call->ArgsError = ArgsError;
		Call->Caller = Caller;
		if (Call->Caller.SessionId.IsEmpty())
		{
			Call->Caller.SessionId = TEXT("default");
		}
		Call->OnComplete = MoveTemp(OnComplete);
		Call->OnProgress = MoveTemp(OnProgress);

		const int32 MaxQueued = FMath::Max(1, URuntimeToolsetsSettings::Get().MaxQueuedCalls);
		if (QueuedCount.fetch_add(1) >= MaxQueued)
		{
			QueuedCount.fetch_sub(1);
			CompleteOnGameThread(Call, FToolResult::Fail(ERuntimeToolError::Busy, FString::Printf(TEXT("queue full (%d calls)"), MaxQueued),
				TEXT("Wait for pending calls to finish")));
		}
		else
		{
			Queue.Enqueue(Call);
		}
		return MakeShared<FToolCallHandle>(Call);
	}

	void CompleteOnGameThread(const TSharedRef<FToolCall>& Call, const FToolResult& Result)
	{
		if (IsInGameThread())
		{
			CompleteNow(Call, Result);
		}
		else
		{
			AsyncTask(ENamedThreads::GameThread, [this, Call, Result]() { CompleteNow(Call, Result); });
		}
	}

	void CompleteNow(const TSharedRef<FToolCall>& Call, const FToolResult& InResult)
	{
		bool bExpected = false;
		if (!Call->bDone.compare_exchange_strong(bExpected, true))
		{
			return;
		}
		FToolResult Result = InResult;
		const int32 Bytes = RecordUsage(Call->Tool, Result, Call->StartTime > 0 ? FPlatformTime::Seconds() - Call->StartTime : 0.0);
		const int32 MaxBytes = URuntimeToolsetsSettings::Get().MaxResultBytes;
		if (Result.IsOk() && MaxBytes > 0 && Bytes > MaxBytes)
		{
			Result = FToolResult::Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("result too large (%d bytes, max %d)"), Bytes, MaxBytes),
				TEXT("Narrow the query with limit, fields or a filter"));
		}
		Call->OnComplete.ExecuteIfBound(Result);
	}

	int32 RecordUsage(const FString& Tool, const FToolResult& Result, double Seconds)
	{
		const int32 Bytes = Result.ToJsonString().Len();
		++UsageCalls;
		UsageErrors += Result.IsOk() ? 0 : 1;
		UsageBytes += Bytes;
		UsageBytesPerTool.FindOrAdd(Tool) += Bytes;
		UsageCallsPerTool.FindOrAdd(Tool) += 1;
		if (Bytes > UsageMaxBytes)
		{
			UsageMaxBytes = Bytes;
			UsageMaxTool = Tool;
		}
		UE_LOG(LogRuntimeToolsetsCore, Verbose, TEXT("%s -> %s (%d bytes, %.1f ms)"), *Tool, Result.IsOk() ? TEXT("ok") : LexToString(Result.Error), Bytes,
			Seconds * 1000.0);
		return Bytes;
	}

	FRuntimeCommandContext MakeContext(const FString& SessionId)
	{
		FRuntimeCommandContext Context;
		Context.World = FindWorld();
		Context.Graph = GetSceneGraph(Context.World);
		Context.SessionId = SessionId;
		return Context;
	}

	TSharedPtr<FToolInvocation> Prepare(const FString& ToolName, const TSharedPtr<FJsonObject>& Args, const FToolCallerContext& Caller, int32 Depth,
		FToolResult& OutError)
	{
		TSharedPtr<const FRuntimeToolDescriptor> Tool = FindTool(ToolName);
		if (!Tool.IsValid())
		{
			OutError = FToolResult::Fail(ERuntimeToolError::UnknownTool, FString::Printf(TEXT("unknown tool '%s'"), *ToolName),
				TEXT("Use session.list_toolsets and session.describe_toolset; names are <toolset>.<tool>"));
			return nullptr;
		}
		if (!IsToolEnabled(*Tool))
		{
			OutError = FToolResult::Fail(ERuntimeToolError::ToolsetDisabled, FString::Printf(TEXT("%s is disabled by project settings"), *ToolName));
			return nullptr;
		}
		TSharedRef<FJsonObject> ArgsRef = Args.IsValid() ? Args.ToSharedRef() : MakeShared<FJsonObject>();
		FString ValidationError;
		if (!RuntimeToolSchema::ValidateAndFill(Tool->InputSchema, ArgsRef, ValidationError))
		{
			OutError = FToolResult::Fail(ERuntimeToolError::InvalidArguments, ValidationError,
				FString::Printf(TEXT("See session.describe_toolset %s"), *Tool->Toolset.ToString().ToLower()));
			return nullptr;
		}
		UWorld* World = FindWorld();
		if (Tool->bRequiresWorld && !World)
		{
			OutError = FToolResult::Fail(ERuntimeToolError::NoWorld, TEXT("no game world is running"));
			return nullptr;
		}
		return MakeShared<FToolInvocation>(Tool.ToSharedRef(), ArgsRef, Caller, World, GetSceneGraph(World), &GetHistory(Caller.SessionId), Depth);
	}

	void Execute(const TSharedRef<FToolCall>& Call)
	{
		Call->StartTime = FPlatformTime::Seconds();
		if (Call->bCancelRequested)
		{
			CompleteNow(Call, FToolResult::Fail(ERuntimeToolError::Cancelled, TEXT("cancelled before start")));
			return;
		}
		if (!Call->ArgsError.IsEmpty())
		{
			CompleteNow(Call, FToolResult::Fail(ERuntimeToolError::InvalidArguments, Call->ArgsError));
			return;
		}
		FToolResult PrepareError;
		TSharedPtr<FToolInvocation> Inv = Prepare(Call->Tool, Call->Args, Call->Caller, 0, PrepareError);
		if (!Inv.IsValid())
		{
			CompleteNow(Call, PrepareError);
			return;
		}

		Inv->OnFinished = [this, Call](const FToolResult& Result)
		{
			if (ActiveCall == Call)
			{
				ActiveCall.Reset();
				ActiveInvocation.Reset();
			}
			CompleteNow(Call, Result);
		};
		Inv->OnProgress = [Call](float Fraction, const FString& Message) { Call->OnProgress.ExecuteIfBound(Fraction, Message); };
		Inv->CancelQuery = [Call]() { return Call->bCancelRequested.load(); };

		Inv->GetTool().Handler(*Inv);

		if (!Inv->IsComplete())
		{
			if (Inv->IsDeferred())
			{
				ActiveCall = Call;
				ActiveInvocation = Inv;
			}
			else
			{
				Inv->Fail(ERuntimeToolError::Internal, TEXT("tool returned without a result"));
			}
		}
	}

	/** Returns true when no deferred call is blocking the queue. */
	bool UpdateActive()
	{
		if (!ActiveCall.IsValid())
		{
			return true;
		}
		const double Timeout = ActiveInvocation->GetTimeoutOverride().Get(ActiveCall->Caller.TimeoutSeconds.Get(URuntimeToolsetsSettings::Get().DefaultTimeoutSeconds));
		const bool bCancelled = ActiveCall->bCancelRequested;
		const bool bTimedOut = FPlatformTime::Seconds() - ActiveCall->StartTime > Timeout;
		if (bCancelled || bTimedOut)
		{
			TSharedPtr<FToolInvocation> Inv = ActiveInvocation;
			Inv->NotifyCancelled();
			Inv->Fail(bCancelled ? ERuntimeToolError::Cancelled : ERuntimeToolError::Timeout,
				bCancelled ? TEXT("cancelled") : FString::Printf(TEXT("timed out after %.0f s"), Timeout));
		}
		return !ActiveCall.IsValid();
	}

	bool Tick(float DeltaTime)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(RuntimeToolsets_Tick);
		DrainLogSubscriptions();

		if (!UpdateActive())
		{
			return true;
		}
		const double Budget = FMath::Max(0.5, static_cast<double>(URuntimeToolsetsSettings::Get().FrameBudgetMs)) / 1000.0;
		const double Start = FPlatformTime::Seconds();
		TSharedPtr<FToolCall> Call;
		while (!ActiveCall.IsValid() && Queue.Dequeue(Call))
		{
			QueuedCount.fetch_sub(1);
			Execute(Call.ToSharedRef());
			if (FPlatformTime::Seconds() - Start > Budget)
			{
				break;
			}
		}
		return true;
	}

	void DrainLogSubscriptions()
	{
		if (!LogCapture)
		{
			return;
		}
		LogCapture->DrainSubscriptions([this](const FString& SessionId, const FRuntimeLogLine& Line)
		{
			TSharedRef<FJsonObject> Event = MakeShared<FJsonObject>();
			Event->SetStringField(TEXT("type"), TEXT("log"));
			Event->SetNumberField(TEXT("seq"), static_cast<double>(Line.Seq));
			Event->SetStringField(TEXT("category"), Line.Category.ToString());
			Event->SetStringField(TEXT("verbosity"), FRuntimeLogCapture::VerbosityToString(Line.Verbosity));
			Event->SetStringField(TEXT("message"), Line.Message);
			EventDelegate.Broadcast(SessionId, Event);
		});
	}

	void OnWorldCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources)
	{
		if (!Graphs.Contains(World))
		{
			return;
		}
		for (TPair<FString, TUniquePtr<FRuntimeCommandHistory>>& Pair : Sessions)
		{
			FRuntimeCommandContext Context;
			Context.World = World;
			Context.Graph = Graphs[World].Get();
			Context.SessionId = Pair.Key;
			Context.bWorldTearingDown = true;
			Pair.Value->Clear(Context);
		}
		Graphs.Remove(World);
		UE_LOG(LogRuntimeToolsetsCore, Log, TEXT("World %s cleaned up: scene graph and histories cleared"), *World->GetName());
	}

	void ConsoleCall(const TArray<FString>& Args)
	{
		if (Args.Num() == 0)
		{
			UE_LOG(LogRuntimeToolsetsCore, Display, TEXT("Usage: RuntimeToolsets.Call <toolset.tool> [json]"));
			return;
		}
		TArray<FString> Rest = Args;
		const FString Tool = Rest[0];
		Rest.RemoveAt(0);
		FToolCallerContext Caller;
		Caller.SessionId = TEXT("console");
		Invoke(Tool, FString::Join(Rest, TEXT(" ")), Caller, FOnToolComplete::CreateLambda([Tool](const FToolResult& Result)
		{
			UE_LOG(LogRuntimeToolsetsCore, Display, TEXT("%s => %s"), *Tool, *Result.ToJsonString());
		}), FOnToolProgress());
	}

	// ---- State ----

	mutable FRWLock RegistryLock;
	TMap<FName, FToolsetEntry> Toolsets;
	TMap<FString, TSharedRef<const FRuntimeToolDescriptor>> ToolsByName;

	TQueue<TSharedPtr<FToolCall>, EQueueMode::Mpsc> Queue;
	std::atomic<int32> QueuedCount{0};
	TSharedPtr<FToolCall> ActiveCall;
	TSharedPtr<FToolInvocation> ActiveInvocation;

	TMap<FString, TUniquePtr<FRuntimeCommandHistory>> Sessions;
	TMap<UWorld*, TUniquePtr<FRuntimeSceneGraph>> Graphs;
	TWeakObjectPtr<UWorld> WorldOverride;
	TUniquePtr<FRuntimeLogCapture> LogCapture;

	FOnRuntimeToolEvent EventDelegate;
	FSimpleMulticastDelegate ToolsetsChangedDelegate;

	FTSTicker::FDelegateHandle TickHandle;
	FDelegateHandle WorldCleanupHandle;
	IConsoleObject* CallCommand = nullptr;

	int64 UsageCalls = 0;
	int64 UsageErrors = 0;
	int64 UsageBytes = 0;
	int32 UsageMaxBytes = 0;
	FString UsageMaxTool;
	TMap<FString, int64> UsageBytesPerTool;
	TMap<FString, int64> UsageCallsPerTool;
};

IMPLEMENT_MODULE(FRuntimeToolsetsCoreModule, RuntimeToolsetsCore)
