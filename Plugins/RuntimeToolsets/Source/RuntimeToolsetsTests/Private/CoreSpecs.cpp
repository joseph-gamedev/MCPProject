#include "ToolTestHarness.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "RuntimeCommandHistory.h"
#include "RuntimeJson.h"
#include "RuntimeToolSchema.h"
#include "RuntimeToolsetsCore.h"
#include "Async/Async.h"
#include "Containers/Ticker.h"
#include <atomic>

// ---------------------------------------------------------------------------------------------------------------------
// Schema validator
// ---------------------------------------------------------------------------------------------------------------------

BEGIN_DEFINE_SPEC(FSchemaValidatorSpec, "RuntimeToolsets.Core.SchemaValidator", RUNTIMETOOLSETS_TEST_FLAGS)
	TSharedRef<FJsonObject> Schema = MakeShared<FJsonObject>();
	bool Check(const FString& Json, FString& Error)
	{
		TSharedPtr<FJsonObject> Args = RuntimeJson::Parse(Json);
		return RuntimeToolSchema::ValidateAndFill(Schema, Args.ToSharedRef(), Error);
	}
END_DEFINE_SPEC(FSchemaValidatorSpec)

void FSchemaValidatorSpec::Define()
{
	BeforeEach([this]()
	{
		Schema = FToolSchema()
			.Add(TEXT("name"), FToolParam::String(TEXT("n"), 8).Required())
			.Add(TEXT("count"), FToolParam::Integer(TEXT("c"), 1, 10).Default(3))
			.Add(TEXT("mode"), FToolParam::Enum(TEXT("m"), {TEXT("a"), TEXT("b")}))
			.Add(TEXT("location"), FToolParam::Vec3(TEXT("l")))
			.Add(TEXT("flag"), FToolParam::Boolean(TEXT("f")))
			.Build();
	});

	It("accepts valid args and fills defaults", [this]()
	{
		TSharedPtr<FJsonObject> Args = RuntimeJson::Parse(TEXT("{\"name\":\"x\",\"location\":[1,2,3]}"));
		FString Error;
		TestTrue(TEXT("valid"), RuntimeToolSchema::ValidateAndFill(Schema, Args.ToSharedRef(), Error));
		TestEqual(TEXT("default filled"), static_cast<int32>(Args->GetNumberField(TEXT("count"))), 3);
	});

	It("rejects missing required", [this]()
	{
		FString Error;
		TestFalse(TEXT("missing"), Check(TEXT("{}"), Error));
		TestTrue(TEXT("message names field"), Error.Contains(TEXT("name")));
	});

	It("rejects unknown properties", [this]()
	{
		FString Error;
		TestFalse(TEXT("unknown"), Check(TEXT("{\"name\":\"x\",\"nope\":1}"), Error));
	});

	It("rejects wrong types, ranges, enums and lengths", [this]()
	{
		FString Error;
		TestFalse(TEXT("type"), Check(TEXT("{\"name\":5}"), Error));
		TestFalse(TEXT("max"), Check(TEXT("{\"name\":\"x\",\"count\":11}"), Error));
		TestFalse(TEXT("integer"), Check(TEXT("{\"name\":\"x\",\"count\":2.5}"), Error));
		TestFalse(TEXT("enum"), Check(TEXT("{\"name\":\"x\",\"mode\":\"c\"}"), Error));
		TestFalse(TEXT("length"), Check(TEXT("{\"name\":\"123456789\"}"), Error));
		TestFalse(TEXT("bool"), Check(TEXT("{\"name\":\"x\",\"flag\":\"yes\"}"), Error));
	});

	It("reports array element paths", [this]()
	{
		FString Error;
		TestFalse(TEXT("vec item"), Check(TEXT("{\"name\":\"x\",\"location\":[1,2,\"z\"]}"), Error));
		TestTrue(TEXT("path"), Error.Contains(TEXT("$.location[2]")));
		TestFalse(TEXT("vec size"), Check(TEXT("{\"name\":\"x\",\"location\":[1,2]}"), Error));
	});

	It("treats null as not provided", [this]()
	{
		FString Error;
		TestTrue(TEXT("null optional"), Check(TEXT("{\"name\":\"x\",\"mode\":null}"), Error));
	});
}

// ---------------------------------------------------------------------------------------------------------------------
// Command history
// ---------------------------------------------------------------------------------------------------------------------

