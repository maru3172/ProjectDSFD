// File: Source/ProjectProject01/Public/ProjectProject01Localization.h
// Central UI text resolver backed by Content/Localization/ProjectProject01UI.csv.

#pragma once

#include "CoreMinimal.h"

struct PROJECTPROJECT01_API FProjectProject01Localization
{
	/** Korean source text is also the stable table key. Missing dynamic text safely falls back unchanged. */
	static FText Text(const FString& SourceOrKey);
	static FText Text(const TCHAR* SourceOrKey) { return Text(FString(SourceOrKey)); }
	static bool SetCulture(const FString& CultureName);
	static FString GetCulture();
};
