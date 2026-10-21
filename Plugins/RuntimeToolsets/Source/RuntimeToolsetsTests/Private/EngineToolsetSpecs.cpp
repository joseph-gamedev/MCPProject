#include "ToolTestHarness.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "RuntimeJson.h"
#include "RuntimeLogCapture.h"
#include "RuntimeToolsetsCore.h"
#include "HAL/IConsoleManager.h"
#include "Misc/App.h"

DEFINE_LOG_CATEGORY_STATIC(LogRuntimeToolsetsTestMarker, Log, All);

// ---------------------------------------------------------------------------------------------------------------------
// Session
// ---------------------------------------------------------------------------------------------------------------------

BEGIN_DEFINE_SPEC(FSessionToolsetSpec, "RuntimeToolsets.Toolsets.Session", RUNTIMETOOLSETS_TEST_FLAGS)
	TUniquePtr<FToolTestHarness> H;
END_DEFINE_SPEC(FSessionToolsetSpec)

void FSessionToolsetSpec::Define()
{
	BeforeEach([this]()
	{
		H = MakeUnique<FToolTestHarness>(*this);
		H->Setup();
	});
	AfterEach([this]() { H.Reset(); });

	It("pings and reports build info and capabilities", [this]()
	{
		TestTrue(TEXT("pong"), H->CallOk(TEXT("session.ping"))->GetBoolField(TEXT("pong")));
		TestFalse(TEXT("engine"), H->CallOk(TEXT("session.get_build_info"))->GetStringField(TEXT("engine")).IsEmpty());
		TestTrue(TEXT("toolsets"), H->CallOk(TEXT("session.get_capabilities"))->GetArrayField(TEXT("toolsets")).Num() >= 8);
	});

	It("describes toolsets", [this]()
	{
		const TSharedRef<FJsonObject> R = H->CallOk(TEXT("session.describe_toolset"), TEXT("{\"name\":\"scene\",\"include_schemas\":false}"));
		TestTrue(TEXT("tools"), R->GetArrayField(TEXT("tools")).Num() >= 10);
		H->CallFails(TEXT("session.describe_toolset"), TEXT("{\"name\":\"nope\"}"), ERuntimeToolError::NotFound);
	});

	It("calls tools through call_tool", [this]()
	{
		const TSharedRef<FJsonObject> R = H->CallOk(TEXT("session.call_tool"), TEXT("{\"name\":\"actors_spawn_actor\",\"arguments\":{\"shape\":\"sphere\"}}"));
		TestTrue(TEXT("nested result"), R->HasField(TEXT("id")));
		TestTrue(TEXT("history id passed through"), R->HasField(TEXT("history_id")));
		H->CallFails(TEXT("session.call_tool"), TEXT("{\"name\":\"session.call_tool\"}"), ERuntimeToolError::NotAllowed);
		H->CallFails(TEXT("session.call_tool"), TEXT("{\"name\":\"actors.spawn_actor\",\"arguments\":{}}"), ERuntimeToolError::InvalidArguments);
	});

	It("reports usage", [this]()
	{
		TestTrue(TEXT("calls"), H->CallOk(TEXT("session.get_usage_report"))->GetNumberField(TEXT("calls")) > 0);
	});
}

// ---------------------------------------------------------------------------------------------------------------------
// Reflection
// ---------------------------------------------------------------------------------------------------------------------

BEGIN_DEFINE_SPEC(FReflectionToolsetSpec, "RuntimeToolsets.Toolsets.Reflection", RUNTIMETOOLSETS_TEST_FLAGS)
	TUniquePtr<FToolTestHarness> H;
	FString Cube;
END_DEFINE_SPEC(FReflectionToolsetSpec)

