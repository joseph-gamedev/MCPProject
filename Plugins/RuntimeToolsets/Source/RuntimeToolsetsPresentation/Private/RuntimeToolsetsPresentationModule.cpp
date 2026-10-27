#include "RuntimeCameraToolset.h"
#include "RuntimeLightingToolset.h"
#include "RuntimeMaterialsToolset.h"
#include "RuntimeStyleToolset.h"
#include "RuntimeToolReflection.h"
#include "RuntimeToolsetsActivity.h"
#include "RuntimeToolsetsCore.h"
#include "RuntimeUiToolset.h"
#include "RuntimeVfxToolset.h"
#include "Modules/ModuleManager.h"

// Tool docs for packaged builds (doc comments are editor-only metadata).
#include "Generated/ToolDocs.gen.inl"

/** Presentation group: materials, camera, ui, vfx, lighting, style. UFUNCTION toolsets (Epic ToolsetRegistry style). */
class FRuntimeToolsetsPresentationModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		RuntimeToolReflection::RegisterCompiledDocs(GRuntimeToolDocs, UE_ARRAY_COUNT(GRuntimeToolDocs));
		IRuntimeToolsetsCore& Core = IRuntimeToolsetsCore::Get();
		Core.RegisterSourceSpawner(TEXT("vfx:"), &URuntimeVfxToolset::SpawnFromSource);
		Core.RegisterSourceSpawner(TEXT("label:"), &URuntimeUiToolset::SpawnLabelFromSource);
		for (UClass* Class : {URuntimeMaterialsToolset::StaticClass(), URuntimeCameraToolset::StaticClass(), URuntimeUiToolset::StaticClass(),
				 URuntimeVfxToolset::StaticClass(), URuntimeLightingToolset::StaticClass(), URuntimeStyleToolset::StaticClass()})
		{
			Registered.Add(RuntimeToolReflection::RegisterToolsetClass(Class));
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
