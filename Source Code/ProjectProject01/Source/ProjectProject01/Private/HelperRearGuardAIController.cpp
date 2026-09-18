// File: Source/ProjectProject01/Private/HelperRearGuardAIController.cpp
// Target: ProjectProject01Editor Win64 DebugGame, Unreal Engine 5.8

#include "HelperRearGuardAIController.h"

#include "HelperRearGuardCharacter.h"
#include "ProjectProject01TuningData.h"

AHelperRearGuardAIController::AHelperRearGuardAIController()
{
	PrimaryActorTick.bCanEverTick = true;
}

void AHelperRearGuardAIController::ApplyHelperNavigationTuning(const FHelperTuningRow& Tuning)
{
	RepathInterval = FMath::Max(0.0f, Tuning.RepathInterval);
	RepathDistance = FMath::Max(0.0f, Tuning.RepathDistance);
	AcceptanceRadius = FMath::Max(0.0f, Tuning.AcceptanceRadius);
	StuckTimeout = FMath::Max(0.0f, Tuning.StuckTimeout);
	StuckWaitTime = FMath::Max(0.0f, Tuning.StuckWaitTime);
	ProgressDistance = FMath::Max(0.0f, Tuning.ProgressDistance);
}

void AHelperRearGuardAIController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	ControlledHelper = Cast<AHelperRearGuardCharacter>(InPawn);
	TimeSinceLastPathRequest = RepathInterval;
	bHasMoveTarget = false;
	bWaitingForRepath = false;
	LastProgressTime = 0.0;

	if (UWorld* World = GetWorld(); IsValid(World) && World->GetNetMode() != NM_Client)
	{
		if (UProjectProject01TuningSubsystem* TuningSubsystem = World->GetSubsystem<UProjectProject01TuningSubsystem>())
		{
			FHelperTuningRow Tuning;
			if (TuningSubsystem->GetHelperTuning(Tuning))
			{
				ApplyHelperNavigationTuning(Tuning);
			}
		}
	}
}

void AHelperRearGuardAIController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!ControlledHelper.IsValid())
	{
		ControlledHelper = Cast<AHelperRearGuardCharacter>(GetPawn());
	}

	if (!ControlledHelper.IsValid())
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!IsValid(World))
	{
		return;
	}

	const double CurrentTime = World->GetTimeSeconds();
	if (bWaitingForRepath)
	{
		if (CurrentTime < WaitUntilTime)
		{
			return;
		}

		bWaitingForRepath = false;
		bHasMoveTarget = false;
	}

	FVector FollowTarget;
	if (!ControlledHelper->GetFollowTargetLocation(FollowTarget))
	{
		StopMovement();
		bHasMoveTarget = false;
		return;
	}

	const FVector CurrentLocation = ControlledHelper->GetActorLocation();
	if (bHasMoveTarget)
	{
		if (FVector::DistSquared2D(CurrentLocation, LastProgressLocation) >= FMath::Square(ProgressDistance))
		{
			LastProgressLocation = CurrentLocation;
			LastProgressTime = CurrentTime;
		}
		else if (LastProgressTime > 0.0 && CurrentTime - LastProgressTime >= StuckTimeout)
		{
			StopMovement();
			bWaitingForRepath = true;
			WaitUntilTime = CurrentTime + StuckWaitTime;
			bHasMoveTarget = false;
			return;
		}
	}

	TimeSinceLastPathRequest += FMath::Max(DeltaSeconds, 0.0f);
	const bool bTargetMoved =
		!bHasMoveTarget ||
		FVector::DistSquared2D(LastMoveTarget, FollowTarget) >= FMath::Square(RepathDistance);

	if (!bTargetMoved && TimeSinceLastPathRequest < RepathInterval)
	{
		return;
	}

	const float EffectiveAcceptanceRadius = FMath::Min(
		FMath::Max(AcceptanceRadius, 0.0f),
		ControlledHelper->GetMaximumFollowAcceptanceRadius()
	);
	MoveToLocation(FollowTarget, EffectiveAcceptanceRadius, true, true, true, true);

	LastMoveTarget = FollowTarget;
	LastProgressLocation = CurrentLocation;
	LastProgressTime = CurrentTime;
	TimeSinceLastPathRequest = 0.0f;
	bHasMoveTarget = true;
}
