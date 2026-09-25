// File: Source/ProjectProject01/Private/MultiplayTestGameMode.cpp
// Build target: ProjectProject01Server / ProjectProject01

#include "MultiplayTestGameMode.h"

#include "MannequinAICharacter.h"
#include "MannequinAIController.h"
#include "MultiplayTestPlayerController.h"
#include "PlayerCharacter.h"
#include "ProjectProject01TuningData.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#endif

DEFINE_LOG_CATEGORY(LogProjectProject01Multiplayer);

namespace
{
	bool IsPointInsideVisionCone(
		const FVector& ViewOrigin,
		const FVector& ViewDirection,
		const FVector& TargetPoint,
		const float HalfAngleDegrees)
	{
		const FVector ToTarget = TargetPoint - ViewOrigin;
		if (ToTarget.IsNearlyZero() || ViewDirection.IsNearlyZero() || ToTarget.ContainsNaN() || ViewDirection.ContainsNaN())
		{
			return false;
		}

		const float MinimumDotProduct = FMath::Cos(FMath::DegreesToRadians(FMath::Max(0.0f, HalfAngleDegrees)));
		return FVector::DotProduct(ViewDirection.GetSafeNormal(), ToTarget.GetSafeNormal()) >= MinimumDotProduct;
	}
}

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectProject01SurvivorVisionConeTest,
	"ProjectProject01.Multiplayer.SurvivorVisionCone",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectProject01SurvivorVisionConeTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("A point directly ahead is inside the vision cone"),
		IsPointInsideVisionCone(FVector::ZeroVector, FVector::ForwardVector, FVector(100.0f, 0.0f, 0.0f), 55.0f));
	TestFalse(TEXT("A point directly behind is outside the vision cone"),
		IsPointInsideVisionCone(FVector::ZeroVector, FVector::ForwardVector, FVector(-100.0f, 0.0f, 0.0f), 55.0f));
	TestFalse(TEXT("An invalid zero-distance target is rejected"),
		IsPointInsideVisionCone(FVector::ZeroVector, FVector::ForwardVector, FVector::ZeroVector, 55.0f));
	return true;
}
#endif

AMultiplayTestGameMode::AMultiplayTestGameMode()
{
	PrimaryActorTick.bCanEverTick = true;

	// 콘텐츠 Blueprint를 불러오지 못해도 네트워크 이동이 가능한 네이티브 Pawn으로 안전하게 대체한다.
	DefaultPawnClass = APlayerCharacter::StaticClass();

	static ConstructorHelpers::FClassFinder<APawn> PlayerPawnClassFinder(
		TEXT("/Game/MyProject/BP_PlayerCharacter"));
	if (PlayerPawnClassFinder.Succeeded())
	{
		DefaultPawnClass = PlayerPawnClassFinder.Class;
	}
	else
	{
		ensureMsgf(false,
			TEXT("MultiplayTestGameMode could not load /Game/MyProject/BP_PlayerCharacter. Falling back to APlayerCharacter."));
		UE_LOG(LogProjectProject01Multiplayer, Error,
			TEXT("MultiplayTestGameMode could not load /Game/MyProject/BP_PlayerCharacter; using APlayerCharacter."));
	}

	PlayerControllerClass = AMultiplayTestPlayerController::StaticClass();
}

void AMultiplayTestGameMode::BeginPlay()
{
	Super::BeginPlay();

	if (UWorld* World = GetWorld(); IsValid(World))
	{
		if (UProjectProject01TuningSubsystem* TuningSubsystem = World->GetSubsystem<UProjectProject01TuningSubsystem>())
		{
			FMannequinAITuningRow Tuning;
			if (TuningSubsystem->GetMannequinTuning(Tuning))
			{
				ApplyMannequinTuning(Tuning);
			}
		}
	}
}

void AMultiplayTestGameMode::ApplyMannequinTuning(const FMannequinAITuningRow& Tuning)
{
	SurvivorVisionCheckIntervalSeconds = FMath::Max(0.0f, Tuning.SurvivorVisionCheckIntervalSeconds);
	SurvivorVisionHalfAngleDegrees = FMath::Max(0.0f, Tuning.SurvivorVisionHalfAngleDegrees);
}

void AMultiplayTestGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!HasAuthority() || DeltaSeconds <= 0.0f)
	{
		return;
	}

	SurvivorVisionCheckAccumulatorSeconds += DeltaSeconds;
	if (SurvivorVisionCheckAccumulatorSeconds < FMath::Max(SurvivorVisionCheckIntervalSeconds, 0.01f))
	{
		return;
	}

	SurvivorVisionCheckAccumulatorSeconds = 0.0f;
	UpdateSurvivorVisionFrozenStates();
}

