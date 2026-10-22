using UnrealBuildTool;

public class RuntimeToolsetsGAS : ModuleRules
{
	public RuntimeToolsetsGAS(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		// Gameplay Ability System: attributes, effects, abilities, gameplay events and cues. Group modules depend only on RuntimeToolsetsCore (plus engine runtime modules).
		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"RuntimeToolsetsCore", "Core", "CoreUObject", "Engine", "Json", "JsonUtilities", "GameplayAbilities", "GameplayTags", "GameplayTasks"
		});
		RuntimeToolsetsBuildGuard.Check(this);
	}
}
