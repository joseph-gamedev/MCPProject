#include "OpsToolset.h"
#include "RuntimeTestRunner.h"
#include "RuntimeToolHelpers.h"
#include "Containers/Ticker.h"

using namespace RuntimeTools;

namespace
{
	constexpr int32 MaxBatchCalls = 100;

	// ---- run_batch -------------------------------------------------------------------------------------------------

	struct FBatchCall
	{
		FString Tool;
		TSharedPtr<FJsonObject> Arguments;
	};

	struct FBatchState
	{
		TArray<FBatchCall> Calls;
		int32 Next = 0;
		bool bAtomic = false;
		bool bStopOnError = true;
		bool bStopped = false;
		int32 FailedIndex = INDEX_NONE;
		FToolResult Failure;
		TArray<TSharedPtr<FJsonValue>> Results;
		TFunction<void()> CancelCurrent;
	};

	void FinishBatch(const TSharedRef<FToolInvocation>& Self, const TSharedRef<FBatchState>& S)
	{
		int64 EntryId = INDEX_NONE;
		const bool bFailed = S->FailedIndex != INDEX_NONE;
		if (S->bAtomic)
		{
			FRuntimeCommandContext Context = Self->MakeCommandContext();
			int32 Count = 0;
			Self->GetHistory().EndTransaction(!bFailed, Context, EntryId, Count);
		}
		if (bFailed && S->bAtomic)
		{
			const FBatchCall& Call = S->Calls[S->FailedIndex];
			Self->Fail(S->Failure.Error, FString::Printf(TEXT("call %d (%s) failed: %s; the batch was rolled back"), S->FailedIndex, *Call.Tool, *S->Failure.Message),
				S->Failure.Hint);
			return;
		}
		TSharedRef<FJsonObject> Out = Obj();
		Out->SetNumberField(TEXT("completed"), S->Results.Num());
		Out->SetNumberField(TEXT("total"), S->Calls.Num());
		if (bFailed)
		{
			Out->SetNumberField(TEXT("failed_at"), S->FailedIndex);
		}
		if (EntryId != INDEX_NONE)
		{
			Out->SetNumberField(TEXT("history_id"), static_cast<double>(EntryId));
		}
		Out->SetArrayField(TEXT("results"), S->Results);
		Self->Succeed(Out);
	}

	void RunStep(const TSharedRef<FToolInvocation>& Self, const TSharedRef<FBatchState>& S)
	{
		if (Self->IsComplete())
		{
			return;
		}
		if (S->bStopped || S->Next >= S->Calls.Num())
		{
			FinishBatch(Self, S);
			return;
		}
		const int32 Index = S->Next++;
		const FBatchCall& Call = S->Calls[Index];
		Self->ReportProgress(static_cast<float>(Index) / S->Calls.Num(), Call.Tool);
		S->CancelCurrent = IRuntimeToolsetsCore::Get().ExecuteNested(Call.Tool, Call.Arguments, Self->GetCaller(), Self->GetDepth() + 1,
			[Self, S, Index](const FToolResult& R)
			{
				TSharedRef<FJsonObject> Item = R.ToJson();
				Item->SetNumberField(TEXT("i"), Index);
				Item->SetStringField(TEXT("tool"), S->Calls[Index].Tool);
				S->Results.Add(MakeShared<FJsonValueObject>(Item));
				for (const FToolAttachment& A : R.Attachments)
				{
					FToolAttachment Copy = A;
					Self->AddAttachment(MoveTemp(Copy));
				}
				if (!R.IsOk() && S->FailedIndex == INDEX_NONE)
				{
					S->FailedIndex = Index;
					S->Failure = R;
					S->bStopped = S->bStopOnError;
				}
				RunStep(Self, S);
			});
	}

	// ---- wait_for_condition ----------------------------------------------------------------------------------------

