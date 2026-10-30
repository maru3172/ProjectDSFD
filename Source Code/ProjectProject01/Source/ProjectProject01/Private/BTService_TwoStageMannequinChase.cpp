// Fill out your copyright notice in the Description page of Project Settings.


#include "BTService_TwoStageMannequinChase.h"

#include "AIController.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Kismet/GameplayStatics.h"
#include "NavigationSystem.h"

#include "HelperRearGuardCharacter.h"
#include "MannequinAICharacter.h"
#include "PlayerCharacter.h"

UBTService_TwoStageMannequinChase::UBTService_TwoStageMannequinChase()
{
	NodeName = TEXT("Two Stage Mannequin Chase");
	
	// 각 마네킹이 독립된 서비스 상태를 갖도록 한다.
	bCreateNodeInstance = true;
	
	// 플레이어 시야에 들어온 프레임에 바로 정지하도록 매 프레임 검사한다.
	Interval = 0.0f;
	RandomDeviation = 0.0f;
}

void UBTService_TwoStageMannequinChase::TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	Super::TickNode(OwnerComp, NodeMemory, DeltaSeconds);

	AAIController* AIController = OwnerComp.GetAIOwner();
	if (!IsValid(AIController))
	{
		return;
	}

	AMannequinAICharacter* CurrentMannequin = Cast<AMannequinAICharacter>(AIController->GetPawn());
	if (!IsValid(CurrentMannequin))
	{
		return;
	}

	UBlackboardComponent* Blackboard = OwnerComp.GetBlackboardComponent();
	if (!IsValid(Blackboard))
	{
		return;
	}

	APlayerCharacter* Player = Cast<APlayerCharacter>(UGameplayStatics::GetActorOfClass(GetWorld(), APlayerCharacter::StaticClass()));
	// 플레이어가 없으면 모든 추적 상태를 제거한다.
	if (!IsValid(Player))
	{
		AIController->StopMovement();
		CurrentMannequin->SetFrozen(true);
		Blackboard->SetValueAsBool(TEXT("PlayerLookingAtMannequin"), false);
		Blackboard->ClearValue(TEXT("TargetActor"));
		Blackboard->ClearValue(TEXT("RoamingLocation"));
		Blackboard->SetValueAsInt(TEXT("ChaseTier"),static_cast<int32>(ETwoStageMannequinTier::Roaming));

		return;
	}

	TArray<AActor*> AllMannequins;

	UGameplayStatics::GetAllActorsOfClass(GetWorld(), AMannequinAICharacter::StaticClass(), AllMannequins);

	const bool bObservedByPlayerOrHelper = IsObservedByPlayerOrHelper(*Player, *CurrentMannequin, AllMannequins);

	Blackboard->SetValueAsBool(TEXT("PlayerLookingAtMannequin"), bObservedByPlayerOrHelper);

	if (bObservedByPlayerOrHelper)
	{
		// 플레이어 또는 조력자 시야에 들어온 즉시 현재 이동 요청을 중단한다.
		AIController->StopMovement();
		// 현재 재생 중인 애니메이션을 현재 프레임에서 그대로 정지한다.
		CurrentMannequin->SetFrozen(true);
		return;
	}

	// 플레이어가 마네킹을 보고 있지 않으므로 애니메이션 정지를 해제한다.
	CurrentMannequin->SetFrozen(false);

	const ETwoStageMannequinTier Tier = DetermineChaseTier(*CurrentMannequin, *Player, AllMannequins);

	switch (Tier)
	{
	case ETwoStageMannequinTier::Primary:
	case ETwoStageMannequinTier::Secondary:
		ApplyChaseState(*Blackboard, *Player, Tier);
		break;

	case ETwoStageMannequinTier::Roaming:
	default:
		ApplyRoamingState(*Blackboard, *CurrentMannequin);
		break;
	}
}

