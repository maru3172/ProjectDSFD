// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/Tasks/BTTask_BlackboardBase.h"
#include "BTTask_SelectWeepingAngelPath.generated.h"

/**
 * 
 */
UCLASS()
class PROJECTPROJECT01_API UBTTask_SelectWeepingAngelPath : public UBTTask_BlackboardBase
{
	GENERATED_BODY()

public:
	UBTTask_SelectWeepingAngelPath();
	
protected:
	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
};