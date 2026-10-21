#include "LogicRuntime.h"
#include "LogicToolset.h"
#include "RuntimeToolsetsCore.h"
#include "Misc/CommandLine.h"
#include "Modules/ModuleManager.h"

/** Game logic group: variables, rules, events, on-screen UI. Depends only on RuntimeToolsetsCore. */
class FRuntimeToolsetsLogicModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		Toolset = MakeShared<FLogicToolset>();
		IRuntimeToolsetsCore::Get().RegisterToolset(Toolset.ToSharedRef());
		FLogicManager::Get().Startup();
	}

	virtual void ShutdownModule() override
	{
		FLogicManager::Get().Shutdown();
		if (IRuntimeToolsetsCore::IsAvailable() && Toolset.IsValid())
		{
			IRuntimeToolsetsCore::Get().UnregisterToolset(Toolset->GetName());
		}
		Toolset.Reset();
	}

private:
	TSharedPtr<FLogicToolset> Toolset;
};

IMPLEMENT_MODULE(FRuntimeToolsetsLogicModule, RuntimeToolsetsLogic)