	/** Follows "a.b[2].c" through a JSON value. */
	TSharedPtr<FJsonValue> FollowPath(const TSharedPtr<FJsonValue>& Root, const FString& Path)
	{
		TSharedPtr<FJsonValue> Cur = Root;
		TArray<FString> Parts;
		Path.ParseIntoArray(Parts, TEXT("."), true);
		for (const FString& Part : Parts)
		{
			FString Name = Part;
			TArray<int32> Indices;
			int32 Bracket;
			while (Name.FindLastChar(TEXT('['), Bracket) && Name.EndsWith(TEXT("]")))
			{
				Indices.Insert(FCString::Atoi(*Name.Mid(Bracket + 1, Name.Len() - Bracket - 2)), 0);
				Name = Name.Left(Bracket);
			}
			if (!Name.IsEmpty())
			{
				const TSharedPtr<FJsonObject>* Obj = nullptr;
				if (!Cur.IsValid() || !Cur->TryGetObject(Obj))
				{
					return nullptr;
				}
				Cur = (*Obj)->TryGetField(Name);
			}
			for (int32 I : Indices)
			{
				const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
				if (!Cur.IsValid() || !Cur->TryGetArray(Arr) || !Arr->IsValidIndex(I))
				{
					return nullptr;
				}
				Cur = (*Arr)[I];
			}
		}
		return Cur;
	}

	bool Compare(const TSharedPtr<FJsonValue>& Actual, const FString& Op, const TSharedPtr<FJsonValue>& Expected)
	{
		const bool bExists = Actual.IsValid() && !Actual->IsNull();
		if (Op == TEXT("exists")) return bExists;
		if (Op == TEXT("not_exists")) return !bExists;
		if (!bExists) return false;
		if (Op == TEXT("truthy"))
		{
			switch (Actual->Type)
			{
			case EJson::Boolean: return Actual->AsBool();
			case EJson::Number: return Actual->AsNumber() != 0.0;
			case EJson::String: return !Actual->AsString().IsEmpty();
			case EJson::Array: return Actual->AsArray().Num() > 0;
			default: return true;
			}
		}
		if (!Expected.IsValid())
		{
			return false;
		}
		if (Op == TEXT("contains"))
		{
			if (Actual->Type == EJson::String)
			{
				return Actual->AsString().Contains(Expected->AsString());
			}
			if (Actual->Type == EJson::Array)
			{
				const FString Needle = RuntimeJson::WriteValue(Expected);
				return Actual->AsArray().ContainsByPredicate([&Needle](const TSharedPtr<FJsonValue>& V) { return RuntimeJson::WriteValue(V) == Needle; });
			}
			return false;
		}
		if (Actual->Type == EJson::Number && Expected->Type == EJson::Number)
		{
			const double A = Actual->AsNumber();
			const double E = Expected->AsNumber();
			if (Op == TEXT("equals")) return FMath::IsNearlyEqual(A, E, 1e-4);
			if (Op == TEXT("not_equals")) return !FMath::IsNearlyEqual(A, E, 1e-4);
			if (Op == TEXT("gt")) return A > E;
			if (Op == TEXT("gte")) return A >= E;
			if (Op == TEXT("lt")) return A < E;
			if (Op == TEXT("lte")) return A <= E;
			return false;
		}
		const bool bEqual = RuntimeJson::WriteValue(Actual) == RuntimeJson::WriteValue(Expected);
		if (Op == TEXT("equals")) return bEqual;
		if (Op == TEXT("not_equals")) return !bEqual;
		return false;
	}

	struct FWaitState
	{
		FString Tool;
		TSharedPtr<FJsonObject> Arguments;
		FString Path;
		FString Op;
		TSharedPtr<FJsonValue> Expected;
		double Start = 0.0;
		double Timeout = 10.0;
		double Interval = 0.25;
		double NextPoll = 0.0;
		int32 Polls = 0;
		bool bPolling = false;
		TSharedPtr<FJsonValue> LastValue;
		FTSTicker::FDelegateHandle Ticker;
	};

	bool IsHardError(ERuntimeToolError E)
	{
		return E == ERuntimeToolError::UnknownTool || E == ERuntimeToolError::InvalidArguments || E == ERuntimeToolError::NotAllowed
			|| E == ERuntimeToolError::ToolsetDisabled;
	}