void AMultiplayTestGameMode::PostLogin(APlayerController* NewPlayer)
{
	AMultiplayTestPlayerController* NewMultiplayController = Cast<AMultiplayTestPlayerController>(NewPlayer);
	if (IsValid(NewMultiplayController) && !MannequinController.IsValid())
	{
		MannequinController = NewMultiplayController;
		UE_LOG(LogProjectProject01Multiplayer, Log,
			TEXT("%s was assigned as the prototype mannequin controller."), *NewMultiplayController->GetName());
	}
	else if (!IsValid(NewMultiplayController))
	{
		UE_LOG(LogProjectProject01Multiplayer, Error,
			TEXT("MultiplayTest requires AMultiplayTestPlayerController, but %s connected."), *GetNameSafe(NewPlayer));
	}

	Super::PostLogin(NewPlayer);
}

void AMultiplayTestGameMode::Logout(AController* Exiting)
{
	AMultiplayTestPlayerController* ExitingMultiplayController = Cast<AMultiplayTestPlayerController>(Exiting);
	if (IsMannequinController(ExitingMultiplayController))
	{
		if (AMannequinAICharacter* ControlledMannequin = Cast<AMannequinAICharacter>(ExitingMultiplayController->GetPawn());
			IsValid(ControlledMannequin))
		{
			ExitingMultiplayController->UnPossess();
			QueueDefaultAIControllerRestore(ControlledMannequin);
		}

		MannequinController.Reset();
		UE_LOG(LogProjectProject01Multiplayer, Log, TEXT("The mannequin controller disconnected."));
	}

	Super::Logout(Exiting);
}

bool AMultiplayTestGameMode::MustSpectate_Implementation(APlayerController* NewPlayerController) const
{
	return IsMannequinController(Cast<AMultiplayTestPlayerController>(NewPlayerController)) ||
		Super::MustSpectate_Implementation(NewPlayerController);
}

bool AMultiplayTestGameMode::TryPossessMannequin(AMultiplayTestPlayerController* RequestingController, int32 Slot)
{
	if (!HasAuthority() || !IsValid(RequestingController) || !IsMannequinController(RequestingController))
	{
		UE_LOG(LogProjectProject01Multiplayer, Warning,
			TEXT("Rejected mannequin slot request %d from %s."), Slot, *GetNameSafe(RequestingController));
		return false;
	}

	AMannequinAICharacter* TargetMannequin = FindMannequinBySlot(Slot);
	if (!IsValid(TargetMannequin))
	{
		UE_LOG(LogProjectProject01Multiplayer, Warning,
			TEXT("No unique mannequin was found for control slot %d."), Slot);
		return false;
	}

	AMannequinAICharacter* PreviousViewedMannequin = RequestingController->GetViewedMannequin();
	if (PreviousViewedMannequin == TargetMannequin)
	{
		return true;
	}

	if (APlayerController* ExistingPlayerController = Cast<APlayerController>(TargetMannequin->GetController());
		IsValid(ExistingPlayerController) && ExistingPlayerController != RequestingController)
	{
		UE_LOG(LogProjectProject01Multiplayer, Warning,
			TEXT("Rejected mannequin slot %d because %s is already player-controlled."), Slot, *GetNameSafe(TargetMannequin));
		return false;
	}

	AMannequinAICharacter* PreviousManuallyControlledMannequin = Cast<AMannequinAICharacter>(RequestingController->GetPawn());
	if (IsValid(PreviousManuallyControlledMannequin))
	{
		PreviousManuallyControlledMannequin->SetManualControlEnabled(false);
		PreviousManuallyControlledMannequin->ActivatePostPossessionCommand();
		RequestingController->UnPossess();
		QueueDefaultAIControllerRestore(PreviousManuallyControlledMannequin);
	}

	// 번호키 단계에서는 AIController를 유지하고, 플레이어는 시점만 공유한다.
	RequestingController->SetViewedMannequin(TargetMannequin);
	TargetMannequin->ForceNetUpdate();
	UE_LOG(LogProjectProject01Multiplayer, Log,
		TEXT("%s now views mannequin slot %d (%s) while its AI remains active."),
		*RequestingController->GetName(), Slot, *TargetMannequin->GetName());
	return true;
}