bool UBTService_TwoStageMannequinChase::IsObservedByPlayerOrHelper(const APlayerCharacter& Player, AMannequinAICharacter& CurrentMannequin, const TArray<AActor*>& AllMannequins) const
{
	APlayerController* PlayerController = Cast<APlayerController>(Player.GetController());
	if (!IsValid(PlayerController))
	{
		return false;
	}

	FVector CameraLocation = FVector::ZeroVector;
	FRotator CameraRotation = FRotator::ZeroRotator;
	PlayerController->GetPlayerViewPoint(CameraLocation, CameraRotation);

	if (CameraLocation.ContainsNaN() || CameraRotation.ContainsNaN())
	{
		return false;
	}

	APlayerCameraManager* CameraManager = PlayerController->PlayerCameraManager;
	const float CameraFOVDegrees = IsValid(CameraManager) ? CameraManager->GetFOVAngle() : 90.0f;
	const float ExpandedHalfFOVDegrees = FMath::Max(0.0f, CameraFOVDegrees * 0.5f * 1.15f);
	const float ViewConeMinimumDot = FMath::Cos(FMath::DegreesToRadians(ExpandedHalfFOVDegrees));
	const FVector CameraForward = CameraRotation.Vector().GetSafeNormal();

	UCapsuleComponent* MannequinCapsule = CurrentMannequin.GetCapsuleComponent();
	if (!IsValid(MannequinCapsule))
	{
		return false;
	}

	USkeletalMeshComponent* MannequinMesh = CurrentMannequin.GetMesh();
	if (!IsValid(MannequinMesh))
	{
		return false;
	}

	const FVector CapsuleCenter = MannequinCapsule->GetComponentLocation();
	const float HalfHeight = MannequinCapsule->GetScaledCapsuleHalfHeight();
	const float Radius = MannequinCapsule->GetScaledCapsuleRadius();
	const float DetectionMargin = 30.0f;
	const float DetectionRadius = Radius + DetectionMargin;
	const float DetectionHalfHeight = HalfHeight + DetectionMargin;
	const FVector Up = MannequinCapsule->GetUpVector();
	const FVector Right = MannequinCapsule->GetRightVector();
	const FVector Forward = MannequinCapsule->GetForwardVector();

	const FName BoneNames[] =
	{
		TEXT("head"),
		TEXT("pelvis"),
		TEXT("hand_l"),
		TEXT("hand_r"),
		TEXT("foot_l"),
		TEXT("foot_r"),
	};

	TArray<FVector> Points;
	Points.Add(CapsuleCenter);
	Points.Add(CapsuleCenter - Up);
	Points.Add(CapsuleCenter + Up);
	Points.Add(CapsuleCenter - Right * Radius);
	Points.Add(CapsuleCenter + Right * Radius);
	Points.Add(CapsuleCenter - Forward * Radius);
	Points.Add(CapsuleCenter + Forward * Radius);

	for (const FName& BoneName : BoneNames)
	{
		if (MannequinMesh->GetBoneIndex(BoneName) != INDEX_NONE)
		{
			Points.Add(MannequinMesh->GetBoneLocation(BoneName));
		}
	}

	bool bInScreen = false;

	FCollisionQueryParams BodyQueryParams;
	BodyQueryParams.AddIgnoredActor(&Player);
	for (AActor* OtherMannequin : AllMannequins)
	{
		if (OtherMannequin != &CurrentMannequin)
		{
			BodyQueryParams.AddIgnoredActor(OtherMannequin);
		}
	}

	for (const FVector& Point : Points)
	{
		const FVector CameraToPoint = (Point - CameraLocation).GetSafeNormal();
		if (CameraToPoint.IsNearlyZero() || FVector::DotProduct(CameraForward, CameraToPoint) < ViewConeMinimumDot)
		{
			continue;
		}

		FHitResult HitResult;
		const bool bHit = GetWorld()->LineTraceSingleByChannel(HitResult, CameraLocation, Point, ECC_GameTraceChannel1, BodyQueryParams);
		if (bHit && HitResult.GetActor() == &CurrentMannequin)
		{
			bInScreen = true;
			break;
		}
	}

	if (!bInScreen)
	{
		TArray<FVector> GuardPoints;
		const FVector GuardDirections[] =
		{
			Right, -Right, Forward, -Forward,
			(Right + Forward).GetSafeNormal(),
			(Right - Forward).GetSafeNormal(),
			(-Right + Forward).GetSafeNormal(),
			(-Right - Forward).GetSafeNormal()
		};
		const float CylinderHalfHeight = FMath::Max(HalfHeight - Radius, 0.0f);
		const float GuardHeights[] = { -CylinderHalfHeight, 0.0f, CylinderHalfHeight };

		for (const float Height : GuardHeights)
		{
			for (const FVector& Direction : GuardDirections)
			{
				GuardPoints.Add(CapsuleCenter + Up * Height + Direction * DetectionRadius);
			}
		}

		GuardPoints.Add(CapsuleCenter + Up * DetectionHalfHeight);
		GuardPoints.Add(CapsuleCenter - Up * DetectionHalfHeight);

		for (const FName& BoneName : BoneNames)
		{
			if (MannequinMesh->GetBoneIndex(BoneName) != INDEX_NONE)
			{
				const FVector BoneLocation = MannequinMesh->GetBoneLocation(BoneName);
				GuardPoints.Add(BoneLocation + Right * DetectionMargin);
				GuardPoints.Add(BoneLocation - Right * DetectionMargin);
				GuardPoints.Add(BoneLocation + Forward * DetectionMargin);
				GuardPoints.Add(BoneLocation - Forward * DetectionMargin);
			}
		}

		FCollisionQueryParams GuardQueryParams;
		GuardQueryParams.AddIgnoredActor(&Player);
		GuardQueryParams.AddIgnoredActor(&CurrentMannequin);
		for (AActor* OtherMannequin : AllMannequins)
		{
			if (OtherMannequin != &CurrentMannequin)
			{
				GuardQueryParams.AddIgnoredActor(OtherMannequin);
			}
		}

		for (const FVector& GuardPoint : GuardPoints)
		{
			const FVector CameraToGuardPoint = (GuardPoint - CameraLocation).GetSafeNormal();
			if (CameraToGuardPoint.IsNearlyZero() || FVector::DotProduct(CameraForward, CameraToGuardPoint) < ViewConeMinimumDot)
			{
				continue;
			}

			FHitResult GuardHit;
			const bool bGuardBlocked = GetWorld()->LineTraceSingleByChannel(GuardHit, CameraLocation, GuardPoint, ECC_GameTraceChannel1, GuardQueryParams);
			if (!bGuardBlocked)
			{
				bInScreen = true;
				break;
			}
		}
	}

	AHelperRearGuardCharacter* HelperRearGuard = Cast<AHelperRearGuardCharacter>(UGameplayStatics::GetActorOfClass(GetWorld(), AHelperRearGuardCharacter::StaticClass()));
	const bool bInHelperRearGuardSight = IsValid(HelperRearGuard) && HelperRearGuard->CanObserveActor(&CurrentMannequin);

	return bInScreen || bInHelperRearGuardSight;
}