	void Poll(const TSharedRef<FToolInvocation>& Self, const TSharedRef<FWaitState>& S)
	{
		S->bPolling = true;
		++S->Polls;
		IRuntimeToolsetsCore::Get().ExecuteNested(S->Tool, S->Arguments, Self->GetCaller(), Self->GetDepth() + 1, [Self, S](const FToolResult& R)
		{
			S->bPolling = false;
			if (Self->IsComplete())
			{
				return;
			}
			if (!R.IsOk() && IsHardError(R.Error))
			{
				Self->Fail(R.Error, FString::Printf(TEXT("polled tool failed: %s"), *R.Message), R.Hint);
				return;
			}
			// Soft errors (not_found, ...) mean "not yet": the path is evaluated on the error object.
			const TSharedPtr<FJsonValue> Root = MakeShared<FJsonValueObject>(R.ToJson());
			S->LastValue = S->Path.IsEmpty() ? Root : FollowPath(Root, S->Path);
			if (Compare(S->LastValue, S->Op, S->Expected))
			{
				TSharedRef<FJsonObject> Out = Obj();
				Out->SetBoolField(TEXT("met"), true);
				Out->SetNumberField(TEXT("polls"), S->Polls);
				Out->SetNumberField(TEXT("waited_s"), RuntimeJson::Round(FPlatformTime::Seconds() - S->Start));
				if (S->LastValue.IsValid())
				{
					Out->SetField(TEXT("value"), S->LastValue);
				}
				Self->Succeed(Out);
				return;
			}
			if (FPlatformTime::Seconds() - S->Start >= S->Timeout)
			{
				Self->Fail(ERuntimeToolError::Timeout, FString::Printf(TEXT("condition not met after %.1f s (%d polls); last value: %s"), S->Timeout, S->Polls,
					*RuntimeJson::WriteValue(S->LastValue)));
			}
		});
	}
}

static void RunBatch(FToolInvocation& Inv)
{
	TSharedRef<FBatchState> S = MakeShared<FBatchState>();
	const TArray<TSharedPtr<FJsonValue>>* Calls = nullptr;
	Inv.GetArgs()->TryGetArrayField(TEXT("calls"), Calls);
	for (int32 i = 0; Calls && i < Calls->Num(); ++i)
	{
		const TSharedPtr<FJsonObject> C = (*Calls)[i]->AsObject();
		FBatchCall Call;
		if (!C.IsValid() || !C->TryGetStringField(TEXT("tool"), Call.Tool))
		{
			Inv.Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("calls[%d] needs a tool name"), i));
			return;
		}
		if (Call.Tool.StartsWith(TEXT("ops.run_batch")) || Call.Tool.StartsWith(TEXT("ops_run_batch")))
		{
			Inv.Fail(ERuntimeToolError::NotAllowed, TEXT("batches cannot contain batches"));
			return;
		}
		const TSharedPtr<FJsonObject>* Args = nullptr;
		Call.Arguments = C->TryGetObjectField(TEXT("arguments"), Args) ? *Args : MakeShared<FJsonObject>();
		S->Calls.Add(Call);
	}
	if (S->Calls.Num() == 0)
	{
		Inv.Fail(ERuntimeToolError::InvalidArguments, TEXT("calls is empty"));
		return;
	}
	S->bAtomic = Inv.GetBool(TEXT("atomic"), false);
	S->bStopOnError = S->bAtomic || Inv.GetBool(TEXT("stop_on_error"), true);
	if (S->bAtomic)
	{
		Inv.GetHistory().BeginTransaction(Inv.GetString(TEXT("label"), TEXT("batch")));
	}
	TSharedRef<FToolInvocation> Self = Inv.AsShared();
	RunStep(Self, S);
	if (!Inv.IsComplete())
	{
		Inv.SetCancelHandler([Self, S]()
		{
			if (S->CancelCurrent)
			{
				S->CancelCurrent();
			}
			if (S->bAtomic)
			{
				FRuntimeCommandContext Context = Self->MakeCommandContext();
				int64 Id;
				int32 Count;
				Self->GetHistory().EndTransaction(false, Context, Id, Count);
			}
		});
		Inv.SetTimeout(600.0);
		Inv.Defer();
	}
}

