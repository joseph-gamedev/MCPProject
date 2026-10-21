#include "GameToolset.h"
#include "InputToolset.h"
#include "LevelToolset.h"
#include "PhysicsToolset.h"
#include "RuntimeToolsetsCore.h"
#include "Modules/ModuleManager.h"

/** Gameplay group: game framework, physics, levels, input (Phase 2). Depends only on RuntimeToolsetsCore. */
class FRuntimeToolsetsGameplayModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		Registered = {MakeShared<FGameToolset>(), MakeShared<FPhysicsToolset>(), MakeShared<FLevelToolset>(), MakeShared<FInputToolset>()};
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

IMPLEMENT_MODULE(FRuntimeToolsetsGameplayModule, RuntimeToolsetsGameplay)
