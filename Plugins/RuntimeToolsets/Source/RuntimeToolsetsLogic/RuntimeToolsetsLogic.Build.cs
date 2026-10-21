using UnrealBuildTool;

public class RuntimeToolsetsLogic : ModuleRules
{
	public RuntimeToolsetsLogic(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		// Group modules depend only on RuntimeToolsetsCore (plus engine runtime modules).
		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"RuntimeToolsetsCore", "Core", "CoreUObject", "Engine", "Json", "JsonUtilities",
			"InputCore", "Slate", "SlateCore"
		});
		RuntimeToolsetsBuildGuard.Check(this);
	}
}
