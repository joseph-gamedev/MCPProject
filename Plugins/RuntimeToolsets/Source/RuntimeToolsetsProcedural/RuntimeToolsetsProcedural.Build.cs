using UnrealBuildTool;

public class RuntimeToolsetsProcedural : ModuleRules
{
	public RuntimeToolsetsProcedural(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		// Splines and PCG scattering. Group modules depend only on RuntimeToolsetsCore (plus engine runtime modules).
		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"RuntimeToolsetsCore", "Core", "CoreUObject", "Engine", "Json", "JsonUtilities",
			"PCG", "GeometryCore", "GeometryFramework", "MeshConversion", "MeshDescription", "StaticMeshDescription"
		});
		RuntimeToolsetsBuildGuard.Check(this);
	}
}