void FReflectionToolsetSpec::Define()
{
	BeforeEach([this]()
	{
		H = MakeUnique<FToolTestHarness>(*this);
		H->Setup();
		Cube = H->SpawnCube(TEXT("\"location\":[1,2,3]"));
	});
	AfterEach([this]() { H.Reset(); });

	It("lists and reads properties", [this]()
	{
		const TSharedRef<FJsonObject> L = H->CallOk(TEXT("reflection.list_properties"), FString::Printf(TEXT("{\"target\":\"%s\",\"filter\":\"Tags\",\"include_values\":true}"), *Cube));
		TestEqual(TEXT("Tags listed"), static_cast<int32>(L->GetNumberField(TEXT("total"))), 1);
		const TSharedRef<FJsonObject> G = H->CallOk(TEXT("reflection.get_property"), FString::Printf(TEXT("{\"target\":\"%s\",\"path\":\"Tags\"}"), *Cube));
		TestTrue(TEXT("array"), G->GetArrayField(TEXT("value")).Num() >= 1);
		H->CallFails(TEXT("reflection.get_property"), FString::Printf(TEXT("{\"target\":\"%s\",\"path\":\"NoSuchProp\"}"), *Cube), ERuntimeToolError::NotFound);
	});

	It("writes a property and undo restores", [this]()
	{
		const FString Args = FString::Printf(TEXT("{\"target\":\"%s\",\"path\":\"Tags\"}"), *Cube);
		const int32 Before = H->CallOk(TEXT("reflection.get_property"), Args)->GetArrayField(TEXT("value")).Num();
		H->CallOk(TEXT("reflection.set_property"), FString::Printf(TEXT("{\"target\":\"%s\",\"path\":\"Tags\",\"value\":[\"a\",\"b\",\"c\",\"d\"]}"), *Cube));
		TestEqual(TEXT("set"), H->CallOk(TEXT("reflection.get_property"), Args)->GetArrayField(TEXT("value")).Num(), 4);
		H->CallOk(TEXT("history.undo"));
		TestEqual(TEXT("restored"), H->CallOk(TEXT("reflection.get_property"), Args)->GetArrayField(TEXT("value")).Num(), Before);
	});

	It("calls functions with policy", [this]()
	{
		const TSharedRef<FJsonObject> R = H->CallOk(TEXT("reflection.call_function"), FString::Printf(TEXT("{\"target\":\"%s\",\"function\":\"K2_GetActorLocation\"}"), *Cube));
		FVector V;
		TestTrue(FString::Printf(TEXT("returned vector (%s)"), *RuntimeJson::Write(R)), RuntimeJson::ToVector(R->TryGetField(TEXT("return")), V) && V.Equals(FVector(1, 2, 3), 0.01));
		H->CallFails(TEXT("reflection.call_function"), FString::Printf(TEXT("{\"target\":\"%s\",\"function\":\"K2_DestroyActor\"}"), *Cube), ERuntimeToolError::NotAllowed);
		H->CallFails(TEXT("reflection.call_function"), FString::Printf(TEXT("{\"target\":\"%s\",\"function\":\"Nope\"}"), *Cube), ERuntimeToolError::NotFound);
		H->CallFails(TEXT("reflection.call_function"), FString::Printf(TEXT("{\"target\":\"%s\",\"function\":\"K2_GetActorLocation\",\"args\":{\"x\":1}}"), *Cube), ERuntimeToolError::InvalidArguments);
	});

	It("calls static library functions with auto world context", [this]()
	{
		const TSharedRef<FJsonObject> R = H->CallOk(TEXT("reflection.call_function"), TEXT("{\"target\":\"class:/Script/Engine.KismetSystemLibrary\",\"function\":\"GetDisplayName\",\"args\":{\"Object\":\"/Script/Engine.Default__StaticMeshActor\"}}"));
		TestTrue(TEXT("string return"), R->HasField(TEXT("return")));
	});

	It("finds classes and functions", [this]()
	{
		TestTrue(TEXT("class"), H->CallOk(TEXT("reflection.find_class"), TEXT("{\"pattern\":\"StaticMeshActor\"}"))->GetNumberField(TEXT("total")) >= 1);
		TestTrue(TEXT("derived"), H->CallOk(TEXT("reflection.find_class"), TEXT("{\"pattern\":\"*Light\",\"base\":\"Actor\",\"limit\":3}"))->GetNumberField(TEXT("total")) >= 1);
		TestTrue(TEXT("functions"), H->CallOk(TEXT("reflection.list_functions"), FString::Printf(TEXT("{\"target\":\"%s\",\"filter\":\"K2_Set*\"}"), *Cube))->GetNumberField(TEXT("total")) >= 1);
	});

	It("enforces object policy", [this]()
	{
		H->CallFails(TEXT("reflection.list_properties"), TEXT("{\"target\":\"class:/Script/Engine.GameEngine\"}"), ERuntimeToolError::NotAllowed);
		H->CallFails(TEXT("reflection.list_properties"), TEXT("{\"target\":\"n999999\"}"), ERuntimeToolError::NotFound);
		H->CallFails(TEXT("reflection.get_subsystem"), TEXT("{\"kind\":\"world\",\"class\":\"NoSuchSubsystem\"}"), ERuntimeToolError::NotFound);
		H->CallFails(TEXT("reflection.get_subsystem"), TEXT("{\"kind\":\"galaxy\",\"class\":\"X\"}"), ERuntimeToolError::InvalidArguments);
		H->CallFails(TEXT("reflection.list_functions"), TEXT("{\"target\":\"n999999\"}"), ERuntimeToolError::NotFound);
		H->CallFails(TEXT("reflection.find_class"), TEXT("{\"base\":\"NoSuchBaseClass\"}"), ERuntimeToolError::NotFound);
		H->CallFails(TEXT("reflection.set_property"), FString::Printf(TEXT("{\"target\":\"%s\",\"path\":\"NoSuchProp\",\"value\":1}"), *Cube), ERuntimeToolError::NotFound);
	});

	It("reads and writes properties in batches as one undo step", [this]()
	{
		const TSharedRef<FJsonObject> G = H->CallOk(TEXT("reflection.get_properties"), FString::Printf(TEXT("{\"target\":\"%s\",\"properties\":[\"Tags\",\"InitialLifeSpan\"]}"), *Cube));
		TestTrue(TEXT("two values"), G->HasField(TEXT("Tags")) && G->HasField(TEXT("InitialLifeSpan")));
		const int32 TagsBefore = G->GetArrayField(TEXT("Tags")).Num();
		const TSharedRef<FJsonObject> S = H->CallOk(TEXT("reflection.set_properties"),
			FString::Printf(TEXT("{\"target\":\"%s\",\"values\":{\"InitialLifeSpan\":5,\"Tags\":[\"a\",\"b\",\"c\",\"d\",\"e\"]}}"), *Cube));
		TestEqual(TEXT("read back"), S->GetNumberField(TEXT("InitialLifeSpan")), 5.0);
		H->CallOk(TEXT("history.undo"));
		const TSharedRef<FJsonObject> After = H->CallOk(TEXT("reflection.get_properties"), FString::Printf(TEXT("{\"target\":\"%s\",\"properties\":[\"Tags\",\"InitialLifeSpan\"]}"), *Cube));
		TestEqual(TEXT("one undo restores both"), After->GetArrayField(TEXT("Tags")).Num(), TagsBefore);
		TestEqual(TEXT("life span restored"), After->GetNumberField(TEXT("InitialLifeSpan")), 0.0);
		// All-or-nothing: an unknown path changes nothing.
		H->CallFails(TEXT("reflection.set_properties"), FString::Printf(TEXT("{\"target\":\"%s\",\"values\":{\"InitialLifeSpan\":7,\"Nope\":1}}"), *Cube), ERuntimeToolError::NotFound);
		TestEqual(TEXT("unchanged"), H->CallOk(TEXT("reflection.get_property"), FString::Printf(TEXT("{\"target\":\"%s\",\"path\":\"InitialLifeSpan\"}"), *Cube))->GetNumberField(TEXT("value")), 0.0);
		H->CallFails(TEXT("reflection.set_properties"), FString::Printf(TEXT("{\"target\":\"%s\",\"values\":{}}"), *Cube), ERuntimeToolError::InvalidArguments);
		H->CallFails(TEXT("reflection.get_properties"), FString::Printf(TEXT("{\"target\":\"%s\",\"properties\":[\"Nope\"]}"), *Cube), ERuntimeToolError::NotFound);
	});

	It("resets properties to class defaults", [this]()
	{
		H->CallOk(TEXT("reflection.set_property"), FString::Printf(TEXT("{\"target\":\"%s\",\"path\":\"InitialLifeSpan\",\"value\":9}"), *Cube));
		const TSharedRef<FJsonObject> R = H->CallOk(TEXT("reflection.reset_properties"), FString::Printf(TEXT("{\"target\":\"%s\",\"properties\":[\"InitialLifeSpan\"]}"), *Cube));
		TestEqual(TEXT("default"), R->GetNumberField(TEXT("InitialLifeSpan")), 0.0);
		H->CallOk(TEXT("history.undo"));
		TestEqual(TEXT("undo reset"), H->CallOk(TEXT("reflection.get_property"), FString::Printf(TEXT("{\"target\":\"%s\",\"path\":\"InitialLifeSpan\"}"), *Cube))->GetNumberField(TEXT("value")), 9.0);
		H->CallFails(TEXT("reflection.reset_properties"), FString::Printf(TEXT("{\"target\":\"%s\",\"properties\":[\"Nope\"]}"), *Cube), ERuntimeToolError::NotFound);
	});

	It("describes enums and lists enum values of properties", [this]()
	{
		const TSharedRef<FJsonObject> E = H->CallOk(TEXT("reflection.describe_enum"), TEXT("{\"enum\":\"ECollisionChannel\"}"));
		bool bStatic = false;
		for (const TSharedPtr<FJsonValue>& V : E->GetArrayField(TEXT("values")))
		{
			bStatic |= V->AsObject()->GetStringField(TEXT("name")) == TEXT("ECC_WorldStatic");
		}
		TestTrue(TEXT("has ECC_WorldStatic"), bStatic);
		H->CallFails(TEXT("reflection.describe_enum"), TEXT("{\"enum\":\"ENoSuchEnumAtAll\"}"), ERuntimeToolError::NotFound);
		const TSharedRef<FJsonObject> L = H->CallOk(TEXT("reflection.list_properties"), FString::Printf(TEXT("{\"target\":\"%s#StaticMeshComponent0\",\"filter\":\"Mobility\"}"), *Cube));
		const TArray<TSharedPtr<FJsonValue>> Props = L->GetArrayField(TEXT("properties"));
		TestTrue(TEXT("enum values listed"), Props.Num() == 1 && Props[0]->AsObject()->GetArrayField(TEXT("enum_values")).Num() >= 3);
	});
}

