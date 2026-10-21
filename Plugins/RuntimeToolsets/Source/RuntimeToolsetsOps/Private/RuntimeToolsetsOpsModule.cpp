#include "OpsTestRunner.h"
#include "RuntimeOpsToolset.h"
#include "RuntimeProfilingToolset.h"
#include "RuntimeToolReflection.h"
#include "RuntimeToolsetsCore.h"
#include "Modules/ModuleManager.h"

// Tool docs for packaged builds (doc comments are editor-only metadata).
#include "Generated/ToolDocs.gen.inl"

/** Ops group: profiling and orchestration/testing. Toolsets are UFUNCTION classes (Epic ToolsetRegistry style). */
class FRuntimeToolsetsOpsModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		RuntimeToolReflection::RegisterCompiledDocs(GRuntimeToolDocs, UE_ARRAY_COUNT(GRuntimeToolDocs));
		Registered = {RuntimeToolReflection::RegisterToolsetClass(URuntimeProfilingToolset::StaticClass()),
			RuntimeToolReflection::RegisterToolsetClass(URuntimeOpsToolset::StaticClass())};
	}

	virtual void ShutdownModule() override
	{
		FOpsTestRunner::Get().Stop();
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

IMPLEMENT_MODULE(FRuntimeToolsetsOpsModule, RuntimeToolsetsOps)
