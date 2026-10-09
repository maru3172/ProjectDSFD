// Copyright Epic Games, Inc. All Rights Reserved.

#include "ProjectProject01.h"

#include "Internationalization/StringTableRegistry.h"
#include "Modules/ModuleManager.h"

class FProjectProject01Module final : public FDefaultGameModuleImpl
{
public:
	virtual void StartupModule() override
	{
		FDefaultGameModuleImpl::StartupModule();
		LOCTABLE_FROMFILE_GAME(
			"ProjectProject01UI", "ProjectProject01.UI", "Localization/ProjectProject01UI.csv");
	}

	virtual void ShutdownModule() override
	{
		FStringTableRegistry::Get().UnregisterStringTable(TEXT("ProjectProject01UI"));
		FDefaultGameModuleImpl::ShutdownModule();
	}
};

IMPLEMENT_PRIMARY_GAME_MODULE(FProjectProject01Module, ProjectProject01, "ProjectProject01");