BEGIN_DEFINE_SPEC(FCommandHistorySpec, "RuntimeToolsets.Core.CommandHistory", RUNTIMETOOLSETS_TEST_FLAGS)
	TSharedPtr<int32> Value;
	TUniquePtr<FRuntimeCommandHistory> History;
	FRuntimeCommandContext Context;
	int32 Discarded = 0;

	/** Command that adds Amount to *Value. */
	TUniquePtr<IRuntimeCommand> Add(int32 Amount)
	{
		TSharedPtr<int32> V = Value;
		int32* DiscardCounter = &Discarded;
		class FAddCommand : public IRuntimeCommand
		{
		public:
			TSharedPtr<int32> V;
			int32 Amount = 0;
			int32* DiscardCounter = nullptr;
			virtual FString GetLabel() const override { return FString::Printf(TEXT("add %d"), Amount); }
			virtual bool Do(FRuntimeCommandContext&, FString&) override { *V += Amount; return true; }
			virtual bool Undo(FRuntimeCommandContext&) override { *V -= Amount; return true; }
			virtual void OnDiscarded(FRuntimeCommandContext&, bool) override { ++*DiscardCounter; }
		};
		TUniquePtr<FAddCommand> C = MakeUnique<FAddCommand>();
		C->V = V;
		C->Amount = Amount;
		C->DiscardCounter = DiscardCounter;
		return C;
	}
	void Exec(int32 Amount)
	{
		FString Error;
		TSharedPtr<FJsonObject> Delta;
		History->Execute(Add(Amount), Context, Error, Delta);
	}
END_DEFINE_SPEC(FCommandHistorySpec)

void FCommandHistorySpec::Define()
{
	BeforeEach([this]()
	{
		Value = MakeShared<int32>(0);
		History = MakeUnique<FRuntimeCommandHistory>(10);
		Discarded = 0;
	});
	AfterEach([this]()
	{
		History->Clear(Context);
		History.Reset();
	});

	It("undoes and redoes", [this]()
	{
		Exec(1);
		Exec(10);
		TestEqual(TEXT("applied"), *Value, 11);
		History->Undo(Context, 1);
		TestEqual(TEXT("undo 1"), *Value, 1);
		History->Redo(Context, 1);
		TestEqual(TEXT("redo"), *Value, 11);
		History->Undo(Context, 5);
		TestEqual(TEXT("undo all"), *Value, 0);
	});

	It("clears redo when a new command runs", [this]()
	{
		Exec(1);
		History->Undo(Context, 1);
		Exec(5);
		TestEqual(TEXT("redo cleared"), History->NumRedo(), 0);
		TestEqual(TEXT("discarded undone command"), Discarded, 1);
	});

	It("commits a transaction as one entry", [this]()
	{
		History->BeginTransaction(TEXT("t"));
		History->BeginTransaction(TEXT("nested"));
		Exec(1);
		Exec(2);
		int64 Id;
		int32 Count;
		TestTrue(TEXT("inner end"), History->EndTransaction(true, Context, Id, Count));
		TestTrue(TEXT("still open"), History->IsInTransaction());
		Exec(3);
		TestTrue(TEXT("outer end"), History->EndTransaction(true, Context, Id, Count));
		TestEqual(TEXT("one entry"), History->NumUndo(), 1);
		TestEqual(TEXT("three commands"), Count, 3);
		History->Undo(Context, 1);
		TestEqual(TEXT("all undone"), *Value, 0);
	});

	It("rolls back a transaction", [this]()
	{
		History->BeginTransaction(TEXT("t"));
		Exec(4);
		int64 Id;
		int32 Count;
		History->EndTransaction(false, Context, Id, Count);
		TestEqual(TEXT("reverted"), *Value, 0);
		TestEqual(TEXT("no entry"), History->NumUndo(), 0);
	});

	It("rolls back to a checkpoint and skips irreversible entries", [this]()
	{
		Exec(1);
		History->Checkpoint(TEXT("cp"), Context);
		Exec(2);
		History->RecordIrreversible(TEXT("exec stat fps"), Context);
		Exec(4);
		FRuntimeHistoryStepResult Step;
		TestTrue(TEXT("marker found"), History->UndoToMarker(Context, TEXT("cp"), Step));
		TestEqual(TEXT("back at checkpoint"), *Value, 1);
		TestEqual(TEXT("irreversible reported"), Step.SkippedIrreversible.Num(), 1);
		TestFalse(TEXT("unknown marker"), History->UndoToMarker(Context, TEXT("nope"), Step));
	});

	It("trims to the limit and discards the oldest", [this]()
	{
		for (int32 i = 0; i < 15; ++i)
		{
			Exec(1);
		}
		TestEqual(TEXT("limit"), History->NumUndo(), 10);
		TestEqual(TEXT("discarded"), Discarded, 5);
	});
}

