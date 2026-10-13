// Fill out your copyright notice in the Description page of Project Settings.


#include "MannequinAIController.h"

#include "BehaviorTree/BlackboardComponent.h"
#include "MultiplayTestGameMode.h"
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

	// MultiplayTest는 싱글용 Behavior Tree와 추적 규칙을 사용하지 않는다.
	// 해당 게임모드가 서버 권위의 별도 상태 머신과 A* MoveTo 요청을 관리한다.
	const bool bUseDedicatedMultiplayerAI = IsValid(GetWorld()) &&
		IsValid(GetWorld()->GetAuthGameMode<AMultiplayTestGameMode>());
	if (bUseDedicatedMultiplayerAI)
	{
		StopMovement();
		return;
	}

	if (BehaviorTree != nullptr)
    {
        ensureMsgf(RunBehaviorTree(BehaviorTree),
            TEXT("Mannequin AI controller could not start its Behavior Tree after possessing %s."),
            *GetNameSafe(InPawn));
    }
}
