using UnrealBuildTool;

public class RuntimeToolsetsLogic : ModuleRules
{
	public RuntimeToolsetsLogic(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		// Each file keeps small private helpers; unity builds would merge them into duplicate definitions.
		bUseUnity = false;
		// Group modules depend only on RuntimeToolsetsCore (plus engine runtime modules).
		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"RuntimeToolsetsCore", "Core", "CoreUObject", "Engine", "Json", "JsonUtilities",
			"InputCore", "Slate", "SlateCore"
		});
		RuntimeToolsetsBuildGuard.Check(this);
	}
}
