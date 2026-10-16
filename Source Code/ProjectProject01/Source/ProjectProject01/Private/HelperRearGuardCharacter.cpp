// File: Source/ProjectProject01/Private/HelperRearGuardCharacter.cpp
// Target: ProjectProject01Editor Win64 DebugGame, Unreal Engine 5.8

#include "HelperRearGuardCharacter.h"

#include "Components/CapsuleComponent.h"
#include "CollisionShape.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "HelperRearGuardAIController.h"
#include "FootstepSynthComponent.h"
#include "Kismet/GameplayStatics.h"
#include "MannequinAICharacter.h"
#include "PlayerCharacter.h"
#include "Engine/World.h"
#include "ProjectProject01TuningData.h"
#include "ProjectProject01DiagnosticsSubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#endif

namespace
{
	bool CapsulesTouch(const UCapsuleComponent* First, const UCapsuleComponent* Second)
	{
		if (!IsValid(First) || !IsValid(Second)) return false;
		const float FirstRadius = First->GetScaledCapsuleRadius();
		const float SecondRadius = Second->GetScaledCapsuleRadius();
		const FVector FirstAxis = First->GetUpVector() * FMath::Max(0.0f, First->GetScaledCapsuleHalfHeight() - FirstRadius);
		const FVector SecondAxis = Second->GetUpVector() * FMath::Max(0.0f, Second->GetScaledCapsuleHalfHeight() - SecondRadius);
		FVector FirstClosest, SecondClosest;
		FMath::SegmentDistToSegmentSafe(First->GetComponentLocation() - FirstAxis, First->GetComponentLocation() + FirstAxis,
			Second->GetComponentLocation() - SecondAxis, Second->GetComponentLocation() + SecondAxis, FirstClosest, SecondClosest);
		return FVector::DistSquared(FirstClosest, SecondClosest) <= FMath::Square(FirstRadius + SecondRadius);
	}

	bool HasUsableGuardSight(const float SightHalfAngleDegrees)
	{
		return SightHalfAngleDegrees > KINDA_SMALL_NUMBER;
	}

