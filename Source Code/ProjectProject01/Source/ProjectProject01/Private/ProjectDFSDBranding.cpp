#include "ProjectDFSDBranding.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ConfigCacheIni.h"

FString ProjectDFSDBranding::GameTitle()
{
	FString Title;
	if (GConfig) GConfig->GetString(TEXT("/Script/EngineSettings.GeneralProjectSettings"), TEXT("ProjectName"), Title, GGameIni);
	return Title.IsEmpty() ? FString(TEXT("백화점 화재사건")) : Title;
}

bool ProjectDFSDBranding::ImportLegacyUserData(const FString& LegacySaved, const FString& NewSaved)
{
	IFileManager& Files = IFileManager::Get();
	const FString Marker = NewSaved / TEXT(".projectdfsd-legacy-imported");
	if (Files.FileExists(*Marker) || !Files.DirectoryExists(*LegacySaved)) return true;
	bool bSucceeded = true;
	const auto CopyMissing = [&](const FString& Relative)
	{
		const FString Source = LegacySaved / Relative, Destination = NewSaved / Relative;
		if (!Files.FileExists(*Source) || Files.FileExists(*Destination)) return;
		Files.MakeDirectory(*FPaths::GetPath(Destination), true);
		bSucceeded &= Files.Copy(*Destination, *Source, false) == COPY_OK;
	};
	TArray<FString> Names;
	Files.FindFiles(Names, *(LegacySaved / TEXT("SaveGames/*.sav")), true, false);
	for (const FString& Name : Names) CopyMissing(TEXT("SaveGames") / Name);
	for (const TCHAR* Platform : {TEXT("Windows"), TEXT("WindowsNoEditor")})
		CopyMissing(FString(TEXT("Config")) / Platform / TEXT("GameUserSettings.ini"));
	// Do not import deleted old slots on every launch after the player starts using the new name.
	if (bSucceeded)
	{
		Files.MakeDirectory(*NewSaved, true);
		bSucceeded = FFileHelper::SaveStringToFile(TEXT("1"), *Marker);
	}
	return bSucceeded;
}
