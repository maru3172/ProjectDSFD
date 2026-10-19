#pragma once

#include "CoreMinimal.h"
class FJsonObject;

namespace ProjectProject01CrashFiles
{
	constexpr int64 MaximumAttachmentBytes = 64LL * 1024 * 1024;
	/** Only native crash evidence bearing this run's exact ID may become a report. */
	FString FindNativeCrashDirectory(const FString& ReportId);
	bool MatchesNativeCrash(const FString& Xml, const FString& ReportId);
	TArray<FString> FindAttachments(const FString& Directory);
	FString AttachmentName(const FString& Path);
	FString ScrubText(FString Text);
	void ExcludeReport(const FString& Path);
	void ExcludeEditorReports();
    bool AwaitNativeEvidence(const FString& MarkerPath,const TSharedPtr<FJsonObject>& Marker,FDateTime Now=FDateTime::UtcNow());
}
