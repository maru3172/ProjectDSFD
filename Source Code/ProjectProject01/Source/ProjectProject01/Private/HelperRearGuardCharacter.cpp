// File: Source/ProjectProject01/Private/HelperRearGuardCharacter.cpp
// Target: ProjectProject01Editor Win64 DebugGame, Unreal Engine 5.8

#include "HelperRearGuardCharacter.h"

#include "Components/CapsuleComponent.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "HelperRearGuardAIController.h"
#include "Kismet/GameplayStatics.h"
#include "MannequinAICharacter.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "PlayerCharacter.h"
#include "ProjectProject01TuningData.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#endif

namespace
{
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
	return true;
}
#endif

AHelperRearGuardCharacter::AHelperRearGuardCharacter()
{
	PrimaryActorTick.bCanEverTick = true;

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

void AHelperRearGuardCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!GuardedPlayer.IsValid())
	{
		RefreshGuardedPlayer();
	}

	UpdateLastPlayerMovementDirection();
	// 커버 중심은 마지막 플레이어 이동 방향을 사용하지만, 실제 시야는 정지 중에도 매 틱 갱신한다.
	UpdateGuardViewDirection();
	if (HasAuthority())
	{
		UpdateVisionRetreatState();
	}

	DrawGuardDebug();
}

bool AHelperRearGuardCharacter::CanObserveActor(const AActor* TargetActor) const
{
	if (!IsValid(TargetActor) || !GuardedPlayer.IsValid() || GuardSightRadius <= 0.0f)
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
	const float MinimumDot = FMath::Cos(FMath::DegreesToRadians(GetEffectiveGuardSightHalfAngleDegrees()));
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
	return FMath::Clamp(GuardHalfAngleDegrees, 0.0f, GetEffectiveGuardSearchHalfAngleDegrees());
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
	ResetVisionDirectionChangeTracking();

	if (IsGuardedPlayerOnRetreatPath(LockedRetreatTarget))
	{
		if (APlayerCharacter* PlayerCharacter = Cast<APlayerCharacter>(GuardedPlayer.Get()); IsValid(PlayerCharacter))
		{
			const UCharacterMovementComponent* Movement = GetCharacterMovement();
			const float PushSpeed = IsValid(Movement) ? FMath::Max(1.0f, Movement->MaxWalkSpeed) : 600.0f;
			PlayerCharacter->LaunchCharacter(-SafeViewDirection * PushSpeed, true, false);
			if (PlayerCharacter->HandlePartnerPushDeath(this))
			{
				UE_LOG(LogTemp, Warning, TEXT("Helper %s pushed player %s during vision retreat."), *GetName(), *PlayerCharacter->GetName());
			}
		}
	}

	if (AHelperRearGuardAIController* HelperController = Cast<AHelperRearGuardAIController>(GetController()))
	{
		HelperController->StopMovement();
	}
}

bool AHelperRearGuardCharacter::IsGuardedPlayerOnRetreatPath(const FVector& RetreatTarget) const
{
	const APlayerCharacter* PlayerCharacter = Cast<APlayerCharacter>(GuardedPlayer.Get());
	const UCapsuleComponent* HelperCapsule = GetCapsuleComponent();
	const UCapsuleComponent* PlayerCapsule = IsValid(PlayerCharacter) ? PlayerCharacter->GetCapsuleComponent() : nullptr;
	UWorld* World = GetWorld();
	if (!IsValid(PlayerCharacter) || !IsValid(HelperCapsule) || !IsValid(PlayerCapsule) || !IsValid(World))
	{
		return false;
	}

	UNavigationPath* NavigationPath = UNavigationSystemV1::FindPathToLocationSynchronously(
		World, GetActorLocation(), RetreatTarget, const_cast<AHelperRearGuardCharacter*>(this));
	if (!IsValid(NavigationPath) || !NavigationPath->IsValid() || NavigationPath->PathPoints.Num() < 2)
	{
		return false;
	}

	FVector PlayerLocation = PlayerCharacter->GetActorLocation();
	PlayerLocation.Z = 0.0f;
	const float CombinedCapsuleRadius = HelperCapsule->GetScaledCapsuleRadius() + PlayerCapsule->GetScaledCapsuleRadius();
	for (int32 PointIndex = 1; PointIndex < NavigationPath->PathPoints.Num(); ++PointIndex)
	{
		FVector SegmentStart = NavigationPath->PathPoints[PointIndex - 1];
		FVector SegmentEnd = NavigationPath->PathPoints[PointIndex];
		SegmentStart.Z = 0.0f;
		SegmentEnd.Z = 0.0f;
		if (FMath::PointDistToSegmentSquared(PlayerLocation, SegmentStart, SegmentEnd) <= FMath::Square(CombinedCapsuleRadius))
		{
			return true;
		}
	}

	return false;
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
