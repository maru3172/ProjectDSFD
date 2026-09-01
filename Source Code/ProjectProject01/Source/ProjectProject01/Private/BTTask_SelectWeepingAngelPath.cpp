// Fill out your copyright notice in the Description page of Project Settings.


#include "BTTask_SelectWeepingAngelPath.h"

#include "BehaviorTree/BlackboardComponent.h"
#include "AIController.h"

#include "WeepingAngelCharacter.h"
#include "WeepingAngelPath.h"

UBTTask_SelectWeepingAngelPath::UBTTask_SelectWeepingAngelPath()
{
	NodeName = TEXT("Select Weeping Angel Path");
}

EBTNodeResult::Type UBTTask_SelectWeepingAngelPath::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	Super::ExecuteTask(OwnerComp, NodeMemory);

    // 현재 Behavior Tree 의 Blackboard 를 가져온다.
    UBlackboardComponent* Blackboard = OwnerComp.GetBlackboardComponent(); 
    if (Blackboard == nullptr)
    {
        return EBTNodeResult::Failed;
    }

    // 현재 AI Controller 를 가져온다.
    AAIController* AIController = OwnerComp.GetAIOwner();
    if (AIController == nullptr)
    {
        return EBTNodeResult::Failed;
    }

    // AI Controller 가 조종하는 천사를 가져온다.
    AWeepingAngelCharacter* Angel = Cast<AWeepingAngelCharacter>(AIController->GetPawn());
    if (Angel == nullptr)
    {
        return EBTNodeResult::Failed;
    }

    // 천사의 현재 Path 를 가져온다.
    AWeepingAngelPath* CurrentPath = Angel->GetCurrentPath();
    if (CurrentPath == nullptr)
    {
        return EBTNodeResult::Failed;
    }

    // 현재 Path 와 연결된 Path 들을 가져온다.
    const TArray<TObjectPtr<AWeepingAngelPath>>& ConnectedPaths = CurrentPath->GetConnectedPaths();
    if (ConnectedPaths.Num() == 0)
    {
        return EBTNodeResult::Failed;
    }

    // 가장 낮은 Weight 를 가진 Path 를 저장한다.
    AWeepingAngelPath* BestPath = nullptr;
    float BestWeight = TNumericLimits<float>::Max();

    // 연결된 Path 들의 Weight 를 비교한다.
    for (AWeepingAngelPath* Path : ConnectedPaths)
    {
        if (Path == nullptr)
        {
            continue;
        }

        float PathWeight = Path->GetWeight();

        if (PathWeight < BestWeight)
        {
            BestWeight = PathWeight;
            BestPath = Path;
        }
    }

    // 선택할 Path 가 없으면 실패한다.
    if (BestPath == nullptr)
    {
        return EBTNodeResult::Failed;
    }

    // 다음에 이동할 Path 를 Blackboard 에 저장한다.
    Blackboard->SetValueAsObject(TEXT("NextPath"), BestPath);
    // 다음 Path 의 실제 천사 이동 위치를 저장한다.
    Blackboard->SetValueAsVector(TEXT("NextPathLocation"), BestPath->GetAngelPathLocation());

    return EBTNodeResult::Succeeded;
}