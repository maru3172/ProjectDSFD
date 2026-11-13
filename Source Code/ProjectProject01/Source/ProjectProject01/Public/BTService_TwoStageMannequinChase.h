// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTService.h"
#include "BTService_TwoStageMannequinChase.generated.h"

class AMannequinAICharacter;
class APlayerCharacter;
class UBlackboardComponent;
struct FMannequinPlayerObservation;

UENUM()
enum class ETwoStageMannequinTier : uint8
{
	Roaming = 0,
	Primary = 1,
	Secondary = 2
};

UENUM()
enum class EMannequinTrackingEvidence : uint8
{
	None = 0,
	DirectObservation = 1,
	StoppedMannequinClue = 2,
	VelocityPrediction = 3
};

/**
 * 
 */
UCLASS()
class PROJECTPROJECT01_API UBTService_TwoStageMannequinChase : public UBTService
{
	GENERATED_BODY()
	
public:
	UBTService_TwoStageMannequinChase();
	
protected:
	virtual void TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
	
private:
	// 플레이어 또는 조력자가 현재 마네킹을 바라보고 있는지 검사한다.
	bool IsObservedByPlayerOrHelper(
		const APlayerCharacter& Player,
		AMannequinAICharacter& CurrentMannequin,
		const TArray<AActor*>& AllMannequins,
		bool& bOutObservedByPlayer
		) const;
	
	// 현재 마네킹이 플레이어를 직접 볼 수 있는지 검사한다.
	bool CanMannequinSeePlayer(
		const AMannequinAICharacter& CurrentMannequin,
		const APlayerCharacter& Player,
		const TArray<AActor*>& AllMannequins
		) const;
	
	// 현재 마네킹이 직접 관측자, 2 차 추적자 또는 배회 상태인지 판단한다.
	ETwoStageMannequinTier DetermineChaseTier(
		const AMannequinAICharacter& CurrentMannequin,
		const TArray<AActor*>& AllMannequins
		) const;
	
	// 현재 마네킹 주변에서 사용할 수 있는 가장 최신 1 차 마네킹의 관측 정보를 찾는다.
	bool TryFindBestPrimaryObservation(
		const AMannequinAICharacter& CurrentMannequin,
		const TArray<AActor*>& AllMannequins,
		bool bRequireDirectObservation,
		FMannequinPlayerObservation& OutObservation
		) const;
	
	// 전달받은 관측 정보를 이용해 플레이어 예상 위치를 계산한다.
	bool TryCalculateTrackingLocation(
		const FMannequinPlayerObservation& Observation,
		FVector& OutTrackingLocation
		) const;
	
	// 배회 목적지 생성
	bool TryFindRoamingLocation(
		const AMannequinAICharacter& CurrentMannequin,
		FVector& OutLocation
		) const;
	
	// 추적 상태 적용 함수
	void ApplyChaseState(
		UBlackboardComponent& Blackboard,
		const FVector& TrackingLocation,
		ETwoStageMannequinTier Tier,
		EMannequinTrackingEvidence TrackingEvidence
		);
	
	// 배회 상태 적용 함수
	void ApplyRoamingState(
		UBlackboardComponent& Blackboard,
		AMannequinAICharacter& CurrentMannequin
		);
	
	// 전달받은 위치를 NavMesh 위의 이동 가능한 위치로 보정한다.
	bool TryProjectTrackingLocation(
		const FVector& CandidateLocation,
		FVector& OutTrackingLocation
		) const;
	
	// 후보 위치와 정지 마네킹 사이가 벽으로 차단되지 않았는지 검사한다.
	bool CanCandidateSeeStoppedMannequin(
		const FVector& CandidateLocation,
		const AMannequinAICharacter& StoppedMannequin,
		const TArray<AActor*>& AllMannequins
		) const;
	
	// 플레이어 시선으로 멈춘 마네킹들의 단서를 이용해 위치 후보를 계산한다.
	bool TryCalculateIndirectTrackingLocation(
		const AMannequinAICharacter& CurrentMannequin,
		const TArray<AActor*>& AllMannequins,
		const FMannequinPlayerObservation& ReferenceObservation,
		FVector& OutTrackingLocation
		) const;
	
