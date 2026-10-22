using UnrealBuildTool;

public class RuntimeToolsetsMCP : ModuleRules
{
	public RuntimeToolsetsMCP(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"RuntimeToolsetsCore", "Core", "CoreUObject", "Engine", "Json", "JsonUtilities",
			"DeveloperSettings", "ModelContextProtocol", "HTTPServer", "HTTP", "Slate", "SlateCore"
		});
	}
}
