// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PlayerHeartbeatComponent.generated.h"

class AMannequinAICharacter;
class APawn;
struct FPlayerTuningRow;

// 각 마네킹의 인지, 노출, 재발견 상태를 보관한다.
struct FHeartbeatMannequinState
{
	// 플레이어가 이 마네킹을 한 번이라도 직접 확인했는지 나타낸다.
	bool bHasEverBeenRecognized = false;
	// 현재 플레이어 시야 안에서 장애물 없이 보이는지 나타낸다.
	bool bCurrentlyVisible = false;
	// 다시 발견했을 때 BPM 상승을 발생시킬 수 있는 상태다.
	bool bSurpriseArmed = true;
	// 현재 거리 기반 심박 계산에 반영되는지 나타낸다.
	bool bActiveForHeartbeat = false;
	// 마지막으로 시야에서 사라진 시간이다.
	double HiddenStartTime = 0.0;
	// 마지막으로 유지 범위 조건을 만족한 시간이다.
	double LastRetentionTime = 0.0;
	// 마지막으로 갑작스러운 BPM 상승이 발생한 시간이다.
	double LastSurpriseTime = -1.0e30;
};

// 로컬 플레이어가 직접 인지한 마네킹을 기준으로 심박 BPM을 계산한다.
UCLASS(ClassGroup=(Player), meta=(BlueprintSpawnableComponent))
class PROJECTPROJECT01_API UPlayerHeartbeatComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPlayerHeartbeatComponent();

	// 현재 로그 박동에 사용하는 최종 BPM이다. 비활성 상태에서는 0이다.
	UFUNCTION(BlueprintPure, Category="Heartbeat|Runtime")
	float GetCurrentBPM() const { return CurrentBPM; }

	// 이번 플레이에서 한 번이라도 직접 본 마네킹 수다.
	UFUNCTION(BlueprintPure, Category="Heartbeat|Runtime")
	int32 GetKnownMannequinCount() const;

	// 현재 심박 계산에 반영되는 인지 완료 마네킹 수다.
	UFUNCTION(BlueprintPure, Category="Heartbeat|Runtime")
	int32 GetActiveMannequinCount() const;

	// 현재 심박 로그가 재생 중인지 반환한다.
	UFUNCTION(BlueprintPure, Category="Heartbeat|Runtime")
	bool IsHeartbeatActive() const { return bHeartbeatActive; }

	// 거리 BPM과 발견 부채꼴에 사용하는 최대 거리다.
	UFUNCTION(BlueprintPure, Category="Heartbeat|Detection")
	float GetHeartbeatRange() const { return FMath::Max(HeartbeatRange, 0.0f); }

	// 마네킹 발견에 사용하는 수평 부채꼴의 반각이다.
	UFUNCTION(BlueprintPure, Category="Heartbeat|Detection")
	float GetRecognitionHalfAngleDegrees() const
	{
		return FMath::Clamp(RecognitionHalfAngleDegrees, 0.0f, 180.0f);
	}

	// 최초 인지 기록과 현재 심박 상태를 모두 초기화한다.
	UFUNCTION(BlueprintCallable, Category="Heartbeat|Runtime")
	void ResetHeartbeatState();

	/** 서버가 검증한 DataTable의 심박 설정을 로컬 생존자에게 적용합니다. */
	void ApplyHeartbeatTuning(const FPlayerTuningRow& Tuning);

protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

