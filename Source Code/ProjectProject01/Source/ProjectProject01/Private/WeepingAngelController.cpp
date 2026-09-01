// Fill out your copyright notice in the Description page of Project Settings.


#include "WeepingAngelController.h"

#include "Kismet/GameplayStatics.h"
#include "BehaviorTree/BlackboardComponent.h"

void AWeepingAngelController::BeginPlay()
{
	Super::BeginPlay();

    if (AIBehavior)
    {
        RunBehaviorTree(AIBehavior);
    }
}