// File: Source/ProjectProject01/Private/ProjectProject01VersionContract.cpp

#include "ProjectProject01VersionContract.h"

#include "Interfaces/IHttpRequest.h"
#include "Misc/ConfigCacheIni.h"

namespace ProjectProject01VersionContract
{
	const TCHAR* Section = TEXT("ProjectProject01.VersionContract");

	FString Read(const TCHAR* Key, const TCHAR* Fallback)
	{
		FString Value;
		if (GConfig)
		{
			GConfig->GetString(Section, Key, Value, GGameIni);
		}
		Value.TrimStartAndEndInline();
		return Value.IsEmpty() ? FString(Fallback) : Value;
	}
}

FString FProjectProject01VersionContract::GetClientBuildVersion()
{
	FString Version;
	if (GConfig)
	{
		GConfig->GetString(TEXT("/Script/EngineSettings.GeneralProjectSettings"), TEXT("ProjectVersion"), Version, GGameIni);
	}
	Version.TrimStartAndEndInline();
	return Version.IsEmpty() ? TEXT("1.0.0") : Version;
}

FString FProjectProject01VersionContract::GetDedicatedServerBuildVersion()
{
	return ProjectProject01VersionContract::Read(TEXT("DedicatedServerBuildVersion"), *GetClientBuildVersion());
}

FString FProjectProject01VersionContract::GetApiVersion()
{
	return ProjectProject01VersionContract::Read(TEXT("ApiVersion"), TEXT("1"));
}

FString FProjectProject01VersionContract::GetGameDataVersion()
{
	return ProjectProject01VersionContract::Read(TEXT("GameDataVersion"), TEXT("1"));
}

FString FProjectProject01VersionContract::GetNetworkProtocolVersion()
{
	return ProjectProject01VersionContract::Read(TEXT("NetworkProtocolVersion"), TEXT("1"));
}

FString FProjectProject01VersionContract::GetUpdateMessage()
{
	return ProjectProject01VersionContract::Read(
		TEXT("UpdateMessage"), TEXT("게임 버전이 서버와 맞지 않습니다. 최신 빌드로 업데이트하세요."));
}

void FProjectProject01VersionContract::ApplyToRequest(
	const TSharedRef<IHttpRequest, ESPMode::ThreadSafe>& Request)
{
	Request->SetHeader(TEXT("X-ProjectProject01-Client-Build"), GetClientBuildVersion());
	Request->SetHeader(TEXT("X-ProjectProject01-Server-Build"), GetDedicatedServerBuildVersion());
	Request->SetHeader(TEXT("X-ProjectProject01-Api-Version"), GetApiVersion());
	Request->SetHeader(TEXT("X-ProjectProject01-Game-Data-Version"), GetGameDataVersion());
	Request->SetHeader(TEXT("X-ProjectProject01-Network-Protocol"), GetNetworkProtocolVersion());
}

bool FProjectProject01VersionContract::Matches(
	const FString& ClientBuildVersion,
	const FString& DedicatedServerBuildVersion,
	const FString& ApiVersion,
	const FString& GameDataVersion,
	const FString& NetworkProtocolVersion,
	FString& OutError)
{
	TArray<FString> Mismatches;
	if (ClientBuildVersion != GetClientBuildVersion()) Mismatches.Add(TEXT("ClientBuild"));
	if (DedicatedServerBuildVersion != GetDedicatedServerBuildVersion()) Mismatches.Add(TEXT("DedicatedServerBuild"));
	if (ApiVersion != GetApiVersion()) Mismatches.Add(TEXT("Api"));
	if (GameDataVersion != GetGameDataVersion()) Mismatches.Add(TEXT("GameData"));
	if (NetworkProtocolVersion != GetNetworkProtocolVersion()) Mismatches.Add(TEXT("NetworkProtocol"));
	if (Mismatches.IsEmpty())
	{
		OutError.Reset();
		return true;
	}
	OutError = FString::Printf(TEXT("%s (불일치: %s)"), *GetUpdateMessage(), *FString::Join(Mismatches, TEXT(", ")));
	return false;
}
