using UnrealBuildTool;

public class RuntimeToolsetsStateTree : ModuleRules
{
	public RuntimeToolsetsStateTree(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		// StateTree at runtime: run state trees on actors, send events, read active states. Group modules depend only on RuntimeToolsetsCore (plus engine runtime modules).
		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"RuntimeToolsetsCore", "Core", "CoreUObject", "Engine", "Json", "JsonUtilities", "StateTreeModule", "GameplayStateTreeModule", "GameplayTags", "AIModule"
		});
		RuntimeToolsetsBuildGuard.Check(this);
	}
}
