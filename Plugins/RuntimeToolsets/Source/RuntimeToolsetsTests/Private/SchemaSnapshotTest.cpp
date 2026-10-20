#include "ToolTestHarness.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "RuntimeJson.h"
#include "RuntimeToolsetsCore.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

static TAutoConsoleVariable<bool> CVarUpdateSchemaSnapshots(TEXT("RuntimeToolsets.UpdateSchemaSnapshots"), false,
	TEXT("When true, the schema snapshot test rewrites Plugins/RuntimeToolsets/Tests/Schemas instead of comparing."));

/**
 * Tool names, descriptions and schemas are the public surface. This test fails when they change without the snapshot
 * being updated (run once with RuntimeToolsets.UpdateSchemaSnapshots=1 and review the diff).
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRuntimeToolsetsSchemaSnapshotTest, "RuntimeToolsets.SchemaSnapshots", RUNTIMETOOLSETS_TEST_FLAGS)

bool FRuntimeToolsetsSchemaSnapshotTest::RunTest(const FString& Parameters)
{
	TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("RuntimeToolsets"));
	if (!Plugin.IsValid())
	{
		AddError(TEXT("plugin not found"));
		return false;
	}
	const FString Dir = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Tests"), TEXT("Schemas"));
	const bool bUpdate = CVarUpdateSchemaSnapshots.GetValueOnGameThread();
	if (!bUpdate && !IFileManager::Get().DirectoryExists(*Dir))
	{
		AddInfo(TEXT("No snapshot directory (packaged build?); skipped"));
		return true;
	}

	const TSharedPtr<FJsonObject> List = RuntimeJson::Parse(IRuntimeToolsetsCore::Get().ListToolsets());
	for (const TSharedPtr<FJsonValue>& Item : List->GetArrayField(TEXT("toolsets")))
	{
		const FString Name = Item->AsObject()->GetStringField(TEXT("name"));
		const TSharedPtr<FJsonObject> Described = RuntimeJson::Parse(IRuntimeToolsetsCore::Get().DescribeToolset(Name, true));
		const FString Current = RuntimeJson::WriteSorted(Described.ToSharedRef()) + TEXT("\n");
		const FString File = FPaths::Combine(Dir, Name + TEXT(".json"));
		if (bUpdate)
		{
			FFileHelper::SaveStringToFile(Current, *File, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
			AddInfo(FString::Printf(TEXT("Updated %s"), *File));
			continue;
		}
		FString Stored;
		if (!FFileHelper::LoadFileToString(Stored, *File))
		{
			AddError(FString::Printf(TEXT("Missing snapshot for toolset '%s'. Run with RuntimeToolsets.UpdateSchemaSnapshots=1."), *Name));
			continue;
		}
		Stored.ReplaceInline(TEXT("\r\n"), TEXT("\n"));
		if (Stored != Current)
		{
			AddError(FString::Printf(TEXT("Toolset '%s' public surface changed. Review and update the snapshot (bump the toolset version)."), *Name));
		}
	}
	return true;
}

#endif
