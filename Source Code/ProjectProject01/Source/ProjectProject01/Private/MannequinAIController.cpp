// Fill out your copyright notice in the Description page of Project Settings.


#include "MannequinAIController.h"

#include "BehaviorTree/BlackboardComponent.h"
#include "ProjectProject01DiagnosticsSubsystem.h"

void AMannequinAIController::BeginPlay()
{
    Super::BeginPlay();
}

void AMannequinAIController::OnPossess(APawn* InPawn)
{
    Super::OnPossess(InPawn);

    UProjectProject01DiagnosticsSubsystem::RecordWorldEvent(GetWorld(), EProjectProject01DiagnosticSeverity::Normal,
        TEXT("MannequinAIControllerPossess"), FString::Printf(TEXT("Controller=%s Pawn=%s"), *GetName(), *GetNameSafe(InPawn)), InPawn);

    if (BehaviorTree != nullptr)
    {
        ensureMsgf(RunBehaviorTree(BehaviorTree),
            TEXT("Mannequin AI controller could not start its Behavior Tree after possessing %s."),
            *GetNameSafe(InPawn));
    }
}
