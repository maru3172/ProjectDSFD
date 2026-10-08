#include "ProjectProject01SinglePlayerSaveGame.h"

#include "Misc/Crc.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#endif

uint32 UProjectProject01SinglePlayerSaveGame::CalculateIntegrityHash() const
{
	TArray<FName> Keys;
	ProgressState.GetKeys(Keys);
	Keys.Sort(FNameLexicalLess());

	FString Payload = FString::Printf(TEXT("%s|%d|%s|%s|%s|%d|%.6f|%s|%s"),
		*Magic,
		SchemaVersion,
		*SavedMapPackageName,
		*CheckpointId.ToString(),
		*PlayerTransform.ToHumanReadableString(),
		RemainingDeathCount,
		CurrentStamina,
		*AppliedGameDataVersion,
		*SavedAtUtc.ToIso8601());
	for (const FName Key : Keys)
	{
		Payload += FString::Printf(TEXT("|%s=%d"), *Key.ToString(), ProgressState.FindRef(Key));
	}
	return FCrc::StrCrc32(*Payload);
}

bool UProjectProject01SinglePlayerSaveGame::HasValidIntegrity() const
{
	return Magic == TEXT("ProjectProject01.SinglePlayerSave") && IntegrityHash != 0 &&
		IntegrityHash == CalculateIntegrityHash();
}

bool UProjectProject01SinglePlayerSaveGame::MigrateToCurrentVersion(FString& OutError)
{
	if (SchemaVersion > CurrentSchemaVersion)
	{
		OutError = TEXT("현재 게임보다 새 버전에서 만든 저장 파일입니다.");
		return false;
	}
	if (SchemaVersion < MinimumSupportedSchemaVersion)
	{
		OutError = TEXT("지원 기간이 지난 저장 파일입니다.");
		return false;
	}
	if (SchemaVersion == 1)
	{
		// v1에는 진행 상태 맵만 없었습니다. 나머지 데이터는 그대로 호환됩니다.
		ProgressState.Reset();
		SchemaVersion = CurrentSchemaVersion;
		IntegrityHash = CalculateIntegrityHash();
	}
	OutError.Reset();
	return true;
}

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectProject01SinglePlayerSaveIntegrityTest,
	"ProjectProject01.SinglePlayer.SaveIntegrityAndMigration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectProject01SinglePlayerSaveIntegrityTest::RunTest(const FString& Parameters)
{
	UProjectProject01SinglePlayerSaveGame* Save = NewObject<UProjectProject01SinglePlayerSaveGame>();
	Save->SavedMapPackageName = TEXT("/Game/MyProject/Level/LevelTest01");
	Save->CheckpointId = TEXT("TestCheckpoint");
	Save->RemainingDeathCount = 2;
	Save->CurrentStamina = 37.5f;
	Save->AppliedGameDataVersion = TEXT("TestDataV1");
	Save->SavedAtUtc = FDateTime(2026, 10, 8, 0, 0, 0);
	Save->ProgressState.Add(TEXT("Door"), 1);
	Save->IntegrityHash = Save->CalculateIntegrityHash();
	TestTrue(TEXT("Fresh save passes integrity"), Save->HasValidIntegrity());

	Save->CurrentStamina = 99.0f;
	TestFalse(TEXT("Tampered save fails integrity"), Save->HasValidIntegrity());
	Save->CurrentStamina = 37.5f;
	Save->IntegrityHash = Save->CalculateIntegrityHash();

	Save->SchemaVersion = 1;
	Save->ProgressState.Reset();
	Save->IntegrityHash = Save->CalculateIntegrityHash();
	FString Error;
	TestTrue(TEXT("Supported v1 save migrates"), Save->MigrateToCurrentVersion(Error));
	TestEqual(TEXT("Migrated schema is current"), Save->SchemaVersion,
		UProjectProject01SinglePlayerSaveGame::CurrentSchemaVersion);
	TestTrue(TEXT("Migrated save has valid integrity"), Save->HasValidIntegrity());

	Save->SchemaVersion = UProjectProject01SinglePlayerSaveGame::CurrentSchemaVersion + 1;
	Save->IntegrityHash = Save->CalculateIntegrityHash();
	TestFalse(TEXT("Future save is rejected"), Save->MigrateToCurrentVersion(Error));
	return true;
}
#endif