private:
	// 월드에 존재하는 마네킹 목록을 주기적으로 갱신한다.
	void RefreshMannequinCache();
	// 실제 시야 진입과 인지 대상 유지 여부를 갱신한다.
	void UpdateDetectionStates(APawn& OwnerPawn);
	// 활성 대상의 거리 하한과 최초 조우 감소를 합쳐 최종 BPM을 계산한다.
	void UpdateBPM(float DeltaTime, const APawn& OwnerPawn);
	// CurrentBPM을 실제 박동 간격으로 환산해 THUMP 로그를 출력한다.
	void UpdateBeatLog(float DeltaTime);
	// 파괴된 마네킹의 약한 참조를 내부 상태에서 제거한다.
	void RemoveInvalidMannequins();
	// 소유 Pawn의 실제 플레이 시점과 정면 방향을 가져온다.
	bool GetPlayerViewPoint(const APawn& OwnerPawn, FVector& OutLocation, FVector& OutForward) const;
	// 최초 인지용 좁은 시야각과 장애물 검사를 수행한다.
	bool IsMannequinActuallyVisible(const APawn& OwnerPawn, const AMannequinAICharacter& Mannequin,
		const FVector& ViewLocation, const FVector& ViewForward) const;
	// 이미 인지한 대상을 유지할 넓은 시야각 안인지 확인한다.
	bool IsInsideRetentionCone(const AMannequinAICharacter& Mannequin,
		const FVector& ViewLocation, const FVector& ViewForward) const;
	// 카메라와 마네킹 사이에 시야를 막는 물체가 없는지 확인한다.
	bool HasClearLineOfSight(const APawn& OwnerPawn, const AMannequinAICharacter& Mannequin,
		const FVector& ViewLocation) const;
	// 0은 심박 거리 바깥, 1은 플레이어와 거의 같은 위치를 뜻한다.
	float CalculateDistanceAlpha(float Distance) const;
	// 현재 거리를 MinBPM~MaxDistanceBPM 범위로 변환한다.
	float CalculateDistanceBPM(float Distance) const;
	// 최초 발견 또는 재발견에 따른 일시적인 BPM 상승을 발생시킨다.
	void TriggerSurpriseBPM(AMannequinAICharacter& Mannequin, float Distance, bool bFirstEncounter);

	// 거리 기반 심박의 최소·최대값과 최초 조우 상한이다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Heartbeat|BPM",
		meta=(AllowPrivateAccess="true", ClampMin="1.0", UIMin="1.0"))
	float MinBPM = 60.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Heartbeat|BPM",
		meta=(AllowPrivateAccess="true", ClampMin="1.0", UIMin="1.0"))
	float MaxDistanceBPM = 120.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Heartbeat|BPM",
		meta=(AllowPrivateAccess="true", ClampMin="1.0", UIMin="1.0"))
	float MaxEncounterBPM = 165.0f;

	// 최초 조우 시 거리에 따라 더할 BPM 범위다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Heartbeat|Encounter",
		meta=(AllowPrivateAccess="true", ClampMin="0.0", UIMin="0.0"))
	float MinEncounterBoost = 15.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Heartbeat|Encounter",
		meta=(AllowPrivateAccess="true", ClampMin="0.0", UIMin="0.0"))
	float MaxEncounterBoost = 45.0f;

	// 최초 조우 BPM이 거리 기반 BPM까지 내려오는 초당 감소량이다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Heartbeat|Encounter",
		meta=(AllowPrivateAccess="true", ClampMin="0.0", UIMin="0.0"))
	float BPMDecayPerSecond = 12.0f;

	// 거리 기반 BPM 계산에 사용하는 최대 거리다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Heartbeat|Detection",
		meta=(AllowPrivateAccess="true", ClampMin="0.0", UIMin="0.0", Units="cm"))
	float HeartbeatRange = 1500.0f;

	// 한 번 인지한 마네킹을 심박 대상으로 유지할 최대 거리다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Heartbeat|Detection",
		meta=(AllowPrivateAccess="true", ClampMin="0.0", UIMin="0.0", Units="cm"))
	float RetentionRange = 1800.0f;

	// 마네킹을 발견할 수평 부채꼴의 한쪽 각도다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Heartbeat|Detection",
		meta=(AllowPrivateAccess="true", ClampMin="0.0", ClampMax="180.0", UIMin="0.0", UIMax="180.0", Units="deg"))
	float RecognitionHalfAngleDegrees = 45.0f;

	// false면 현재 발견 부채꼴과 거리만으로 심박 활성 여부를 결정한다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Heartbeat|Detection",
		meta=(AllowPrivateAccess="true"))
	bool bUseRetentionRules = false;

	// 인지 완료 마네킹을 유지할 더 넓은 시야의 한쪽 각도다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Heartbeat|Detection",
		meta=(AllowPrivateAccess="true", ClampMin="0.0", ClampMax="180.0", UIMin="0.0", UIMax="180.0", Units="deg"))
	float RetentionHalfAngleDegrees = 80.0f;

	// 유지 조건을 잃은 뒤에도 심박 반영을 계속하는 유예시간이다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Heartbeat|Detection",
		meta=(AllowPrivateAccess="true", ClampMin="0.0", UIMin="0.0", Units="s"))
	float LostSightGraceSeconds = 1.5f;
	
	// 이 시간 이상 연속으로 보이지 않아야 재발견 BPM이 준비된다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Heartbeat|Encounter",
		meta=(AllowPrivateAccess="true", ClampMin="0.0", UIMin="0.0", Units="s"))
	float SurpriseRearmDelay = 0.75f;
	
	// 마지막 BPM 상승 이후 재발견 상승을 막는 최소 시간이다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Heartbeat|Encounter",
		meta=(AllowPrivateAccess="true", ClampMin="0.0", UIMin="0.0", Units="s"))
	float SurpriseCooldown = 3.0f;

	// false면 최초 발견 상승만 허용하고 재발견 상승은 사용하지 않는다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Heartbeat|Encounter",
		meta=(AllowPrivateAccess="true"))
	bool bEnableRediscovery = false;

	// 시야 판정 주기다. 낮을수록 즉각적이지만 검사량이 늘어난다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Heartbeat|Detection",
		meta=(AllowPrivateAccess="true", ClampMin="0.01", UIMin="0.01", Units="s"))
	float VisionCheckInterval = 0.05f;

	// 런타임에 생성·삭제되는 마네킹 목록을 다시 찾는 주기다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Heartbeat|Detection",
		meta=(AllowPrivateAccess="true", ClampMin="0.1", UIMin="0.1", Units="s"))
	float MannequinRefreshInterval = 1.0f;

	// false면 BPM 계산은 유지하면서 관련 로그만 끈다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Heartbeat|Debug", meta=(AllowPrivateAccess="true"))
	bool bEnableHeartbeatLog = true;

	// 마지막 출력값과 BPM 차이가 이 값 이상일 때 상태 로그를 갱신한다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Heartbeat|Debug",
		meta=(AllowPrivateAccess="true", ClampMin="0.0", UIMin="0.0"))
	float BPMLogThreshold = 1.0f;

	// 아래 값들은 실행 중 확인만 가능한 현재 계산 결과다.
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Heartbeat|Runtime",
		meta=(AllowPrivateAccess="true"))
	float CurrentBPM = 0.0f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Heartbeat|Runtime",
		meta=(AllowPrivateAccess="true"))
	float DistanceBPM = 0.0f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Heartbeat|Runtime",
		meta=(AllowPrivateAccess="true"))
	float EncounterBPM = 0.0f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Heartbeat|Runtime",
		meta=(AllowPrivateAccess="true"))
	bool bHeartbeatActive = false;

	// 전체 마네킹, 최초 인지 대상, 현재 활성 대상을 구분해 보관한다.
	TArray<TWeakObjectPtr<AMannequinAICharacter>> CachedMannequins;
	// 마네킹별 인지, 노출, 재발견 상태를 하나의 Map에서 관리한다.
	TMap<TWeakObjectPtr<AMannequinAICharacter>, FHeartbeatMannequinState> MannequinStates;

	// 매 프레임 계산 대신 설정된 주기로 처리하기 위한 시간 누적값이다.
	float BeatAccumulator = 0.0f;
	float VisionCheckAccumulator = 0.0f;
	float RefreshAccumulator = 0.0f;
	float LastLoggedBPM = 0.0f;
};
