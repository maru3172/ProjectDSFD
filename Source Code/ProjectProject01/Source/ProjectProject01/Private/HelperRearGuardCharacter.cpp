// File: Source/ProjectProject01/Private/HelperRearGuardCharacter.cpp
// Target: ProjectProject01Editor Win64, Unreal Engine 5.8

#include "HelperRearGuardCharacter.h"

#include "Components/CapsuleComponent.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "Kismet/GameplayStatics.h"
#include "WeepingAngelCharacter.h"

AHelperRearGuardCharacter::AHelperRearGuardCharacter()
{
	PrimaryActorTick.bCanEverTick = true;

	if (UCapsuleComponent* HelperCapsule = GetCapsuleComponent())
	{
		HelperCapsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}

	if (UCharacterMovementComponent* HelperCharacterMovement = GetCharacterMovement())
	{
		HelperCharacterMovement->DisableMovement();
	}
}

void AHelperRearGuardCharacter::BeginPlay()
{
	Super::BeginPlay();

	RefreshGuardedPlayer();
	UpdateFollowTransform();
}

void AHelperRearGuardCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!GuardedPlayer.IsValid())
	{
		RefreshGuardedPlayer();
	}

	UpdateFollowTransform();
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

	const FVector RearDirection = -GetPlayerViewForward();
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

	TArray<AActor*> AllAngels;
	UGameplayStatics::GetAllActorsOfClass(World, AWeepingAngelCharacter::StaticClass(), AllAngels);

	for (AActor* OtherAngel : AllAngels)
	{
		if (IsValid(OtherAngel) && OtherAngel != TargetActor)
		{
			QueryParams.AddIgnoredActor(OtherAngel);
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

bool AHelperRearGuardCharacter::RefreshGuardedPlayer()
{
	UWorld* World = GetWorld();
	if (!IsValid(World))
	{
		GuardedPlayer.Reset();
		return false;
	}

	APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(World, 0);
	if (!IsValid(PlayerPawn))
	{
		GuardedPlayer.Reset();
		return false;
	}

	GuardedPlayer = PlayerPawn;
	return true;
}

FVector AHelperRearGuardCharacter::GetPlayerViewForward() const
{
	if (!GuardedPlayer.IsValid())
	{
		return GetActorForwardVector().GetSafeNormal2D();
	}

	if (const AController* PlayerController = GuardedPlayer->GetController())
	{
		const FVector ControlForward = PlayerController->GetControlRotation().Vector().GetSafeNormal2D();
		if (!ControlForward.IsNearlyZero())
		{
			return ControlForward;
		}
	}

	return GuardedPlayer->GetActorForwardVector().GetSafeNormal2D();
}

void AHelperRearGuardCharacter::UpdateFollowTransform()
{
	if (!GuardedPlayer.IsValid())
	{
		return;
	}

	const FVector PlayerForward = GetPlayerViewForward();
	if (PlayerForward.IsNearlyZero())
	{
		return;
	}

	FVector DesiredLocation = GuardedPlayer->GetActorLocation() - (PlayerForward * FollowDistance);
	DesiredLocation.Z = GuardedPlayer->GetActorLocation().Z;

	SetActorLocation(DesiredLocation, false, nullptr, ETeleportType::TeleportPhysics);
	SetActorRotation(PlayerForward.Rotation(), ETeleportType::TeleportPhysics);
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

	const FVector RearDirection = -GetPlayerViewForward();
	if (RearDirection.IsNearlyZero())
	{
		return;
	}

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
