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
}

// ---------------------------------------------------------------------------------------------------------------------
// Profiling
// ---------------------------------------------------------------------------------------------------------------------

BEGIN_DEFINE_SPEC(FProfilingToolsetSpec, "RuntimeToolsets.Toolsets.Profiling", RUNTIMETOOLSETS_TEST_FLAGS)
	TUniquePtr<FToolTestHarness> H;
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
		TestTrue(TEXT("rhi named"), !H->CallOk(TEXT("profiling.gpu_stats"))->GetStringField(TEXT("rhi")).IsEmpty());
	});

	LatentIt("samples frame timing", FTimespan::FromSeconds(15), [this](const FDoneDelegate& Done)
	{
		InvokeLatent(*H, TEXT("profiling.frame_timing"), TEXT("{\"seconds\":0.3}"), Done, [this](const FToolResult& R)
		{
			if (TestTrue(TEXT("ok"), R.IsOk()))
			{
				TestTrue(TEXT("frames"), R.Payload->GetNumberField(TEXT("frames")) > 0);
				TestTrue(TEXT("percentiles ordered"), R.Payload->GetNumberField(TEXT("p50_ms")) <= R.Payload->GetNumberField(TEXT("max_ms")));
			}
		});
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
	});
}

// ---------------------------------------------------------------------------------------------------------------------
// Ops
// ---------------------------------------------------------------------------------------------------------------------

BEGIN_DEFINE_SPEC(FOpsToolsetSpec, "RuntimeToolsets.Toolsets.Ops", RUNTIMETOOLSETS_TEST_FLAGS)
	TUniquePtr<FToolTestHarness> H;
END_DEFINE_SPEC(FOpsToolsetSpec)

void FOpsToolsetSpec::Define()
{
	BeforeEach([this]()
	{
		H = MakeUnique<FToolTestHarness>(*this);
		H->Setup();
	});
	AfterEach([this]() { H.Reset(); });

	It("runs a batch and returns every result", [this]()
	{
		const TSharedRef<FJsonObject> R = H->CallOk(TEXT("ops.run_batch"), TEXT(R"({"calls":[
			{"tool":"scene.create_group","arguments":{"name":"g"}},
			{"tool":"actors.spawn_actor","arguments":{"shape":"cube","parent":"g","name":"a"}},
			{"tool":"actors_spawn_actor","arguments":{"shape":"sphere","parent":"g","name":"b","location":[200,0,0]}}]})"));
		TestEqual(TEXT("completed"), static_cast<int32>(R->GetNumberField(TEXT("completed"))), 3);
		TestEqual(TEXT("nodes"), H->GetGraph()->NumLiveNodes(), 3);
		H->CallOk(TEXT("scene.get_node"), TEXT("{\"target\":\"g/b\"}"));
	});

	It("atomic batches are one undo step and roll back on failure", [this]()
	{
		const FString Calls = TEXT(R"([{"tool":"actors.spawn_actor","arguments":{"shape":"cube"}},{"tool":"actors.spawn_actor","arguments":{"shape":"cone"}}])");
		H->CallOk(TEXT("ops.run_batch"), FString::Printf(TEXT("{\"atomic\":true,\"label\":\"two\",\"calls\":%s}"), *Calls));
		TestEqual(TEXT("two nodes"), H->GetGraph()->NumLiveNodes(), 2);
		H->CallOk(TEXT("history.undo"));
		TestEqual(TEXT("one undo removes both"), H->GetGraph()->NumLiveNodes(), 0);

		H->CallFails(TEXT("ops.run_batch"), TEXT(R"({"atomic":true,"calls":[
			{"tool":"actors.spawn_actor","arguments":{"shape":"cube"}},
			{"tool":"actors.spawn_actor","arguments":{"shape":"donut"}}]})"), ERuntimeToolError::InvalidArguments);
		TestEqual(TEXT("rolled back"), H->GetGraph()->NumLiveNodes(), 0);
	});

	It("non-atomic batches report where they stopped", [this]()
	{
		const TSharedRef<FJsonObject> R = H->CallOk(TEXT("ops.run_batch"), TEXT(R"({"calls":[
			{"tool":"actors.spawn_actor","arguments":{"shape":"cube"}},
			{"tool":"nope.tool"},
			{"tool":"actors.spawn_actor","arguments":{"shape":"cube"}}]})"));
		TestEqual(TEXT("failed at 1"), static_cast<int32>(R->GetNumberField(TEXT("failed_at"))), 1);
		TestEqual(TEXT("stopped"), static_cast<int32>(R->GetNumberField(TEXT("completed"))), 2);
		TestEqual(TEXT("first kept"), H->GetGraph()->NumLiveNodes(), 1);
		H->CallFails(TEXT("ops.run_batch"), TEXT("{\"calls\":[{\"tool\":\"ops.run_batch\"}]}"), ERuntimeToolError::NotAllowed);
		H->CallFails(TEXT("ops.run_batch"), TEXT("{\"calls\":[]}"), ERuntimeToolError::InvalidArguments);
	});

	It("waits for a condition that already holds", [this]()
	{
		const FString Cube = H->SpawnCube(TEXT("\"location\":[0,0,250]"));
		const TSharedRef<FJsonObject> R = H->CallOk(TEXT("ops.wait_for_condition"),
			FString::Printf(TEXT("{\"tool\":\"scene.get_node\",\"arguments\":{\"target\":\"%s\"},\"path\":\"location[2]\",\"op\":\"gte\",\"value\":200}"), *Cube));
		TestTrue(TEXT("met"), R->GetBoolField(TEXT("met")));
		TestEqual(TEXT("value"), R->GetNumberField(TEXT("value")), 250.0);
		H->CallFails(TEXT("ops.wait_for_condition"), TEXT("{\"tool\":\"nope.tool\"}"), ERuntimeToolError::UnknownTool);
		H->CallFails(TEXT("ops.wait_for_condition"), TEXT("{\"tool\":\"session.ping\",\"op\":\"equals\"}"), ERuntimeToolError::InvalidArguments);
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
		});
	});

	It("lists tests and reports runner status", [this]()
	{
		const TSharedRef<FJsonObject> L = H->CallOk(TEXT("ops.list_tests"), TEXT("{\"filter\":\"RuntimeToolsets.Core.SchemaValidator\"}"));
		TestTrue(TEXT("found"), L->GetNumberField(TEXT("total")) >= 6);
		H->CallOk(TEXT("ops.get_test_status"));
		// Inside the in-game runner a run is already in progress, so the tool must answer busy.
		H->CallFails(TEXT("ops.run_automation_test"), TEXT("{\"filter\":\"No.Such.Tests\"}"),
			FRuntimeTestRunner::Get().IsRunning() ? ERuntimeToolError::Busy : ERuntimeToolError::NotFound);
	});
}

#endif