// ---------------------------------------------------------------------------------------------------------------------
// Console
// ---------------------------------------------------------------------------------------------------------------------

BEGIN_DEFINE_SPEC(FConsoleToolsetSpec, "RuntimeToolsets.Toolsets.Console", RUNTIMETOOLSETS_TEST_FLAGS)
	TUniquePtr<FToolTestHarness> H;
END_DEFINE_SPEC(FConsoleToolsetSpec)

void FConsoleToolsetSpec::Define()
{
	BeforeEach([this]()
	{
		H = MakeUnique<FToolTestHarness>(*this);
		H->Setup();
	});
	AfterEach([this]() { H.Reset(); });

	It("reads, sets and undoes cvars", [this]()
	{
		const FString Before = H->CallOk(TEXT("console.get_cvar"), TEXT("{\"name\":\"t.MaxFPS\"}"))->GetStringField(TEXT("value"));
		const TSharedRef<FJsonObject> S = H->CallOk(TEXT("console.set_cvar"), TEXT("{\"name\":\"t.MaxFPS\",\"value\":37}"));
		TestEqual(TEXT("set"), IConsoleManager::Get().FindConsoleVariable(TEXT("t.MaxFPS"))->GetFloat(), 37.f);
		H->CallOk(TEXT("history.undo"));
		TestEqual(TEXT("restored"), IConsoleManager::Get().FindConsoleVariable(TEXT("t.MaxFPS"))->GetString(), Before);
		TestEqual(TEXT("previous"), S->GetStringField(TEXT("previous")), Before);
		TestTrue(TEXT("prefix list"), H->CallOk(TEXT("console.list_cvars"), TEXT("{\"prefix\":\"t.Max\"}"))->GetNumberField(TEXT("total")) >= 1);
		H->CallFails(TEXT("console.get_cvar"), TEXT("{\"name\":\"no.such.cvar\"}"), ERuntimeToolError::NotFound);
		H->CallFails(TEXT("console.list_cvars"), TEXT("{\"prefix\":\"t\"}"), ERuntimeToolError::InvalidArguments);
		H->CallFails(TEXT("console.set_cvar"), TEXT("{\"name\":\"no.such.cvar\",\"value\":1}"), ERuntimeToolError::NotFound);
		H->CallFails(TEXT("console.set_cvar"), TEXT("{\"name\":\"t.MaxFPS\",\"value\":[1]}"), ERuntimeToolError::InvalidArguments);
	});

	It("runs allowed commands and refuses denied ones", [this]()
	{
		H->CallOk(TEXT("console.exec_command"), TEXT("{\"command\":\"t.MaxFPS\"}"));
		H->CallFails(TEXT("console.exec_command"), TEXT("{\"command\":\"quit\"}"), ERuntimeToolError::NotAllowed);
		H->CallFails(TEXT("console.exec_command"), TEXT("{\"command\":\"  EXIT\"}"), ERuntimeToolError::NotAllowed);
		H->CallFails(TEXT("console.exec_command"), TEXT("{\"command\":\"stat fps|quit\"}"), ERuntimeToolError::NotAllowed);
		H->CallFails(TEXT("console.exec_command"), TEXT("{\"command\":\"  \"}"), ERuntimeToolError::InvalidArguments);
	});

	It("sets scalability and undo restores", [this]()
	{
		const double Before = H->CallOk(TEXT("console.get_user_settings"))->GetObjectField(TEXT("scalability"))->GetNumberField(TEXT("shadows"));
		const double Target = Before == 0 ? 1 : 0;
		H->CallOk(TEXT("console.set_scalability"), FString::Printf(TEXT("{\"shadows\":%d}"), static_cast<int32>(Target)));
		TestEqual(TEXT("set"), H->CallOk(TEXT("console.get_user_settings"))->GetObjectField(TEXT("scalability"))->GetNumberField(TEXT("shadows")), Target);
		H->CallOk(TEXT("history.undo"));
		TestEqual(TEXT("restored"), H->CallOk(TEXT("console.get_user_settings"))->GetObjectField(TEXT("scalability"))->GetNumberField(TEXT("shadows")), Before);
		H->CallFails(TEXT("console.set_scalability"), TEXT("{\"shadows\":7}"), ERuntimeToolError::InvalidArguments);
		H->CallFails(TEXT("console.set_scalability"), TEXT("{}"), ERuntimeToolError::InvalidArguments);
	});
}

