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
		const TArray<AActor*>& AllMannequins
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
		ETwoStageMannequinTier Tier
		);
	
	// 배회 상태 적용 함수
	void ApplyRoamingState(
		UBlackboardComponent& Blackboard,
		AMannequinAICharacter& CurrentMannequin
		);
	
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
};