static void WaitForCondition(FToolInvocation& Inv)
{
	TSharedRef<FWaitState> S = MakeShared<FWaitState>();
	S->Tool = Inv.GetString(TEXT("tool"));
	S->Arguments = Inv.GetObject(TEXT("arguments"));
	S->Path = Inv.GetString(TEXT("path"));
	S->Op = Inv.GetString(TEXT("op"), TEXT("truthy"));
	S->Expected = Inv.GetValue(TEXT("value"));
	S->Timeout = Inv.GetNumber(TEXT("timeout_s"), 10.0);
	S->Interval = Inv.GetNumber(TEXT("interval_s"), 0.25);
	S->Start = FPlatformTime::Seconds();
	const bool bNeedsValue = S->Op != TEXT("exists") && S->Op != TEXT("not_exists") && S->Op != TEXT("truthy");
	if (bNeedsValue && !S->Expected.IsValid())
	{
		Inv.Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("op '%s' needs a value"), *S->Op));
		return;
	}
	if (S->Tool.StartsWith(TEXT("ops.")) || S->Tool.StartsWith(TEXT("ops_")))
	{
		Inv.Fail(ERuntimeToolError::NotAllowed, TEXT("cannot poll ops tools"));
		return;
	}
	TSharedRef<FToolInvocation> Self = Inv.AsShared();
	Poll(Self, S); // first check right away
	if (Inv.IsComplete())
	{
		return;
	}
	S->NextPoll = FPlatformTime::Seconds() + S->Interval;
	S->Ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Self, S](float)
	{
		if (Self->IsComplete())
		{
			return false;
		}
		const double Now = FPlatformTime::Seconds();
		if (!S->bPolling && Now >= S->NextPoll)
		{
			S->NextPoll = Now + S->Interval;
			Poll(Self, S);
		}
		return !Self->IsComplete();
	}));
	Inv.SetCancelHandler([S]() { FTSTicker::GetCoreTicker().RemoveTicker(S->Ticker); });
	Inv.SetTimeout(S->Timeout + 5.0);
	Inv.Defer();
}

static void Wait(FToolInvocation& Inv)
{
	const double Seconds = Inv.GetNumber(TEXT("seconds"), 1.0);
	const double End = FPlatformTime::Seconds() + Seconds;
	TSharedRef<FToolInvocation> Self = Inv.AsShared();
	FTSTicker::FDelegateHandle Handle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Self, End, Seconds](float)
	{
		if (Self->IsComplete())
		{
			return false;
		}
		if (FPlatformTime::Seconds() < End)
		{
			return true;
		}
		TSharedRef<FJsonObject> Out = Obj();
		Out->SetNumberField(TEXT("waited_s"), RuntimeJson::Round(Seconds));
		Self->Succeed(Out);
		return false;
	}));
	Inv.SetCancelHandler([Handle]() { FTSTicker::GetCoreTicker().RemoveTicker(Handle); });
	Inv.SetTimeout(Seconds + 5.0);
	Inv.Defer();
}

static void ListTests(FToolInvocation& Inv)
{
	const TArray<FString> All = FRuntimeTestRunner::ListTests(Inv.GetString(TEXT("filter")));
	TArray<FString> Shown = All;
	const int32 Limit = Inv.GetLimit();
	if (Shown.Num() > Limit)
	{
		Shown.SetNum(Limit);
	}
	TSharedRef<FJsonObject> Out = Obj();
	Out->SetNumberField(TEXT("total"), All.Num());
	Out->SetField(TEXT("tests"), RuntimeJson::Strings(Shown));
	Inv.Succeed(Out);
}

static void RunAutomationTest(FToolInvocation& Inv)
{
	FString Error;
	const FString Filter = Inv.GetString(TEXT("filter"));
	if (!FRuntimeTestRunner::Get().Start(Filter, false, Error))
	{
		Inv.Fail(FRuntimeTestRunner::Get().IsRunning() ? ERuntimeToolError::Busy : ERuntimeToolError::NotFound, Error);
		return;
	}
	Inv.RecordIrreversible(FString::Printf(TEXT("run tests %s"), *Filter));
	TSharedRef<FJsonObject> Out = FRuntimeTestRunner::Get().GetStatus();
	Out->SetStringField(TEXT("note"), TEXT("tests run over the next frames; poll ops.get_test_status and avoid other world changes meanwhile"));
	Inv.Succeed(Out);
}

