#include "RuntimeOpsToolset.h"
#include "OpsTestRunner.h"
#include "RuntimeCommandHistory.h"
#include "RuntimeJson.h"
#include "RuntimeTestRunner.h"
#include "RuntimeToolInvocation.h"
#include "RuntimeToolReflection.h"
#include "RuntimeToolsetsCore.h"
#include "RuntimeToolsetsSettings.h"
#include "Containers/Ticker.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	constexpr int32 MaxBatchCalls = 100;

	// ---- JSON paths ------------------------------------------------------------------------------------------------

	/** Follows "a.b[2].c" through a JSON value. */
	TSharedPtr<FJsonValue> OpsFollowPath(const TSharedPtr<FJsonValue>& Root, const FString& Path)
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

	/** A field of a tool result; reflected tools wrap results in returnValue, so its fields are found directly too. */
	TSharedPtr<FJsonValue> OpsResultField(const TSharedPtr<FJsonObject>& Root, const FString& Path)
	{
		if (!Root.IsValid())
		{
			return nullptr;
		}
		if (Path.IsEmpty())
		{
			// "$0" -> the returnValue of a reflected tool (else the whole result).
			const TSharedPtr<FJsonValue> RetValue = Root->TryGetField(TEXT("returnValue"));
			return RetValue.IsValid() ? RetValue : TSharedPtr<FJsonValue>(MakeShared<FJsonValueObject>(Root));
		}
		TSharedPtr<FJsonValue> V = OpsFollowPath(MakeShared<FJsonValueObject>(Root), Path);
		const TSharedPtr<FJsonObject>* Ret = nullptr;
		if ((!V.IsValid() || V->IsNull()) && Root->TryGetObjectField(TEXT("returnValue"), Ret))
		{
			V = OpsFollowPath(MakeShared<FJsonValueObject>(*Ret), Path);
		}
		return V;
	}

	bool OpsCompare(const TSharedPtr<FJsonValue>& Actual, EOpsToolCompareOp Op, const TSharedPtr<FJsonValue>& Expected)
	{
		const bool bExists = Actual.IsValid() && !Actual->IsNull();
		if (Op == EOpsToolCompareOp::Exists) return bExists;
		if (Op == EOpsToolCompareOp::NotExists) return !bExists;
		if (!bExists) return false;
		if (Op == EOpsToolCompareOp::Truthy)
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
		if (Op == EOpsToolCompareOp::Contains)
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
			switch (Op)
			{
			case EOpsToolCompareOp::Equals: return FMath::IsNearlyEqual(A, E, 1e-4);
			case EOpsToolCompareOp::NotEquals: return !FMath::IsNearlyEqual(A, E, 1e-4);
			case EOpsToolCompareOp::Gt: return A > E;
			case EOpsToolCompareOp::Gte: return A >= E;
			case EOpsToolCompareOp::Lt: return A < E;
			case EOpsToolCompareOp::Lte: return A <= E;
			default: return false;
			}
		}
		const bool bEqual = RuntimeJson::WriteValue(Actual) == RuntimeJson::WriteValue(Expected);
		if (Op == EOpsToolCompareOp::Equals) return bEqual;
		if (Op == EOpsToolCompareOp::NotEquals) return !bEqual;
		return false;
	}

	// ---- run_batch -------------------------------------------------------------------------------------------------

	/** "$3.id" -> (3, "id"). False for plain strings and "$$" escapes. */
	bool OpsParseReference(const FString& S, int32& OutIndex, FString& OutPath)
	{
		if (S.Len() < 2 || S[0] != TEXT('$') || !FChar::IsDigit(S[1]))
		{
			return false;
		}
		int32 i = 1;
		while (i < S.Len() && FChar::IsDigit(S[i]))
		{
			++i;
		}
		const FString Rest = S.Mid(i);
		if (!Rest.IsEmpty() && Rest[0] != TEXT('.') && Rest[0] != TEXT('['))
		{
			return false; // "$5abc" is plain text
		}
		OutIndex = FCString::Atoi(*S.Mid(1, i - 1));
		OutPath = Rest.StartsWith(TEXT(".")) ? Rest.Mid(1) : Rest;
		return true;
	}

	/** Checks that every reference in Value points to an earlier call. */
	bool OpsCheckReferences(const TSharedPtr<FJsonValue>& Value, int32 CallIndex, FString& OutError)
	{
		if (!Value.IsValid())
		{
			return true;
		}
		int32 N = 0;
		FString Path;
		if (Value->Type == EJson::String && OpsParseReference(Value->AsString(), N, Path) && N >= CallIndex)
		{
			OutError = FString::Printf(TEXT("calls[%d]: '%s' refers to call %d, which does not run before it"), CallIndex, *Value->AsString(), N);
			return false;
		}
		if (Value->Type == EJson::Array)
		{
			for (const TSharedPtr<FJsonValue>& V : Value->AsArray())
			{
				if (!OpsCheckReferences(V, CallIndex, OutError))
				{
					return false;
				}
			}
		}
		else if (Value->Type == EJson::Object)
		{
			for (const TPair<FString, TSharedPtr<FJsonValue>>& KV : Value->AsObject()->Values)
			{
				if (!OpsCheckReferences(KV.Value, CallIndex, OutError))
				{
					return false;
				}
			}
		}
		return true;
	}

	struct FOpsBatchCall
	{
		FString Tool;
		TSharedPtr<FJsonObject> Arguments;
	};

	struct FOpsBatchState
	{
		TArray<FOpsBatchCall> Calls;
		int32 Next = 0;
		bool bAtomic = false;
		bool bStopOnError = true;
		bool bCompact = false;   // successful calls report only {i, tool, ok} (recipes: hundreds of calls)
		bool bStopped = false;
		bool bDone = false;
		int32 FailedIndex = INDEX_NONE;
		FToolResult Failure;
		FOpsToolBatchResult Value;
		/** Payload and success of each call that ran (for "$N" references). */
		TArray<TSharedPtr<FJsonObject>> Payloads;
		TArray<bool> Succeeded;
		TFunction<void()> CancelCurrent;
		TSharedPtr<FToolInvocation> Inv;
		TWeakObjectPtr<UOpsToolBatchAsyncResult> Result;
	};

	/** Replaces "$N.path" strings with values from earlier results ("$$" -> "$"). */
	TSharedPtr<FJsonValue> OpsSubstitute(const TSharedPtr<FJsonValue>& Value, const FOpsBatchState& S, FString& OutError)
	{
		if (!Value.IsValid() || !OutError.IsEmpty())
		{
			return Value;
		}
		if (Value->Type == EJson::String)
		{
			const FString Str = Value->AsString();
			if (Str.StartsWith(TEXT("$$")))
			{
				return MakeShared<FJsonValueString>(Str.Mid(1));
			}
			int32 N = 0;
			FString Path;
			if (!OpsParseReference(Str, N, Path))
			{
				return Value;
			}
			if (!S.Succeeded.IsValidIndex(N) || !S.Succeeded[N])
			{
				OutError = FString::Printf(TEXT("'%s' refers to call %d, which %s"), *Str, N, S.Succeeded.IsValidIndex(N) ? TEXT("failed") : TEXT("has not run"));
				return Value;
			}
			const TSharedPtr<FJsonValue> Found = OpsResultField(S.Payloads[N], Path);
			if (!Found.IsValid())
			{
				TArray<FString> Keys = RuntimeJson::Keys(S.Payloads[N]);
				const TSharedPtr<FJsonObject>* Ret = nullptr;
				if (S.Payloads[N].IsValid() && S.Payloads[N]->TryGetObjectField(TEXT("returnValue"), Ret))
				{
					Keys.Append(RuntimeJson::Keys(*Ret));
				}
				OutError = FString::Printf(TEXT("'%s': call %d (%s) has no field '%s' (fields: %s)"), *Str, N, *S.Calls[N].Tool, *Path,
					*FString::Join(Keys, TEXT(", ")));
				return Value;
			}
			return Found;
		}
		if (Value->Type == EJson::Array)
		{
			TArray<TSharedPtr<FJsonValue>> Out;
			for (const TSharedPtr<FJsonValue>& V : Value->AsArray())
			{
				Out.Add(OpsSubstitute(V, S, OutError));
			}
			return MakeShared<FJsonValueArray>(Out);
		}
		if (Value->Type == EJson::Object)
		{
			TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
			for (const TPair<FString, TSharedPtr<FJsonValue>>& KV : Value->AsObject()->Values)
			{
				Out->SetField(KV.Key, OpsSubstitute(KV.Value, S, OutError));
			}
			return MakeShared<FJsonValueObject>(Out);
		}
		return Value;
	}

	void OpsFinishBatch(const TSharedRef<FOpsBatchState>& S)
	{
		if (S->bDone)
		{
			return;
		}
		S->bDone = true;
		S->CancelCurrent = nullptr;
		const TSharedPtr<FToolInvocation> Inv = S->Inv;
		S->Inv.Reset();
		const bool bFailed = S->FailedIndex != INDEX_NONE;
		int64 EntryId = INDEX_NONE;
		if (S->bAtomic && Inv.IsValid())
		{
			FRuntimeCommandContext Context = Inv->MakeCommandContext();
			int32 Count = 0;
			Inv->GetHistory().EndTransaction(!bFailed, Context, EntryId, Count);
		}
		UOpsToolBatchAsyncResult* Result = S->Result.Get();
		if (!Result)
		{
			return;
		}
		if (bFailed && S->bAtomic)
		{
			FString Message = FString::Printf(TEXT("call %d (%s) failed: %s; the batch was rolled back"), S->FailedIndex, *S->Calls[S->FailedIndex].Tool,
				*S->Failure.Message);
			if (!S->Failure.Hint.IsEmpty())
			{
				Message += TEXT(". Hint: ") + S->Failure.Hint;
			}
			Result->SetError(Message, S->Failure.Error);
			return;
		}
		S->Value.Completed = S->Value.Results.Num();
		S->Value.Total = S->Calls.Num();
		S->Value.FailedAt = S->FailedIndex;
		S->Value.HistoryId = EntryId;
		Result->SetValue(S->Value);
	}

	void OpsRunStep(const TSharedRef<FOpsBatchState>& S);

	void OpsOnCallDone(const TSharedRef<FOpsBatchState>& S, int32 Index, const FToolResult& R)
	{
		if (S->bDone)
		{
			return;
		}
		TSharedRef<FJsonObject> Item = (S->bCompact && R.IsOk()) ? MakeShared<FJsonObject>() : R.ToJson();
		if (S->bCompact && R.IsOk())
		{
			Item->SetBoolField(TEXT("ok"), true);
		}
		Item->SetNumberField(TEXT("i"), Index);
		Item->SetStringField(TEXT("tool"), S->Calls[Index].Tool);
		S->Value.Results.Add(FRuntimeToolJson::Make(Item));
		S->Payloads.Add(R.Payload);
		S->Succeeded.Add(R.IsOk());
		if (S->Inv.IsValid())
		{
			for (const FToolAttachment& A : R.Attachments)
			{
				FToolAttachment Copy = A;
				S->Inv->AddAttachment(MoveTemp(Copy));
			}
		}
		if (!R.IsOk() && S->FailedIndex == INDEX_NONE)
		{
			S->FailedIndex = Index;
			S->Failure = R;
			S->bStopped = S->bStopOnError;
		}
		OpsRunStep(S);
	}

	void OpsRunStep(const TSharedRef<FOpsBatchState>& S)
	{
		if (S->bDone || !S->Inv.IsValid())
		{
			return;
		}
		if (S->bStopped || S->Next >= S->Calls.Num())
		{
			OpsFinishBatch(S);
			return;
		}
		const int32 Index = S->Next++;
		const FString Tool = S->Calls[Index].Tool;
		FString RefError;
		const TSharedPtr<FJsonValue> Args = OpsSubstitute(MakeShared<FJsonValueObject>(S->Calls[Index].Arguments), *S, RefError);
		if (!RefError.IsEmpty())
		{
			OpsOnCallDone(S, Index, FToolResult::Fail(ERuntimeToolError::InvalidArguments, RefError, TEXT("Reference earlier calls that succeed, e.g. \"$0.id\"")));
			return;
		}
		S->Inv->ReportProgress(static_cast<float>(Index) / S->Calls.Num(), Tool);
		TFunction<void()> Cancel = IRuntimeToolsetsCore::Get().ExecuteNested(Tool, Args->AsObject(), S->Inv->GetCaller(), S->Inv->GetDepth() + 1,
			[S, Index](const FToolResult& R) { OpsOnCallDone(S, Index, R); });
		if (!S->bDone && S->Value.Results.Num() <= Index)
		{
			S->CancelCurrent = MoveTemp(Cancel); // still running (async tool)
		}
	}

	// ---- wait_for_condition ----------------------------------------------------------------------------------------

	struct FOpsWaitState
	{
		FString Tool;
		TSharedPtr<FJsonObject> Arguments;
		FString Path;
		EOpsToolCompareOp Op = EOpsToolCompareOp::Truthy;
		TSharedPtr<FJsonValue> Expected;
		double Start = 0.0;
		double Timeout = 10.0;
		double Interval = 0.25;
		double NextPoll = 0.0;
		int32 Polls = 0;
		bool bPolling = false;
		bool bDone = false;
		TSharedPtr<FJsonValue> LastValue;
		FTSTicker::FDelegateHandle Ticker;
		FToolCallerContext Caller;
		int32 Depth = 0;
		TWeakObjectPtr<UOpsToolConditionAsyncResult> Result;
	};

	bool OpsIsHardError(ERuntimeToolError E)
	{
		return E == ERuntimeToolError::UnknownTool || E == ERuntimeToolError::InvalidArguments || E == ERuntimeToolError::NotAllowed
			|| E == ERuntimeToolError::ToolsetDisabled;
	}

	void OpsPoll(const TSharedRef<FOpsWaitState>& S)
	{
		S->bPolling = true;
		++S->Polls;
		IRuntimeToolsetsCore::Get().ExecuteNested(S->Tool, S->Arguments, S->Caller, S->Depth, [S](const FToolResult& R)
		{
			S->bPolling = false;
			UOpsToolConditionAsyncResult* Result = S->Result.Get();
			if (S->bDone || !Result)
			{
				return;
			}
			if (!R.IsOk() && OpsIsHardError(R.Error))
			{
				S->bDone = true;
				Result->SetError(FString::Printf(TEXT("polled tool failed: %s%s%s"), *R.Message, R.Hint.IsEmpty() ? TEXT("") : TEXT(". Hint: "), *R.Hint), R.Error);
				return;
			}
			// Soft errors (not_found, ...) mean "not yet": the path is evaluated on the error result.
			const TSharedRef<FJsonObject> Root = R.ToJson();
			S->LastValue = S->Path.IsEmpty() ? TSharedPtr<FJsonValue>(MakeShared<FJsonValueObject>(Root)) : OpsResultField(Root, S->Path);
			if (OpsCompare(S->LastValue, S->Op, S->Expected))
			{
				S->bDone = true;
				FOpsToolConditionResult V;
				V.bMet = true;
				V.Polls = S->Polls;
				V.WaitedS = static_cast<float>(RuntimeJson::Round(FPlatformTime::Seconds() - S->Start));
				V.Value = FRuntimeToolJson::Make(S->LastValue);
				Result->SetValue(V);
				return;
			}
			if (FPlatformTime::Seconds() - S->Start >= S->Timeout)
			{
				S->bDone = true;
				Result->SetError(FString::Printf(TEXT("condition not met after %.1f s (%d polls); last value: %s"), S->Timeout, S->Polls,
					*RuntimeJson::WriteValue(S->LastValue)), ERuntimeToolError::Timeout);
			}
		});
	}

	// ---- tests -----------------------------------------------------------------------------------------------------

	bool OpsParseFilter(const FString& Text, FOpsTestFilter& Out)
	{
		FString Err;
		if (!Out.Parse(Text, Err))
		{
			RuntimeToolCall::Fail(ERuntimeToolError::NotFound, Err, TEXT("Use StartsWith:, ^, $, Group: or a substring, joined with +"));
			return false;
		}
		return true;
	}

	FOpsToolTestStatus OwnStatus(int32 MaxFailures)
	{
		const FOpsTestRunner& R = FOpsTestRunner::Get();
		FOpsToolTestStatus S;
		S.bRunning = R.IsRunning();
		S.Filter = R.GetFilter();
		S.Total = R.GetTotal();
		S.Current = R.GetCurrent();
		for (const FOpsToolTestResult& T : R.GetResults())
		{
			if (T.State == EOpsToolTestState::Passed)
			{
				++S.Passed;
			}
			else if (T.State == EOpsToolTestState::Failed || T.State == EOpsToolTestState::Stopped)
			{
				++S.Failed;
				if (S.Failures.Num() < MaxFailures)
				{
					FOpsToolTestFailure& F = S.Failures.AddDefaulted_GetRef();
					F.Test = T.Test;
					F.Errors = T.Errors;
					if (F.Errors.Num() > 5)
					{
						F.Errors.SetNum(5);
					}
				}
			}
		}
		S.Completed = S.Passed + S.Failed;
		return S;
	}

	/** Status of the core runner (packaged smoke runs started from the command line). */
	FOpsToolTestStatus CoreStatus(int32 MaxFailures)
	{
		FOpsToolTestStatus S;
		RuntimeToolReflection::FConvertContext Ctx;
		FString Err;
		RuntimeToolReflection::JsonToStruct(FRuntimeTestRunner::Get().GetStatus(MaxFailures), FOpsToolTestStatus::StaticStruct(), &S, Ctx, Err);
		return S;
	}

	bool OpsAnyRunInProgress()
	{
		return FRuntimeTestRunner::Get().IsRunning() || FOpsTestRunner::Get().IsRunning();
	}
}

