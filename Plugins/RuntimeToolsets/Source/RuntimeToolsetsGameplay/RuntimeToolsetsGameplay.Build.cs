using UnrealBuildTool;

public class RuntimeToolsetsGameplay : ModuleRules
{
	public RuntimeToolsetsGameplay(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		// Group modules depend only on RuntimeToolsetsCore (plus engine/runtime plugin modules).
		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"RuntimeToolsetsCore", "Core", "CoreUObject", "Engine", "Json", "JsonUtilities",
			"InputCore", "EnhancedInput", "PhysicsCore", "AssetRegistry", "Projects"
		});
		RuntimeToolsetsBuildGuard.Check(this);
	}
}
