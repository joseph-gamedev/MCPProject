using UnrealBuildTool;

public class RuntimeToolsetsCoreTools : ModuleRules
{
	public RuntimeToolsetsCoreTools(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"RuntimeToolsetsCore", "Core", "CoreUObject", "Engine", "Json", "JsonUtilities",
			"RenderCore", "RHI", "ImageCore", "ImageWrapper", "Projects"
		});
		RuntimeToolsetsBuildGuard.Check(this);
	}
}
