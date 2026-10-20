using UnrealBuildTool;

public class RuntimeToolsetsTests : ModuleRules
{
	public RuntimeToolsetsTests(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"RuntimeToolsetsCore", "Core", "CoreUObject", "Engine", "Json", "JsonUtilities", "Projects"
		});
		RuntimeToolsetsBuildGuard.Check(this);
	}
}
