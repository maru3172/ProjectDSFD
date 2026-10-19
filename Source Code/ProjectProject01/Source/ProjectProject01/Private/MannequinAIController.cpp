// Fill out your copyright notice in the Description page of Project Settings.


#include "MannequinAIController.h"

#include "BehaviorTree/BlackboardComponent.h"
#include "MultiplayTestGameMode.h"
#include "ProjectProject01DiagnosticsSubsystem.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/Tasks/BTTask_Wait.h"

void AMannequinAIController::BeginPlay()
{
    Super::BeginPlay();
}

void AMannequinAIController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (RestoredWaitNodeIndex == INDEX_NONE) return;
	auto* Tree = Cast<UBehaviorTreeComponent>(BrainComponent);
	const auto* Wait = Tree ? Cast<UBTTask_Wait>(Tree->GetActiveNode()) : nullptr;
	if (Wait && Wait->GetExecutionIndex() == RestoredWaitNodeIndex)
	{
		if (uint8* Memory = Tree->GetNodeMemory(Wait, Tree->GetActiveInstanceIdx()))
			Wait->GetSpecialNodeMemory<FBTTaskMemory>(Memory)->NextTickRemainingTime = RestoredWaitSeconds;
		RestoredWaitNodeIndex = INDEX_NONE;
	}
	else if (!GetWorld() || GetWorld()->GetTimeSeconds() > RestoredWaitDeadline) RestoredWaitNodeIndex = INDEX_NONE;
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
