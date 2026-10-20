#pragma once
#include "CoreMinimal.h"

namespace ProjectDFSDBranding
{
	PROJECTPROJECT01_API FString GameTitle();
	/** One-time, copy-only import. Never overwrites new saves or copies auth/log/crash data. */
	PROJECTPROJECT01_API bool ImportLegacyUserData(const FString& LegacySaved, const FString& NewSaved);
}
