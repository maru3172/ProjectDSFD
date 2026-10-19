#include "ProjectProject01CheckpointAI.h"
#include "BTService_MannequinAI.h"
#include "MannequinAICharacter.h"
#include "MannequinAIController.h"
#include "PlayerCharacter.h"
#include "HelperRearGuardCharacter.h"
#include "HelperRearGuardAIController.h"
#include "MultiplayTestGameMode.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/Tasks/BTTask_Wait.h"
#include "Engine/World.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "UObject/UObjectHash.h"

namespace
{
	bool IsSingle(APawn* Pawn)
	{
		return IsValid(Pawn) && Pawn->GetWorld() && Pawn->GetNetMode() == NM_Standalone &&
			!Pawn->GetWorld()->GetAuthGameMode<AMultiplayTestGameMode>();
	}
	const FName BoolKeys[] = {TEXT("PlayerLookingAtMannequin"), TEXT("RoamingSequenceCompleted"),
		TEXT("IsOutsideOuterRange"), TEXT("IsBetweenPlayerRanges"), TEXT("IsInsideInnerRange"),
		TEXT("IsInChaseSector"), TEXT("IsInRoamingSector")};
	const FName VectorKeys[] = {TEXT("RoamingLocation"), TEXT("PendingMoveLocation")};
	TArray<UBTService_MannequinAI*> Services(UBehaviorTreeComponent* Tree)
	{
		TArray<UBTService_MannequinAI*> Result;
		if (!Tree) return Result;
		TArray<UObject*> Objects;
		GetObjectsWithOuter(Tree, Objects, EGetObjectsFlags::None);
		for (UObject* Object : Objects) if (auto* Service = Cast<UBTService_MannequinAI>(Object)) Result.Add(Service);
		return Result;
	}
}

TArray<uint8> FProjectCheckpointAI::CaptureService(const UBTService_MannequinAI* Source, const double Now)
{
	TArray<uint8> Data;
	FMemoryWriter Writer(Data, true);
	// Copy only value state; never modify an executing service while saving.
	auto* S = const_cast<UBTService_MannequinAI*>(Source);
	uint8 Version = 1;
	double Remaining = FMath::Max(0.0, S->NextRoamingQueryTime - Now);
	Writer << Version << S->PlayerSeeMannequin << S->MannequinSeePlayer << S->RoamingDestination
		<< S->bHasRoamingDestination << S->bRoamingCommitted << S->bLoggedRoamingQueryFailure << Remaining
		<< S->bRoamingStartedOutsideChaseSector << S->ActiveRangeRegion << S->PendingRangeRegion
		<< S->bHasPendingOutwardTransition << S->bPendingTransitionUsesCommittedLocation << S->PendingMoveLocation << S->LastLoggedRangeRegion;
	return Data;
}

bool FProjectCheckpointAI::RestoreService(UBTService_MannequinAI* S, const TArray<uint8>& Data, const double Now)
{
	if (!S || Data.IsEmpty() || Data.Num() > 1024) return false;
	FMemoryReader Reader(Data, true);
	uint8 Version = 0; Reader << Version; if (Version != 1) return false;
	double Remaining = 0;
	Reader << S->PlayerSeeMannequin << S->MannequinSeePlayer << S->RoamingDestination
		<< S->bHasRoamingDestination << S->bRoamingCommitted << S->bLoggedRoamingQueryFailure << Remaining
		<< S->bRoamingStartedOutsideChaseSector << S->ActiveRangeRegion << S->PendingRangeRegion
		<< S->bHasPendingOutwardTransition << S->bPendingTransitionUsesCommittedLocation << S->PendingMoveLocation << S->LastLoggedRangeRegion;
	S->NextRoamingQueryTime = Now + FMath::Clamp(Remaining, 0.0, 60.0);
	return !Reader.IsError();
}

