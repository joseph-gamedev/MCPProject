#include "CameraToolset.h"
#include "LightingToolset.h"
#include "MaterialsToolset.h"
#include "UiToolset.h"
#include "StyleToolset.h"
#include "RuntimeToolsetsActivity.h"
#include "VfxToolset.h"
#include "RuntimeToolsetsCore.h"
#include "Modules/ModuleManager.h"

/** Presentation group: materials, camera, ui, vfx, lighting (Phase 2). Depends only on RuntimeToolsetsCore. */
class FRuntimeToolsetsPresentationModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		Registered = {MakeShared<FMaterialsToolset>(), MakeShared<FCameraToolset>(), MakeShared<FUiToolset>(), MakeShared<FVfxToolset>(), MakeShared<FLightingToolset>(), MakeShared<FStyleToolset>()};
		IRuntimeToolsetsCore::Get().RegisterSourceSpawner(TEXT("vfx:"), &FVfxToolset::SpawnFromSource);
		IRuntimeToolsetsCore::Get().RegisterSourceSpawner(TEXT("label:"), &FUiToolset::SpawnLabelFromSource);
		for (const TSharedRef<IRuntimeToolset>& Toolset : Registered)
		{
			IRuntimeToolsetsCore::Get().RegisterToolset(Toolset);
		}
		if (!IsRunningCommandlet())
		{
			FRuntimeToolsetsActivity::Get().Startup();
		}
	}

	virtual void ShutdownModule() override
	{
		FRuntimeToolsetsActivity::Get().Shutdown();
		if (IRuntimeToolsetsCore::IsAvailable())
		{
			for (const TSharedRef<IRuntimeToolset>& Toolset : Registered)
			{
				IRuntimeToolsetsCore::Get().UnregisterToolset(Toolset->GetName());
			}
			IRuntimeToolsetsCore::Get().UnregisterSourceSpawner(TEXT("vfx:"));
			IRuntimeToolsetsCore::Get().UnregisterSourceSpawner(TEXT("label:"));
		}
		Registered.Reset();
	}

private:
	TArray<TSharedRef<IRuntimeToolset>> Registered;
};

IMPLEMENT_MODULE(FRuntimeToolsetsPresentationModule, RuntimeToolsetsPresentation)
