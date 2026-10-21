#include "RuntimeGameToolset.h"
#include "RuntimeInputToolset.h"
#include "RuntimeLevelToolset.h"
#include "RuntimePhysicsToolset.h"
#include "RuntimeToolReflection.h"
#include "RuntimeToolsetsCore.h"
#include "Modules/ModuleManager.h"

// Tool docs for packaged builds (doc comments are editor-only metadata).
#include "Generated/ToolDocs.gen.inl"

/** Gameplay group: game framework, physics, levels, input. Toolsets are UFUNCTION classes (Epic ToolsetRegistry style). */
class FRuntimeToolsetsGameplayModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		RuntimeToolReflection::RegisterCompiledDocs(GRuntimeToolDocs, UE_ARRAY_COUNT(GRuntimeToolDocs));
		Registered = {RuntimeToolReflection::RegisterToolsetClass(URuntimeGameToolset::StaticClass()),
			RuntimeToolReflection::RegisterToolsetClass(URuntimePhysicsToolset::StaticClass()),
			RuntimeToolReflection::RegisterToolsetClass(URuntimeLevelToolset::StaticClass()),
			RuntimeToolReflection::RegisterToolsetClass(URuntimeInputToolset::StaticClass())};
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