// ---- batch ----------------------------------------------------------------------------------------------------------

static UOpsToolBatchAsyncResult* OpsStartBatch(const TArray<FOpsToolBatchCall>& Calls, bool bAtomic, bool bStopOnError, const FString& Label, int32 MaxCalls,
	bool bCompact = false)
{
	if (Calls.Num() == 0 || Calls.Num() > MaxCalls)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments,
			Calls.Num() == 0 ? FString(TEXT("calls is empty")) : FString::Printf(TEXT("at most %d calls per batch (got %d)"), MaxCalls, Calls.Num()),
			Calls.Num() == 0 ? TEXT("Pass [{\"tool\": \"...\", \"arguments\": {...}}]") : TEXT("Split the work into several batches"));
		return nullptr;
	}
	TSharedRef<FOpsBatchState> S = MakeShared<FOpsBatchState>();
	for (int32 i = 0; i < Calls.Num(); ++i)
	{
		FOpsBatchCall Call;
		Call.Tool = Calls[i].Tool.TrimStartAndEnd();
		if (Call.Tool.IsEmpty())
		{
			RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("calls[%d] needs a tool name"), i));
			return nullptr;
		}
		if (Call.Tool.StartsWith(TEXT("ops.run_batch")) || Call.Tool.StartsWith(TEXT("ops_run_batch")))
		{
			RuntimeToolCall::Fail(ERuntimeToolError::NotAllowed, TEXT("batches cannot contain batches"), TEXT("Put all calls in one batch"));
			return nullptr;
		}
		if (Calls[i].Arguments.IsSet() && !Calls[i].Arguments.AsObject().IsValid())
		{
			RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("calls[%d].arguments must be an object"), i));
			return nullptr;
		}
		Call.Arguments = Calls[i].Arguments.IsSet() ? Calls[i].Arguments.AsObject() : MakeShared<FJsonObject>();
		FString RefError;
		if (!OpsCheckReferences(MakeShared<FJsonValueObject>(Call.Arguments), i, RefError))
		{
			RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, RefError, TEXT("\"$N\" can only refer to calls before this one (0-based)"));
			return nullptr;
		}
		S->Calls.Add(Call);
	}
	UOpsToolBatchAsyncResult* Result = NewObject<UOpsToolBatchAsyncResult>();
	S->Result = Result;
	S->Inv = RuntimeToolCall::Current()->AsShared();
	S->bAtomic = bAtomic;
	S->bCompact = bCompact;
	S->bStopOnError = bAtomic || bStopOnError;
	if (bAtomic)
	{
		S->Inv->GetHistory().BeginTransaction(Label.IsEmpty() ? FString(TEXT("batch")) : Label);
	}
	Result->OnCancelled = [S]()
	{
		if (S->bDone)
		{
			return;
		}
		S->bDone = true;
		if (S->CancelCurrent)
		{
			S->CancelCurrent();
		}
		if (S->bAtomic && S->Inv.IsValid())
		{
			FRuntimeCommandContext Context = S->Inv->MakeCommandContext();
			int64 Id = INDEX_NONE;
			int32 Count = 0;
			S->Inv->GetHistory().EndTransaction(false, Context, Id, Count);
		}
		S->CancelCurrent = nullptr;
		S->Inv.Reset();
	};
	RuntimeToolCall::SetTimeout(600.0);
	OpsRunStep(S);
	return Result;
}

