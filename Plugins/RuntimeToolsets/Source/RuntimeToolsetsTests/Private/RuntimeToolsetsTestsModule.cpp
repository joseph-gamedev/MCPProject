#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "HAL/IConsoleManager.h"
#include "Misc/AutomationTest.h"
#include "Modules/ModuleManager.h"
#include "Misc/OutputDeviceRedirector.h"

DEFINE_LOG_CATEGORY_STATIC(LogRuntimeToolsetsTests, Log, All);

#if WITH_DEV_AUTOMATION_TESTS

/**
 * Runs automation tests inside a packaged game, where the AutomationController is not available.
 * Usage: RuntimeToolsets.RunTests [filter] [quit]
 * One test per tick so latent tests get frames. Prints a summary line: "RuntimeToolsets tests: N passed, M failed".
 */
class FPackagedTestRunner
{
public:
	void Start(const FString& InFilter, bool bInQuit)
	{
		if (TickHandle.IsValid())
		{
			UE_LOG(LogRuntimeToolsetsTests, Warning, TEXT("Tests are already running"));
			return;
		}
		Filter = InFilter.IsEmpty() ? TEXT("RuntimeToolsets.") : InFilter;
		bQuit = bInQuit;
		FAutomationTestFramework& Framework = FAutomationTestFramework::Get();
		Framework.SetRequestedTestFilter(EAutomationTestFlags_FilterMask);
		TArray<FAutomationTestInfo> Infos;
		Framework.GetValidTestNames(Infos);
		Queue.Reset();
		for (const FAutomationTestInfo& Info : Infos)
		{
			if (Info.GetDisplayName().StartsWith(Filter) || Info.GetFullTestPath().StartsWith(Filter))
			{
				Queue.Add(Info);
			}
		}
		Passed = Failed = 0;
		Failures.Reset();
		bRunning = false;
		UE_LOG(LogRuntimeToolsetsTests, Display, TEXT("Running %d tests matching '%s'"), Queue.Num(), *Filter);
		TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FPackagedTestRunner::Tick));
	}

	void Stop()
	{
		if (TickHandle.IsValid())
		{
			FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
			TickHandle.Reset();
		}
	}

private:
	bool Tick(float)
	{
		FAutomationTestFramework& Framework = FAutomationTestFramework::Get();
		if (bRunning)
		{
			// Normally managed by the automation controller: latent execution and StopTest require it set.
			GIsAutomationTesting = true;
			if (!Framework.ExecuteLatentCommands())
			{
				// Only hold the flag during our own calls so other engine systems do not act on our test between ticks.
				GIsAutomationTesting = false;
				return true; // latent work still pending
			}
			FAutomationTestExecutionInfo Exec;
			// A test that never started (bad name) leaves no current test; StopTest would crash.
			const bool bStarted = Framework.GetCurrentTest() != nullptr;
			if (!bStarted)
			{
				UE_LOG(LogRuntimeToolsetsTests, Error, TEXT("  test was not running when its latent work finished"));
			}
			const bool bOk = bStarted && Framework.StopTest(Exec);
			bRunning = false;
			const FString Name = Current.GetFullTestPath();
			if (bOk)
			{
				++Passed;
				UE_LOG(LogRuntimeToolsetsTests, Display, TEXT("PASS %s"), *Name);
			}
			else
			{
				++Failed;
				Failures.Add(Name);
				for (const FAutomationExecutionEntry& Entry : Exec.GetEntries())
				{
					if (Entry.Event.Type != EAutomationEventType::Info)
					{
						UE_LOG(LogRuntimeToolsetsTests, Error, TEXT("  %s"), *Entry.Event.Message);
					}
				}
				UE_LOG(LogRuntimeToolsetsTests, Error, TEXT("FAIL %s"), *Name);
			}
		}
		if (Queue.Num() == 0)
		{
			UE_LOG(LogRuntimeToolsetsTests, Display, TEXT("RuntimeToolsets tests: %d passed, %d failed%s%s"), Passed, Failed,
				Failures.Num() ? TEXT(": ") : TEXT(""), *FString::Join(Failures, TEXT(", ")));
			TickHandle.Reset();
			GIsAutomationTesting = false;
			if (bQuit)
			{
				// Forced exit so the process exit code reflects the result (CI).
				GLog->Flush();
				FPlatformMisc::RequestExitWithStatus(true, Failed > 0 ? 1 : 0);
			}
			return false;
		}
		Current = Queue[0];
		Queue.RemoveAt(0);
		// StartTestByName treats a set flag as "a test is still running" and tries to stop it.
		GIsAutomationTesting = false;
		// Spec tests share one class: the parameter selects the individual It().
		FString ToRun = Current.GetTestName();
		const FString Parameter = Current.GetTestParameter();
		if (!Parameter.IsEmpty() && !ToRun.EndsWith(Parameter))
		{
			ToRun += TEXT(" ") + Parameter;
		}
		UE_LOG(LogRuntimeToolsetsTests, Verbose, TEXT("Starting '%s'"), *ToRun);
		Framework.StartTestByName(ToRun, 0, Current.GetFullTestPath());
		GIsAutomationTesting = false;
		if (!Framework.GetCurrentTest())
		{
			UE_LOG(LogRuntimeToolsetsTests, Warning, TEXT("'%s' finished during start"), *ToRun);
		}
		bRunning = true;
		return true;
	}

	FString Filter;
	bool bQuit = false;
	TArray<FAutomationTestInfo> Queue;
	FAutomationTestInfo Current;
	bool bRunning = false;
	int32 Passed = 0;
	int32 Failed = 0;
	TArray<FString> Failures;
	FTSTicker::FDelegateHandle TickHandle;
};

#endif

class FRuntimeToolsetsTestsModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
#if WITH_DEV_AUTOMATION_TESTS
		Command = IConsoleManager::Get().RegisterConsoleCommand(TEXT("RuntimeToolsets.RunTests"),
			TEXT("RuntimeToolsets.RunTests [filter] [quit] - run automation tests in this process (works in packaged Development builds)"),
			FConsoleCommandWithArgsDelegate::CreateLambda([this](const TArray<FString>& Args)
			{
				FString Filter;
				bool bQuit = false;
				for (const FString& A : Args)
				{
					if (A.Equals(TEXT("quit"), ESearchCase::IgnoreCase))
					{
						bQuit = true;
					}
					else
					{
						Filter = A;
					}
				}
				Runner.Start(Filter, bQuit);
			}),
			ECVF_Default);
#endif
	}

	virtual void ShutdownModule() override
	{
#if WITH_DEV_AUTOMATION_TESTS
		Runner.Stop();
		if (Command)
		{
			IConsoleManager::Get().UnregisterConsoleObject(Command);
			Command = nullptr;
		}
#endif
	}

private:
#if WITH_DEV_AUTOMATION_TESTS
	FPackagedTestRunner Runner;
	IConsoleObject* Command = nullptr;
#endif
};

IMPLEMENT_MODULE(FRuntimeToolsetsTestsModule, RuntimeToolsetsTests)
