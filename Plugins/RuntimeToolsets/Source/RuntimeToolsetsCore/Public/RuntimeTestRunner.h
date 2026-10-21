#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Dom/JsonObject.h"
#include "Misc/AutomationTest.h"

/**
 * Runs automation tests inside the current process, including packaged Development games where the
 * AutomationController is not available. One test per tick so latent tests get frames. Game thread only.
 */
class RUNTIMETOOLSETSCORE_API FRuntimeTestRunner
{
public:
	static FRuntimeTestRunner& Get();

	/** Starts every test whose path starts with Filter. False (with a reason) if a run is in progress or nothing matches. */
	bool Start(const FString& Filter, bool bQuitWhenDone, FString& OutError);
	void Stop();
	bool IsRunning() const { return TickHandle.IsValid(); }

	/** {running, filter, total, completed, passed, failed, current, failures:[{test, errors}]} */
	TSharedRef<FJsonObject> GetStatus(int32 MaxFailures = 20) const;

	/** Every test path starting with Filter. */
	static TArray<FString> ListTests(const FString& Filter);

private:
	bool Tick(float DeltaTime);

	struct FFailure
	{
		FString Test;
		TArray<FString> Errors;
	};

	FString Filter;
	bool bQuit = false;
	TArray<FAutomationTestInfo> Queue;
	FAutomationTestInfo Current;
	bool bTestRunning = false;
	int32 Total = 0;
	int32 Passed = 0;
	TArray<FFailure> Failures;
	FTSTicker::FDelegateHandle TickHandle;
};
