// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.Collections.Generic;

public class ProjectDFSDTarget : TargetRules
{
	public ProjectDFSDTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
		ExtraModuleNames.Add("ProjectProject01");

		// Build guard (Docs/BuildGuard.md): fails a build once when source files were saved while it
		// compiled, after deleting the object files that may hold the old code. Skipped on non-Windows
		// hosts or when the script is missing; problems inside the script never fail the build.
		if (BuildHostPlatform.Current.Platform == UnrealTargetPlatform.Win64)
		{
			const string Guard = "$(ProjectDir)\\Tools\\BuildGuard\\BuildStepGuard.ps1";
			PreBuildSteps.Add("if exist \"" + Guard + "\" powershell -NoProfile -NonInteractive -ExecutionPolicy Bypass -File \"" + Guard + "\" -Phase Pre -Target $(TargetName)");
			PostBuildSteps.Add("if exist \"" + Guard + "\" powershell -NoProfile -NonInteractive -ExecutionPolicy Bypass -File \"" + Guard + "\" -Phase Post -Target $(TargetName)");
		}
	}
}