	/** 실제 시야 부채꼴 전체가 커버 부채꼴 안에 남도록 중심 방향을 제한한다. */
	FVector ClampViewDirectionToCoverage(
		const FVector& CoverageCenterDirection,
		const FVector& DesiredViewDirection,
		const float CoverageHalfAngleDegrees,
		const float SightHalfAngleDegrees)
	{
		const FVector NormalizedCoverageCenter = CoverageCenterDirection.GetSafeNormal2D();
		const FVector NormalizedDesiredDirection = DesiredViewDirection.GetSafeNormal2D();
		if (NormalizedCoverageCenter.IsNearlyZero() || NormalizedDesiredDirection.IsNearlyZero())
		{
			return NormalizedCoverageCenter;
		}

		const float MaximumCenterOffsetDegrees = FMath::Max(0.0f, CoverageHalfAngleDegrees - SightHalfAngleDegrees);
		const float Dot = FMath::Clamp(FVector::DotProduct(NormalizedCoverageCenter, NormalizedDesiredDirection), -1.0f, 1.0f);
		const float UnsignedAngleDegrees = FMath::RadiansToDegrees(FMath::Acos(Dot));
		const float SignedAngleDegrees = FVector::CrossProduct(NormalizedCoverageCenter, NormalizedDesiredDirection).Z < 0.0f
			? -UnsignedAngleDegrees
			: UnsignedAngleDegrees;

		return NormalizedCoverageCenter.RotateAngleAxis(
			FMath::Clamp(SignedAngleDegrees, -MaximumCenterOffsetDegrees, MaximumCenterOffsetDegrees),
			FVector::UpVector).GetSafeNormal2D();
	}
}

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectProject01HelperVisionBoundsTest,
	"ProjectProject01.Helper.VisionBounds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectProject01HelperVisionBoundsTest::RunTest(const FString& Parameters)
{
	const FVector CoverageCenter = FVector::ForwardVector;
	const FVector DesiredAtCoverageEdge = FVector::RightVector;
	const FVector ConstrainedView = ClampViewDirectionToCoverage(CoverageCenter, DesiredAtCoverageEdge, 90.0f, 60.0f);
	const float OffsetDegrees = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(
		FVector::DotProduct(CoverageCenter, ConstrainedView), -1.0f, 1.0f)));

	TestTrue(TEXT("120-degree actual sight stays inside 180-degree coverage"), FMath::IsNearlyEqual(OffsetDegrees, 30.0f, 0.01f));
	TestFalse(TEXT("A zero-degree helper sight observes nothing"), HasUsableGuardSight(0.0f));
	TestTrue(TEXT("A positive helper sight remains active"), HasUsableGuardSight(0.01f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectProject01HelperRetreatContactTest,
	"ProjectProject01.Helper.RetreatContact", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectProject01HelperRetreatContactTest::RunTest(const FString&)
{
	const auto Values = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true)
		.CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Values);
	AHelperRearGuardCharacter* Helper = World->SpawnActor<AHelperRearGuardCharacter>();
	APlayerCharacter* Player = World->SpawnActor<APlayerCharacter>();
	if (!IsValid(Helper) || !IsValid(Player))
	{
		AddError(TEXT("Contact fixture actors could not spawn"));
		World->DestroyWorld(false);
		return false;
	}
	Helper->SetActorLocation(FVector(0, 0, 100));
	Player->SetActorLocation(FVector(1000, 0, 100));
	// This test world intentionally does not run BeginPlay; initialize the movement receiver explicitly.
	Player->GetCharacterMovement()->SetUpdatedComponent(Player->GetCapsuleComponent());
	Player->GetCharacterMovement()->Activate(true);
	Player->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	Player->SetRemainingDeathCountForGameMode(3);
	AddExpectedMessage(TEXT("lost one death count after being pushed by helper"), ELogVerbosity::Warning,
		EAutomationExpectedMessageFlags::Contains, 2);
	AddExpectedMessage(TEXT("after actual retreat capsule contact."), ELogVerbosity::Warning,
		EAutomationExpectedMessageFlags::Contains, 2);
	Helper->GuardedPlayer = Player;
	Helper->BeginVisionRetreat(-FVector::ForwardVector);
	Helper->UpdateRetreatContact();
	TestEqual(TEXT("Player on future retreat route is not damaged at planning time"), Player->GetRemainingDeathCount(), 3);
	TestFalse(TEXT("Direct damage call without confirmed contact is rejected"), Player->HandlePartnerPushDeath(Helper));
	Helper->SetActorLocation(FVector(400, 0, 100));
	Helper->UpdateRetreatContact();
	TestEqual(TEXT("Actual movement still short of the player is safe"), Player->GetRemainingDeathCount(), 3);
	Player->SetActorLocation(FVector(1000, 0, 500));
	Helper->SetActorLocation(FVector(930, 0, 100));
	Helper->UpdateRetreatContact();
	TestEqual(TEXT("Same XY on a different floor does not count as contact"), Player->GetRemainingDeathCount(), 3);
	Player->SetActorLocation(FVector(1000, 0, 100));
	Helper->SetActorLocation(FVector(940, 0, 100));
	Helper->UpdateRetreatContact();
	TestEqual(TEXT("Actual retreat capsule contact consumes one death count"), Player->GetRemainingDeathCount(), 2);
	TestTrue(TEXT("Physical contact also queues a push"), Player->GetCharacterMovement()->PendingLaunchVelocity.X > 0.0f);
	Player->SetRemainingDeathCountForGameMode(3); // Also clears invulnerability; test the one-push latch, not the cooldown.
	Helper->SetActorLocation(FVector(950, 0, 100));
	Helper->UpdateRetreatContact();
	TestEqual(TEXT("Same retreat cannot consume again even with cooldown reset"), Player->GetRemainingDeathCount(), 3);
	Helper->bFollowDistanceLocked = false;
	Helper->BeginVisionRetreat(-FVector::ForwardVector);
	Player->SetActorLocation(FVector(1500, 0, 100));
	Helper->SetActorLocation(FVector(1700, 0, 100));
	Helper->UpdateRetreatContact();
	TestEqual(TEXT("Sweep of already travelled movement catches a crossed player"), Player->GetRemainingDeathCount(), 2);
	Player->SetRemainingDeathCountForGameMode(3);
	Helper->bFollowDistanceLocked = false;
	Helper->BeginVisionRetreat(-FVector::ForwardVector);
	Helper->SetActorLocation(FVector(5000, 0, 100));
	Player->SetActorLocation(FVector(5000, 0, 100));
	Helper->UpdateRetreatContact();
	TestEqual(TEXT("Teleport/load jump is not a retreat collision"), Player->GetRemainingDeathCount(), 3);
	Helper->UpdateRetreatContact();
	TestEqual(TEXT("Standing in contact without retreat movement does not push"), Player->GetRemainingDeathCount(), 3);
	Helper->bFollowDistanceLocked = false;
	Helper->SetActorLocation(FVector(5010, 0, 100));
	Helper->UpdateRetreatContact();
	TestEqual(TEXT("Normal following is not a retreat attack"), Player->GetRemainingDeathCount(), 3);
	Helper->BeginVisionRetreat(-FVector::ForwardVector);
	Player->GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Helper->SetActorLocation(FVector(5020, 0, 100));
	Helper->UpdateRetreatContact();
	TestEqual(TEXT("Disabled player collision does not permit a retreat push"), Player->GetRemainingDeathCount(), 3);
	World->DestroyWorld(false);
	return true;
}
#endif

