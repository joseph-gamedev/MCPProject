#include "OpsTestRunner.h"
#include "Internationalization/Regex.h"
#include "Misc/ConfigCacheIni.h"

namespace
{
	const TCHAR* GroupsSection = TEXT("/Script/AutomationController.AutomationControllerSettings");

	/** Raw "+Groups=(Name=\"Smoke\",Filters=((Contains=\"X\",MatchFromStart=True),...))" config lines. */
	TArray<FString> GroupLines()
	{
		TArray<FString> Lines;
		if (GConfig)
		{
			GConfig->GetArray(GroupsSection, TEXT("Groups"), Lines, GEngineIni);
		}
		return Lines;
	}

	FString GroupName(const FString& Line)
	{
		const FRegexPattern Pattern(TEXT("Name\\s*=\\s*\"([^\"]*)\""));
		FRegexMatcher M(Pattern, Line);
		return M.FindNext() ? M.GetCaptureGroup(1) : FString();
	}
}

TArray<FString> FOpsTestFilter::GroupNames()
{
	TArray<FString> Names;
	for (const FString& Line : GroupLines())
	{
		const FString N = GroupName(Line);
		if (!N.IsEmpty())
		{
			Names.AddUnique(N);
		}
	}
	Names.Sort();
	return Names;
}

bool FOpsTestFilter::Parse(const FString& Expression, FString& OutError)
{
	Terms.Reset();
	TArray<FString> Parts;
	Expression.ParseIntoArray(Parts, TEXT("+"), true);
	for (FString Part : Parts)
	{
		Part.TrimStartAndEndInline();
		if (Part.IsEmpty())
		{
			continue;
		}
		if (Part.StartsWith(TEXT("Group:"), ESearchCase::IgnoreCase))
		{
			const FString Name = Part.Mid(6).TrimStartAndEnd();
			bool bFound = false;
			for (const FString& Line : GroupLines())
			{
				if (!GroupName(Line).Equals(Name, ESearchCase::IgnoreCase))
				{
					continue;
				}
				bFound = true;
				const FRegexPattern Pattern(TEXT("\\(\\s*Contains\\s*=\\s*\"([^\"]*)\"([^)]*)\\)"));
				FRegexMatcher M(Pattern, Line);
				while (M.FindNext())
				{
					FTerm T;
					T.Text = M.GetCaptureGroup(1);
					const FString Rest = M.GetCaptureGroup(2);
					T.bStart = Rest.Contains(TEXT("MatchFromStart=True"), ESearchCase::IgnoreCase);
					T.bEnd = Rest.Contains(TEXT("MatchFromEnd=True"), ESearchCase::IgnoreCase);
					Terms.Add(T);
				}
			}
			if (!bFound)
			{
				const TArray<FString> Names = GroupNames();
				OutError = FString::Printf(TEXT("no test group '%s' (groups: %s)"), *Name,
					Names.Num() ? *FString::Join(Names, TEXT(", ")) : TEXT("none configured in AutomationControllerSettings"));
				return false;
			}
			continue;
		}
		FTerm T;
		if (Part.StartsWith(TEXT("StartsWith:"), ESearchCase::IgnoreCase))
		{
			T.Text = Part.Mid(11).TrimStartAndEnd();
			T.bStart = true;
		}
		else
		{
			T.Text = Part;
			if (T.Text.StartsWith(TEXT("^")))
			{
				T.Text.RightChopInline(1);
				T.bStart = true;
			}
			if (T.Text.EndsWith(TEXT("$")))
			{
				T.Text.LeftChopInline(1);
				T.bEnd = true;
			}
		}
		Terms.Add(T);
	}
	return true;
}

bool FOpsTestFilter::Matches(const FString& Path) const
{
	if (Terms.Num() == 0)
	{
		return true;
	}
	for (const FTerm& T : Terms)
	{
		bool bMatch = true;
		if (T.bStart || T.bEnd)
		{
			bMatch = (!T.bStart || Path.StartsWith(T.Text)) && (!T.bEnd || Path.EndsWith(T.Text));
		}
		else
		{
			bMatch = Path.Contains(T.Text);
		}
		if (bMatch)
		{
			return true;
		}
	}
	return false;
}

// ---- runner -------------------------------------------------------------------------------------------------------------

FOpsTestRunner& FOpsTestRunner::Get()
{
	static FOpsTestRunner Instance;
	return Instance;
}

static TArray<FAutomationTestInfo> MatchingTests(const FOpsTestFilter& Filter)
{
	FAutomationTestFramework& Framework = FAutomationTestFramework::Get();
	Framework.SetRequestedTestFilter(EAutomationTestFlags_FilterMask);
	TArray<FAutomationTestInfo> Infos;
	Framework.GetValidTestNames(Infos);
	TArray<FAutomationTestInfo> Out;
	for (const FAutomationTestInfo& Info : Infos)
	{
		if (Filter.Matches(Info.GetFullTestPath()))
		{
			Out.Add(Info);
		}
	}
	Out.Sort([](const FAutomationTestInfo& A, const FAutomationTestInfo& B) { return A.GetFullTestPath() < B.GetFullTestPath(); });
	return Out;
}

