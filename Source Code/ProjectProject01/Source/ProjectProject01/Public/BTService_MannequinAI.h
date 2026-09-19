// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/Services/BTService_BlackboardBase.h"
#include "BTService_MannequinAI.generated.h"


enum class EMannequinRangeRegion : uint8
{
	Unknown,
	OutsideOuterRange,
	BetweenRangesChaseSector,
	BetweenRangesRoamingSector,
	InsideInnerRange
};

/**
 * 
 */
UCLASS()
class PROJECTPROJECT01_API UBTService_MannequinAI : public UBTService_BlackboardBase
{
	GENERATED_BODY()
	
public:
	UBTService_MannequinAI();

protected:
	virtual void TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;

private:
	bool TryFindRoamingDestination(
		const FVector& MannequinLocation,
		const FVector& PlayerLocation,
		float InnerRadius,
		float OuterRadius,
		class APawn& MannequinPawn,
		FVector& OutDestination) const;

	void ClearRoamingState(class UBlackboardComponent& Blackboard);

	UPROPERTY(EditAnywhere, Category = "AI|Roaming", meta = (ClampMin = "0.0", UIMin = "0.0", Units = "cm"))
	float MannequinGatherRadius = 500.0f;

	UPROPERTY(EditAnywhere, Category = "AI|Roaming", meta = (ClampMin = "1", UIMin = "1"))
	int32 RequiredMannequinCount = 3;

	// 플레이어의 화면에 Mannequin 이 확인되었는지 저장한다.
	bool PlayerSeeMannequin = false;

	// Mannequin 이 플레이어를 발견했는지 저장한다.
	bool MannequinSeePlayer = false;

	FVector RoamingDestination = FVector::ZeroVector;
	bool bHasRoamingDestination = false;
	bool bRoamingCommitted = false;
	bool bLoggedRoamingQueryFailure = false;
	double NextRoamingQueryTime = 0.0;
	
	// 후방 배회 영역에서 시작된 랜덤 이동인지 구분한다.
	// true :	후방 영역에 있어서 랜덤 이동을 시작함
	// false :	전방에서 마네킹 집결로 분열 이동을 시작함
	bool bRoamingStartedFromRoamingSector = false;
	
	// 마네킹의 마지막 상태를 저장하기
	EMannequinRangeRegion LastLoggedRangeRegion = EMannequinRangeRegion::Unknown;
};