// ---------------------------------------------------------------------------------------------------------------------
// Log
// ---------------------------------------------------------------------------------------------------------------------

BEGIN_DEFINE_SPEC(FLogToolsetSpec, "RuntimeToolsets.Toolsets.Log", RUNTIMETOOLSETS_TEST_FLAGS)
	TUniquePtr<FToolTestHarness> H;
	FDelegateHandle EventHandle;
END_DEFINE_SPEC(FLogToolsetSpec)

void FLogToolsetSpec::Define()
{
	BeforeEach([this]()
	{
		H = MakeUnique<FToolTestHarness>(*this);
		H->Setup();
	});
	AfterEach([this]()
	{
		IRuntimeToolsetsCore::Get().OnEvent().Remove(EventHandle);
		H.Reset();
	});

	It("tails the log with filters", [this]()
	{
		const FString Marker = FGuid::NewGuid().ToString();
		UE_LOG(LogRuntimeToolsetsTestMarker, Display, TEXT("marker %s"), *Marker);
		const TSharedRef<FJsonObject> R = H->CallOk(TEXT("log.tail_log"), FString::Printf(TEXT("{\"contains\":\"%s\",\"category\":\"LogRuntimeToolsetsTestMarker\"}"), *Marker));
		TestEqual(TEXT("one line"), R->GetArrayField(TEXT("lines")).Num(), 1);
		const int64 Last = static_cast<int64>(R->GetNumberField(TEXT("last_seq")));
		const TSharedRef<FJsonObject> Since = H->CallOk(TEXT("log.tail_log"), FString::Printf(TEXT("{\"since_seq\":%lld,\"contains\":\"%s\"}"), Last, *Marker));
		TestEqual(TEXT("nothing new"), Since->GetArrayField(TEXT("lines")).Num(), 0);
		H->CallFails(TEXT("log.tail_log"), TEXT("{\"min_verbosity\":\"Loud\"}"), ERuntimeToolError::InvalidArguments);
		H->CallFails(TEXT("log.subscribe_log_category"), TEXT("{\"category\":\"\"}"), ERuntimeToolError::InvalidArguments);
	});

	It("reads stats", [this]()
	{
		const TSharedRef<FJsonObject> R = H->CallOk(TEXT("log.get_stat"), TEXT("{\"names\":[\"fps\",\"memory_mb\",\"nodes\"]}"));
		TestTrue(TEXT("memory"), R->GetNumberField(TEXT("memory_mb")) > 0);
		H->CallFails(TEXT("log.get_stat"), TEXT("{\"names\":[\"bogus\"]}"), ERuntimeToolError::InvalidArguments);
	});

	LatentIt("pushes subscribed categories as events", FTimespan::FromSeconds(10), [this](const FDoneDelegate& Done)
	{
		const FString Session = H->GetSession();
		const FString Marker = FGuid::NewGuid().ToString();
		H->CallOk(TEXT("log.subscribe_log_category"), TEXT("{\"category\":\"LogRuntimeToolsetsTestMarker\"}"));
		EventHandle = IRuntimeToolsetsCore::Get().OnEvent().AddLambda([this, Session, Marker, Done](const FString& EventSession, const TSharedRef<FJsonObject>& Event)
		{
			if (EventSession == Session && Event->GetStringField(TEXT("message")).Contains(Marker))
			{
				// Copy before removing: removal frees this lambda's captures.
				const FDoneDelegate DoneCopy = Done;
				IRuntimeToolsetsCore::Get().OnEvent().Remove(EventHandle);
				DoneCopy.Execute();
			}
		});
		UE_LOG(LogRuntimeToolsetsTestMarker, Display, TEXT("event %s"), *Marker);
	});
}

