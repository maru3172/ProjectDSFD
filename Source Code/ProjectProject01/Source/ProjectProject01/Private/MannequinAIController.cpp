// Fill out your copyright notice in the Description page of Project Settings.


#include "MannequinAIController.h"

#include "BehaviorTree/BlackboardComponent.h"

void AMannequinAIController::BeginPlay()
{
    Super::BeginPlay();
}

void AMannequinAIController::OnPossess(APawn* InPawn)
{
    Super::OnPossess(InPawn);

    if (BehaviorTree != nullptr)
    {
        ensureMsgf(RunBehaviorTree(BehaviorTree),
            TEXT("Mannequin AI controller could not start its Behavior Tree after possessing %s."),
            *GetNameSafe(InPawn));
    }
}
