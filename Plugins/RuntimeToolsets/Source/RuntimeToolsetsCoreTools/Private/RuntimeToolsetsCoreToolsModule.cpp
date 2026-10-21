#include "HistoryToolset.h"
#include "RuntimeActorsToolset.h"
#include "RuntimeCaptureToolset.h"
#include "RuntimeConsoleToolset.h"
#include "RuntimeLogToolset.h"
#include "RuntimeReflectionToolset.h"
#include "RuntimeSceneToolset.h"
#include "RuntimeSkillsToolset.h"
#include "SessionToolset.h"
#include "RuntimeToolReflection.h"
#include "Helpers/SpawnHelpers.h"
#include "Helpers/ToolsCommon.h"
#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY(LogRuntimeToolsetsTools);

// Tool docs for packaged builds (doc comments are editor-only metadata).
#include "Generated/ToolDocs.gen.inl"

/**
 * Core group: session and history (framework tools), skills, and the actors, scene, reflection, console, log and capture
 * toolsets (UFUNCTION classes, Epic ToolsetRegistry style).
 */
class FRuntimeToolsetsCoreToolsModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		IRuntimeToolsetsCore& Core = IRuntimeToolsetsCore::Get();
		RuntimeToolReflection::RegisterCompiledDocs(GRuntimeToolDocs, UE_ARRAY_COUNT(GRuntimeToolDocs));
		Registered = {
			RuntimeToolReflection::MakeToolset(URuntimeSkillsToolset::StaticClass()),
			MakeShared<FSessionToolset>(),
			MakeShared<FHistoryToolset>(),
			RuntimeToolReflection::MakeToolset(URuntimeActorsToolset::StaticClass()),
			RuntimeToolReflection::MakeToolset(URuntimeSceneToolset::StaticClass()),
			RuntimeToolReflection::MakeToolset(URuntimeReflectionToolset::StaticClass()),
			RuntimeToolReflection::MakeToolset(URuntimeConsoleToolset::StaticClass()),
			RuntimeToolReflection::MakeToolset(URuntimeLogToolset::StaticClass()),
			RuntimeToolReflection::MakeToolset(URuntimeCaptureToolset::StaticClass()),
		};
		for (const FString& Prefix : {FString(TEXT("group")), FString(TEXT("shape:")), FString(TEXT("class:"))})
		{
			Core.RegisterSourceSpawner(Prefix, &RuntimeSpawn::SpawnBuiltin);
		}
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
			for (const TCHAR* Prefix : {TEXT("group"), TEXT("shape:"), TEXT("class:")})
			{
				IRuntimeToolsetsCore::Get().UnregisterSourceSpawner(Prefix);
			}
		}
		Registered.Reset();
	}

private:
	TArray<TSharedRef<IRuntimeToolset>> Registered;
};

IMPLEMENT_MODULE(FRuntimeToolsetsCoreToolsModule, RuntimeToolsetsCoreTools)
