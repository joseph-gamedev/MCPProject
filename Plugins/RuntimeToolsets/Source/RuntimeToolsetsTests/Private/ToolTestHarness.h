#pragma once

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "RuntimeToolTypes.h"

#if WITH_DEV_AUTOMATION_TESTS

class UWorld;
class FRuntimeSceneGraph;

/** Isolated world + unique session for one test. Runs tools synchronously through the real executor. */
class FToolTestHarness
{
public:
	explicit FToolTestHarness(FAutomationTestBase& InTest);
	~FToolTestHarness();

	/** Creates a fresh world and points the core at it. */
	void Setup();
	/**
	 * Uses the real game world (packaged game / PIE) instead of an isolated one. Returns false when there is none
	 * (e.g. headless editor). Teardown undoes everything this session did, leaving the live world as it was.
	 */
	bool SetupLiveWorld();

	/** Ends the session and destroys the world (or rolls back the live world). Safe to call twice. */
	void Teardown();

	FToolResult Call(const FString& Tool, const FString& Json = TEXT("{}"));
	/** Calls and requires success. Returns the payload (never null). */
	TSharedRef<FJsonObject> CallOk(const FString& Tool, const FString& Json = TEXT("{}"));
	/** Calls and requires the given error code. */
	void CallFails(const FString& Tool, const FString& Json, ERuntimeToolError Expected);

	UWorld* GetWorld() const { return World; }
	FRuntimeSceneGraph* GetGraph() const;
	const FString& GetSession() const { return Session; }

	/** Spawns a cube via the tools and returns its node id. */
	FString SpawnCube(const FString& Extra = FString());

private:
	FAutomationTestBase& Test;
	UWorld* World = nullptr;
	FString Session;
	bool bLive = false;
};

/** Spec flags shared by all RuntimeToolsets tests. */
#define RUNTIMETOOLSETS_TEST_FLAGS (EAutomationTestFlags::ProductFilter | EAutomationTestFlags_ApplicationContextMask)

#endif