bool AMultiplayTestGameMode::TryEnableMannequinManualControl(AMultiplayTestPlayerController* RequestingController)
{
	if (!HasAuthority() || !IsValid(RequestingController) || !IsMannequinController(RequestingController))
	{
		return false;
	}

	AMannequinAICharacter* TargetMannequin = RequestingController->GetViewedMannequin();
	if (!IsValid(TargetMannequin))
	{
		UE_LOG(LogProjectProject01Multiplayer, Warning,
			TEXT("Rejected manual-control request from %s because no viewed mannequin is available."),
			*GetNameSafe(RequestingController));
		return false;
	}

	if (APlayerController* ExistingPlayerController = Cast<APlayerController>(TargetMannequin->GetController());
		IsValid(ExistingPlayerController) && ExistingPlayerController != RequestingController)
	{
		UE_LOG(LogProjectProject01Multiplayer, Warning,
			TEXT("Rejected manual control of %s because it is already player-controlled."), *GetNameSafe(TargetMannequin));
		return false;
	}

	if (RequestingController->GetPawn() == TargetMannequin)
	{
		TargetMannequin->SetManualControlEnabled(true);
		return true;
	}

	if (UCharacterMovementComponent* Movement = TargetMannequin->GetCharacterMovement(); IsValid(Movement))
	{
		Movement->StopMovementImmediately();
	}

	RequestingController->Possess(TargetMannequin);
	if (!ensureMsgf(RequestingController->GetPawn() == TargetMannequin,
		TEXT("Failed to enable manual control for mannequin %s."), *GetNameSafe(TargetMannequin)))
	{
		QueueDefaultAIControllerRestore(TargetMannequin);
		RequestingController->SetViewedMannequin(TargetMannequin);
		return false;
	}

	TargetMannequin->SetManualControlEnabled(true);
	TargetMannequin->ForceNetUpdate();
	UE_LOG(LogProjectProject01Multiplayer, Log,
		TEXT("%s enabled manual control of mannequin %s."),
		*RequestingController->GetName(), *TargetMannequin->GetName());
	return true;
}

bool AMultiplayTestGameMode::TryQueuePostPossessionChaseCommand(AMultiplayTestPlayerController* RequestingController)
{
	if (!HasAuthority() || !IsValid(RequestingController) || !IsMannequinController(RequestingController))
	{
		return false;
	}

	AMannequinAICharacter* ManuallyControlledMannequin = Cast<AMannequinAICharacter>(RequestingController->GetPawn());
	if (!IsValid(ManuallyControlledMannequin) || !ManuallyControlledMannequin->IsManualControlEnabled())
	{
		UE_LOG(LogProjectProject01Multiplayer, Warning,
			TEXT("Rejected post-possession chase command from %s because manual control is not active."),
			*GetNameSafe(RequestingController));
		return false;
	}

	return ManuallyControlledMannequin->QueuePostPossessionChaseCommand();
}

void AMultiplayTestGameMode::QueueDefaultAIControllerRestore(AMannequinAICharacter* Mannequin) const
{
	UWorld* World = GetWorld();
	if (!HasAuthority() || !IsValid(World) || !IsValid(Mannequin))
	{
		return;
	}

	const TWeakObjectPtr<AMannequinAICharacter> WeakMannequin(Mannequin);
	World->GetTimerManager().SetTimerForNextTick([WeakMannequin]()
	{
		AMannequinAICharacter* RestoredMannequin = WeakMannequin.Get();
		if (!IsValid(RestoredMannequin) || !RestoredMannequin->HasAuthority())
		{
			return;
		}

		// 슬롯을 빠르게 전환해 이 마네킹을 다시 플레이어가 잡은 경우에는 AI를 덮어쓰지 않는다.
		if (IsValid(RestoredMannequin->GetController()))
		{
			return;
		}

		RestoredMannequin->SpawnDefaultController();
		AMannequinAIController* RestoredAIController = Cast<AMannequinAIController>(RestoredMannequin->GetController());
		if (!ensureMsgf(IsValid(RestoredAIController),
			TEXT("Mannequin %s could not restore its default AIController after player possession."),
			*GetNameSafe(RestoredMannequin)))
		{
			UE_LOG(LogProjectProject01Multiplayer, Error,
				TEXT("Mannequin %s has no valid default AIController after control transfer."),
				*GetNameSafe(RestoredMannequin));
			return;
		}

		RestoredMannequin->ForceNetUpdate();
		UE_LOG(LogProjectProject01Multiplayer, Verbose,
			TEXT("Restored AI control for mannequin %s after player control transfer."),
			*GetNameSafe(RestoredMannequin));
	});
}

AMannequinAICharacter* AMultiplayTestGameMode::FindMannequinBySlot(int32 Slot) const
{
	if (Slot < 0 || Slot > 9 || !IsValid(GetWorld()))
	{
		return nullptr;
	}

	TArray<AActor*> MannequinActors;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), AMannequinAICharacter::StaticClass(), MannequinActors);

	AMannequinAICharacter* MatchingMannequin = nullptr;
	for (AActor* Actor : MannequinActors)
	{
		AMannequinAICharacter* Mannequin = Cast<AMannequinAICharacter>(Actor);
		if (!IsValid(Mannequin) || Mannequin->GetControlSlot() != Slot)
		{
			continue;
		}

		if (IsValid(MatchingMannequin))
		{
			UE_LOG(LogProjectProject01Multiplayer, Error,
				TEXT("Duplicate mannequin control slot %d found on %s and %s."),
				Slot, *MatchingMannequin->GetName(), *Mannequin->GetName());
			return nullptr;
		}

		MatchingMannequin = Mannequin;
	}

	return MatchingMannequin;
}