static void GetTestStatus(FToolInvocation& Inv)
{
	Inv.Succeed(FRuntimeTestRunner::Get().GetStatus(static_cast<int32>(Inv.GetInt(TEXT("max_failures"), 20))));
}

void FOpsToolset::RegisterTools(FRuntimeToolsetBuilder& B)
{
	TSharedRef<FJsonObject> CallSchema = FToolSchema()
		.Add(TEXT("tool"), FToolParam::String(TEXT("Tool name"), 128).Required())
		.Add(TEXT("arguments"), FToolParam::Object(TEXT("Arguments")))
		.Build();
	FToolParam CallParam;
	CallParam.Json = CallSchema;

	B.Tool(TEXT("run_batch"), TEXT("Run several tool calls in order in one request. atomic = all-or-nothing, one undo step."))
		.Args(FToolSchema()
			.Add(TEXT("calls"), FToolParam::Array(TEXT("[{tool, arguments}]"), CallParam, MaxBatchCalls).Required())
			.Add(TEXT("atomic"), FToolParam::Boolean(TEXT("Roll everything back if a call fails; one undo step on success")).Default(false))
			.Add(TEXT("stop_on_error"), FToolParam::Boolean(TEXT("Stop at the first failure (always true when atomic)")).Default(true))
			.Add(TEXT("label"), FToolParam::String(TEXT("History label for atomic batches"), 128)))
		.Undoable()
		.Handle(&RunBatch);

	B.Tool(TEXT("wait_for_condition"), TEXT("Poll a tool until a field of its result meets a condition (or time out)."))
		.Args(FToolSchema()
			.Add(TEXT("tool"), FToolParam::String(TEXT("Tool to poll, e.g. scene.get_node"), 128).Required())
			.Add(TEXT("arguments"), FToolParam::Object(TEXT("Arguments for the polled tool")))
			.Add(TEXT("path"), FToolParam::String(TEXT("Field in the result, e.g. location[2] or level.visible (empty = whole result)"), 256))
			.Add(TEXT("op"), FToolParam::Enum(TEXT("Comparison"), {TEXT("truthy"), TEXT("exists"), TEXT("not_exists"), TEXT("equals"), TEXT("not_equals"), TEXT("gt"), TEXT("gte"), TEXT("lt"), TEXT("lte"), TEXT("contains")}).Default(TEXT("truthy")))
			.Add(TEXT("value"), FToolParam::Any(TEXT("Value to compare with")))
			.Add(TEXT("timeout_s"), FToolParam::Number(TEXT("Give up after"), 0.05, 300.0).Default(10.0))
			.Add(TEXT("interval_s"), FToolParam::Number(TEXT("Seconds between polls"), 0.01, 10.0).Default(0.25)))
		.Handle(&WaitForCondition);

	B.Tool(TEXT("wait"), TEXT("Let the game run for a number of seconds (real time)."))
		.Args(FToolSchema().Add(TEXT("seconds"), FToolParam::Number(TEXT("Seconds"), 0.0, 120.0).Required()))
		.Async()
		.Handle(&Wait);

	B.Tool(TEXT("list_tests"), TEXT("Automation tests available in this build (filter by path prefix)."))
		.Args(FToolSchema().Add(TEXT("filter"), FToolParam::String(TEXT("Path prefix, e.g. RuntimeToolsets."), 256)).Add(TEXT("limit"), FToolParam::Limit()))
		.Handle(&ListTests);

	B.Tool(TEXT("run_automation_test"), TEXT("Start automation tests in-process (also in packaged builds). Poll get_test_status."))
		.Args(FToolSchema().Add(TEXT("filter"), FToolParam::String(TEXT("Path prefix, e.g. RuntimeToolsets.Toolsets.Actors"), 256).Required()))
		.Irreversible()
		.Handle(&RunAutomationTest);

	B.Tool(TEXT("get_test_status"), TEXT("Progress and failures of the current/last test run."))
		.Args(FToolSchema().Add(TEXT("max_failures"), FToolParam::Integer(TEXT("Failures to include"), 0, 200).Default(20)))
		.Handle(&GetTestStatus);
}
