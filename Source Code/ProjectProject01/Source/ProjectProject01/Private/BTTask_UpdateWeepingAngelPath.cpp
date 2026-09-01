// Fill out your copyright notice in the Description page of Project Settings.


#include "BTTask_UpdateWeepingAngelPath.h"

#include "BehaviorTree/BlackboardComponent.h"
#include "AIController.h"

#include "WeepingAngelCharacter.h"
#include "WeepingAngelPath.h"

UBTTask_UpdateWeepingAngelPath::UBTTask_UpdateWeepingAngelPath()
{
	NodeName = TEXT("Update Current Weeping Angel Path");
}

EBTNodeResult::Type UBTTask_UpdateWeepingAngelPath::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	Super::ExecuteTask(OwnerComp, NodeMemory);

    // Blackboard 가져오기
    UBlackboardComponent* Blackboard = OwnerComp.GetBlackboardComponent();
    if (Blackboard == nullptr)
    {
        return EBTNodeResult::Failed;
    }

    // AI Controller 가져오기
    AAIController* AIController = OwnerComp.GetAIOwner();
    if (AIController == nullptr)
    {
        return EBTNodeResult::Failed;
    }

    // AI Controller가 조종하는 천사 가져오기
    AWeepingAngelCharacter* Angel = Cast<AWeepingAngelCharacter>(AIController->GetPawn());
    if (Angel == nullptr)
    {
        return EBTNodeResult::Failed;
    }

    // Blackboard에서 다음 Path 가져오기
    AWeepingAngelPath* NextPath = Cast<AWeepingAngelPath>(Blackboard->GetValueAsObject(TEXT("NextPath")));
    if (NextPath == nullptr)
    {
        return EBTNodeResult::Failed;
    }

    AWeepingAngelPath* PreviousPath = Angel->GetCurrentPath();

    // Character의 CurrentPath 갱신
    Angel->SetCurrentPath(NextPath);

    // Blackboard의 CurrentPath도 함께 갱신
    Blackboard->SetValueAsObject(TEXT("CurrentPath"), NextPath);

    UE_LOG(
        LogTemp,
        Warning,
        TEXT("CurrentPath Updated: %s -> %s"),
        PreviousPath
            ? *PreviousPath->GetName()
            : TEXT("None"),
        *NextPath->GetName()
    );

    // 도착한 NextPath는 더 이상 다음 목적지가 아님
    Blackboard->ClearValue(TEXT("NextPath"));
    Blackboard->ClearValue(TEXT("NextPathLocation"));

    return EBTNodeResult::Succeeded;
}