AHelperRearGuardCharacter::AHelperRearGuardCharacter()
{
	PrimaryActorTick.bCanEverTick = true;
	FootstepSFXComponent = CreateDefaultSubobject<UFootstepSynthComponent>(TEXT("Footstep SFX Component"));
	FootstepSFXComponent->SetupAttachment(RootComponent);
	FootstepSFXComponent->Configure(EProjectProject01FootstepStyle::MarbleSneaker, 0.425f, true);

	AIControllerClass = AHelperRearGuardAIController::StaticClass();
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;

	if (UCapsuleComponent* HelperCapsule = GetCapsuleComponent())
	{
		HelperCapsule->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		HelperCapsule->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	}

	if (UCharacterMovementComponent* HelperMovementComponent = GetCharacterMovement())
	{
		HelperMovementComponent->bOrientRotationToMovement = true;
		HelperMovementComponent->bUseControllerDesiredRotation = false;
	}
}

void AHelperRearGuardCharacter::BeginPlay()
{
	Super::BeginPlay();

	if (UWorld* World = GetWorld(); IsValid(World) && World->GetNetMode() != NM_Client)
	{
		if (UProjectProject01TuningSubsystem* TuningSubsystem = World->GetSubsystem<UProjectProject01TuningSubsystem>())
		{
			FHelperTuningRow Tuning;
			if (TuningSubsystem->GetHelperTuning(Tuning))
			{
				ApplyHelperTuning(Tuning);
			}
		}
	}
	RefreshGuardedPlayer();
	UpdateGuardViewDirection();
}

void AHelperRearGuardCharacter::ApplyHelperTuning(const FHelperTuningRow& Tuning)
{
	MinimumFollowSeparation = FMath::Max(0.0f, Tuning.MinimumFollowSeparation);
	FollowDistance = FMath::Max(0.0f, Tuning.FollowDistance);
	GuardSightRadius = FMath::Max(0.0f, Tuning.GuardSightRadius);
	GuardHalfAngleDegrees = FMath::Max(0.0f, Tuning.GuardHalfAngleDegrees);
	GuardSearchHalfAngleDegrees = FMath::Max(0.0f, Tuning.GuardSearchHalfAngleDegrees);
	StaminaVisionDrainMultiplier = FMath::Max(0.0f, Tuning.StaminaVisionDrainMultiplier);
	StaminaVisionRecoveryMultiplier = FMath::Max(0.0f, Tuning.StaminaVisionRecoveryMultiplier);
	if (!bGuardHalfAngleInitialized)
	{
		CurrentGuardHalfAngleDegrees = GetConfiguredGuardSightHalfAngleDegrees();
		bGuardHalfAngleInitialized = true;
	}
	else
	{
		CurrentGuardHalfAngleDegrees = FMath::Clamp(CurrentGuardHalfAngleDegrees, 0.0f, GetConfiguredGuardSightHalfAngleDegrees());
	}
	VisionDirectionChangeWindowSeconds = FMath::Max(0.0f, Tuning.VisionDirectionChangeWindowSeconds);
	VisionDirectionChangeRequiredCount = FMath::Max(1, Tuning.VisionDirectionChangeRequiredCount);
	VisionDirectionChangeThresholdDegrees = FMath::Max(0.0f, Tuning.VisionDirectionChangeThresholdDegrees);

	if (UCharacterMovementComponent* Movement = GetCharacterMovement(); IsValid(Movement))
	{
		Movement->MaxWalkSpeed = FMath::Max(0.0f, Tuning.HelperWalkSpeed);
	}
	else
	{
		ensureMsgf(false, TEXT("Helper %s has no CharacterMovementComponent."), *GetName());
	}

	if (AHelperRearGuardAIController* HelperController = Cast<AHelperRearGuardAIController>(GetController()))
	{
		HelperController->ApplyHelperNavigationTuning(Tuning);
	}
}

