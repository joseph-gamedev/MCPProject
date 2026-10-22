#include "PrimitiveMesh.h"
#include "RuntimeBuildToolset.h"
#include "RuntimeLayoutToolset.h"
#include "RuntimeToolReflection.h"
#include "RuntimeToolsetsCore.h"
#include "Modules/ModuleManager.h"

// Tool docs for packaged builds (doc comments are editor-only metadata).
#include "Generated/ToolDocs.gen.inl"

/** Content group: layout and the primitive builder. Toolsets are UFUNCTION classes (Epic ToolsetRegistry style). */
class FRuntimeToolsetsContentModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		RuntimeToolReflection::RegisterCompiledDocs(GRuntimeToolDocs, UE_ARRAY_COUNT(GRuntimeToolDocs));
		IRuntimeToolsetsCore::Get().RegisterSourceSpawner(TEXT("prim:"), &RuntimePrimitives::SpawnFromSource);
		IRuntimeToolsetsCore::Get().RegisterSourceSpawner(TEXT("mesh:"), &RuntimeMeshAssets::SpawnFromSource);
		Registered = {RuntimeToolReflection::RegisterToolsetClass(URuntimeLayoutToolset::StaticClass()),
			RuntimeToolReflection::RegisterToolsetClass(URuntimeBuildToolset::StaticClass())};
	}

	virtual void ShutdownModule() override
	{
		if (IRuntimeToolsetsCore::IsAvailable())
		{
			for (const TSharedRef<IRuntimeToolset>& Toolset : Registered)
			{
				IRuntimeToolsetsCore::Get().UnregisterToolset(Toolset->GetName());
			}
			IRuntimeToolsetsCore::Get().UnregisterSourceSpawner(TEXT("prim:"));
		}
		Registered.Reset();
	}

private:
	TArray<TSharedRef<IRuntimeToolset>> Registered;
};

IMPLEMENT_MODULE(FRuntimeToolsetsContentModule, RuntimeToolsetsContent)
