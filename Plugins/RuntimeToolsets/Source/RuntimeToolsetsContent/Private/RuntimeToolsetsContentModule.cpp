#include "BuildToolset.h"
#include "LayoutToolset.h"
#include "PrimitiveMesh.h"
#include "RuntimeToolsetsCore.h"
#include "Modules/ModuleManager.h"

/** Content group: layout and the primitive builder. Depends only on RuntimeToolsetsCore. */
class FRuntimeToolsetsContentModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		Registered = {MakeShared<FLayoutToolset>(), MakeShared<FBuildToolset>()};
		IRuntimeToolsetsCore::Get().RegisterSourceSpawner(TEXT("prim:"), &RuntimePrimitives::SpawnFromSource);
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
			IRuntimeToolsetsCore::Get().UnregisterSourceSpawner(TEXT("prim:"));
		}
		Registered.Reset();
	}

private:
	TArray<TSharedRef<IRuntimeToolset>> Registered;
};

IMPLEMENT_MODULE(FRuntimeToolsetsContentModule, RuntimeToolsetsContent)