TArray<FString> FOpsTestRunner::ListTests(const FOpsTestFilter& Filter)
{
	TArray<FString> Out;
	for (const FAutomationTestInfo& Info : MatchingTests(Filter))
	{
		Out.Add(Info.GetFullTestPath());
	}
	return Out;
}

bool FOpsTestRunner::Start(const FString& InFilterText, const FOpsTestFilter& Filter, FString& OutError)
{
	if (IsRunning())
	{
		OutError = TEXT("a test run is already in progress");
		return false;
	}
	TArray<FAutomationTestInfo> Matching = MatchingTests(Filter);
	if (Matching.Num() == 0)
	{
		OutError = FString::Printf(TEXT("no tests match '%s'"), *InFilterText);
		return false;
	}
	FilterText = InFilterText;
	Queue = MoveTemp(Matching);
	Total = Queue.Num();
	Results.Reset();
	CurrentPath.Reset();
	bTestRunning = false;
	TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FOpsTestRunner::Tick));
	return true;
}

void FOpsTestRunner::FinishCurrent(bool bStopped)
{
	FAutomationTestFramework& Framework = FAutomationTestFramework::Get();
	FAutomationTestExecutionInfo Exec;
	const bool bStarted = Framework.GetCurrentTest() != nullptr;
	GIsAutomationTesting = true;
	const bool bOk = bStarted && Framework.StopTest(Exec);
	GIsAutomationTesting = false;
	bTestRunning = false;
	FOpsToolTestResult* R = Results.FindByPredicate([this](const FOpsToolTestResult& X) { return X.Test == CurrentPath && X.State == EOpsToolTestState::Running; });
	if (!R)
	{
		R = &Results.AddDefaulted_GetRef();
		R->Test = CurrentPath;
	}
	R->DurationS = static_cast<float>(FPlatformTime::Seconds() - CurrentStart);
	R->State = bStopped ? EOpsToolTestState::Stopped : bOk ? EOpsToolTestState::Passed : EOpsToolTestState::Failed;
	if (!bStarted)
	{
		R->Errors.Add(TEXT("test was not running when its latent work finished"));
	}
	for (const FAutomationExecutionEntry& Entry : Exec.GetEntries())
	{
		if (Entry.Event.Type == EAutomationEventType::Error)
		{
			R->Errors.Add(Entry.Event.Message);
		}
		else if (Entry.Event.Type == EAutomationEventType::Warning)
		{
			R->Warnings.Add(Entry.Event.Message);
		}
	}
	if (bStopped)
	{
		R->Errors.Add(TEXT("stopped by ops.stop_tests"));
	}
	CurrentPath.Reset();
}

void FOpsTestRunner::Stop()
{
	if (!IsRunning())
	{
		return;
	}
	FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
	TickHandle.Reset();
	if (bTestRunning)
	{
		FAutomationTestFramework::Get().DequeueAllCommands();
		FinishCurrent(true);
	}
	for (const FAutomationTestInfo& Info : Queue)
	{
		FOpsToolTestResult& R = Results.AddDefaulted_GetRef();
		R.Test = Info.GetFullTestPath();
		R.State = EOpsToolTestState::Skipped;
	}
	Queue.Reset();
	GIsAutomationTesting = false;
}

bool FOpsTestRunner::Tick(float)
{
	FAutomationTestFramework& Framework = FAutomationTestFramework::Get();
	if (bTestRunning)
	{
		// Latent execution and StopTest require the flag; it is only held during our own calls (see FRuntimeTestRunner).
		GIsAutomationTesting = true;
		if (!Framework.ExecuteLatentCommands())
		{
			GIsAutomationTesting = false;
			return true;
		}
		GIsAutomationTesting = false;
		FinishCurrent(false);
	}
	if (Queue.Num() == 0)
	{
		TickHandle.Reset();
		GIsAutomationTesting = false;
		return false;
	}
	Current = Queue[0];
	Queue.RemoveAt(0);
	CurrentPath = Current.GetFullTestPath();
	CurrentStart = FPlatformTime::Seconds();
	FOpsToolTestResult& R = Results.AddDefaulted_GetRef();
	R.Test = CurrentPath;
	R.State = EOpsToolTestState::Running;
	// Spec tests share one class: the parameter selects the individual It().
	FString ToRun = Current.GetTestName();
	const FString Parameter = Current.GetTestParameter();
	if (!Parameter.IsEmpty() && !ToRun.EndsWith(Parameter))
	{
		ToRun += TEXT(" ") + Parameter;
	}
	GIsAutomationTesting = false;
	Framework.StartTestByName(ToRun, 0, CurrentPath);
	GIsAutomationTesting = false;
	bTestRunning = true;
	return true;
}
