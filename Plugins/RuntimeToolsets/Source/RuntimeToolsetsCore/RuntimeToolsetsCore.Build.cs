using System.Collections.Generic;
using UnrealBuildTool;

/// Fails the build if a runtime module pulls in editor-only or Python modules.
public static class RuntimeToolsetsBuildGuard
{
	private static readonly string[] Forbidden =
	{
		"UnrealEd", "LevelEditor", "EditorSubsystem", "EditorScriptingUtilities", "PythonScriptPlugin",
		"ToolMenus", "ToolsetRegistry", "ModelContextProtocol", "ModelContextProtocolEngine", "ModelContextProtocolEditor"
	};

	public static void Check(ModuleRules Rules)
	{
		var All = new List<string>();
		All.AddRange(Rules.PublicDependencyModuleNames);
		All.AddRange(Rules.PrivateDependencyModuleNames);
		All.AddRange(Rules.DynamicallyLoadedModuleNames);
		foreach (string Name in Forbidden)
		{
			if (All.Contains(Name))
			{
				throw new BuildException("RuntimeToolsets module '{0}' must not depend on editor-only module '{1}'.", Rules.Name, Name);
			}
		}
	}
}

public class RuntimeToolsetsCore : ModuleRules
{
	public RuntimeToolsetsCore(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "Json", "JsonUtilities", "DeveloperSettings" });
		PrivateDependencyModuleNames.AddRange(new string[] { "Projects", "ImageCore" });
		RuntimeToolsetsBuildGuard.Check(this);
	}
}