UOpsToolBatchAsyncResult* URuntimeOpsToolset::RunBatch(const TArray<FOpsToolBatchCall>& Calls, bool bAtomic, bool bStopOnError, const FString& Label)
{
	return OpsStartBatch(Calls, bAtomic, bStopOnError, Label, MaxBatchCalls);
}

// ---- recipes ----------------------------------------------------------------------------------------------------------

// recipes made by Scripts/recipes/make_recipes.py (sea_coast from the FluidNinja LIVE demo harvest)
#include "Generated/OpsRecipes.gen.inl"

namespace
{
	TSharedPtr<FJsonObject> OpsRecipes()
	{
		static TSharedPtr<FJsonObject> Parsed;
		if (!Parsed.IsValid())
		{
			FString Text;
			for (const TCHAR* Part : GOpsRecipesJsonParts)
			{
				Text += Part;
			}
			TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(Text);
			if (!FJsonSerializer::Deserialize(R, Parsed) || !Parsed.IsValid())
			{
				Parsed = MakeShared<FJsonObject>();
			}
		}
		return Parsed;
	}
}

TArray<FOpsToolRecipe> URuntimeOpsToolset::ListRecipes()
{
	TArray<FOpsToolRecipe> Out;
	for (const TPair<FString, TSharedPtr<FJsonValue>>& KV : OpsRecipes()->Values)
	{
		const TSharedPtr<FJsonObject> R = KV.Value->AsObject();
		FOpsToolRecipe& O = Out.AddDefaulted_GetRef();
		O.Name = KV.Key;
		O.About = R->GetStringField(TEXT("about"));
		TSet<FString> Parts;
		for (const TSharedPtr<FJsonValue>& C : R->GetArrayField(TEXT("calls")))
		{
			FString Part;
			if (C->AsObject()->TryGetStringField(TEXT("part"), Part))
			{
				Parts.Add(Part);
			}
		}
		O.Parts = Parts.Array();
		O.Calls = R->GetArrayField(TEXT("calls")).Num();
	}
	return Out;
}

