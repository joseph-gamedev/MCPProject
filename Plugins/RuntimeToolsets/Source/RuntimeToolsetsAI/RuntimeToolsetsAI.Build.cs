using UnrealBuildTool;

public class RuntimeToolsetsAI : ModuleRules
{
	public RuntimeToolsetsAI(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		// AI and gameplay tags: AI controllers, navigation, perception, behavior trees, blackboards, gameplay tags. Group modules depend only on RuntimeToolsetsCore (plus engine runtime modules).
		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"RuntimeToolsetsCore", "Core", "CoreUObject", "Engine", "Json", "JsonUtilities", "AIModule", "NavigationSystem", "GameplayTags", "GameplayTasks", "AssetRegistry"
		});
		RuntimeToolsetsBuildGuard.Check(this);
	}
}
