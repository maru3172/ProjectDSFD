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
	RefreshGuardedPlayer();
}

void AHelperRearGuardCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!GuardedPlayer.IsValid())
	{
		RefreshGuardedPlayer();
	}

	UpdateLastPlayerMovementDirection();

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

	FVector PlayerMovementDirection;
	if (!GetPlayerMovementDirection(PlayerMovementDirection))
	{
		return false;
	}

	const FVector RearDirection = -PlayerMovementDirection;
	const FVector DirectionToTarget = ToTarget.GetSafeNormal();
	const float MinimumDot = FMath::Cos(FMath::DegreesToRadians(GuardHalfAngleDegrees));
	if (FVector::DotProduct(RearDirection, DirectionToTarget) < MinimumDot)
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

bool AHelperRearGuardCharacter::GetFollowTargetLocation(FVector& OutFollowTarget) const
{
	if (!GuardedPlayer.IsValid())
	{
		return false;
	}

	FVector PlayerMovementDirection;
	if (!GetPlayerMovementDirection(PlayerMovementDirection))
	{
		return false;
	}

	const float DesiredFollowDistance = FMath::Max(FollowDistance, MinimumFollowSeparation);
	OutFollowTarget = GuardedPlayer->GetActorLocation() - (PlayerMovementDirection * DesiredFollowDistance);
	OutFollowTarget.Z = GuardedPlayer->GetActorLocation().Z;
	return true;
}

float AHelperRearGuardCharacter::GetMaximumFollowAcceptanceRadius() const
{
	return FMath::Max(0.0f, FMath::Max(FollowDistance, MinimumFollowSeparation) - MinimumFollowSeparation);
}

bool AHelperRearGuardCharacter::RefreshGuardedPlayer()
{
	UWorld* World = GetWorld();
	if (!IsValid(World))
	{
		GuardedPlayer.Reset();
		LastPlayerMovementDirection = FVector::ZeroVector;
		return false;
	}

	APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(World, 0);
	if (!IsValid(PlayerPawn))
	{
		GuardedPlayer.Reset();
		LastPlayerMovementDirection = FVector::ZeroVector;
		return false;
	}

	if (GuardedPlayer.Get() != PlayerPawn)
	{
		LastPlayerMovementDirection = FVector::ZeroVector;
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

	const FVector RearDirection = -PlayerMovementDirection;

	const FVector RightDirection = FVector::CrossProduct(FVector::UpVector, RearDirection).GetSafeNormal();
	const FVector DrawOrigin = GetActorLocation() + FVector(0.0f, 0.0f, 10.0f);
	constexpr int32 ArcSegments = 20;

	FVector PreviousPoint = FVector::ZeroVector;
	for (int32 SegmentIndex = 0; SegmentIndex <= ArcSegments; ++SegmentIndex)
	{
		const float Alpha = static_cast<float>(SegmentIndex) / static_cast<float>(ArcSegments);
		const float AngleDegrees = FMath::Lerp(-GuardHalfAngleDegrees, GuardHalfAngleDegrees, Alpha);
		const float AngleRadians = FMath::DegreesToRadians(AngleDegrees);
		const FVector Direction =
			(RearDirection * FMath::Cos(AngleRadians) + RightDirection * FMath::Sin(AngleRadians)).GetSafeNormal();
		const FVector CurrentPoint = DrawOrigin + Direction * GuardSightRadius;

		if (SegmentIndex > 0)
		{
			DrawDebugLine(World, PreviousPoint, CurrentPoint, FColor::Yellow, false, 0.0f, 0, 2.0f);
		}

		if (SegmentIndex == 0 || SegmentIndex == ArcSegments / 2 || SegmentIndex == ArcSegments)
		{
			DrawDebugLine(World, DrawOrigin, CurrentPoint, FColor::Yellow, false, 0.0f, 0, 1.5f);
		}

		PreviousPoint = CurrentPoint;
	}
}