bool AMultiplayTestGameMode::IsMannequinController(const AMultiplayTestPlayerController* Controller) const
{
	return IsValid(Controller) && MannequinController.Get() == Controller;
}

bool AMultiplayTestGameMode::IsSurvivorController(const APlayerController* Controller) const
{
	const APawn* ControlledPawn = IsValid(Controller) ? Controller->GetPawn() : nullptr;
	return IsValid(ControlledPawn) && !IsMannequinController(Cast<AMultiplayTestPlayerController>(Controller)) &&
		!ControlledPawn->IsA<AMannequinAICharacter>();
}

bool AMultiplayTestGameMode::CanSurvivorSeeMannequin(
	const APlayerController* SurvivorController,
	const AMannequinAICharacter* Mannequin) const
{
	if (!IsValid(SurvivorController) || !IsValid(Mannequin) || !IsValid(GetWorld()))
	{
		return false;
	}

	const APawn* SurvivorPawn = SurvivorController->GetPawn();
	const UCapsuleComponent* MannequinCapsule = Mannequin->GetCapsuleComponent();
	if (!IsValid(SurvivorPawn) || !IsValid(MannequinCapsule))
	{
		return false;
	}

	FVector ViewLocation;
	FRotator ViewRotation;
	SurvivorController->GetPlayerViewPoint(ViewLocation, ViewRotation);
	if (ViewLocation.ContainsNaN() || ViewRotation.ContainsNaN())
	{
		UE_LOG(LogProjectProject01Multiplayer, Warning,
			TEXT("Rejected invalid survivor view data from %s."), *GetNameSafe(SurvivorController));
		return false;
	}

	const FVector CapsuleCenter = MannequinCapsule->GetComponentLocation();
	const FVector CapsuleUp = MannequinCapsule->GetUpVector();
	const float CapsuleHalfHeight = MannequinCapsule->GetScaledCapsuleHalfHeight();
	const FVector TargetPoints[] =
	{
		CapsuleCenter,
		CapsuleCenter + CapsuleUp * (CapsuleHalfHeight * 0.55f),
		CapsuleCenter - CapsuleUp * (CapsuleHalfHeight * 0.35f)
	};

	FCollisionQueryParams TraceParameters(SCENE_QUERY_STAT(SurvivorVision), true, SurvivorPawn);
	TraceParameters.AddIgnoredActor(SurvivorController);
	for (const FVector& TargetPoint : TargetPoints)
	{
		if (!IsPointInsideVisionCone(ViewLocation, ViewRotation.Vector(), TargetPoint, SurvivorVisionHalfAngleDegrees))
		{
			continue;
		}

		FHitResult HitResult;
		const bool bReachedMannequin = GetWorld()->LineTraceSingleByChannel(
			HitResult, ViewLocation, TargetPoint, ECC_GameTraceChannel1, TraceParameters) && HitResult.GetActor() == Mannequin;
		if (bReachedMannequin)
		{
			return true;
		}
	}

	return false;
}

void AMultiplayTestGameMode::UpdateSurvivorVisionFrozenStates()
{
	if (!HasAuthority() || !IsValid(GetWorld()))
	{
		return;
	}

	TArray<APlayerController*> SurvivorControllers;
	for (FConstPlayerControllerIterator ControllerIterator = GetWorld()->GetPlayerControllerIterator(); ControllerIterator; ++ControllerIterator)
	{
		APlayerController* Controller = ControllerIterator->Get();
		if (IsSurvivorController(Controller))
		{
			SurvivorControllers.Add(Controller);
		}
	}

	TArray<AActor*> MannequinActors;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), AMannequinAICharacter::StaticClass(), MannequinActors);
	for (AActor* Actor : MannequinActors)
	{
		AMannequinAICharacter* Mannequin = Cast<AMannequinAICharacter>(Actor);
		if (!IsValid(Mannequin))
		{
			continue;
		}

		bool bObservedByAnySurvivor = false;
		for (const APlayerController* SurvivorController : SurvivorControllers)
		{
			if (CanSurvivorSeeMannequin(SurvivorController, Mannequin))
			{
				bObservedByAnySurvivor = true;
				break;
			}
		}

		Mannequin->SetFrozenBySurvivorVision(bObservedByAnySurvivor);
	}
}