UOpsToolBatchAsyncResult* URuntimeOpsToolset::RunRecipe(const FString& Name, const TArray<FString>& Skip)
{
	const TSharedPtr<FJsonObject>* R = nullptr;
	if (!OpsRecipes()->TryGetObjectField(Name, R))
	{
		TArray<FString> Names;
		for (const TPair<FString, TSharedPtr<FJsonValue>>& KV : OpsRecipes()->Values)
		{
			Names.Add(KV.Key);
		}
		RuntimeToolCall::Fail(ERuntimeToolError::NotFound, FString::Printf(TEXT("no recipe '%s' (%s)"), *Name, *FString::Join(Names, TEXT(", "))),
			TEXT("ops.list_recipes lists them"));
		return nullptr;
	}
	TArray<FOpsToolBatchCall> Calls;
	for (const TSharedPtr<FJsonValue>& V : (*R)->GetArrayField(TEXT("calls")))
	{
		const TSharedPtr<FJsonObject> C = V->AsObject();
		FString Part;
		if (C->TryGetStringField(TEXT("part"), Part) && Skip.Contains(Part))
		{
			continue;
		}
		FOpsToolBatchCall& Call = Calls.AddDefaulted_GetRef();
		Call.Tool = C->GetStringField(TEXT("tool"));
		const TSharedPtr<FJsonObject>* Args = nullptr;
		Call.Arguments = FRuntimeToolJson::Make(MakeShared<FJsonValueObject>(C->TryGetObjectField(TEXT("arguments"), Args) ? *Args : MakeShared<FJsonObject>()));
	}
	return OpsStartBatch(Calls, false, false, TEXT("recipe ") + Name, 2000, true);
}

