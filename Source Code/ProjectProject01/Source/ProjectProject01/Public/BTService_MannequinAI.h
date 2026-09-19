// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/Services/BTService_BlackboardBase.h"
#include "BTService_MannequinAI.generated.h"

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
	
	// 플레이어가 정지해도 마지막 이동 방향을 유지하도록 설정함.
	FVector LastPlayerMovementDirection = FVector::ZeroVector;
};