ETwoStageMannequinTier UBTService_TwoStageMannequinChase::DetermineChaseTier(const AMannequinAICharacter& CurrentMannequin, const APlayerCharacter& Player, const TArray<AActor*>& AllMannequins) const
{
	const FVector CurrentLocation = CurrentMannequin.GetActorLocation();
	const FVector PlayerLocation = Player.GetActorLocation();
	const double PrimaryRadiusSquared = FMath::Square(static_cast<double>(PlayerPrimaryRadius));
	const double PropagationRadiusSquared = FMath::Square(static_cast<double>(PrimaryPropagationRadius));
	
	// 첫 번째 검사: 현재 마네킹이 플레이어 반경 안에 있으면 1 차 마네킹으로 지정
	const double CurrentDistanceToPlayerSquared = FVector::DistSquared2D(CurrentLocation, PlayerLocation);
	if (CurrentDistanceToPlayerSquared <= PrimaryRadiusSquared)
	{
		return ETwoStageMannequinTier::Primary;
	}
	
	// 두 번째 검사: 현재 마네킹 주변에 1 차 마네킹이 있는지 검사한다.
	for (AActor* MannequinActor : AllMannequins)
	{
		const AMannequinAICharacter* OtherMannequin = Cast<AMannequinAICharacter>(MannequinActor);

		if (!IsValid(OtherMannequin))
		{
			continue;
		}

		// 자기 자신은 검사하지 않는다.
		if (OtherMannequin == &CurrentMannequin)
		{
			continue;
		}

		const FVector OtherLocation = OtherMannequin->GetActorLocation();
		
		// 다른 층의 마네킹이 전파원이 되는 것을 방지한다. 
		const float HeightDifference = FMath::Abs(CurrentLocation.Z - OtherLocation.Z);

		if (HeightDifference > MaximumHeightDifference)
		{
			continue;
		}
		
		// 이 마네킹이 추적 중인지 검사하는 것이 아니다. 
		// 반드시 플레이어 반경 안에 있는지를 직접 검사해서 1 차 마네킹인지 판단해야 한다.
		const double OtherDistanceToPlayerSquared = FVector::DistSquared2D(OtherLocation, PlayerLocation);

		const bool bOtherIsPrimary = OtherDistanceToPlayerSquared <= PrimaryRadiusSquared;

		if (!bOtherIsPrimary)
		{
			continue;
		}
		
		// 현재 마네킹이 1차 마네킹의 전파 반경 안에 있는지 검사한다. 
		const double DistanceToPrimarySquared = FVector::DistSquared2D(CurrentLocation, OtherLocation);

		if (DistanceToPrimarySquared <= PropagationRadiusSquared)
		{
			return ETwoStageMannequinTier::Secondary;
		}
	}
	
	return ETwoStageMannequinTier::Roaming;
}

