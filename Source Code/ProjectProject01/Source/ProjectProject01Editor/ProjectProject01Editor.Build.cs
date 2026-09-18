// File: Source/ProjectProject01Editor/ProjectProject01Editor.Build.cs
// Target: ProjectProject01Editor Win64 Development, Unreal Engine 5.8.2

using UnrealBuildTool;

public class ProjectProject01Editor : ModuleRules
{
	public ProjectProject01Editor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "ProjectProject01" });
		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"AssetTools", "EditorFramework", "LevelEditor", "Slate", "SlateCore", "ToolMenus", "UnrealEd"
		});
	}
}
