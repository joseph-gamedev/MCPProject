#include "LogicRuntime.h"
#include "LogicTuning.h"
#include "RuntimeLogicToolset.h"
#include "RuntimeToolReflection.h"
#include "RuntimeToolsetsCore.h"
#include "Modules/ModuleManager.h"

// Tool docs for packaged builds (doc comments are editor-only metadata).
#include "Generated/ToolDocs.gen.inl"

/** Game logic group: variables, rules, events, on-screen UI. Toolsets are UFUNCTION classes (Epic ToolsetRegistry style). */
class FRuntimeToolsetsLogicModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		RuntimeToolReflection::RegisterCompiledDocs(GRuntimeToolDocs, UE_ARRAY_COUNT(GRuntimeToolDocs));
		Toolset = RuntimeToolReflection::RegisterToolsetClass(URuntimeLogicToolset::StaticClass());
		LogicTuning::LoadConfig();
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
	TSharedPtr<IRuntimeToolset> Toolset;
};

IMPLEMENT_MODULE(FRuntimeToolsetsLogicModule, RuntimeToolsetsLogic)
