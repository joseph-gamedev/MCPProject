#include "OpsToolset.h"
#include "ProfilingToolset.h"
#include "RuntimeToolsetsCore.h"
#include "Modules/ModuleManager.h"

/** Ops group: profiling and orchestration/testing (Phase 2). Depends only on RuntimeToolsetsCore. */
class FRuntimeToolsetsOpsModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		Registered = {MakeShared<FProfilingToolset>(), MakeShared<FOpsToolset>()};
		for (const TSharedRef<IRuntimeToolset>& Toolset : Registered)
		{
			IRuntimeToolsetsCore::Get().RegisterToolset(Toolset);
		}
	}

	virtual void ShutdownModule() override
	{
		if (IRuntimeToolsetsCore::IsAvailable())
		{
			for (const TSharedRef<IRuntimeToolset>& Toolset : Registered)
			{
				IRuntimeToolsetsCore::Get().UnregisterToolset(Toolset->GetName());
			}
		}
		Registered.Reset();
	}

private:
	TArray<TSharedRef<IRuntimeToolset>> Registered;
};

IMPLEMENT_MODULE(FRuntimeToolsetsOpsModule, RuntimeToolsetsOps)