// ---- waiting --------------------------------------------------------------------------------------------------------

UOpsToolConditionAsyncResult* URuntimeOpsToolset::WaitForCondition(const FString& Tool, const FRuntimeToolJson& Arguments, const FRuntimeToolJson& Value,
	const FString& Path, EOpsToolCompareOp Op, float TimeoutS, float IntervalS)
{
	const bool bNeedsValue = Op != EOpsToolCompareOp::Exists && Op != EOpsToolCompareOp::NotExists && Op != EOpsToolCompareOp::Truthy;
	if (bNeedsValue && !Value.IsSet())
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments,
			FString::Printf(TEXT("op '%s' needs a value"), *StaticEnum<EOpsToolCompareOp>()->GetNameStringByValue(static_cast<int64>(Op))),
			TEXT("Pass value, or use truthy / exists / not_exists"));
		return nullptr;
	}
	if (TimeoutS < 0.05f || TimeoutS > 300.f || IntervalS < 0.01f || IntervalS > 10.f)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments,
			FString::Printf(TEXT("timeout_s must be 0.05-300 and interval_s 0.01-10 (got %g, %g)"), TimeoutS, IntervalS));
		return nullptr;
	}
	if (Tool.StartsWith(TEXT("ops.")) || Tool.StartsWith(TEXT("ops_")))
	{
		RuntimeToolCall::Fail(ERuntimeToolError::NotAllowed, TEXT("cannot poll ops tools"), TEXT("Poll the tool that reports the state instead"));
		return nullptr;
	}
	if (Arguments.IsSet() && !Arguments.AsObject().IsValid())
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, TEXT("arguments must be an object"));
		return nullptr;
	}
	FToolInvocation* Inv = RuntimeToolCall::Current();
	TSharedRef<FOpsWaitState> S = MakeShared<FOpsWaitState>();
	S->Tool = Tool;
	S->Arguments = Arguments.IsSet() ? Arguments.AsObject() : MakeShared<FJsonObject>();
	S->Path = Path;
	S->Op = Op;
	S->Expected = Value.IsSet() ? Value.Value : nullptr;
	S->Timeout = TimeoutS;
	S->Interval = IntervalS;
	S->Start = FPlatformTime::Seconds();
	S->Caller = Inv->GetCaller();
	S->Depth = Inv->GetDepth() + 1;
	UOpsToolConditionAsyncResult* Result = NewObject<UOpsToolConditionAsyncResult>();
	S->Result = Result;
	RuntimeToolCall::SetTimeout(TimeoutS + 5.0);
	OpsPoll(S); // first check right away
	if (S->bDone)
	{
		return Result;
	}
	S->NextPoll = FPlatformTime::Seconds() + S->Interval;
	S->Ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([S](float)
	{
		if (S->bDone || !S->Result.IsValid())
		{
			return false;
		}
		const double Now = FPlatformTime::Seconds();
		if (!S->bPolling && Now >= S->NextPoll)
		{
			S->NextPoll = Now + S->Interval;
			OpsPoll(S);
		}
		return !S->bDone;
	}));
	Result->OnCancelled = [S]()
	{
		S->bDone = true;
		FTSTicker::GetCoreTicker().RemoveTicker(S->Ticker);
	};
	return Result;
}

