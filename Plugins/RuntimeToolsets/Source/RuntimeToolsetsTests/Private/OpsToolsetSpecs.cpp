#include "ToolTestHarness.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "RuntimeJson.h"
#include "RuntimeSceneGraph.h"
#include "RuntimeTestRunner.h"
#include "RuntimeToolsetsCore.h"
#include "GameFramework/Actor.h"
#include "ProfilingDebugging/TraceAuxiliary.h"

namespace
{
	/** Invokes a tool through the real queue (async tools) and runs Check on completion. */
	void InvokeLatent(FToolTestHarness& H, const FString& Tool, const FString& Json, const FDoneDelegate& Done, TFunction<void(const FToolResult&)> Check)
	{
		FToolCallerContext Caller;
		Caller.SessionId = H.GetSession();
		IRuntimeToolsetsCore::Get().Invoke(Tool, Json, Caller, FOnToolComplete::CreateLambda([Done, Check](const FToolResult& R)
		{
			Check(R);
			Done.Execute();
		}));
	}

	/** returnValue of a reflected tool result (empty object if missing). */
	TSharedRef<FJsonObject> RetOf(const FToolResult& R)
	{
		const TSharedPtr<FJsonObject>* O = nullptr;
		return R.Payload.IsValid() && R.Payload->TryGetObjectField(TEXT("returnValue"), O) ? O->ToSharedRef() : MakeShared<FJsonObject>();
	}