void AHelperRearGuardCharacter::GetDiagnosticAppliedTuning(FHelperTuningRow& OutTuning) const
{
	OutTuning.HelperWalkSpeed = IsValid(GetCharacterMovement()) ? GetCharacterMovement()->MaxWalkSpeed : 0.0f;
	OutTuning.FollowDistance = FollowDistance;
	OutTuning.MinimumFollowSeparation = MinimumFollowSeparation;
	OutTuning.GuardSightRadius = GuardSightRadius;
	OutTuning.GuardHalfAngleDegrees = GuardHalfAngleDegrees;
	OutTuning.GuardSearchHalfAngleDegrees = GuardSearchHalfAngleDegrees;
	OutTuning.StaminaVisionDrainMultiplier = StaminaVisionDrainMultiplier;
	OutTuning.StaminaVisionRecoveryMultiplier = StaminaVisionRecoveryMultiplier;
	OutTuning.VisionDirectionChangeWindowSeconds = VisionDirectionChangeWindowSeconds;
	OutTuning.VisionDirectionChangeRequiredCount = VisionDirectionChangeRequiredCount;
	OutTuning.VisionDirectionChangeThresholdDegrees = VisionDirectionChangeThresholdDegrees;
	if (const AHelperRearGuardAIController* HelperController = Cast<AHelperRearGuardAIController>(GetController()))
	{
		HelperController->GetDiagnosticNavigationTuning(OutTuning.RepathInterval, OutTuning.RepathDistance,
			OutTuning.AcceptanceRadius, OutTuning.StuckTimeout, OutTuning.StuckWaitTime, OutTuning.ProgressDistance);
	}
}

void AHelperRearGuardCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (IsValid(FootstepSFXComponent))
	{
		FootstepSFXComponent->UpdateFootsteps(DeltaTime);
	}

	if (!GuardedPlayer.IsValid())
	{
		RefreshGuardedPlayer();
	}

	UpdateLastPlayerMovementDirection();
	// 커버 중심은 마지막 플레이어 이동 방향을 사용하지만, 실제 시야는 정지 중에도 매 틱 갱신한다.
	UpdateGuardViewDirection();
	if (HasAuthority())
	{
		// Inspect the movement that actually happened, before updating/cancelling the next retreat.
		UpdateRetreatContact();
		UpdateVisionRetreatState();
	}

	DrawGuardDebug();
}

bool AHelperRearGuardCharacter::CanObserveActor(const AActor* TargetActor) const
{
	FProjectProject01DiagnosticWorkScope DiagnosticWork(GetWorld());
	DiagnosticWork.AddVisionCheck();
	const float SightHalfAngleDegrees = GetEffectiveGuardSightHalfAngleDegrees();
	if (!IsValid(TargetActor) || !GuardedPlayer.IsValid() || GuardSightRadius <= 0.0f ||
		!HasUsableGuardSight(SightHalfAngleDegrees))
	{
		return false;
	}

	const FVector ViewOrigin = GetGuardViewOrigin();
	FVector ToTarget = TargetActor->GetActorLocation() - ViewOrigin;
	ToTarget.Z = 0.0f;

	const float DistanceSquared = ToTarget.SizeSquared();
	if (DistanceSquared <= KINDA_SMALL_NUMBER || DistanceSquared > FMath::Square(GuardSightRadius))
	{
		return false;
	}

	FVector GuardViewDirection;
	if (!GetGuardViewDirection(GuardViewDirection))
	{
		return false;
	}

	const FVector DirectionToTarget = ToTarget.GetSafeNormal();
	const float MinimumDot = FMath::Cos(FMath::DegreesToRadians(SightHalfAngleDegrees));
	if (FVector::DotProduct(GuardViewDirection, DirectionToTarget) < MinimumDot)
	{
		return false;
	}

	UWorld* World = GetWorld();
	if (!IsValid(World))
	{
		return false;
	}

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(HelperRearGuardSight), false, this);
	QueryParams.AddIgnoredActor(this);
	QueryParams.AddIgnoredActor(GuardedPlayer.Get());

	TArray<AActor*> AllMannequins;
	UGameplayStatics::GetAllActorsOfClass(World, AMannequinAICharacter::StaticClass(), AllMannequins);
	for (AActor* OtherMannequin : AllMannequins)
	{
		if (IsValid(OtherMannequin) && OtherMannequin != TargetActor)
		{
			QueryParams.AddIgnoredActor(OtherMannequin);
		}
	}

	FHitResult HitResult;
	DiagnosticWork.AddLineTrace();
	const bool bHit = World->LineTraceSingleByChannel(
		HitResult,
		ViewOrigin,
		TargetActor->GetActorLocation(),
		ECC_GameTraceChannel1,
		QueryParams
	);

	return bHit && HitResult.GetActor() == TargetActor;
}

FVector AHelperRearGuardCharacter::GetGuardViewOrigin() const
{
	return GetActorLocation() + FVector(0.0f, 0.0f, BaseEyeHeight);
}

bool AHelperRearGuardCharacter::GetGuardViewDirection(FVector& OutViewDirection) const
{
	if (CurrentGuardViewDirection.IsNearlyZero())
	{
		return false;
	}

	OutViewDirection = CurrentGuardViewDirection;
	return true;
}

