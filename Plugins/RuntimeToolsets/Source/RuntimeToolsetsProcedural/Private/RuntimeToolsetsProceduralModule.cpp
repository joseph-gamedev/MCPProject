#include "SplineToolset.h"
#include "PcgToolset.h"
#include "RuntimeToolsetsCore.h"
#include "Modules/ModuleManager.h"

/** Procedural group: splines (paths, roads, walls, rivers) and PCG scattering. Depends only on RuntimeToolsetsCore. */
class FRuntimeToolsetsProceduralModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		Registered = {MakeShared<FSplineToolset>(), MakeShared<FPcgToolset>()};
		IRuntimeToolsetsCore::Get().RegisterSourceSpawner(TEXT("spline:"), &FSplineToolset::SpawnFromSource);
		IRuntimeToolsetsCore::Get().RegisterSourceSpawner(TEXT("scatter:"), &FPcgToolset::SpawnFromSource);
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
			IRuntimeToolsetsCore::Get().UnregisterSourceSpawner(TEXT("spline:"));
			IRuntimeToolsetsCore::Get().UnregisterSourceSpawner(TEXT("scatter:"));
		}
		Registered.Reset();
	}

private:
	TArray<TSharedRef<IRuntimeToolset>> Registered;
};

IMPLEMENT_MODULE(FRuntimeToolsetsProceduralModule, RuntimeToolsetsProcedural)
