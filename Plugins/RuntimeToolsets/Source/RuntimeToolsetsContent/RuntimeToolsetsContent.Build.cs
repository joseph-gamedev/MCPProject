using UnrealBuildTool;

public class RuntimeToolsetsContent : ModuleRules
{
	public RuntimeToolsetsContent(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		// Group modules depend only on RuntimeToolsetsCore (plus engine runtime modules).
		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"RuntimeToolsetsCore", "Core", "CoreUObject", "Engine", "Json", "JsonUtilities",
			"GeometryCore", "GeometryFramework", "GeometryScriptingCore", "RenderCore"
		});
		RuntimeToolsetsBuildGuard.Check(this);
	}
}
