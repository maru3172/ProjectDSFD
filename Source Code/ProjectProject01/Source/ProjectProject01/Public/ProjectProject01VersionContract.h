// File: Source/ProjectProject01/Public/ProjectProject01VersionContract.h
// Shared client/server compatibility contract used by every backend request.

#pragma once

#include "CoreMinimal.h"

class IHttpRequest;

struct PROJECTPROJECT01_API FProjectProject01VersionContract
{
	static FString GetClientBuildVersion();
	static FString GetDedicatedServerBuildVersion();
	static FString GetApiVersion();
	static FString GetGameDataVersion();
	static FString GetNetworkProtocolVersion();
	static FString GetUpdateMessage();

	static void ApplyToRequest(const TSharedRef<IHttpRequest, ESPMode::ThreadSafe>& Request);
	static bool Matches(
		const FString& ClientBuildVersion,
		const FString& DedicatedServerBuildVersion,
		const FString& ApiVersion,
		const FString& GameDataVersion,
		const FString& NetworkProtocolVersion,
		FString& OutError);
};
