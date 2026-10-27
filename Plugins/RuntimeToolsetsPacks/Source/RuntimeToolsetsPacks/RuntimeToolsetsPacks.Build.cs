using UnrealBuildTool;

public class RuntimeToolsetsPacks : ModuleRules
{
	public RuntimeToolsetsPacks(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		// Like the core group modules: RuntimeToolsetsCore plus engine runtime modules. The packs themselves are content,
		// loaded by path at runtime, so the code builds without them.
		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"RuntimeToolsetsCore", "Core", "CoreUObject", "Engine", "Json", "JsonUtilities", "RenderCore", "Niagara", "AssetRegistry"
		});
		RuntimeToolsetsBuildGuard.Check(this);
	}
}
