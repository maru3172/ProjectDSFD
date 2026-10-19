// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "MannequinAIController.generated.h"

/**
 * 
 */
UCLASS()
class PROJECTPROJECT01_API AMannequinAIController : public AAIController
{
	GENERATED_BODY()
	friend struct FProjectCheckpointAI;
	friend class FProjectReviewSingleAIStateTest;

public:
	virtual void BeginPlay() override;
	virtual void OnPossess(APawn* InPawn) override;
	virtual void Tick(float DeltaSeconds) override;

private:
	int32 RestoredWaitNodeIndex = INDEX_NONE;
	float RestoredWaitSeconds = 0;
	double RestoredWaitDeadline = 0;
	UPROPERTY(EditAnywhere, Category = "Behavior Tree")
	class UBehaviorTree* BehaviorTree;
};
