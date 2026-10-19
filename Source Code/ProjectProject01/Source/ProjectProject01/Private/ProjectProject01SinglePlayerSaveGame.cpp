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

	// v1/v2 저장 파일은 당시 계산식을 그대로 사용해야 무결성 확인 후 v3로 이관할 수 있다.
	if (SchemaVersion <= 2)
	{
		return FCrc::StrCrc32(*Payload);
	}

	Payload += FString::Printf(TEXT("|PV=%s|PCR=%s|WT=%.6f"),
		*PlayerVelocity.ToString(), *PlayerControlRotation.ToString(), SavedWorldTimeSeconds);
	TArray<FProjectProject01SavedActorState> SortedActors = ActorStates;
	SortedActors.Sort([](const FProjectProject01SavedActorState& Left, const FProjectProject01SavedActorState& Right)
	{
		return Left.ActorId.LexicalLess(Right.ActorId);
	});
	for (const FProjectProject01SavedActorState& Actor : SortedActors)
	{
		const uint32 ActorDataCrc = Actor.SaveGameData.IsEmpty()
			? 0
			: FCrc::MemCrc32(Actor.SaveGameData.GetData(), Actor.SaveGameData.Num());
		Payload += FString::Printf(TEXT("|A:%s:%s:%s:%s:%s:%d%d%d%d:%d:%08X"),
			*Actor.ActorId.ToString(),
			*Actor.ActorClassPath,
			*Actor.Transform.ToHumanReadableString(),
			*Actor.LinearVelocity.ToString(),
			*Actor.AngularVelocityDegrees.ToString(),
			Actor.bMapPlacedActor ? 1 : 0,
			Actor.bHidden ? 1 : 0,
			Actor.bCollisionEnabled ? 1 : 0,
			Actor.bTickEnabled ? 1 : 0,
			Actor.SaveGameData.Num(),
			ActorDataCrc);
		if (SchemaVersion >= 7)
			Payload += FString::Printf(TEXT("|AI:%d:%08X"), Actor.SinglePlayerAIData.Num(), Actor.SinglePlayerAIData.IsEmpty() ? 0 : FCrc::MemCrc32(Actor.SinglePlayerAIData.GetData(), Actor.SinglePlayerAIData.Num()));

		TArray<FProjectProject01SavedComponentState> SortedComponents = Actor.Components;
		SortedComponents.Sort([](const FProjectProject01SavedComponentState& Left,
			const FProjectProject01SavedComponentState& Right)
		{
			return Left.ComponentName.LexicalLess(Right.ComponentName);
		});
		for (const FProjectProject01SavedComponentState& Component : SortedComponents)
		{
			const uint32 ComponentDataCrc = Component.SaveGameData.IsEmpty()
				? 0
				: FCrc::MemCrc32(Component.SaveGameData.GetData(), Component.SaveGameData.Num());
			Payload += FString::Printf(TEXT("|C:%s:%s:%d%d%d%d:%d:%d:%08X"),
				*Component.ComponentName.ToString(),
				*Component.RelativeTransform.ToHumanReadableString(),
				Component.bIsSceneComponent ? 1 : 0,
				Component.bActive ? 1 : 0,
				Component.bTickEnabled ? 1 : 0,
				Component.bVisible ? 1 : 0,
				static_cast<int32>(Component.CollisionEnabled.GetValue()),
				Component.SaveGameData.Num(),
				ComponentDataCrc);
		}
	}
	TArray<FName> SortedDestroyed = DestroyedActorIds;
	SortedDestroyed.Sort(FNameLexicalLess());
	for (const FName ActorId : SortedDestroyed)
	{
		Payload += FString::Printf(TEXT("|D:%s"), *ActorId.ToString());
	}
	// v3 저장 파일은 LastPlayedAtUtc가 없으므로 기존 계산식을 유지한 채 검증한 뒤 v4로 이관한다.
	if (SchemaVersion >= 4)
	{
		Payload += FString::Printf(TEXT("|LP=%s"), *LastPlayedAtUtc.ToIso8601());
	}
	if (SchemaVersion >= 6) Payload += FString::Printf(TEXT("|REV=%lld"), SaveRevision);
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
	if (SchemaVersion <= 2)
	{
		// v1에는 진행 상태 맵이 없고, v1/v2에는 월드 액터 스냅샷이 없었습니다.
		if (SchemaVersion == 1)
		{
			ProgressState.Reset();
		}
		PlayerVelocity = FVector::ZeroVector;
		PlayerControlRotation = FRotator::ZeroRotator;
		SavedWorldTimeSeconds = 0.0;
		ActorStates.Reset();
		DestroyedActorIds.Reset();
	}
	if (SchemaVersion <= 3)
	{
		LastPlayedAtUtc = SavedAtUtc;
	}
	if (SchemaVersion <= 5) SaveRevision = 0;
	if (SchemaVersion < CurrentSchemaVersion)
	{
		if (SchemaVersion < 5)
		{
			PreviewPng.Reset();
			PreviewCrc = 0;
		}
		SchemaVersion = CurrentSchemaVersion;
		IntegrityHash = CalculateIntegrityHash();
	}
	OutError.Reset();
	return true;
}

