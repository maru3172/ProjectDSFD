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
struct PROJECTPROJECT01_API FPlayerTuningRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Player", meta = (ClampMin = "0.0", Units = "cm/s"))
	float PlayerWalkSpeed = 600.0f;

	/** 심박 대상이 멀리 있을 때 적용되는 최소 BPM입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Player|Heartbeat", meta = (ClampMin = "0.0"))
	float MinBPM = 60.0f;

	/** 심박 대상이 플레이어와 매우 가까울 때 적용되는 거리 기반 최대 BPM입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Player|Heartbeat", meta = (ClampMin = "0.0"))
	float MaxDistanceBPM = 120.0f;

	/** 최초 발견 및 재발견 상승을 포함한 최종 BPM 상한입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Player|Heartbeat", meta = (ClampMin = "0.0"))
	float MaxEncounterBPM = 165.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Player|Heartbeat", meta = (ClampMin = "0.0"))
	float MinEncounterBoost = 15.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Player|Heartbeat", meta = (ClampMin = "0.0"))
	float MaxEncounterBoost = 45.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Player|Heartbeat", meta = (ClampMin = "0.0"))
	float BPMDecayPerSecond = 12.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Player|Heartbeat", meta = (ClampMin = "0.0", Units = "cm"))
	float HeartbeatRange = 1500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Player|Heartbeat", meta = (ClampMin = "0.0", Units = "cm"))
	float RetentionRange = 1800.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Player|Heartbeat", meta = (ClampMin = "0.0", Units = "deg"))
	float RecognitionHalfAngleDegrees = 45.0f;

	/** false면 현재 발견 부채꼴과 거리만으로 심박 활성 여부를 결정합니다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Player|Heartbeat")
	bool bUseRetentionRules = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Player|Heartbeat", meta = (ClampMin = "0.0", Units = "deg"))
	float RetentionHalfAngleDegrees = 80.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Player|Heartbeat", meta = (ClampMin = "0.0", Units = "s"))
	float LostSightGraceSeconds = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Player|Heartbeat", meta = (ClampMin = "0.0", Units = "s"))
	float SurpriseRearmDelay = 0.75f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Player|Heartbeat", meta = (ClampMin = "0.0", Units = "s"))
	float SurpriseCooldown = 3.0f;

	/** false면 최초 발견 상승만 허용하고 재발견 상승은 사용하지 않습니다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Player|Heartbeat")
	bool bEnableRediscovery = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Player|Heartbeat", meta = (ClampMin = "0.0", Units = "s"))
	float VisionCheckInterval = 0.05f;

	/** 런타임에 생성·삭제되는 마네킹 목록을 다시 찾는 주기입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Player|Heartbeat", meta = (ClampMin = "0.0", Units = "s"))
	float MannequinRefreshInterval = 1.0f;

	/** false면 BPM 계산은 유지하면서 로그만 끕니다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Player|Heartbeat")
	bool bEnableHeartbeatLog = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Player|Heartbeat", meta = (ClampMin = "0.0"))
	float BPMLogThreshold = 1.0f;

	bool IsValidForApplication(FString& OutError) const;
};

USTRUCT(BlueprintType)
struct PROJECTPROJECT01_API FMannequinAITuningRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mannequin AI", meta = (ClampMin = "0.0", Units = "cm/s"))
	float MannequinWalkSpeed = 600.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mannequin AI", meta = (ClampMin = "0.0", Units = "cm"))
	float DirectChaseRadius = 500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mannequin AI", meta = (ClampMin = "0.0", Units = "cm"))
	float RoamingOuterRadius = 1500.0f;

	/** 마네킹 플레이어가 남긴 정지 또는 추격 명령을 AI보다 우선하는 시간입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mannequin AI|Player Command", meta = (ClampMin = "0.0", Units = "s"))
	float PostPossessionCommandDurationSeconds = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mannequin AI", meta = (ClampMin = "0.0", Units = "deg"))
	float DirectChaseHalfAngleDegrees = 90.0f;

	/** 플레이어 중심에서 직접 추격 부채꼴 끝까지의 반경입니다. 내부 빨간 원보다 작으면 내부 반경으로, 바깥 배회 반경보다 크면 바깥 반경으로 적용됩니다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mannequin AI", meta = (ClampMin = "0.0", Units = "cm"))
	float DirectChaseSectorRadius = 1000.0f;

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
	float GuardHalfAngleDegrees = 60.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Helper|Vision", meta = (ClampMin = "0.0", Units = "deg"))
	float GuardSearchHalfAngleDegrees = 90.0f;

	/** 실제 시야 중심 변경을 누적하는 시간창입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Helper|Vision Retreat", meta = (ClampMin = "0.0", Units = "s"))
	float VisionDirectionChangeWindowSeconds = 3.0f;

	/** 시간창 안에서 후방 이격을 발동할 최소 유효 시야 중심 변경 횟수입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Helper|Vision Retreat", meta = (ClampMin = "1"))
	int32 VisionDirectionChangeRequiredCount = 5;

	/** 이 각도 이상 실제 시야 중심이 달라져야 유효 변경 한 번으로 기록합니다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Helper|Vision Retreat", meta = (ClampMin = "0.0", Units = "deg"))
	float VisionDirectionChangeThresholdDegrees = 5.0f;

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

/** 서버 권한 월드에서만 세 DataTable을 함께 검증하고 현재 액터에 적용합니다. */
UCLASS()
class PROJECTPROJECT01_API UProjectProject01TuningSubsystem final : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	bool ReloadFromDataTables(FString& OutError);
	bool ApplyToCurrentWorld(FString& OutError);
	bool GetPlayerTuning(FPlayerTuningRow& OutTuning) const;
	bool GetMannequinTuning(FMannequinAITuningRow& OutTuning) const;
	bool GetHelperTuning(FHelperTuningRow& OutTuning) const;

private:
	bool LoadAndValidateTables(FString& OutError);

	UPROPERTY(Transient)
	TObjectPtr<UDataTable> PlayerTuningTable;

	UPROPERTY(Transient)
	TObjectPtr<UDataTable> MannequinTuningTable;

	UPROPERTY(Transient)
	TObjectPtr<UDataTable> HelperTuningTable;

	FPlayerTuningRow CachedPlayerTuning;
	FMannequinAITuningRow CachedMannequinTuning;
	FHelperTuningRow CachedHelperTuning;
	bool bHasValidPlayerTuning = false;
	bool bHasValidMannequinTuning = false;
	bool bHasValidHelperTuning = false;
};
