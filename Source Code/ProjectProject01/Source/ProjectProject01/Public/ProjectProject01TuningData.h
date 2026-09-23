// File: Source/ProjectProject01/Public/ProjectProject01TuningData.h
// Target: ProjectProject01Editor Win64 Development, Unreal Engine 5.8.2

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "Subsystems/WorldSubsystem.h"
#include "ProjectProject01TuningData.generated.h"

class UDataTable;

PROJECTPROJECT01_API DECLARE_LOG_CATEGORY_EXTERN(LogProjectProject01Tuning, Log, All);

USTRUCT(BlueprintType)
struct PROJECTPROJECT01_API FMannequinAITuningRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Player", meta = (ClampMin = "0.0", Units = "cm/s"))
	float PlayerWalkSpeed = 600.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mannequin AI", meta = (ClampMin = "0.0", Units = "cm/s"))
	float MannequinWalkSpeed = 600.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mannequin AI", meta = (ClampMin = "0.0", Units = "cm"))
	float DirectChaseRadius = 500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mannequin AI", meta = (ClampMin = "0.0", Units = "cm"))
	float RoamingOuterRadius = 1500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mannequin AI", meta = (ClampMin = "0.0", Units = "deg"))
	float DirectChaseHalfAngleDegrees = 90.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mannequin AI", meta = (ClampMin = "0.0", Units = "cm"))
	float MannequinGatherRadius = 500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mannequin AI", meta = (ClampMin = "0"))
	int32 RequiredMannequinCount = 3;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mannequin AI|Survivor Vision", meta = (ClampMin = "0.0", Units = "s"))
	float SurvivorVisionCheckIntervalSeconds = 0.05f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mannequin AI|Survivor Vision", meta = (ClampMin = "0.0", Units = "deg"))
	float SurvivorVisionHalfAngleDegrees = 55.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mannequin AI|Survivor Vision", meta = (ClampMin = "0.0", Units = "cm"))
	float DetectionMargin = 30.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mannequin AI|Survivor Vision", meta = (ClampMin = "0.0"))
	float VisionFovMarginMultiplier = 1.15f;

	bool IsValidForApplication(FString& OutError) const;
};

USTRUCT(BlueprintType)
struct PROJECTPROJECT01_API FHelperTuningRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Helper", meta = (ClampMin = "0.0", Units = "cm/s"))
	float HelperWalkSpeed = 600.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Helper", meta = (ClampMin = "0.0", Units = "cm"))
	float FollowDistance = 5050.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Helper", meta = (ClampMin = "0.0", Units = "cm"))
	float MinimumFollowSeparation = 5000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Helper|Vision", meta = (ClampMin = "0.0", Units = "cm"))
	float GuardSightRadius = 1500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Helper|Vision", meta = (ClampMin = "0.0", Units = "deg"))
	float GuardHalfAngleDegrees = 70.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Helper|Navigation", meta = (ClampMin = "0.0", Units = "s"))
	float RepathInterval = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Helper|Navigation", meta = (ClampMin = "0.0", Units = "cm"))
	float RepathDistance = 25.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Helper|Navigation", meta = (ClampMin = "0.0", Units = "cm"))
	float AcceptanceRadius = 25.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Helper|Navigation", meta = (ClampMin = "0.0", Units = "s"))
	float StuckTimeout = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Helper|Navigation", meta = (ClampMin = "0.0", Units = "s"))
	float StuckWaitTime = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Helper|Navigation", meta = (ClampMin = "0.0", Units = "cm"))
	float ProgressDistance = 20.0f;

	bool IsValidForApplication(FString& OutError) const;
};

/** 서버 권한 월드에서만 밸런스 DataTable을 검증하고 현재 액터에 적용합니다. */
UCLASS()
class PROJECTPROJECT01_API UProjectProject01TuningSubsystem final : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	bool ReloadFromDataTables(FString& OutError);
	bool ApplyToCurrentWorld(FString& OutError);
	bool GetMannequinTuning(FMannequinAITuningRow& OutTuning) const;
	bool GetHelperTuning(FHelperTuningRow& OutTuning) const;

private:
	bool LoadAndValidateTables(FString& OutError);

	UPROPERTY(Transient)
	TObjectPtr<UDataTable> MannequinTuningTable;

	UPROPERTY(Transient)
	TObjectPtr<UDataTable> HelperTuningTable;

	FMannequinAITuningRow CachedMannequinTuning;
	FHelperTuningRow CachedHelperTuning;
	bool bHasValidMannequinTuning = false;
	bool bHasValidHelperTuning = false;
};