UOpsToolWaitAsyncResult* URuntimeOpsToolset::Wait(float Seconds)
{
	if (Seconds < 0.f || Seconds > 120.f)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("seconds must be 0-120 (got %g)"), Seconds),
			TEXT("Wait in several steps, or use wait_for_condition"));
		return nullptr;
	}
	UOpsToolWaitAsyncResult* Result = NewObject<UOpsToolWaitAsyncResult>();
	TWeakObjectPtr<UOpsToolWaitAsyncResult> Weak = Result;
	const double End = FPlatformTime::Seconds() + Seconds;
	const FTSTicker::FDelegateHandle Handle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Weak, End, Seconds](float)
	{
		UOpsToolWaitAsyncResult* R = Weak.Get();
		if (!R || R->bIsComplete)
		{
			return false;
		}
		if (FPlatformTime::Seconds() < End)
		{
			return true;
		}
		FOpsToolWaitResult V;
		V.WaitedS = Seconds;
		R->SetValue(V);
		return false;
	}));
	Result->OnCancelled = [Handle]() { FTSTicker::GetCoreTicker().RemoveTicker(Handle); };
	RuntimeToolCall::SetTimeout(Seconds + 5.0);
	return Result;
}

// ---- tests ----------------------------------------------------------------------------------------------------------