bool AHelperRearGuardCharacter::CalculateGuardViewDirection(FVector& OutViewDirection) const
{
	if (!GuardedPlayer.IsValid() || GuardSightRadius <= 0.0f)
	{
		return false;
	}

	FVector PlayerMovementDirection;
	if (!GetPlayerMovementDirection(PlayerMovementDirection))
	{
		return false;
	}

	const FVector CoverageCenterDirection = -PlayerMovementDirection;
	const float CoverageHalfAngleDegrees = GetEffectiveGuardSearchHalfAngleDegrees();
	const float SightHalfAngleDegrees = GetEffectiveGuardSightHalfAngleDegrees();
	if (!HasUsableGuardSight(SightHalfAngleDegrees))
	{
		return false;
	}
	const float CoverageMinimumDot = FMath::Cos(FMath::DegreesToRadians(CoverageHalfAngleDegrees));
	const FVector ViewOrigin = GetGuardViewOrigin();
	const float MaximumDistanceSquared = FMath::Square(GuardSightRadius);

	FVector ClosestDirection = FVector::ZeroVector;
	FVector SecondClosestDirection = FVector::ZeroVector;
	float ClosestDistanceSquared = TNumericLimits<float>::Max();
	float SecondClosestDistanceSquared = TNumericLimits<float>::Max();

	UWorld* World = GetWorld();
	if (!IsValid(World))
	{
		return false;
	}

	TArray<AActor*> Mannequins;
	UGameplayStatics::GetAllActorsOfClass(World, AMannequinAICharacter::StaticClass(), Mannequins);
	for (AActor* CandidateActor : Mannequins)
	{
		if (!IsValid(CandidateActor))
		{
			continue;
		}

		FVector ToCandidate = CandidateActor->GetActorLocation() - ViewOrigin;
		ToCandidate.Z = 0.0f;
		const float DistanceSquared = ToCandidate.SizeSquared();
		if (DistanceSquared <= KINDA_SMALL_NUMBER || DistanceSquared > MaximumDistanceSquared)
		{
			continue;
		}

		const FVector CandidateDirection = ToCandidate.GetSafeNormal();
		if (FVector::DotProduct(CoverageCenterDirection, CandidateDirection) < CoverageMinimumDot)
		{
			continue;
		}

		if (DistanceSquared < ClosestDistanceSquared)
		{
			SecondClosestDistanceSquared = ClosestDistanceSquared;
			SecondClosestDirection = ClosestDirection;
			ClosestDistanceSquared = DistanceSquared;
			ClosestDirection = CandidateDirection;
		}
		else if (DistanceSquared < SecondClosestDistanceSquared)
		{
			SecondClosestDistanceSquared = DistanceSquared;
			SecondClosestDirection = CandidateDirection;
		}
	}

	FVector DesiredViewDirection = CoverageCenterDirection;
	if (!ClosestDirection.IsNearlyZero())
	{
		DesiredViewDirection = ClosestDirection;
		if (!SecondClosestDirection.IsNearlyZero())
		{
			const float SeparationDegrees = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(
				FVector::DotProduct(ClosestDirection, SecondClosestDirection), -1.0f, 1.0f)));
			if (SeparationDegrees <= SightHalfAngleDegrees * 2.0f)
			{
				const FVector MidpointDirection = (ClosestDirection + SecondClosestDirection).GetSafeNormal2D();
				if (!MidpointDirection.IsNearlyZero())
				{
					DesiredViewDirection = MidpointDirection;
				}
			}
		}
	}

	OutViewDirection = ClampViewDirectionToCoverage(
		CoverageCenterDirection, DesiredViewDirection, CoverageHalfAngleDegrees, SightHalfAngleDegrees);
	return !OutViewDirection.IsNearlyZero();
}

void AHelperRearGuardCharacter::UpdateGuardViewDirection()
{
	FVector UpdatedViewDirection;
	if (CalculateGuardViewDirection(UpdatedViewDirection))
	{
		CurrentGuardViewDirection = UpdatedViewDirection;
	}
	else
	{
		CurrentGuardViewDirection = FVector::ZeroVector;
	}
}

bool AHelperRearGuardCharacter::IsGuardingPlayer(const APawn* PlayerPawn) const
{
	return IsValid(PlayerPawn) && GuardedPlayer.Get() == PlayerPawn;
}

bool AHelperRearGuardCharacter::GetFollowTargetLocation(FVector& OutFollowTarget) const
{
	if (!GuardedPlayer.IsValid())
	{
		return false;
	}

	if (bFollowDistanceLocked)
	{
		if (LockedRetreatTarget.IsNearlyZero())
		{
			ensureMsgf(false, TEXT("Helper %s has a follow-distance lock without a retreat target."), *GetName());
			return false;
		}

		OutFollowTarget = LockedRetreatTarget;
		return true;
	}

	FVector PlayerMovementDirection;
	if (!GetPlayerMovementDirection(PlayerMovementDirection))
	{
		return false;
	}

	const float DesiredFollowDistance = FMath::Max(0.0f, FollowDistance);
	OutFollowTarget = GuardedPlayer->GetActorLocation() - (PlayerMovementDirection * DesiredFollowDistance);
	OutFollowTarget.Z = GuardedPlayer->GetActorLocation().Z;
	return true;
}

