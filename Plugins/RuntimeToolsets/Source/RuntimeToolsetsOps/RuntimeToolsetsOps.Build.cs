using UnrealBuildTool;

public class RuntimeToolsetsOps : ModuleRules
{
	public RuntimeToolsetsOps(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		// Group modules depend only on RuntimeToolsetsCore (plus engine runtime modules).
		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"RuntimeToolsetsCore", "Core", "CoreUObject", "Engine", "Json", "JsonUtilities", "RHI", "RenderCore"
		});
		RuntimeToolsetsBuildGuard.Check(this);
	}
}
