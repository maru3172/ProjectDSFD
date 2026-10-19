#include "ProjectProject01CrashFiles.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Internationalization/Regex.h"
#include "HAL/PlatformTime.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace ProjectProject01CrashFiles
{
	bool MatchesNativeCrash(const FString& Xml, const FString& ReportId)
	{
		return !ReportId.IsEmpty() && Xml.Contains(TEXT("<ProjectReportId>") + ReportId + TEXT("</ProjectReportId>")) &&
			!Xml.Contains(TEXT("<IsEnsure>true</IsEnsure>"), ESearchCase::IgnoreCase);
	}

	FString FindNativeCrashDirectory(const FString& ReportId)
	{
		if (ReportId.IsEmpty()) return FString();
		static double LastScan = -100.0;
		static TMap<FString, FString> Directories;
		if (FPlatformTime::Seconds() - LastScan < 30.0) return Directories.FindRef(ReportId);
		LastScan = FPlatformTime::Seconds();
		Directories.Reset();
		TArray<FString> Contexts;
		IFileManager::Get().FindFilesRecursive(Contexts,
			*FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Crashes")), TEXT("CrashContext.runtime-xml"), true, false);
		for (const FString& Path : Contexts)
		{
			FString Xml;
			if (IFileManager::Get().FileSize(*Path) > 4 * 1024 * 1024 || !FFileHelper::LoadFileToString(Xml, *Path)) continue;
			const FString StartTag = TEXT("<ProjectReportId>");
			const int32 Start = Xml.Find(StartTag);
			const int32 End = Xml.Find(TEXT("</ProjectReportId>"));
			if (Start == INDEX_NONE || End <= Start) continue;
			const FString Id = Xml.Mid(Start + StartTag.Len(), End - Start - StartTag.Len());
			if (MatchesNativeCrash(Xml, Id)) Directories.Add(Id, FPaths::GetPath(Path));
		}
		return Directories.FindRef(ReportId);
	}

	FString AttachmentName(const FString& Path)
	{
		if (Path.EndsWith(TEXT(".dmp"), ESearchCase::IgnoreCase)) return TEXT("Crash.dmp");
		if (Path.EndsWith(TEXT(".log"), ESearchCase::IgnoreCase)) return TEXT("Game.log");
		return TEXT("CrashContext.xml");
	}

	TArray<FString> FindAttachments(const FString& Directory)
	{
		TArray<FString> Files, Result;
		if (Directory.IsEmpty()) return Result;
		IFileManager::Get().FindFiles(Files, *FPaths::Combine(Directory, TEXT("*")), true, false);
		TSet<FString> Names;
		for (const FString& Name : Files)
		{
			if (!(Name.EndsWith(TEXT(".dmp")) || Name.EndsWith(TEXT(".log")) || Name == TEXT("CrashContext.runtime-xml"))) continue;
			const FString FullPath = FPaths::Combine(Directory, Name);
			const int64 Size = IFileManager::Get().FileSize(*FullPath);
			if (Size <= 0 || Size > MaximumAttachmentBytes || Names.Contains(AttachmentName(Name))) continue;
			Names.Add(AttachmentName(Name));
			Result.Add(FullPath);
		}
		return Result;
	}

	static FString ReplacePattern(const FString& Input, const FString& Pattern)
	{
		FRegexMatcher Matcher(FRegexPattern(Pattern), Input);
		FString Output;
		int32 Last = 0;
		while (Matcher.FindNext())
		{
			Output += Input.Mid(Last, Matcher.GetMatchBeginning() - Last);
			Output += Matcher.GetCaptureGroup(1) + TEXT("[REDACTED]");
			Last = Matcher.GetMatchEnding();
		}
		return Output + Input.Mid(Last);
	}

	FString ScrubText(FString Text)
	{
		Text = ReplacePattern(Text, TEXT("(?i)(Bearer\\s+)[A-Za-z0-9._~+/=\\-]+"));
		Text = ReplacePattern(Text, TEXT("(?i)((?:access[_-]?token|refresh[_-]?token|id[_-]?token|productUserIdToken|JsonWebToken|participant[_-]?token|client[_-]?secret|password|authorization|encryption[_-]?key|api[_-]?key|game[_-]?server[_-]?secret|admin[_-]?key|EncryptionToken)[\"']?\\s*[:=]\\s*)(?:\"[^\"]*\"|'[^']*'|[^\\s<>;,]+)"));
		Text = ReplacePattern(Text, TEXT("()\\beyJ[A-Za-z0-9_-]+\\.[A-Za-z0-9_-]+\\.[A-Za-z0-9_-]+\\b"));
		Text = ReplacePattern(Text, TEXT("(?is)(<(access[_-]?token|refresh[_-]?token|client[_-]?secret|password|encryption[_-]?key)>).*?(?=</\\2>)"));
		// Native command lines can carry secrets in arbitrary option names.
		Text = ReplacePattern(Text, TEXT("(?i)(<CommandLine>)[\\s\\S]*?(?=</CommandLine>)"));
		Text = ReplacePattern(Text, TEXT("(?im)(Command line:)[^\\r\\n]*"));
		return Text;
	}

	void ExcludeReport(const FString& Path)
	{
		const FString Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("CrashReports/Excluded"));
		IFileManager::Get().MakeDirectory(*Directory, true);
		IFileManager::Get().Move(*FPaths::Combine(Directory, FPaths::GetCleanFilename(Path)), *Path, true, true);
	}

    bool AwaitNativeEvidence(const FString& Path,const TSharedPtr<FJsonObject>& Marker,FDateTime Now)
    {
        if(!Marker)return false;
        FString Value;FDateTime First;
        if(!Marker->TryGetStringField(TEXT("evidenceWaitStartedAtUtc"),Value)||!FDateTime::ParseIso8601(*Value,First)||First>Now+FTimespan::FromSeconds(5))
        {
            Marker->SetStringField(TEXT("evidenceWaitStartedAtUtc"),Now.ToIso8601());FString Json;
            FJsonSerializer::Serialize(Marker.ToSharedRef(),TJsonWriterFactory<>::Create(&Json));
            // Failure to record the grace period must not classify an unconfirmed run as a crash.
            FFileHelper::SaveStringToFile(Json,*Path,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
            return true;
        }
        return (Now-First).GetTotalSeconds()<120;
    }

	void ExcludeEditorReports()
	{
		const FString Root = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("CrashReports"));
		TArray<FString> Names;
		IFileManager::Get().FindFiles(Names, *FPaths::Combine(Root, TEXT("*.json")), true, false);
		for (const FString& Name : Names)
		{
			if (!Name.StartsWith(TEXT("Active-")) && !Name.StartsWith(TEXT("Pending-"))) continue;
			FString Stored, MapName; TSharedPtr<FJsonObject> Json;
			const FString Path = FPaths::Combine(Root, Name);
			if (FFileHelper::LoadFileToString(Stored, *Path) &&
				FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Stored), Json) && Json.IsValid() &&
				Json->TryGetStringField(TEXT("mapName"), MapName) && MapName.StartsWith(TEXT("UEDPIE_"))) ExcludeReport(Path);
		}
	}
}

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectCrashEvidenceTest, "ProjectProject01.CrashReports.EvidenceAndRedaction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectCrashEvidenceTest::RunTest(const FString&)
{
	const FString Xml = TEXT("<GameData><ProjectReportId>known-id</ProjectReportId></GameData>");
	TestTrue(TEXT("Exact native run evidence is accepted"), ProjectProject01CrashFiles::MatchesNativeCrash(Xml, TEXT("known-id")));
	TestFalse(TEXT("Unrelated run is not a crash"), ProjectProject01CrashFiles::MatchesNativeCrash(Xml, TEXT("other-id")));
	TestFalse(TEXT("Ensure is not a fatal crash"), ProjectProject01CrashFiles::MatchesNativeCrash(Xml + TEXT("<IsEnsure>true</IsEnsure>"), TEXT("known-id")));
	const FString Scrubbed = ProjectProject01CrashFiles::ScrubText(TEXT("password=secret-value Bearer secret-token\n<CommandLine>-secret=hidden-value</CommandLine>"));
	TestFalse(TEXT("Password redacted before upload"), Scrubbed.Contains(TEXT("secret-value")));
	TestFalse(TEXT("Token redacted before upload"), Scrubbed.Contains(TEXT("secret-token")));
	TestFalse(TEXT("Command line redacted before upload"), Scrubbed.Contains(TEXT("hidden-value")));
	const FString Structured = ProjectProject01CrashFiles::ScrubText(TEXT("{\"password\":\"spaced secret\"}<AccessToken>xml-secret</AccessToken>"));
	TestFalse(TEXT("JSON secret redacted"), Structured.Contains(TEXT("spaced secret")));
	TestFalse(TEXT("XML token redacted"), Structured.Contains(TEXT("xml-secret")));
	const FString Proof=ProjectProject01CrashFiles::ScrubText(TEXT("JsonWebToken=eyJhbGciOiJSUzI1NiJ9.eyJzdWIiOiJmaXh0dXJlIn0.c2lnbmF0dXJl"));
	TestFalse(TEXT("EOS identity proof redacted before upload"),Proof.Contains(TEXT("eyJ")));
	TestEqual(TEXT("Dump has canonical server file name"), ProjectProject01CrashFiles::AttachmentName(TEXT("UE4Minidump.dmp")), FString(TEXT("Crash.dmp")));
	return true;
}
#endif
