// Fill out your copyright notice in the Description page of Project Settings.


#include "BTTask_CompleteRoamingTask.h"

#include "BehaviorTree/BlackboardComponent.h"

UBTTask_CompleteRoamingTask::UBTTask_CompleteRoamingTask()
{
	NodeName = TEXT("Complete Roaming Task");
}

EBTNodeResult::Type UBTTask_CompleteRoamingTask::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	UBlackboardComponent* Blackboard = OwnerComp.GetBlackboardComponent();
	if (!IsValid(Blackboard))
	{
		return EBTNodeResult::Failed;
	}

	// Move To와 선택적인 Wait가 모두 끝났음을 서비스에 알린다.
	Blackboard->SetValueAsBool(TEXT("RoamingSequenceCompleted"), true);

	return EBTNodeResult::Succeeded;
}
