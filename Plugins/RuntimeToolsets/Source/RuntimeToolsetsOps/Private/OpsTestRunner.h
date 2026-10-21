#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Misc/AutomationTest.h"
#include "RuntimeOpsToolset.h"

/**
 * Test filter expressions (Epic's AutomationTestToolset syntax): expressions joined by '+', a test matches if any matches.
 * "StartsWith:X" / "^X" prefix, "X$" suffix, "Group:Name" (AutomationControllerSettings Groups config), bare = substring.
 */
class FOpsTestFilter
{
public:
	/** Parses Expression. False (with a reason) when a group is unknown. */
	bool Parse(const FString& Expression, FString& OutError);
	bool Matches(const FString& TestPath) const;
	bool IsEmpty() const { return Terms.Num() == 0; }

	/** Group names configured in AutomationControllerSettings (sorted). */
	static TArray<FString> GroupNames();

private:
	struct FTerm
	{
		FString Text;
		bool bStart = false;
		bool bEnd = false;
	};
	TArray<FTerm> Terms;
};

/**
 * In-process automation test runner for the ops toolset (one test per frame so latent tests get frames), keeping per-test
 * results. Based on the core FRuntimeTestRunner, which only supports prefix filters and keeps failures only.
 */
class FOpsTestRunner
{
public:
	static FOpsTestRunner& Get();

	/** Every test path matching Filter, sorted. */
	static TArray<FString> ListTests(const FOpsTestFilter& Filter);

	/** Starts the matching tests. Tests must not be running (checked by the caller). */
	bool Start(const FString& FilterText, const FOpsTestFilter& Filter, FString& OutError);
	/** Stops the run: the current test is stopped, queued tests are skipped. */
	void Stop();
	bool IsRunning() const { return TickHandle.IsValid(); }

	const FString& GetFilter() const { return FilterText; }
	int32 GetTotal() const { return Total; }
	const FString& GetCurrent() const { return CurrentPath; }
	const TArray<FOpsToolTestResult>& GetResults() const { return Results; }

private:
	bool Tick(float DeltaTime);
	void FinishCurrent(bool bStopped);

	FString FilterText;
	TArray<FAutomationTestInfo> Queue;
	FAutomationTestInfo Current;
	FString CurrentPath;
	double CurrentStart = 0.0;
	bool bTestRunning = false;
	int32 Total = 0;
	TArray<FOpsToolTestResult> Results;
	FTSTicker::FDelegateHandle TickHandle;
};
