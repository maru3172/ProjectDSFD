// Copyright Epic Games, Inc. All Rights Reserved.
// Dedicated server build target for UE 5.8.

using UnrealBuildTool;
using System.Collections.Generic;

public class ProjectProject01ServerTarget : TargetRules
{
	public ProjectProject01ServerTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Server;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
		ExtraModuleNames.Add("ProjectProject01");
	}
}
