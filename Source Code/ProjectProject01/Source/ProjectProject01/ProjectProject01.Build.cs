// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class ProjectProject01 : ModuleRules
{
	public ProjectProject01(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
	
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput", "NavigationSystem", "UMG", "AudioMixer", "VoiceChat" });

		PrivateDependencyModuleNames.AddRange(new string[] { "ApplicationCore", "HTTP", "Json", "JsonUtilities", "RenderCore", "RHI", "Slate", "SlateCore", "EOSVoiceChat", "EOSShared", "EOSSDK" });

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });
		
		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