float AHelperRearGuardCharacter::GetMaximumFollowAcceptanceRadius() const
{
	return FMath::Max(0.0f, FollowDistance - MinimumFollowSeparation);
}

float AHelperRearGuardCharacter::GetEffectiveGuardSightHalfAngleDegrees() const
{
	return FMath::Clamp(GetCurrentGuardSightHalfAngleDegrees(), 0.0f, GetEffectiveGuardSearchHalfAngleDegrees());
}

float AHelperRearGuardCharacter::GetCurrentGuardSightHalfAngleDegrees() const
{
	return FMath::Clamp(CurrentGuardHalfAngleDegrees, 0.0f, GetConfiguredGuardSightHalfAngleDegrees());
}

void AHelperRearGuardCharacter::ReduceCurrentGuardSightHalfAngle(const float Degrees)
{
	if (!HasAuthority() || Degrees <= 0.0f)
	{
		return;
	}
	CurrentGuardHalfAngleDegrees = FMath::Max(0.0f, GetCurrentGuardSightHalfAngleDegrees() - Degrees);
}

void AHelperRearGuardCharacter::RestoreCurrentGuardSightHalfAngle(const float Degrees)
{
	if (!HasAuthority() || Degrees <= 0.0f)
	{
		return;
	}
	CurrentGuardHalfAngleDegrees = FMath::Min(
		GetConfiguredGuardSightHalfAngleDegrees(),
		GetCurrentGuardSightHalfAngleDegrees() + Degrees);
}

float AHelperRearGuardCharacter::GetEffectiveGuardSearchHalfAngleDegrees() const
{
	return FMath::Clamp(GuardSearchHalfAngleDegrees, 0.0f, 180.0f);
}

bool AHelperRearGuardCharacter::IsGuardedPlayerStationary() const
{
	if (!GuardedPlayer.IsValid() || HasGuardedPlayerMovementInput())
	{
		return false;
	}

	FVector HorizontalVelocity = GuardedPlayer->GetVelocity();
	HorizontalVelocity.Z = 0.0f;
	return HorizontalVelocity.IsNearlyZero();
}

bool AHelperRearGuardCharacter::HasGuardedPlayerMovementInput() const
{
	const ACharacter* GuardedCharacter = Cast<ACharacter>(GuardedPlayer.Get());
	const UCharacterMovementComponent* Movement = IsValid(GuardedCharacter)
		? GuardedCharacter->GetCharacterMovement()
		: nullptr;
	return IsValid(Movement) && !Movement->GetCurrentAcceleration().GetSafeNormal2D().IsNearlyZero();
}

void AHelperRearGuardCharacter::UpdateVisionRetreatState()
{
	if (!GuardedPlayer.IsValid())
	{
		ResetVisionDirectionChangeTracking();
		bFollowDistanceLocked = false;
		LockedRetreatTarget = FVector::ZeroVector;
		return;
	}

	if (bFollowDistanceLocked)
	{
		// 밀침으로 생긴 속도가 아니라 플레이어의 실제 이동 입력만 잠금 해제 조건으로 쓴다.
		if (HasGuardedPlayerMovementInput())
		{
			bFollowDistanceLocked = false;
			LockedRetreatTarget = FVector::ZeroVector;
			ResetVisionDirectionChangeTracking();
		}
		return;
	}

	if (!IsGuardedPlayerStationary())
	{
		ResetVisionDirectionChangeTracking();
		return;
	}

	UWorld* World = GetWorld();
	if (!IsValid(World))
	{
		return;
	}

	FVector CurrentViewDirection;
	if (!GetGuardViewDirection(CurrentViewDirection))
	{
		return;
	}

	if (LastRecordedGuardViewDirection.IsNearlyZero())
	{
		LastRecordedGuardViewDirection = CurrentViewDirection;
		return;
	}

	const float DirectionChangeDegrees = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(
		FVector::DotProduct(LastRecordedGuardViewDirection, CurrentViewDirection), -1.0f, 1.0f)));
	if (DirectionChangeDegrees >= VisionDirectionChangeThresholdDegrees)
	{
		LastRecordedGuardViewDirection = CurrentViewDirection;
		VisionDirectionChangeTimes.Add(World->GetTimeSeconds());
	}

	const double WindowStartTime = World->GetTimeSeconds() - FMath::Max(0.0f, VisionDirectionChangeWindowSeconds);
	VisionDirectionChangeTimes.RemoveAll([WindowStartTime](const double ChangeTime)
	{
		return ChangeTime < WindowStartTime;
	});

	if (VisionDirectionChangeTimes.Num() >= FMath::Max(1, VisionDirectionChangeRequiredCount))
	{
		BeginVisionRetreat(CurrentViewDirection);
	}
}