// ---------------------------------------------------------------------------------------------------------------------
// Capture
// ---------------------------------------------------------------------------------------------------------------------

BEGIN_DEFINE_SPEC(FCaptureToolsetSpec, "RuntimeToolsets.Toolsets.Capture", RUNTIMETOOLSETS_TEST_FLAGS)
	TUniquePtr<FToolTestHarness> H;
END_DEFINE_SPEC(FCaptureToolsetSpec)

void FCaptureToolsetSpec::Define()
{
	BeforeEach([this]()
	{
		H = MakeUnique<FToolTestHarness>(*this);
		H->Setup();
	});
	AfterEach([this]() { H.Reset(); });

	It("renders a scene capture as JPEG, or reports unsupported without rendering", [this]()
	{
		H->SpawnCube(TEXT("\"location\":[300,0,0]"));
		const FToolResult R = H->Call(TEXT("capture.scene_capture"), TEXT("{\"location\":[0,0,0],\"look_at\":[300,0,0],\"width\":320,\"height\":180,\"max_dim\":256}"));
		if (!FApp::CanEverRender())
		{
			TestTrue(TEXT("unsupported"), R.Error == ERuntimeToolError::Unsupported);
			return;
		}
		if (TestTrue(TEXT("ok"), R.IsOk()) && TestEqual(TEXT("one image"), R.Attachments.Num(), 1))
		{
			TestEqual(TEXT("jpeg"), R.Attachments[0].MimeType, FString(TEXT("image/jpeg")));
			TestTrue(TEXT("downscaled"), R.Attachments[0].Width <= 256);
			TestTrue(TEXT("bytes"), R.Attachments[0].Bytes.Num() > 100);
		}
	});

	It("refuses to nest the async screenshot inline", [this]()
	{
		// ExecuteInline is sync-only; async tools are reached through Invoke or session.call_tool.
		H->CallFails(TEXT("capture.screenshot"), TEXT("{}"), ERuntimeToolError::Unsupported);
	});

	It("refuses render-target reads of other objects", [this]()
	{
		const FString Cube = H->SpawnCube();
		H->CallFails(TEXT("capture.capture_render_target"), FString::Printf(TEXT("{\"target\":\"%s\"}"), *Cube), ERuntimeToolError::InvalidArguments);
		H->CallFails(TEXT("capture.capture_render_target"), TEXT("{\"target\":\"n999999\"}"), ERuntimeToolError::NotFound);
		H->CallFails(TEXT("capture.scene_capture"), TEXT("{\"format\":\"gif\"}"), ERuntimeToolError::InvalidArguments);
	});
}

#endif