TArray<uint8> FProjectCheckpointAI::Capture(APawn* Pawn, const TFunction<FName(AActor*)>& ActorId)
{
	TArray<uint8> Data;
	if (!IsSingle(Pawn)) return Data;
	const double Now = Pawn->GetWorld()->GetTimeSeconds();
	FMemoryWriter Writer(Data, true);
	uint8 Version = 1;
	if (auto* M = Cast<AMannequinAICharacter>(Pawn))
	{
		uint8 Kind = 1; Writer << Version << Kind;
		uint8 Command = static_cast<uint8>(M->ActivePostPossessionCommand);
		Writer << Command << M->bPostPossessionChaseCommandQueued << M->bLegacyAnimationFrozen;
		TArray<FName> Contacts;
		for (const auto& Contact : M->CaughtSurvivorsInCurrentContact) if (Contact.IsValid()) Contacts.Add(ActorId(Contact.Get()));
		Writer << Contacts;
		auto* AI = Cast<AMannequinAIController>(Pawn->GetController());
		auto* BB = AI ? AI->GetBlackboardComponent() : nullptr;
		for (const FName Key : BoolKeys) { bool Value = BB && BB->GetValueAsBool(Key); Writer << Value; }
		for (const FName Key : VectorKeys)
		{
			bool bSet = BB && BB->IsVectorValueSet(Key); FVector Value = bSet ? BB->GetValueAsVector(Key) : FVector::ZeroVector;
			Writer << bSet << Value;
		}
		FName Target = BB ? ActorId(Cast<AActor>(BB->GetValueAsObject(TEXT("TargetActor")))) : NAME_None; Writer << Target;
		auto* Tree = AI ? Cast<UBehaviorTreeComponent>(AI->GetBrainComponent()) : nullptr;
		bool Running = Tree && Tree->IsRunning(), Paused = Tree && Tree->IsPaused(); Writer << Running << Paused;
		auto Nodes = Services(Tree); int32 Count = Nodes.Num(); Writer << Count;
		for (const auto* S : Nodes)
		{
			int32 Index = S->GetExecutionIndex(); auto State = CaptureService(S, Now); Writer << Index << State;
		}
		int32 WaitIndex = INDEX_NONE; float WaitSeconds = 0;
		if (const auto* Wait = Tree ? Cast<UBTTask_Wait>(Tree->GetActiveNode()) : nullptr)
			if (uint8* Memory = Tree->GetNodeMemory(Wait, Tree->GetActiveInstanceIdx()))
			{ WaitIndex = Wait->GetExecutionIndex(); WaitSeconds = FMath::Max(0.0f, Wait->GetSpecialNodeMemory<FBTTaskMemory>(Memory)->NextTickRemainingTime); }
		Writer << WaitIndex << WaitSeconds;
	}
	else if (auto* H = Cast<AHelperRearGuardCharacter>(Pawn))
	{
		uint8 Kind = 2; Writer << Version << Kind;
		auto* AI = Cast<AHelperRearGuardAIController>(H->GetController());
		bool bHasController = AI != nullptr; Writer << bHasController;
		if (AI)
		{
			double ProgressAge = FMath::Max(0.0, Now - AI->LastProgressTime), WaitRemaining = FMath::Max(0.0, AI->WaitUntilTime - Now);
			Writer << AI->LastMoveTarget << AI->LastProgressLocation << ProgressAge << WaitRemaining
				<< AI->TimeSinceLastPathRequest << AI->bHasMoveTarget << AI->bWaitingForRepath;
		}
		TArray<double> Ages; for (double Time : H->VisionDirectionChangeTimes) Ages.Add(FMath::Max(0.0, Now - Time)); Writer << Ages;
	}
	else Data.Reset();
	return Data;
}