	// 계산 주기가 되지 않았다면 이전 간접 추론 결과를 재사용한다.
	bool TryGetCachedIndirectTrackingLocation(
		const AMannequinAICharacter& CurrentMannequin,
		const TArray<AActor*>& AllMannequins,
		const FMannequinPlayerObservation& ReferenceObservation,
		FVector& OutTrackingLocation
		);
	
	// 저장된 간섭 추론 결과를 초기화한다.
	void ResetIndirectTrackingCache();
	
	// 목표 위치가 지나치게 자주 바뀌지 않도록 안정화한다.
	FVector StabilizeTrackingLocation(
		const FVector& NewTrackingLocation,
		EMannequinTrackingEvidence TrackingEvidence
		);
	
	// 저장된 추적 목표 안정화 상태를 초기화한다.
	void ResetTrackingLocationStabilization();
	
	// 예측 위치가 실패하면 마지막 관측 위치를 대체 목표로 사용한다.
	bool TryResolveObservationTrackingLocation(
		const FMannequinPlayerObservation& Observation,
		FVector& OutTrackingLocation
		) const;
	
	// 더 이상 플레이어 반경이 아니라 직접 관측 정보로 결정되므로 사용되지 않는다.
	// // 플레이어를 중심으로 1차 마네킹을 지정하는 반경
	// UPROPERTY(EditAnywhere, Category = "Two Stage Chase", meta = (ClampMin = "0.0", Units = "cm"))
	// float PlayerPrimaryRadius = 1000.0f;
	
	// 각각의 1차 마네킹을 중심으로 2차 마네킹을 지정하는 반경
	UPROPERTY(EditAnywhere, Category = "Two Stage Chase", meta = (ClampMin = "0.0", Units = "cm"))
	float PrimaryPropagationRadius = 600.0f;
	
	// 배회 마네킹의 랜덤 목적지 검색 반경
	UPROPERTY(EditAnywhere, Category = "Two Stage Chase", meta = (ClampMin = "0.0", Units = "cm"))
	float RoamingSearchRadius = 800.0f;
	
	// 다른 층의 마네킹이 전파원이 되는 것을 막는 높이 제한
	UPROPERTY(EditAnywhere, Category = "Two Stage Chase", meta = (ClampMin = "0.0", Units = "cm"))
	float MaximumHeightDifference = 300.0f;
	
	// 마네킹이 플레이어를 직접 발견할 수 있는 최대 수평 거리
	UPROPERTY(EditAnywhere, Category = "Two Stage Chase", meta = (ClampMin = "0.0", Units = "cm"))
	float MannequinSightRadius = 1500.0f;
	
	// 마네킹 정면을 기준으로 한 좌우 시야각의 절반
	UPROPERTY(EditAnywhere, Category = "Two Stage Chase", meta = (ClampMin = "0.0", ClampMax = "180.0", Units = "deg"))
	float MannequinSightHalfAngle = 60.0f;
	
	// 층이 다른 플레이어를 직접 관측하지 않도록 하는 최대 높이 차이
	UPROPERTY(EditAnywhere, Category = "Two Stage Chase", meta = (ClampMin = "0.0", Units = "cm"))
	float MannequinSightMaximumHeightDifference = 300.0f;
	
	// 마지막 직접 관측 이후 정보를 유지할 시간
	UPROPERTY(EditAnywhere, Category = "Two Stage Chase", meta = (ClampMin = "0.0", Units = "s"))
	float ObservationLifetime = 8.0f;
	
	// 플레이어 위치를 앞쪽으로 예측할 최대 시간
	UPROPERTY(EditAnywhere, Category = "Two Stage Chase", meta = (ClampMin = "0.0", Units = "s"))
	float MaximumPredictionSeconds = 3.0f;
	
