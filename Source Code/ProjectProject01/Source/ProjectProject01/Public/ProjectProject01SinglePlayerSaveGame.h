#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "ProjectProject01SinglePlayerSaveGame.generated.h"

UCLASS()
class PROJECTPROJECT01_API UProjectProject01SinglePlayerSaveGame final : public USaveGame
{
	GENERATED_BODY()

public:
	static constexpr int32 CurrentSchemaVersion = 2;
	static constexpr int32 MinimumSupportedSchemaVersion = 1;

	UPROPERTY()
	FString Magic = TEXT("ProjectProject01.SinglePlayerSave");

	UPROPERTY()
	int32 SchemaVersion = CurrentSchemaVersion;

	UPROPERTY()
	FString SavedMapPackageName;

	UPROPERTY()
	FName CheckpointId = NAME_None;

	UPROPERTY()
	FTransform PlayerTransform = FTransform::Identity;

	UPROPERTY()
	int32 RemainingDeathCount = 3;

	UPROPERTY()
	float CurrentStamina = 100.0f;

	UPROPERTY()
	TMap<FName, int32> ProgressState;

	UPROPERTY()
	FString AppliedGameDataVersion;

	UPROPERTY()
	FDateTime SavedAtUtc;

	UPROPERTY()
	uint32 IntegrityHash = 0;

	uint32 CalculateIntegrityHash() const;
	bool HasValidIntegrity() const;
	bool MigrateToCurrentVersion(FString& OutError);
};
