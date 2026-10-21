#include "LayoutToolset.h"
#include "RuntimeToolsetsCore.h"
#include "Modules/ModuleManager.h"

/** Content group: layout (Phase 2). Depends only on RuntimeToolsetsCore. */
class FRuntimeToolsetsContentModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		Registered = {MakeShared<FLayoutToolset>()};
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

IMPLEMENT_MODULE(FRuntimeToolsetsContentModule, RuntimeToolsetsContent)
