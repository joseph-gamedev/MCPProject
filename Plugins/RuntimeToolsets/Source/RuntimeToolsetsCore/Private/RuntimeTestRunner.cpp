#include "RuntimeTestRunner.h"
#include "RuntimeJson.h"
#include "RuntimeToolsetsLog.h"
#include "HAL/PlatformMisc.h"
#include "Misc/OutputDeviceRedirector.h"

FRuntimeTestRunner& FRuntimeTestRunner::Get()
{
	static FRuntimeTestRunner Instance;
	return Instance;
}

static TArray<FAutomationTestInfo> MatchingTests(const FString& Filter)
{
	FAutomationTestFramework& Framework = FAutomationTestFramework::Get();
	Framework.SetRequestedTestFilter(EAutomationTestFlags_FilterMask);
	TArray<FAutomationTestInfo> Infos;
	Framework.GetValidTestNames(Infos);
	TArray<FAutomationTestInfo> Out;
	for (const FAutomationTestInfo& Info : Infos)
	{
		if (Filter.IsEmpty() || Info.GetDisplayName().StartsWith(Filter) || Info.GetFullTestPath().StartsWith(Filter))
		{
			Out.Add(Info);
		}
	}
	return Out;
}

TArray<FString> FRuntimeTestRunner::ListTests(const FString& Filter)
{
	TArray<FString> Out;
	for (const FAutomationTestInfo& Info : MatchingTests(Filter))
	{
		Out.Add(Info.GetFullTestPath());
	}
	return Out;
}

bool FRuntimeTestRunner::Start(const FString& InFilter, bool bQuitWhenDone, FString& OutError)
{
	if (IsRunning())
	{
		OutError = TEXT("a test run is already in progress");
		return false;
	}
	Filter = InFilter;
	Queue = MatchingTests(Filter);
	if (Queue.Num() == 0)
	{
		OutError = FString::Printf(TEXT("no tests match '%s'"), *Filter);
		return false;
	}
	bQuit = bQuitWhenDone;
	Total = Queue.Num();
	Passed = 0;
	Failures.Reset();
	bTestRunning = false;
	UE_LOG(LogRuntimeToolsetsCore, Display, TEXT("Running %d tests matching '%s'"), Total, *Filter);
	TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FRuntimeTestRunner::Tick));
	return true;
}

void FRuntimeTestRunner::Stop()
{
	if (TickHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
		TickHandle.Reset();
	}
}

bool FRuntimeTestRunner::Tick(float)
{
	FAutomationTestFramework& Framework = FAutomationTestFramework::Get();
	if (bTestRunning)
	{
		// Normally managed by the automation controller: latent execution and StopTest require the flag;
		// it is only held during our own calls so other engine systems do not act on our test between ticks.
		GIsAutomationTesting = true;
		if (!Framework.ExecuteLatentCommands())
		{
			GIsAutomationTesting = false;
			return true;
		}
		FAutomationTestExecutionInfo Exec;
		const bool bStarted = Framework.GetCurrentTest() != nullptr;
		const bool bOk = bStarted && Framework.StopTest(Exec);
		bTestRunning = false;
		const FString Name = Current.GetFullTestPath();
		if (bOk)
		{
			++Passed;
			UE_LOG(LogRuntimeToolsetsCore, Display, TEXT("PASS %s"), *Name);
		}
		else
		{
			FFailure& F = Failures.AddDefaulted_GetRef();
			F.Test = Name;
			if (!bStarted)
			{
				F.Errors.Add(TEXT("test was not running when its latent work finished"));
			}
			for (const FAutomationExecutionEntry& Entry : Exec.GetEntries())
			{
				if (Entry.Event.Type == EAutomationEventType::Error)
				{
					F.Errors.Add(Entry.Event.Message);
					UE_LOG(LogRuntimeToolsetsCore, Error, TEXT("  %s"), *Entry.Event.Message);
				}
			}
			UE_LOG(LogRuntimeToolsetsCore, Error, TEXT("FAIL %s"), *Name);
		}
	}
	if (Queue.Num() == 0)
	{
		TArray<FString> Names;
		for (const FFailure& F : Failures)
		{
			Names.Add(F.Test);
		}
		UE_LOG(LogRuntimeToolsetsCore, Display, TEXT("RuntimeToolsets tests: %d passed, %d failed%s%s"), Passed, Failures.Num(),
			Failures.Num() ? TEXT(": ") : TEXT(""), *FString::Join(Names, TEXT(", ")));
		TickHandle.Reset();
		GIsAutomationTesting = false;
		if (bQuit)
		{
			// Forced exit so the process exit code reflects the result (CI).
			GLog->Flush();
			FPlatformMisc::RequestExitWithStatus(true, Failures.Num() > 0 ? 1 : 0);
		}
		return false;
	}
	Current = Queue[0];
	Queue.RemoveAt(0);
	// Spec tests share one class: the parameter selects the individual It().
	FString ToRun = Current.GetTestName();
	const FString Parameter = Current.GetTestParameter();
	if (!Parameter.IsEmpty() && !ToRun.EndsWith(Parameter))
	{
		ToRun += TEXT(" ") + Parameter;
	}
	// StartTestByName treats a set flag as "a test is still running" and tries to stop it.
	GIsAutomationTesting = false;
	Framework.StartTestByName(ToRun, 0, Current.GetFullTestPath());
	GIsAutomationTesting = false;
	bTestRunning = true;
	return true;
}

TSharedRef<FJsonObject> FRuntimeTestRunner::GetStatus(int32 MaxFailures) const
{
	TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
	Out->SetBoolField(TEXT("running"), IsRunning());
	Out->SetStringField(TEXT("filter"), Filter);
	Out->SetNumberField(TEXT("total"), Total);
	Out->SetNumberField(TEXT("completed"), Passed + Failures.Num());
	Out->SetNumberField(TEXT("passed"), Passed);
	Out->SetNumberField(TEXT("failed"), Failures.Num());
	if (IsRunning() && bTestRunning)
	{
		Out->SetStringField(TEXT("current"), Current.GetFullTestPath());
	}
	TArray<TSharedPtr<FJsonValue>> Items;
	for (int32 i = 0; i < Failures.Num() && i < MaxFailures; ++i)
	{
		TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
		J->SetStringField(TEXT("test"), Failures[i].Test);
		TArray<FString> Errors = Failures[i].Errors;
		if (Errors.Num() > 5)
		{
			Errors.SetNum(5);
		}
		J->SetField(TEXT("errors"), RuntimeJson::Strings(Errors));
		Items.Add(MakeShared<FJsonValueObject>(J));
	}
	Out->SetArrayField(TEXT("failures"), Items);
	return Out;
}