void UBTService_TwoStageMannequinChase::ApplyChaseState(UBlackboardComponent& Blackboard, APlayerCharacter& Player, ETwoStageMannequinTier Tier)
{
	Blackboard.SetValueAsObject(TEXT("TargetActor"), &Player);
	Blackboard.ClearValue(TEXT("RoamingLocation"));
	Blackboard.SetValueAsBool(TEXT("RoamingSequenceCompleted"), false);
	Blackboard.SetValueAsInt(TEXT("ChaseTier"), static_cast<int32>(Tier));
}

bool UBTService_TwoStageMannequinChase::TryFindRoamingLocation(const AMannequinAICharacter& CurrentMannequin, FVector& OutLocation) const
{
	UWorld* World = GetWorld();

	if (!IsValid(World))
	{
		return false;
	}

	UNavigationSystemV1* NavigationSystem = UNavigationSystemV1::GetCurrent(World);

	if (!IsValid(NavigationSystem))
	{
		return false;
	}

	FNavLocation RandomLocation;

	const bool bFoundLocation = NavigationSystem->GetRandomReachablePointInRadius(CurrentMannequin.GetActorLocation(), RoamingSearchRadius, RandomLocation);

	if (!bFoundLocation)
	{
		return false;
	}

	OutLocation = RandomLocation.Location;
	
	return true;
}

void UBTService_TwoStageMannequinChase::ApplyRoamingState(UBlackboardComponent& Blackboard, AMannequinAICharacter& CurrentMannequin)
{
	Blackboard.ClearValue(TEXT("TargetActor"));

	Blackboard.SetValueAsInt(TEXT("ChaseTier"), static_cast<int32>(ETwoStageMannequinTier::Roaming));


	// 기존 배회 목적지가 있고 아직 배회를 완료하지 않았다면 그 목적지를 계속 사용한다.
	const bool bHasRoamingLocation = Blackboard.IsVectorValueSet(TEXT("RoamingLocation"));

	const bool bRoamingCompleted = Blackboard.GetValueAsBool(TEXT("RoamingSequenceCompleted"));

	if (bHasRoamingLocation && !bRoamingCompleted)
	{
		return;
	}

	FVector NewRoamingLocation;

	if (TryFindRoamingLocation(CurrentMannequin, NewRoamingLocation))
	{
		Blackboard.SetValueAsVector(TEXT("RoamingLocation"), NewRoamingLocation);
		Blackboard.SetValueAsBool(TEXT("RoamingSequenceCompleted"), false);
	}
	else
	{
		Blackboard.ClearValue(TEXT("RoamingLocation"));
	}
}
