#include "CoreMinimal.h"
#include "HAL/IConsoleManager.h"
#include "Modules/ModuleManager.h"
#include "RuntimeTestRunner.h"

DEFINE_LOG_CATEGORY_STATIC(LogRuntimeToolsetsTests, Log, All);

/** Hosts the specs and the RuntimeToolsets.RunTests console command (works in packaged Development builds). */
class FRuntimeToolsetsTestsModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		Command = IConsoleManager::Get().RegisterConsoleCommand(TEXT("RuntimeToolsets.RunTests"),
			TEXT("RuntimeToolsets.RunTests [filter] [quit] - run automation tests in this process; summary line 'RuntimeToolsets tests: N passed, M failed'"),
			FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
			{
				FString Filter = TEXT("RuntimeToolsets.");
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
				FString Error;
				if (!FRuntimeTestRunner::Get().Start(Filter, bQuit, Error))
				{
					UE_LOG(LogRuntimeToolsetsTests, Warning, TEXT("%s"), *Error);
				}
			}),
			ECVF_Default);
	}

	virtual void ShutdownModule() override
	{
		FRuntimeTestRunner::Get().Stop();
		if (Command)
		{
			IConsoleManager::Get().UnregisterConsoleObject(Command);
			Command = nullptr;
		}
	}

private:
	IConsoleObject* Command = nullptr;
};

IMPLEMENT_MODULE(FRuntimeToolsetsTestsModule, RuntimeToolsetsTests)