FOpsToolTestList URuntimeOpsToolset::ListTests(const FString& Filter, int32 Limit)
{
	FOpsToolTestList Out;
	FOpsTestFilter F;
	if (!OpsParseFilter(Filter, F))
	{
		return Out;
	}
	Out.Tests = FOpsTestRunner::ListTests(F);
	Out.Total = Out.Tests.Num();
	const int32 Max = URuntimeToolsetsSettings::Get().ClampLimit(Limit);
	if (Out.Tests.Num() > Max)
	{
		Out.Tests.SetNum(Max);
	}
	return Out;
}

FOpsToolTestStatus URuntimeOpsToolset::RunAutomationTest(const FString& Filter)
{
	if (OpsAnyRunInProgress())
	{
		RuntimeToolCall::Fail(ERuntimeToolError::Busy, TEXT("a test run is already in progress"), TEXT("Poll ops.get_test_status, or call ops.stop_tests"));
		return {};
	}
	FOpsTestFilter F;
	if (!OpsParseFilter(Filter, F))
	{
		return {};
	}
	if (FOpsTestRunner::ListTests(F).Num() == 0)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::NotFound, FString::Printf(TEXT("no tests match '%s'"), *Filter), TEXT("See ops.list_tests"));
		return {};
	}
	if (FAutomationTestFramework::Get().GetCurrentTest() != nullptr)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::Busy, TEXT("another automation test is running (session frontend or command line)"));
		return {};
	}
	FString Err;
	if (!FOpsTestRunner::Get().Start(Filter, F, Err))
	{
		RuntimeToolCall::Fail(ERuntimeToolError::Busy, Err);
		return {};
	}
	RuntimeToolCall::Current()->RecordIrreversible(FString::Printf(TEXT("run tests %s"), *Filter));
	return OwnStatus(20);
}