uint32 UProjectProject01CheckpointRecency::CalculateChecksum() const
{
	return FCrc::StrCrc32(*FString::Printf(TEXT("%08X|%s"), SnapshotHash, *LastPlayedAtUtc.ToIso8601()));
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
	Save->PlayerVelocity = FVector(100.0, 20.0, 0.0);
	Save->PlayerControlRotation = FRotator(5.0, 90.0, 0.0);
	Save->SavedWorldTimeSeconds = 42.0;
	Save->AppliedGameDataVersion = TEXT("TestDataV1");
	Save->SavedAtUtc = FDateTime(2026, 10, 8, 0, 0, 0);
	Save->LastPlayedAtUtc = Save->SavedAtUtc;
	Save->ProgressState.Add(TEXT("Door"), 1);
	Save->IntegrityHash = Save->CalculateIntegrityHash();
	TestTrue(TEXT("Fresh save passes integrity"), Save->HasValidIntegrity());

	Save->CurrentStamina = 99.0f;
	TestFalse(TEXT("Tampered save fails integrity"), Save->HasValidIntegrity());
	Save->CurrentStamina = 37.5f;
	Save->IntegrityHash = Save->CalculateIntegrityHash();

	Save->SchemaVersion = 3;
	Save->LastPlayedAtUtc = FDateTime::MinValue();
	Save->IntegrityHash = Save->CalculateIntegrityHash();
	TestTrue(TEXT("Legacy v3 save passes its original integrity calculation"), Save->HasValidIntegrity());
	FString Error;
	TestTrue(TEXT("Supported v3 save migrates"), Save->MigrateToCurrentVersion(Error));
	TestEqual(TEXT("Migrated v3 save uses save time as last played time"),
		Save->LastPlayedAtUtc, Save->SavedAtUtc);
	TestTrue(TEXT("Migrated v3 save has valid integrity"), Save->HasValidIntegrity());

	Save->SchemaVersion = 4;
	Save->IntegrityHash = Save->CalculateIntegrityHash();
	const FDateTime PreviousPlayTime = Save->LastPlayedAtUtc;
	TestTrue(TEXT("Legacy v4 save validates before optional preview migration"), Save->HasValidIntegrity());
	TestTrue(TEXT("Existing three-slot v4 save migrates"), Save->MigrateToCurrentVersion(Error));
	TestEqual(TEXT("v4 migration preserves last played metadata"), Save->LastPlayedAtUtc, PreviousPlayTime);
	TestTrue(TEXT("Legacy save remains loadable without a screenshot"), Save->PreviewPng.IsEmpty() && Save->HasValidIntegrity());

	Save->SchemaVersion = 2;
	Save->PlayerVelocity = FVector::ZeroVector;
	Save->PlayerControlRotation = FRotator::ZeroRotator;
	Save->SavedWorldTimeSeconds = 0.0;
	Save->ActorStates.Reset();
	Save->DestroyedActorIds.Reset();
	Save->ProgressState.Reset();
	Save->IntegrityHash = Save->CalculateIntegrityHash();
	TestTrue(TEXT("Supported v2 save migrates"), Save->MigrateToCurrentVersion(Error));
	TestEqual(TEXT("Migrated schema is current"), Save->SchemaVersion,
		UProjectProject01SinglePlayerSaveGame::CurrentSchemaVersion);
	TestTrue(TEXT("Migrated save has valid integrity"), Save->HasValidIntegrity());
	TestEqual(TEXT("Migrated save uses save time as last played time"),
		Save->LastPlayedAtUtc, Save->SavedAtUtc);

	Save->SchemaVersion = UProjectProject01SinglePlayerSaveGame::CurrentSchemaVersion + 1;
	Save->IntegrityHash = Save->CalculateIntegrityHash();
	TestFalse(TEXT("Future save is rejected"), Save->MigrateToCurrentVersion(Error));
	return true;
}
#endif
