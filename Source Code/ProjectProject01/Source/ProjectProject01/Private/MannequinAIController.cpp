// Fill out your copyright notice in the Description page of Project Settings.


#include "MannequinAIController.h"

#include "BehaviorTree/BlackboardComponent.h"

void AMannequinAIController::BeginPlay()
{
    Super::BeginPlay();

    if (BehaviorTree != nullptr)
    {
        RunBehaviorTree(BehaviorTree);
    }
}