FOpsToolTestStatus URuntimeOpsToolset::GetTestStatus(int32 MaxFailures)
{
	if (MaxFailures < 0 || MaxFailures > 200)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("max_failures must be 0-200 (got %d)"), MaxFailures));
		return {};
	}
	const FOpsTestRunner& Own = FOpsTestRunner::Get();
	if (FRuntimeTestRunner::Get().IsRunning() || (!Own.IsRunning() && Own.GetTotal() == 0))
	{
		return CoreStatus(MaxFailures);
	}
	return OwnStatus(MaxFailures);
}

FOpsToolTestResults URuntimeOpsToolset::GetTestResults(const FString& Filter, int32 Limit)
{
	FOpsToolTestResults Out;
	FOpsTestFilter F;
	if (!OpsParseFilter(Filter, F))
	{
		return Out;
	}
	const int32 Max = URuntimeToolsetsSettings::Get().ClampLimit(Limit);
	Out.RunFilter = FOpsTestRunner::Get().GetFilter();
	for (const FOpsToolTestResult& R : FOpsTestRunner::Get().GetResults())
	{
		if (Out.Results.Num() >= Max)
		{
			break;
		}
		if (F.Matches(R.Test))
		{
			Out.Results.Add(R);
		}
	}
	return Out;
}

FOpsToolTestStatus URuntimeOpsToolset::StopTests()
{
	if (!FOpsTestRunner::Get().IsRunning())
	{
		RuntimeToolCall::Fail(ERuntimeToolError::WrongState, TEXT("no test run started by ops.run_automation_test is in progress"),
			FRuntimeTestRunner::Get().IsRunning() ? TEXT("A command-line run (-RunTests) cannot be stopped from here") : TEXT("See ops.get_test_status"));
		return {};
	}
	FOpsTestRunner::Get().Stop();
	RuntimeToolCall::Current()->RecordIrreversible(TEXT("stop tests"));
	return OwnStatus(20);
}
