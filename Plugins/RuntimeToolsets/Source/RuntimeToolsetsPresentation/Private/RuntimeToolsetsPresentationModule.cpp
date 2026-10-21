#include "CameraToolset.h"
#include "MaterialsToolset.h"
#include "RuntimeToolsetsCore.h"
#include "Modules/ModuleManager.h"

/** Presentation group: materials and camera (Phase 2). Depends only on RuntimeToolsetsCore. */
class FRuntimeToolsetsPresentationModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		Registered = {MakeShared<FMaterialsToolset>(), MakeShared<FCameraToolset>()};
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

IMPLEMENT_MODULE(FRuntimeToolsetsPresentationModule, RuntimeToolsetsPresentation)