// ---------------------------------------------------------------------------------------------------------------------
// Dispatcher
// ---------------------------------------------------------------------------------------------------------------------

BEGIN_DEFINE_SPEC(FDispatcherSpec, "RuntimeToolsets.Core.Dispatcher", RUNTIMETOOLSETS_TEST_FLAGS)
END_DEFINE_SPEC(FDispatcherSpec)

void FDispatcherSpec::Define()
{
	It("reports unknown tools and bad JSON", [this]()
	{
		FToolCallerContext Caller;
		const FToolResult Unknown = IRuntimeToolsetsCore::Get().ExecuteInline(TEXT("nope.nothing"), nullptr, Caller, 0);
		TestTrue(TEXT("unknown"), Unknown.Error == ERuntimeToolError::UnknownTool);
		const FToolResult Underscore = IRuntimeToolsetsCore::Get().ExecuteInline(TEXT("session_ping"), nullptr, Caller, 0);
		TestTrue(TEXT("underscore name accepted"), Underscore.IsOk());
	});

	It("lists and describes toolsets", [this]()
	{
		const TSharedPtr<FJsonObject> List = RuntimeJson::Parse(IRuntimeToolsetsCore::Get().ListToolsets());
		TestTrue(TEXT("has toolsets"), List.IsValid() && List->GetArrayField(TEXT("toolsets")).Num() >= 8);
		TestTrue(TEXT("describe actors"), !IRuntimeToolsetsCore::Get().DescribeToolset(TEXT("actors")).IsEmpty());
		TestTrue(TEXT("describe unknown"), IRuntimeToolsetsCore::Get().DescribeToolset(TEXT("nope")).IsEmpty());
	});

	LatentIt("runs calls from many threads on the game thread", FTimespan::FromSeconds(20), [this](const FDoneDelegate& Done)
	{
		constexpr int32 Calls = 32;
		struct FState
		{
			std::atomic<int32> Completed{0};
			std::atomic<int32> Failed{0};
			std::atomic<int32> OffGameThread{0};
		};
		TSharedRef<FState> State = MakeShared<FState>();
		// Worker threads only touch State; the spec's Done delegate stays on the game thread.
		for (int32 i = 0; i < Calls; ++i)
		{
			Async(EAsyncExecution::ThreadPool, [State]()
			{
				FToolCallerContext Caller;
				Caller.SessionId = TEXT("dispatcher-test");
				IRuntimeToolsetsCore::Get().Invoke(TEXT("session.ping"), FString(), Caller, FOnToolComplete::CreateLambda([State](const FToolResult& R)
				{
					State->OffGameThread += IsInGameThread() ? 0 : 1;
					State->Failed += R.IsOk() ? 0 : 1;
					++State->Completed;
				}));
			});
		}
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([this, State, Done](float)
		{
			if (State->Completed.load() < Calls)
			{
				return true;
			}
			TestEqual(TEXT("all succeeded"), State->Failed.load(), 0);
			TestEqual(TEXT("callbacks on game thread"), State->OffGameThread.load(), 0);
			Done.Execute();
			return false;
		}));
	});

	LatentIt("fails invalid argument JSON on a later tick", FTimespan::FromSeconds(10), [this](const FDoneDelegate& Done)
	{
		TSharedRef<bool> bCalled = MakeShared<bool>(false);
		FToolCallerContext Caller;
		IRuntimeToolsetsCore::Get().Invoke(TEXT("session.ping"), TEXT("not json"), Caller, FOnToolComplete::CreateLambda([this, bCalled, Done](const FToolResult& R)
		{
			*bCalled = true;
			TestTrue(TEXT("invalid"), R.Error == ERuntimeToolError::InvalidArguments);
			Done.Execute();
		}));
		TestFalse(TEXT("queued, not run inline"), *bCalled);
	});
}

#endif
