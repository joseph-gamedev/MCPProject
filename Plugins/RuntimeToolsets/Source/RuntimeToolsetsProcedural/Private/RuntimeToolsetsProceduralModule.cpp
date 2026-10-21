#include "PcgScatter.h"
#include "RuntimePcgToolset.h"
#include "RuntimeSplineToolset.h"
#include "SplineCore.h"
#include "RuntimeToolReflection.h"
#include "RuntimeToolsetsCore.h"
#include "Modules/ModuleManager.h"

// Tool docs for packaged builds (doc comments are editor-only metadata).
#include "Generated/ToolDocs.gen.inl"

/** Procedural group: splines and PCG. Toolsets are UFUNCTION classes (Epic ToolsetRegistry style). */
class FRuntimeToolsetsProceduralModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		RuntimeToolReflection::RegisterCompiledDocs(GRuntimeToolDocs, UE_ARRAY_COUNT(GRuntimeToolDocs));
		RuntimePcg::RegisterConverters();
		IRuntimeToolsetsCore& Core = IRuntimeToolsetsCore::Get();
		Core.RegisterSourceSpawner(TEXT("spline:"), &RuntimeSplines::SpawnFromSource);
		Core.RegisterSourceSpawner(TEXT("scatter:"), &RuntimePcg::SpawnScatterFromSource);
		Core.RegisterSourceSpawner(TEXT("pcginst:"), &RuntimePcg::SpawnInstanceFromSource);
		Registered = {RuntimeToolReflection::RegisterToolsetClass(URuntimeSplineToolset::StaticClass()),
			RuntimeToolReflection::RegisterToolsetClass(URuntimePcgToolset::StaticClass())};
	}

	virtual void ShutdownModule() override
	{
		if (IRuntimeToolsetsCore::IsAvailable())
		{
			for (const TSharedRef<IRuntimeToolset>& Toolset : Registered)
			{
				IRuntimeToolsetsCore::Get().UnregisterToolset(Toolset->GetName());
			}
			for (const TCHAR* P : {TEXT("spline:"), TEXT("scatter:"), TEXT("pcginst:")})
			{
				IRuntimeToolsetsCore::Get().UnregisterSourceSpawner(P);
			}
		}
		RuntimePcg::UnregisterConverters();
		Registered.Reset();
	}

private:
	TArray<TSharedRef<IRuntimeToolset>> Registered;
};

IMPLEMENT_MODULE(FRuntimeToolsetsProceduralModule, RuntimeToolsetsProcedural)
