using UnrealBuildTool;

public class RuntimeToolsetsPresentation : ModuleRules
{
	public RuntimeToolsetsPresentation(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		// Group modules depend only on RuntimeToolsetsCore (plus engine runtime modules).
		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"RuntimeToolsetsCore", "Core", "CoreUObject", "Engine", "Json", "JsonUtilities", "RenderCore",
			"UMG", "Slate", "SlateCore", "Niagara"
		});
		RuntimeToolsetsBuildGuard.Check(this);
	}
}