bool FProjectCheckpointAI::Restore(APawn* Pawn, const TArray<uint8>& Data, const TFunction<AActor*(FName)>& Resolve)
{
	if (!IsSingle(Pawn) || Data.IsEmpty() || Data.Num() > 1024 * 1024) return false;
	const double Now = Pawn->GetWorld()->GetTimeSeconds();
	FMemoryReader Reader(Data, true); uint8 Version = 0, Kind = 0; Reader << Version << Kind;
	if (Version != 1) return false;
	if (auto* M = Cast<AMannequinAICharacter>(Pawn); M && Kind == 1)
	{
		uint8 Command = 0; bool Frozen = false;
		Reader << Command << M->bPostPossessionChaseCommandQueued << Frozen;
		if (Command > static_cast<uint8>(AMannequinAICharacter::EPostPossessionCommand::ChaseNearestSurvivor)) return false;
		M->ActivePostPossessionCommand = static_cast<AMannequinAICharacter::EPostPossessionCommand>(Command); M->SetFrozen(Frozen);
		TArray<FName> Contacts; Reader << Contacts; M->CaughtSurvivorsInCurrentContact.Reset();
		for (const auto Id : Contacts) if (auto* Player = Cast<APlayerCharacter>(Resolve(Id))) M->CaughtSurvivorsInCurrentContact.Add(Player);
		auto* AI = Cast<AMannequinAIController>(Pawn->GetController());
		auto* Tree = AI ? Cast<UBehaviorTreeComponent>(AI->GetBrainComponent()) : nullptr;
		auto* BB = AI ? AI->GetBlackboardComponent() : nullptr;
		if (Tree) Tree->PauseLogic(TEXT("Checkpoint restore"));
		if (BB) BB->PauseObserverNotifications();
		for (const FName Key : BoolKeys) { bool Value = false; Reader << Value; if (BB) BB->SetValueAsBool(Key, Value); }
		for (const FName Key : VectorKeys)
		{
			bool bSet = false; FVector Value; Reader << bSet << Value;
			if (BB) { if (bSet) BB->SetValueAsVector(Key, Value); else BB->ClearValue(Key); }
		}
		FName Target; Reader << Target; if (BB) BB->SetValueAsObject(TEXT("TargetActor"), Resolve(Target));
		bool Running = false, Paused = false; Reader << Running << Paused;
		int32 Count = 0; Reader << Count;
		if (Count < 0 || Count > 256) { if (BB) BB->ResumeObserverNotifications(false); if (Tree) Tree->ResumeLogic(TEXT("Checkpoint restore rejected")); return false; }
		auto Nodes = Services(Tree);
		for (int32 I = 0; I < Count && !Reader.IsError(); ++I)
		{
			int32 Index = 0; TArray<uint8> State; Reader << Index << State;
			for (auto* S : Nodes) if (S->GetExecutionIndex() == Index) RestoreService(S, State, Now);
		}
		int32 WaitIndex = INDEX_NONE; float WaitSeconds = 0; Reader << WaitIndex << WaitSeconds;
		if (AI) { AI->RestoredWaitNodeIndex = WaitIndex; AI->RestoredWaitSeconds = FMath::Max(0.0f, WaitSeconds); AI->RestoredWaitDeadline = Now + 2.0; }
		if (BB) BB->ResumeObserverNotifications(true);
		if (Tree)
		{
			Tree->ResumeLogic(TEXT("Checkpoint restore"));
			if (Running && Tree->IsRunning()) Tree->RestartTree(EBTRestartMode::ForceReevaluateRootNode);
			if (!Running) Tree->StopLogic(TEXT("Checkpoint restored stopped AI"));
			else if (Paused) Tree->PauseLogic(TEXT("Checkpoint restored paused AI"));
		}
	}
	else if (auto* H = Cast<AHelperRearGuardCharacter>(Pawn); H && Kind == 2)
	{
		bool bHasController = false; Reader << bHasController;
		if (bHasController)
		{
			FVector Target, Progress; double Age = 0, Wait = 0; float Since = 0; bool Has = false, Waiting = false;
			Reader << Target << Progress << Age << Wait << Since << Has << Waiting;
			if (auto* AI = Cast<AHelperRearGuardAIController>(H->GetController()))
			{
				AI->LastMoveTarget = Target; AI->LastProgressLocation = Progress; AI->LastProgressTime = Now - Age;
				AI->WaitUntilTime = Now + Wait; AI->TimeSinceLastPathRequest = Since; AI->bHasMoveTarget = Has; AI->bWaitingForRepath = Waiting;
			}
		}
		TArray<double> Ages; Reader << Ages; H->VisionDirectionChangeTimes.Reset();
		for (double Age : Ages) H->VisionDirectionChangeTimes.Add(Now - Age);
		H->bRetreatLocationInitialized = false; H->bRetreatContactConfirmed = false;
	}
	else return false;
	return !Reader.IsError();
}