	// 예상 위치를 NavMesh 위로 보정할 때 사용하는 검색 범위
	UPROPERTY(EditAnywhere, Category = "Two Stage Chase", meta = (ClampMin = "0.0", Units = "cm"))
	FVector PredictionNavigationExtent = FVector(200.0f, 200.0f, 300.0f);
	
	// 정지 마네킹이 플레이어 위치 단서로 작용할 수 있는 최대 거리
	UPROPERTY(EditAnywhere, Category = "Two Stage Chase|Indirect Clue", meta = (ClampMin = "0.0", Units = "cm"))
	float StoppedMannequinClueRadius = 2000.0f;
	
	// 간접 위치 후보를 생성하는 간격
	UPROPERTY(EditAnywhere, Category = "Two Stage Chase|Indirect Clue", meta = (ClampMin = "1.0", Units = "cm"))
	float IndirectCandidateSpacing = 250.0f;
	
	// 한 원 둘레에 생성할 간접 위치 후보 개수
	UPROPERTY(EditAnywhere, Category = "Two Stage Chase|Indirect Clue", meta = (ClampMin = "4"))
	int32 IndirectCandidateDirectionCount = 12;
	
	// 플레이어가 마지막 관측 이후 이동할 수 있다고 보는 최대 속도
	UPROPERTY(EditAnywhere, Category = "Two Stage Chase|Indirect Clue", meta = (ClampMin = "0.0", Units = "cm/s"))
	float EstimatedPlayerMaximumSpeed = 600.0f;
	
	// 간접 추론 검색 범위의 최대 크기
	UPROPERTY(EditAnywhere, Category = "Two Stage Chase|Indirect Clue", meta = (ClampMin = "0.0", Units = "cm"))
	float MaximumIndirectSearchRadius = 1800.0f;
	
	// 간접 위치 추론을 다시 계산하는 시간 간격
	UPROPERTY(EditAnywhere, Category = "Two Stage Chase|Optimization", meta = (ClampMin = "0.0", Units = "s"))
	float IndirectInferenceInterval = 0.25f;
	
	// 마지막으로 간접 위치를 계산한 월드 시각
	double LastIndirectInferenceTime = -1.0;
	
	// 마지막으로 성공한 간접 위치
	FVector CachedIndirectTrackingLocation = FVector::ZeroVector;
	
	// 사용할 수 있는 간접 위치 캐시가 있는지
	bool bHasCachedIndirectTrackingLocation = false;
	
	// 이 거리보다 작은 목표 변화는 기존 위치를 유지한다.
	UPROPERTY(EditAnywhere, Category = "Two Stage Chase|Stabilization", meta = (ClampMin = "0.0", Units = "cm"))
	float MinimumTrackingLocationChange = 100.0f;
	
	// 이 거리 이상 목표가 바뀌면 유지 시간과 관계없이 즉시 교체한다.
	UPROPERTY(EditAnywhere, Category = "Two Stage Chase|Stabilization", meta = (ClampMin = "0.0", Units = "cm"))
	float ImmediateTrackingLocationChange = 400.0f;
	
	// 작은 목표 변화가 있을 때 기존 목표를 유지하는 최대 시간
	UPROPERTY(EditAnywhere, Category = "Two Stage Chase|Stabilization", meta = (ClampMin = "0.0", Units = "s"))
	float MinimumTrackingLocationHoldTime = 0.25f;
	
	// 목표 위치가 자주 바뀌지 않도록 현재 유지하고 있는 추적 위치
	FVector StabilizedTrackingLocation = FVector::ZeroVector;
	
	// 현재 추적 위치를 마지막으로 교체한 월드 시각
	double LastTrackingLocationChangeTime = -1.0;
	
	// 현재 사용할 수 있는 안정화된 추적 위치가 저장되어 있는지
	bool bHasStabilizedTrackingLocation = false;
	
	// 현재 안정화된 추적 위치를 계산할 때 사용한 정보 종류
	EMannequinTrackingEvidence StabilizedTrackingEvidence = EMannequinTrackingEvidence::None;
};
