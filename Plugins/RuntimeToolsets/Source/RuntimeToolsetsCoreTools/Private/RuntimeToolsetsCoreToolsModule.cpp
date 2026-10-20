#include "ActorsToolset.h"
#include "CaptureToolset.h"
#include "ConsoleToolset.h"
#include "HistoryToolset.h"
#include "LogToolset.h"
#include "ReflectionToolset.h"
#include "SceneGraphToolset.h"
#include "SessionToolset.h"
#include "Helpers/ToolsCommon.h"
#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY(LogRuntimeToolsetsTools);

/** Registers the Phase 1 core-group toolsets with RuntimeToolsetsCore. */
class FRuntimeToolsetsCoreToolsModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		IRuntimeToolsetsCore& Core = IRuntimeToolsetsCore::Get();
		Registered = {
			MakeShared<FSessionToolset>(),
			MakeShared<FHistoryToolset>(),
			MakeShared<FActorsToolset>(),
			MakeShared<FSceneGraphToolset>(),
			MakeShared<FReflectionToolset>(),
			MakeShared<FConsoleToolset>(),
			MakeShared<FLogToolset>(),
			MakeShared<FCaptureToolset>(),
		};
		for (const TSharedRef<IRuntimeToolset>& Toolset : Registered)
		{
			Core.RegisterToolset(Toolset);
		}
		UE_LOG(LogRuntimeToolsetsTools, Log, TEXT("Registered %d core toolsets"), Registered.Num());
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

IMPLEMENT_MODULE(FRuntimeToolsetsCoreToolsModule, RuntimeToolsetsCoreTools)
