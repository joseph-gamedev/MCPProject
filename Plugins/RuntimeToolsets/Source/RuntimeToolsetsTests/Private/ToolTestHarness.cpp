#include "ToolTestHarness.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "RuntimeJson.h"
#include "RuntimeToolsetsCore.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

FToolTestHarness::FToolTestHarness(FAutomationTestBase& InTest)
	: Test(InTest)
{
}

FToolTestHarness::~FToolTestHarness()
{
	Teardown();
}

void FToolTestHarness::Setup()
{
	Teardown();
	Session = FString::Printf(TEXT("test-%s"), *FGuid::NewGuid().ToString(EGuidFormats::Short));
	World = UWorld::CreateWorld(EWorldType::Game, false, MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("RuntimeToolsetsTestWorld")));
	// Like a real game world: actors must be initialized or AActor::ProcessEvent skips script calls.
	World->InitializeActorsForPlay(FURL());
	IRuntimeToolsetsCore::Get().SetWorldOverride(World);
}

bool FToolTestHarness::SetupLiveWorld()
{
	Teardown();
	IRuntimeToolsetsCore::Get().SetWorldOverride(nullptr);
	UWorld* Live = IRuntimeToolsetsCore::Get().FindWorld();
	if (!Live || !GEngine || !GEngine->GameViewport || !Live->GetFirstPlayerController())
	{
		return false;
	}
	Session = FString::Printf(TEXT("live-test-%s"), *FGuid::NewGuid().ToString(EGuidFormats::Short));
	World = Live;
	bLive = true;
	return true;
}

void FToolTestHarness::Teardown()
{
	if (!World)
	{
		return;
	}
	if (bLive)
	{
		// Roll the live world back to how the test found it.
		while (Call(TEXT("history.undo"), TEXT("{\"count\":1000}")).IsOk())
		{
		}
		IRuntimeToolsetsCore::Get().EndSession(Session);
		World = nullptr;
		bLive = false;
		return;
	}
	IRuntimeToolsetsCore::Get().EndSession(Session);
	IRuntimeToolsetsCore::Get().SetWorldOverride(nullptr);
	World->DestroyWorld(false);
	if (World->IsRooted())
	{
		World->RemoveFromRoot();
	}
	World = nullptr;
}

FRuntimeSceneGraph* FToolTestHarness::GetGraph() const
{
	return IRuntimeToolsetsCore::Get().GetSceneGraph(World);
}

FToolResult FToolTestHarness::Call(const FString& Tool, const FString& Json)
{
	FToolCallerContext Caller;
	Caller.SessionId = Session;
	Caller.ClientName = TEXT("test");
	TSharedPtr<FJsonObject> Args = RuntimeJson::Parse(Json);
	if (!Args.IsValid())
	{
		Test.AddError(FString::Printf(TEXT("test bug: bad JSON for %s: %s"), *Tool, *Json));
		return FToolResult::Fail(ERuntimeToolError::Internal, TEXT("bad test json"));
	}
	return IRuntimeToolsetsCore::Get().ExecuteInline(Tool, Args, Caller, 0);
}

TSharedRef<FJsonObject> FToolTestHarness::CallOk(const FString& Tool, const FString& Json)
{
	const FToolResult R = Call(Tool, Json);
	if (!R.IsOk())
	{
		Test.AddError(FString::Printf(TEXT("%s %s failed: %s"), *Tool, *Json, *R.ToJsonString()));
		return MakeShared<FJsonObject>();
	}
	return R.Payload.IsValid() ? R.Payload.ToSharedRef() : MakeShared<FJsonObject>();
}

void FToolTestHarness::CallFails(const FString& Tool, const FString& Json, ERuntimeToolError Expected)
{
	const FToolResult R = Call(Tool, Json);
	if (R.Error != Expected)
	{
		Test.AddError(FString::Printf(TEXT("%s %s: expected %s, got %s"), *Tool, *Json, LexToString(Expected), *R.ToJsonString()));
	}
}

FString FToolTestHarness::SpawnCube(const FString& Extra)
{
	const FString Json = Extra.IsEmpty() ? TEXT("{\"shape\":\"cube\"}") : FString::Printf(TEXT("{\"shape\":\"cube\",%s}"), *Extra);
	return CallOk(TEXT("actors.spawn_actor"), Json)->GetStringField(TEXT("id"));
}

#endif
