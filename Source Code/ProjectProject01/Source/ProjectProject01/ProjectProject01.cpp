// Copyright Epic Games, Inc. All Rights Reserved.

#include "ProjectProject01.h"

#include "Internationalization/StringTableRegistry.h"
#include "Modules/ModuleManager.h"
#include "ProjectDFSDBranding.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformProperties.h"
#include "Misc/Paths.h"
#include "HAL/IConsoleManager.h"

class FProjectProject01Module final : public FDefaultGameModuleImpl
{
public:
	virtual void StartupModule() override
	{
		FDefaultGameModuleImpl::StartupModule();
		// UE 5.8 TSR reads this engine reference CVar on the render thread. Enabling
		// its supported shadow-value path also orders future writes on that thread.
		// Preserve both the current value and all existing priority/config flags.
		if (IConsoleVariable* MotionVectors = IConsoleManager::Get().FindConsoleVariable(TEXT("r.MotionVectorSimulation")))
		{
			MotionVectors->SetFlags(static_cast<EConsoleVariableFlags>(MotionVectors->GetFlags() | ECVF_RenderThreadSafe));
		}
		if (FPlatformProperties::RequiresCookedData() && !IsRunningDedicatedServer())
		{
			const FString Legacy = FString(FPlatformProcess::UserSettingsDir()) / TEXT("ProjectProject01/Saved");
			if (!ProjectDFSDBranding::ImportLegacyUserData(Legacy, FPaths::ProjectSavedDir()))
				UE_LOG(LogTemp, Warning, TEXT("ProjectDFSD: previous user data import was incomplete; original data is unchanged."));
		}
		LOCTABLE_FROMFILE_GAME(
			"ProjectProject01UI", "ProjectProject01.UI", "Localization/ProjectProject01UI.csv");
	}

	virtual void ShutdownModule() override
	{
		FStringTableRegistry::Get().UnregisterStringTable(TEXT("ProjectProject01UI"));
		FDefaultGameModuleImpl::ShutdownModule();
	}
};

// The script module name stays stable for existing Blueprint and save references.
IMPLEMENT_PRIMARY_GAME_MODULE(FProjectProject01Module, ProjectProject01, "ProjectDFSD");
