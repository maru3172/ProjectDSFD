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

	/** LShift를 누르는 동안 적용하는 플레이어 달리기 최대 속도입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Player", meta = (ClampMin = "0.0", Units = "cm/s"))
	float PlayerSprintSpeed = 750.0f;

	/** 플레이어가 시작할 때 보유하는 최대 스태미나입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Player|Stamina", meta = (ClampMin = "0.0"))
	float MaxStamina = 100.0f;

	/** 실제로 달리는 동안 초당 소모하는 스태미나입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Player|Stamina", meta = (ClampMin = "0.0"))
	float StaminaDrainPerSecond = 5.0f;

	/** 달리지 않을 때 초당 회복하는 스태미나입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Player|Stamina", meta = (ClampMin = "0.0"))
	float StaminaRecoveryPerSecond = 3.0f;

	/** 기본 심박 판정 부채꼴의 최대 거리입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Player|Heartbeat|Base", meta = (ClampMin = "0.0", Units = "cm"))
	float BaseVisionRange = 5000.0f;

	/** 기본 심박 판정 부채꼴의 한쪽 각도입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Player|Heartbeat|Base", meta = (ClampMin = "0.0", ClampMax = "180.0", Units = "deg"))
	float BaseVisionHalfAngleDegrees = 45.0f;

	/** 기본 시야 범위 끝에서 적용되는 최소 BPM입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Player|Heartbeat|Base", meta = (ClampMin = "0.0"))
	float BaseMinBPM = 60.0f;

	/** 기본 시야 안에서 마네킹이 플레이어와 겹칠 때 적용되는 최대 BPM입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Player|Heartbeat|Base", meta = (ClampMin = "0.0"))
	float BaseMaxBPM = 120.0f;

	/** 최초 발견·재발견 조우 판정 부채꼴의 최대 거리입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Player|Heartbeat|Encounter", meta = (ClampMin = "0.0", Units = "cm"))
	float EncounterVisionRange = 1500.0f;

	/** 최초 발견·재발견 조우 판정 부채꼴의 한쪽 각도입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Player|Heartbeat|Encounter", meta = (ClampMin = "0.0", ClampMax = "180.0", Units = "deg"))
	float EncounterVisionHalfAngleDegrees = 45.0f;

	/** 조우 시야 범위 끝에서 발생하는 최소 조우 BPM입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Player|Heartbeat|Encounter", meta = (ClampMin = "0.0"))
	float EncounterMinBPM = 75.0f;

	/** 조우 시야 안에서 마네킹이 플레이어와 겹칠 때 발생하는 최대 조우 BPM입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Player|Heartbeat|Encounter", meta = (ClampMin = "0.0"))
	float EncounterMaxBPM = 165.0f;

	/** 조우 BPM이 기본 BPM으로 초당 감소하는 양입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Player|Heartbeat|Encounter", meta = (ClampMin = "0.0"))
	float EncounterDecayPerSecond = 12.0f;

	/** 조우 시야와 직접 가시선이 모두 끊긴 뒤 재발견이 준비되는 시간입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Player|Heartbeat|Encounter", meta = (ClampMin = "0.0", Units = "s"))
	float EncounterMemorySeconds = 3.0f;

	/** 심박 VFX의 거리·밀집도 계산에 사용하는 최대 거리입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Player|Heartbeat VFX", meta = (ClampMin = "0.0", Units = "cm"))
	float HeartbeatRange = 1500.0f;

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

	/** 로컬 플레이어 화면의 심박 연동 후처리 효과를 켭니다. 전용 서버에는 적용되지 않습니다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Player|Heartbeat VFX")
	bool bEnableHeartbeatVFX = true;

	/** 이 수 이상의 활성 마네킹이 가까이 모이면 밀집도 기반 효과가 최대가 됩니다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Player|Heartbeat VFX", meta = (ClampMin = "1"))
	int32 HeartbeatVFXDensityCountForMax = 5;

	/** 가장 가까운 활성 마네킹이 플레이어와 겹칠 때 사용할 최대 화면 효과 불투명도입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Player|Heartbeat VFX", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float HeartbeatVFXMaxOpacity = 1.0f;

	/** 밀집도가 최대일 때 TV 노이즈의 최대 밝기 변화량입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Player|Heartbeat VFX", meta = (ClampMin = "0.0"))
	float HeartbeatVFXMaxNoiseIntensity = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Player|Heartbeat VFX", meta = (ClampMin = "0.0"))
	float HeartbeatVFXMinNoiseSpeed = 0.75f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Player|Heartbeat VFX", meta = (ClampMin = "0.0"))
	float HeartbeatVFXMaxNoiseSpeed = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Player|Heartbeat VFX", meta = (ClampMin = "0.0"))
	float HeartbeatVFXMinNoiseFrequency = 80.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Player|Heartbeat VFX", meta = (ClampMin = "0.0"))
	float HeartbeatVFXMaxNoiseFrequency = 320.0f;

	/** 밀집도가 최대일 때 화면 UV를 좌우로 흔드는 최대 크기입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Player|Heartbeat VFX", meta = (ClampMin = "0.0"))
	float HeartbeatVFXMaxDistortionAmount = 0.012f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Player|Heartbeat VFX", meta = (ClampMin = "0.0"))
	float HeartbeatVFXMinDistortionSpeed = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Player|Heartbeat VFX", meta = (ClampMin = "0.0"))
	float HeartbeatVFXMaxDistortionSpeed = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Player|Heartbeat VFX", meta = (ClampMin = "0.0"))
	float HeartbeatVFXMinDistortionFrequency = 6.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Player|Heartbeat VFX", meta = (ClampMin = "0.0"))
	float HeartbeatVFXMaxDistortionFrequency = 40.0f;

	/** 마네킹이 가까워질 때 효과가 선명해지는 보간 속도입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Player|Heartbeat VFX", meta = (ClampMin = "0.0"))
	float HeartbeatVFXBlendInSpeed = 4.0f;

	/** 마네킹이 멀어지거나 범위를 벗어날 때 효과가 투명해지는 보간 속도입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Player|Heartbeat VFX", meta = (ClampMin = "0.0"))
	float HeartbeatVFXBlendOutSpeed = 2.0f;

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

	/** 최대 스태미나 대비 소모율로 계산한 실제 시야 반각 감소 속도에 곱하는 배율입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Helper|Vision|Stamina", meta = (ClampMin = "0.0"))
	float StaminaVisionDrainMultiplier = 1.0f;

	/** 최대 스태미나 대비 회복률로 계산한 실제 시야 반각 복구 속도에 곱하는 배율입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Helper|Vision|Stamina", meta = (ClampMin = "0.0"))
	float StaminaVisionRecoveryMultiplier = 1.0f;

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
