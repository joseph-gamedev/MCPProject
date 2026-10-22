#include "LogicRuntime.h"
#include "LogicTuning.h"
#include "LogicStartupGame.h"
#include "RuntimeLogicToolset.h"
#include "RuntimeCharacterToolset.h"
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
		CharacterToolset = RuntimeToolReflection::RegisterToolsetClass(URuntimeCharacterToolset::StaticClass());
		LogicTuning::LoadConfig();
		FLogicManager::Get().Startup();
		LogicStartupGame::Startup();
	}

	virtual void ShutdownModule() override
	{
		LogicStartupGame::Shutdown();
		FLogicManager::Get().Shutdown();
		if (IRuntimeToolsetsCore::IsAvailable() && Toolset.IsValid())
		{
			IRuntimeToolsetsCore::Get().UnregisterToolset(Toolset->GetName());
		}
		if (IRuntimeToolsetsCore::IsAvailable() && CharacterToolset.IsValid())
		{
			IRuntimeToolsetsCore::Get().UnregisterToolset(CharacterToolset->GetName());
		}
		Toolset.Reset();
		CharacterToolset.Reset();
	}

private:
	TSharedPtr<IRuntimeToolset> Toolset;
	TSharedPtr<IRuntimeToolset> CharacterToolset;
};

IMPLEMENT_MODULE(FRuntimeToolsetsLogicModule, RuntimeToolsetsLogic)