void AHelperRearGuardCharacter::BeginVisionRetreat(const FVector& CurrentViewDirection)
{
	const FVector SafeViewDirection = CurrentViewDirection.GetSafeNormal2D();
	const float RetreatDistance = FMath::Max(0.0f, MinimumFollowSeparation) * 2.0f;
	if (SafeViewDirection.IsNearlyZero() || RetreatDistance <= KINDA_SMALL_NUMBER || bFollowDistanceLocked)
	{
		return;
	}

	LockedRetreatTarget = GetActorLocation() - SafeViewDirection * RetreatDistance;
	LockedRetreatTarget.Z = GetActorLocation().Z;
	bFollowDistanceLocked = true;
	bRetreatPushConsumed = false;
	bRetreatContactConfirmed = false;
	PreviousRetreatLocation = GetActorLocation();
	bRetreatLocationInitialized = true;
	ResetVisionDirectionChangeTracking();

	if (AHelperRearGuardAIController* HelperController = Cast<AHelperRearGuardAIController>(GetController()))
	{
		HelperController->StopMovement();
	}
}

bool AHelperRearGuardCharacter::CanApplyRetreatPushToPlayer(const APlayerCharacter* Player) const
{
	return HasAuthority() && IsValid(Player) && GuardedPlayer.Get() == Player &&
		bFollowDistanceLocked && !bRetreatPushConsumed && bRetreatContactConfirmed;
}

void AHelperRearGuardCharacter::UpdateRetreatContact()
{
	bRetreatContactConfirmed = false;
	APlayerCharacter* Player = Cast<APlayerCharacter>(GuardedPlayer.Get());
	UCapsuleComponent* HelperCapsule = GetCapsuleComponent();
	UCapsuleComponent* PlayerCapsule = IsValid(Player) ? Player->GetCapsuleComponent() : nullptr;
	if (!HasAuthority() || !bFollowDistanceLocked || bRetreatPushConsumed || !IsValid(PlayerCapsule) ||
		!IsValid(HelperCapsule) || !PlayerCapsule->IsQueryCollisionEnabled() ||
		!HelperCapsule->IsQueryCollisionEnabled() || Player->IsGameOver())
	{
		bRetreatLocationInitialized = false;
		return;
	}
	const FVector CurrentLocation = GetActorLocation();
	if (!bRetreatLocationInitialized)
	{
		PreviousRetreatLocation = CurrentLocation;
		bRetreatLocationInitialized = true;
		return;
	}
	const FVector PreviousLocation = PreviousRetreatLocation;
	PreviousRetreatLocation = CurrentLocation;
	const FVector ActualMovement = CurrentLocation - PreviousLocation;
	// No damage for merely planning a route, standing nearby, another floor, or teleport/load.
	if (ActualMovement.SizeSquared2D() <= FMath::Square(0.1f) || ActualMovement.SizeSquared() > FMath::Square(1000.0f))
	{
		return;
	}
	bRetreatContactConfirmed = CapsulesTouch(HelperCapsule, PlayerCapsule);
	if (!bRetreatContactConfirmed)
	{
		// Sweep ONLY the short distance already travelled this frame. Never the future NavMesh route.
		FHitResult Contact;
		bRetreatContactConfirmed = PlayerCapsule->SweepComponent(Contact, PreviousLocation, CurrentLocation,
			HelperCapsule->GetComponentQuat(), FCollisionShape::MakeCapsule(
				HelperCapsule->GetScaledCapsuleRadius(), HelperCapsule->GetScaledCapsuleHalfHeight()));
	}
	if (!CanApplyRetreatPushToPlayer(Player)) return;
	const UCharacterMovementComponent* Movement = GetCharacterMovement();
	const float PushSpeed = IsValid(Movement) ? FMath::Max(1.0f, Movement->MaxWalkSpeed) : 600.0f;
	const FString HelperName = GetName();
	if (Player->HandlePartnerPushDeath(this))
	{
		bRetreatPushConsumed = true;
		bRetreatContactConfirmed = false;
		Player->LaunchCharacter(ActualMovement.GetSafeNormal2D() * PushSpeed, true, false);
		UE_LOG(LogTemp, Warning, TEXT("Helper %s pushed player %s after actual retreat capsule contact."), *HelperName, *Player->GetName());
	}
}

void AHelperRearGuardCharacter::ResetVisionDirectionChangeTracking()
{
	LastRecordedGuardViewDirection = FVector::ZeroVector;
	VisionDirectionChangeTimes.Reset();
}