	/** "id" of a batch item, directly or inside its returnValue. */
	FString IdOf(const TSharedPtr<FJsonObject>& Item)
	{
		FString Id;
		if (Item.IsValid() && !Item->TryGetStringField(TEXT("id"), Id))
		{
			const TSharedPtr<FJsonObject>* O = nullptr;
			if (Item->TryGetObjectField(TEXT("returnValue"), O))
			{
				(*O)->TryGetStringField(TEXT("id"), Id);
			}
		}
		return Id;
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// Profiling
// ---------------------------------------------------------------------------------------------------------------------

BEGIN_DEFINE_SPEC(FProfilingToolsetSpec, "RuntimeToolsets.Toolsets.Profiling", RUNTIMETOOLSETS_TEST_FLAGS)
	TUniquePtr<FToolTestHarness> H;
	void ExpectError(const FString& Tool, const FString& Json, ERuntimeToolError Expected, const FDoneDelegate& Done)
	{
		InvokeLatent(*H, Tool, Json, Done, [this, Tool, Json, Expected](const FToolResult& R)
		{
			TestTrue(FString::Printf(TEXT("%s %s fails with %s (got %s)"), *Tool, *Json, LexToString(Expected), *R.ToJsonString()), R.Error == Expected);
		});
	}
END_DEFINE_SPEC(FProfilingToolsetSpec)

void FProfilingToolsetSpec::Define()
{
	BeforeEach([this]()
	{
		H = MakeUnique<FToolTestHarness>(*this);
		H->Setup();
	});
	AfterEach([this]() { H.Reset(); });

	It("reports memory and GPU info", [this]()
	{
		const TSharedRef<FJsonObject> M = H->CallOk(TEXT("profiling.memory_report"), TEXT("{\"top_classes\":5}"));
		TestTrue(TEXT("memory"), M->GetNumberField(TEXT("used_physical_mb")) > 0);
		TestEqual(TEXT("top classes"), M->GetArrayField(TEXT("top_classes")).Num(), 5);
		TestTrue(TEXT("class named"), !M->GetArrayField(TEXT("top_classes"))[0]->AsObject()->GetStringField(TEXT("class")).IsEmpty());
		H->CallFails(TEXT("profiling.memory_report"), TEXT("{\"top_classes\":500}"), ERuntimeToolError::InvalidArguments);
		const TSharedRef<FJsonObject> G = H->CallOk(TEXT("profiling.gpu_stats"));
		TestTrue(TEXT("rhi named"), !G->GetStringField(TEXT("rhi")).IsEmpty());
		TestEqual(TEXT("viewport [w,h]"), G->GetArrayField(TEXT("viewport")).Num(), 2);
		H->CallFails(TEXT("profiling.gpu_stats"), TEXT("{\"nope\":1}"), ERuntimeToolError::InvalidArguments);
	});

	LatentIt("samples frame timing", FTimespan::FromSeconds(15), [this](const FDoneDelegate& Done)
	{
		InvokeLatent(*H, TEXT("profiling.frame_timing"), TEXT("{\"seconds\":0.3}"), Done, [this](const FToolResult& R)
		{
			if (TestTrue(TEXT("ok"), R.IsOk()))
			{
				const TSharedRef<FJsonObject> V = RetOf(R);
				TestTrue(TEXT("frames"), V->GetNumberField(TEXT("frames")) > 0);
				TestTrue(TEXT("percentiles ordered"), V->GetNumberField(TEXT("p50_ms")) <= V->GetNumberField(TEXT("max_ms")));
				TestTrue(TEXT("thread breakdown"), V->HasField(TEXT("last_frame")));
			}
		});
	});

	LatentIt("rejects a bad frame timing window", FTimespan::FromSeconds(10), [this](const FDoneDelegate& Done)
	{
		ExpectError(TEXT("profiling.frame_timing"), TEXT("{\"seconds\":0}"), ERuntimeToolError::InvalidArguments, Done);
	});

	LatentIt("finds hitches", FTimespan::FromSeconds(15), [this](const FDoneDelegate& Done)
	{
		InvokeLatent(*H, TEXT("profiling.find_hitches"), TEXT("{\"seconds\":0.3,\"hitch_ms\":0.001,\"limit\":3}"), Done, [this](const FToolResult& R)
		{
			if (TestTrue(TEXT("ok"), R.IsOk()))
			{
				const TSharedRef<FJsonObject> V = RetOf(R);
				TestTrue(TEXT("every frame is a hitch at 0.001 ms"), V->GetNumberField(TEXT("hitch_count")) >= V->GetNumberField(TEXT("frames")));
				const TArray<TSharedPtr<FJsonValue>> Worst = V->GetArrayField(TEXT("worst"));
				TestTrue(TEXT("limited"), Worst.Num() > 0 && Worst.Num() <= 3);
				if (Worst.Num() > 1)
				{
					TestTrue(TEXT("worst first"), Worst[0]->AsObject()->GetNumberField(TEXT("ms")) >= Worst[1]->AsObject()->GetNumberField(TEXT("ms")));
				}
			}
		});
	});

	LatentIt("rejects a bad hitch threshold", FTimespan::FromSeconds(10), [this](const FDoneDelegate& Done)
	{
		ExpectError(TEXT("profiling.find_hitches"), TEXT("{\"hitch_ms\":-5}"), ERuntimeToolError::InvalidArguments, Done);
	});

	It("starts and stops a trace (or reports one already running)", [this]()
	{
		if (FTraceAuxiliary::IsConnected())
		{
			H->CallFails(TEXT("profiling.start_trace"), TEXT("{}"), ERuntimeToolError::WrongState);
			return;
		}
		H->CallFails(TEXT("profiling.stop_trace"), TEXT("{}"), ERuntimeToolError::WrongState);
		H->CallFails(TEXT("profiling.start_trace"), TEXT("{\"file\":\"../escape\"}"), ERuntimeToolError::NotAllowed);
		const FToolResult Start = H->Call(TEXT("profiling.start_trace"), TEXT("{\"file\":\"rt_test\",\"channels\":\"cpu,frame\"}"));
		if (Start.Error == ERuntimeToolError::Unsupported)
		{
			AddInfo(TEXT("Tracing not available in this build"));
			return;
		}
		TestTrue(TEXT("started"), Start.IsOk());
		const TSharedRef<FJsonObject> Stop = H->CallOk(TEXT("profiling.stop_trace"));
		TestTrue(TEXT("utrace path"), Stop->GetStringField(TEXT("file")).Contains(TEXT("rt_test")));
		TestEqual(TEXT("open with"), Stop->GetStringField(TEXT("open_with")), FString(TEXT("UnrealInsights.exe")));
	});
}

// ---------------------------------------------------------------------------------------------------------------------
// Ops
// ---------------------------------------------------------------------------------------------------------------------

BEGIN_DEFINE_SPEC(FOpsToolsetSpec, "RuntimeToolsets.Toolsets.Ops", RUNTIMETOOLSETS_TEST_FLAGS)
	TUniquePtr<FToolTestHarness> H;
	void ExpectError(const FString& Tool, const FString& Json, ERuntimeToolError Expected, const FDoneDelegate& Done, TFunction<void(const FToolResult&)> Then = nullptr)
	{
		InvokeLatent(*H, Tool, Json, Done, [this, Tool, Json, Expected, Then](const FToolResult& R)
		{
			TestTrue(FString::Printf(TEXT("%s %s fails with %s (got %s)"), *Tool, *Json, LexToString(Expected), *R.ToJsonString()), R.Error == Expected);
			if (Then)
			{
				Then(R);
			}
		});
	}
	TArray<TSharedPtr<FJsonValue>> Results(const FToolResult& R)
	{
		return RetOf(R)->GetArrayField(TEXT("results"));
	}
END_DEFINE_SPEC(FOpsToolsetSpec)

void FOpsToolsetSpec::Define()
{
	BeforeEach([this]()
	{
		H = MakeUnique<FToolTestHarness>(*this);
		H->Setup();
	});
	AfterEach([this]() { H.Reset(); });

	LatentIt("runs a batch and returns every result", FTimespan::FromSeconds(10), [this](const FDoneDelegate& Done)
	{
		InvokeLatent(*H, TEXT("ops.run_batch"), TEXT(R"({"calls":[
			{"tool":"scene.create_group","arguments":{"name":"g"}},
			{"tool":"actors.spawn_actor","arguments":{"shape":"cube","parent":"g","name":"a"}},
			{"tool":"actors_spawn_actor","arguments":{"shape":"sphere","parent":"g","name":"b","location":[200,0,0]}}]})"), Done,
			[this](const FToolResult& R)
			{
				if (!TestTrue(FString::Printf(TEXT("ok: %s"), *R.ToJsonString()), R.IsOk()))
				{
					return;
				}
				const TSharedRef<FJsonObject> V = RetOf(R);
				TestEqual(TEXT("completed"), static_cast<int32>(V->GetNumberField(TEXT("completed"))), 3);
				TestEqual(TEXT("none failed"), static_cast<int32>(V->GetNumberField(TEXT("failed_at"))), -1);
				TestEqual(TEXT("not atomic"), static_cast<int32>(V->GetNumberField(TEXT("history_id"))), -1);
				TestEqual(TEXT("nodes"), H->GetGraph()->NumLiveNodes(), 3);
				H->CallOk(TEXT("scene.get_node"), TEXT("{\"target\":\"g/b\"}"));
			});
	});

	LatentIt("lets later calls use earlier results ($N.path)", FTimespan::FromSeconds(10), [this](const FDoneDelegate& Done)
	{
		InvokeLatent(*H, TEXT("ops.run_batch"), TEXT(R"({"calls":[
			{"tool":"scene.create_group","arguments":{"name":"g"}},
			{"tool":"actors.spawn_actor","arguments":{"shape":"cube","parent":"$0.id","name":"$$literal"}}]})"), Done,
			[this](const FToolResult& R)
			{
				if (!TestTrue(FString::Printf(TEXT("ok: %s"), *R.ToJsonString()), R.IsOk()))
				{
					return;
				}
				const TArray<TSharedPtr<FJsonValue>> Items = Results(R);
				if (!TestEqual(TEXT("two results"), Items.Num(), 2))
				{
					return;
				}
				const FString Group = IdOf(Items[0]->AsObject());
				const FRuntimeSceneNode* Child = H->GetGraph()->Find(IdOf(Items[1]->AsObject()));
				if (TestNotNull(TEXT("child created"), Child))
				{
					TestEqual(TEXT("parented through $0.id"), Child->ParentId, Group);
					TestEqual(TEXT("$$ escapes"), Child->Name, FString(TEXT("$literal")));
				}
			});
	});

	LatentIt("rejects references to calls that do not run first", FTimespan::FromSeconds(10), [this](const FDoneDelegate& Done)
	{
		ExpectError(TEXT("ops.run_batch"), TEXT(R"({"calls":[{"tool":"actors.spawn_actor","arguments":{"shape":"cube","parent":"$1.id"}},{"tool":"session.ping"}]})"),
			ERuntimeToolError::InvalidArguments, Done);
	});

	LatentIt("fails a call whose reference has no such field", FTimespan::FromSeconds(10), [this](const FDoneDelegate& Done)
	{
		InvokeLatent(*H, TEXT("ops.run_batch"), TEXT(R"({"calls":[{"tool":"scene.create_group","arguments":{"name":"g"}},
			{"tool":"actors.spawn_actor","arguments":{"shape":"cube","parent":"$0.nope"}}]})"), Done,
			[this](const FToolResult& R)
			{
				if (TestTrue(TEXT("ok"), R.IsOk()))
				{
					TestEqual(TEXT("failed at 1"), static_cast<int32>(RetOf(R)->GetNumberField(TEXT("failed_at"))), 1);
					TestTrue(TEXT("names the field"), RuntimeJson::Write(RetOf(R)).Contains(TEXT("nope")));
				}
			});
	});

	LatentIt("atomic batches are one undo step", FTimespan::FromSeconds(10), [this](const FDoneDelegate& Done)
	{
		InvokeLatent(*H, TEXT("ops.run_batch"),
			TEXT(R"({"atomic":true,"label":"two","calls":[{"tool":"actors.spawn_actor","arguments":{"shape":"cube"}},{"tool":"actors.spawn_actor","arguments":{"shape":"cone"}}]})"),
			Done, [this](const FToolResult& R)
			{
				TestTrue(TEXT("ok"), R.IsOk());
				TestTrue(TEXT("history id"), RetOf(R)->GetNumberField(TEXT("history_id")) > 0);
				TestEqual(TEXT("two nodes"), H->GetGraph()->NumLiveNodes(), 2);
				H->CallOk(TEXT("history.undo"));
				TestEqual(TEXT("one undo removes both"), H->GetGraph()->NumLiveNodes(), 0);
			});
	});

	LatentIt("atomic batches roll back on failure", FTimespan::FromSeconds(10), [this](const FDoneDelegate& Done)
	{
		ExpectError(TEXT("ops.run_batch"), TEXT(R"({"atomic":true,"calls":[
			{"tool":"actors.spawn_actor","arguments":{"shape":"cube"}},
			{"tool":"actors.spawn_actor","arguments":{"shape":"donut"}}]})"), ERuntimeToolError::InvalidArguments, Done,
			[this](const FToolResult&) { TestEqual(TEXT("rolled back"), H->GetGraph()->NumLiveNodes(), 0); });
	});

	LatentIt("non-atomic batches report where they stopped", FTimespan::FromSeconds(10), [this](const FDoneDelegate& Done)
	{
		InvokeLatent(*H, TEXT("ops.run_batch"), TEXT(R"({"calls":[
			{"tool":"actors.spawn_actor","arguments":{"shape":"cube"}},
			{"tool":"nope.tool"},
			{"tool":"actors.spawn_actor","arguments":{"shape":"cube"}}]})"), Done, [this](const FToolResult& R)
			{
				const TSharedRef<FJsonObject> V = RetOf(R);
				TestEqual(TEXT("failed at 1"), static_cast<int32>(V->GetNumberField(TEXT("failed_at"))), 1);
				TestEqual(TEXT("stopped"), static_cast<int32>(V->GetNumberField(TEXT("completed"))), 2);
				TestEqual(TEXT("first kept"), H->GetGraph()->NumLiveNodes(), 1);
			});
	});

	LatentIt("rejects nested batches", FTimespan::FromSeconds(10), [this](const FDoneDelegate& Done)
	{
		ExpectError(TEXT("ops.run_batch"), TEXT("{\"calls\":[{\"tool\":\"ops.run_batch\"}]}"), ERuntimeToolError::NotAllowed, Done);
	});

	LatentIt("rejects empty batches", FTimespan::FromSeconds(10), [this](const FDoneDelegate& Done)
	{
		ExpectError(TEXT("ops.run_batch"), TEXT("{\"calls\":[]}"), ERuntimeToolError::InvalidArguments, Done);
	});

	LatentIt("waits for a condition that already holds", FTimespan::FromSeconds(10), [this](const FDoneDelegate& Done)
	{
		const FString Cube = H->SpawnCube(TEXT("\"location\":[0,0,250]"));
		InvokeLatent(*H, TEXT("ops.wait_for_condition"),
			FString::Printf(TEXT("{\"tool\":\"scene.get_node\",\"arguments\":{\"target\":\"%s\"},\"path\":\"location[2]\",\"op\":\"gte\",\"value\":200}"), *Cube), Done,
			[this](const FToolResult& R)
			{
				if (TestTrue(FString::Printf(TEXT("ok: %s"), *R.ToJsonString()), R.IsOk()))
				{
					TestTrue(TEXT("met"), RetOf(R)->GetBoolField(TEXT("met")));
					TestEqual(TEXT("value"), RetOf(R)->GetNumberField(TEXT("value")), 250.0);
				}
			});
	});

	LatentIt("fails when the polled tool does not exist", FTimespan::FromSeconds(10), [this](const FDoneDelegate& Done)
	{
		ExpectError(TEXT("ops.wait_for_condition"), TEXT("{\"tool\":\"nope.tool\"}"), ERuntimeToolError::UnknownTool, Done);
	});

	LatentIt("needs a value for comparisons", FTimespan::FromSeconds(10), [this](const FDoneDelegate& Done)
	{
		ExpectError(TEXT("ops.wait_for_condition"), TEXT("{\"tool\":\"session.ping\",\"op\":\"equals\"}"), ERuntimeToolError::InvalidArguments, Done);
	});

	LatentIt("times out when a condition never holds", FTimespan::FromSeconds(15), [this](const FDoneDelegate& Done)
	{
		InvokeLatent(*H, TEXT("ops.wait_for_condition"), TEXT(R"({"tool":"scene.get_node","arguments":{"target":"n999999"},"path":"ok","op":"truthy","timeout_s":0.3,"interval_s":0.05})"),
			Done, [this](const FToolResult& R)
			{
				TestTrue(TEXT("timeout"), R.Error == ERuntimeToolError::Timeout);
				TestTrue(TEXT("polled several times"), R.Message.Contains(TEXT("polls")));
			});
	});

	LatentIt("waits real time", FTimespan::FromSeconds(10), [this](const FDoneDelegate& Done)
	{
		const double Start = FPlatformTime::Seconds();
		InvokeLatent(*H, TEXT("ops.wait"), TEXT("{\"seconds\":0.2}"), Done, [this, Start](const FToolResult& R)
		{
			TestTrue(TEXT("ok"), R.IsOk());
			TestTrue(TEXT("waited"), FPlatformTime::Seconds() - Start >= 0.19);
			TestEqual(TEXT("waited_s"), RetOf(R)->GetNumberField(TEXT("waited_s")), 0.2, 0.001);
		});
	});

	LatentIt("rejects overly long waits", FTimespan::FromSeconds(10), [this](const FDoneDelegate& Done)
	{
		ExpectError(TEXT("ops.wait"), TEXT("{\"seconds\":500}"), ERuntimeToolError::InvalidArguments, Done);
	});

	It("lists tests with Epic's filter syntax", [this]()
	{
		const TSharedRef<FJsonObject> L = H->CallOk(TEXT("ops.list_tests"), TEXT("{\"filter\":\"RuntimeToolsets.Core.SchemaValidator\"}"));
		TestTrue(TEXT("substring"), L->GetNumberField(TEXT("total")) >= 6);
		const int32 Ops = static_cast<int32>(H->CallOk(TEXT("ops.list_tests"), TEXT("{\"filter\":\"^RuntimeToolsets.Toolsets.Ops\"}"))->GetNumberField(TEXT("total")));
		const int32 Prof = static_cast<int32>(H->CallOk(TEXT("ops.list_tests"), TEXT("{\"filter\":\"StartsWith:RuntimeToolsets.Toolsets.Profiling\"}"))->GetNumberField(TEXT("total")));
		TestTrue(TEXT("prefix"), Ops > 0 && Prof > 0);
		const TSharedRef<FJsonObject> Both = H->CallOk(TEXT("ops.list_tests"),
			TEXT("{\"filter\":\"^RuntimeToolsets.Toolsets.Ops + ^RuntimeToolsets.Toolsets.Profiling\",\"limit\":1}"));
		TestEqual(TEXT("+ joins filters"), static_cast<int32>(Both->GetNumberField(TEXT("total"))), Ops + Prof);
		TestEqual(TEXT("limit"), Both->GetArrayField(TEXT("tests")).Num(), 1);
		const FString First = Both->GetArrayField(TEXT("tests"))[0]->AsString();
		const TSharedRef<FJsonObject> Suffix = H->CallOk(TEXT("ops.list_tests"), FString::Printf(TEXT("{\"filter\":\"%s$\"}"), *First.Right(12)));
		TArray<FString> Tests;
		Suffix->TryGetStringArrayField(TEXT("tests"), Tests);
		TestTrue(TEXT("suffix"), Tests.Contains(First));
		H->CallFails(TEXT("ops.list_tests"), TEXT("{\"filter\":\"Group:NoSuchGroup\"}"), ERuntimeToolError::NotFound);
	});

	It("reports runner status and results, and refuses to stop or start when it cannot", [this]()
	{
		H->CallOk(TEXT("ops.get_test_status"));
		H->CallFails(TEXT("ops.get_test_status"), TEXT("{\"max_failures\":500}"), ERuntimeToolError::InvalidArguments);
		TestTrue(TEXT("results list"), H->CallOk(TEXT("ops.get_test_results"))->HasField(TEXT("results")));
		H->CallFails(TEXT("ops.get_test_results"), TEXT("{\"filter\":\"Group:NoSuchGroup\"}"), ERuntimeToolError::NotFound);
		H->CallFails(TEXT("ops.stop_tests"), TEXT("{}"), ERuntimeToolError::WrongState);
		// Inside the in-game runner a run is already in progress, so the tool must answer busy.
		H->CallFails(TEXT("ops.run_automation_test"), TEXT("{\"filter\":\"No.Such.Tests\"}"),
			FRuntimeTestRunner::Get().IsRunning() ? ERuntimeToolError::Busy : ERuntimeToolError::NotFound);
	});
}

#endif