bool AHelperRearGuardCharacter::RefreshGuardedPlayer()
{
	UWorld* World = GetWorld();
	if (!IsValid(World))
	{
		GuardedPlayer.Reset();
		LastPlayerMovementDirection = FVector::ZeroVector;
		CurrentGuardViewDirection = FVector::ZeroVector;
		return false;
	}

	APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(World, 0);
	if (!IsValid(PlayerPawn))
	{
		GuardedPlayer.Reset();
		LastPlayerMovementDirection = FVector::ZeroVector;
		CurrentGuardViewDirection = FVector::ZeroVector;
		return false;
	}

	if (GuardedPlayer.Get() != PlayerPawn)
	{
		LastPlayerMovementDirection = FVector::ZeroVector;
		CurrentGuardViewDirection = FVector::ZeroVector;
		bFollowDistanceLocked = false;
		LockedRetreatTarget = FVector::ZeroVector;
		ResetVisionDirectionChangeTracking();
	}

	GuardedPlayer = PlayerPawn;
	return true;
}

bool AHelperRearGuardCharacter::GetPlayerMovementDirection(FVector& OutMovementDirection) const
{
	if (!GuardedPlayer.IsValid())
	{
		return false;
	}

	FVector CurrentMovementDirection = GuardedPlayer->GetVelocity();
	CurrentMovementDirection.Z = 0.0f;
	if (!CurrentMovementDirection.IsNearlyZero())
	{
		OutMovementDirection = CurrentMovementDirection.GetSafeNormal();
		return true;
	}

	if (!LastPlayerMovementDirection.IsNearlyZero())
	{
		OutMovementDirection = LastPlayerMovementDirection.GetSafeNormal();
		return true;
	}

	return false;
}

void AHelperRearGuardCharacter::UpdateLastPlayerMovementDirection()
{
	if (!GuardedPlayer.IsValid())
	{
		return;
	}

	FVector CurrentMovementDirection = GuardedPlayer->GetVelocity();
	CurrentMovementDirection.Z = 0.0f;
	if (!CurrentMovementDirection.IsNearlyZero())
	{
		LastPlayerMovementDirection = CurrentMovementDirection.GetSafeNormal();
	}
}

void AHelperRearGuardCharacter::DrawGuardDebug() const
{
	if (!bDrawGuardDebug || GuardSightRadius <= 0.0f)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!IsValid(World))
	{
		return;
	}

	FVector PlayerMovementDirection;
	if (!GetPlayerMovementDirection(PlayerMovementDirection))
	{
		return;
	}

	const FVector CoverageCenterDirection = -PlayerMovementDirection;
	FVector GuardViewDirection;
	if (!GetGuardViewDirection(GuardViewDirection))
	{
		return;
	}

	const FVector DrawOrigin = GetActorLocation() + FVector(0.0f, 0.0f, 10.0f);
	constexpr int32 ArcSegments = 20;
	const auto DrawSightArc = [World, DrawOrigin, this](const FVector& CenterDirection, const float HalfAngleDegrees, const FColor& Color, const float Thickness)
	{
		FVector PreviousPoint = FVector::ZeroVector;
		for (int32 SegmentIndex = 0; SegmentIndex <= ArcSegments; ++SegmentIndex)
		{
			const float Alpha = static_cast<float>(SegmentIndex) / static_cast<float>(ArcSegments);
			const FVector Direction = CenterDirection.RotateAngleAxis(
				FMath::Lerp(-HalfAngleDegrees, HalfAngleDegrees, Alpha), FVector::UpVector).GetSafeNormal2D();
			const FVector CurrentPoint = DrawOrigin + Direction * GuardSightRadius;

			if (SegmentIndex > 0)
			{
				DrawDebugLine(World, PreviousPoint, CurrentPoint, Color, false, 0.0f, 0, Thickness);
			}

			if (SegmentIndex == 0 || SegmentIndex == ArcSegments || SegmentIndex == ArcSegments / 2)
			{
				DrawDebugLine(World, DrawOrigin, CurrentPoint, Color, false, 0.0f, 0, Thickness);
			}

			PreviousPoint = CurrentPoint;
		}
	};

	DrawSightArc(CoverageCenterDirection, GetEffectiveGuardSearchHalfAngleDegrees(), FColor::Cyan, 1.5f);
	DrawSightArc(GuardViewDirection, GetEffectiveGuardSightHalfAngleDegrees(), FColor::Yellow, 3.0f);
	DrawDebugLine(World, DrawOrigin, DrawOrigin + GuardViewDirection * GuardSightRadius, FColor::Green, false, 0.0f, 0, 4.0f);
	if (bFollowDistanceLocked)
	{
		DrawDebugLine(World, DrawOrigin, LockedRetreatTarget, FColor::Orange, false, 0.0f, 0, 3.0f);
	}
}
