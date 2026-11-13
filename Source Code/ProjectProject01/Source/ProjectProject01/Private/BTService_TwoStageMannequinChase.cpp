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
		Blackboard->SetValueAsBool(TEXT("MannequinCanSeePlayer"), false);
		Blackboard->ClearValue(TEXT("TargetActor"));
		Blackboard->ClearValue(TEXT("TrackingLocation"));
		Blackboard->ClearValue(TEXT("RoamingLocation"));
		Blackboard->SetValueAsInt(TEXT("ChaseTier"),static_cast<int32>(ETwoStageMannequinTier::Roaming));
		Blackboard->SetValueAsInt(TEXT("TrackingEvidence"), static_cast<int32>(EMannequinTrackingEvidence::None));
		
		Blackboard->SetValueAsBool(TEXT("HasPlayerObservation"), false);
		Blackboard->SetValueAsFloat(TEXT("ObservationConfidence"), 0.0f);
		Blackboard->ClearValue(TEXT("LastSeenPlayerLocation"));
		Blackboard->ClearValue(TEXT("LastSeenPlayerVelocity"));
		
		CurrentMannequin->ClearPlayerObservation();

		return;
	}

	TArray<AActor*> AllMannequins;

	UGameplayStatics::GetAllActorsOfClass(GetWorld(), AMannequinAICharacter::StaticClass(), AllMannequins);

	// 직접 시야 판정
	// 1. 직접 시야 판정
	// 2. 관측 데이터 갱신
	// 3. 신뢰도 계산
	// 4. Blackboard 디버그 값 기록
	// 5. 플레이어가 보고 있다면 이동 정지
	// 6. 아니면 기존 추적/배회 진행
	bool bObservedByPlayer = false;
	
	const bool bObservedByPlayerOrHelper = IsObservedByPlayerOrHelper(*Player, *CurrentMannequin, AllMannequins, bObservedByPlayer);
	CurrentMannequin->SetObservedByPlayer(bObservedByPlayer);
	
	const bool bMannequinCanSeePlayer = CanMannequinSeePlayer(*CurrentMannequin, *Player, AllMannequins);
	
	const double CurrentTime = GetWorld()->GetTimeSeconds();
	
	CurrentMannequin->SetPlayerDirectlyVisible(bMannequinCanSeePlayer);
	
	if (bMannequinCanSeePlayer)
	{
		CurrentMannequin->UpdatePlayerObservation(Player->GetActorLocation(), Player->GetVelocity(), CurrentTime);
	}
	else
	{
		CurrentMannequin->UpdatePlayerObservationConfidence(CurrentTime, ObservationLifetime);
	}

	Blackboard->SetValueAsBool(TEXT("PlayerLookingAtMannequin"), bObservedByPlayerOrHelper);
	Blackboard->SetValueAsBool(TEXT("MannequinCanSeePlayer"), bMannequinCanSeePlayer);
	
	const FMannequinPlayerObservation& Observation = CurrentMannequin->GetPlayerObservation();
	
	Blackboard->SetValueAsBool(TEXT("HasPlayerObservation"), Observation.bHasObservation);
	Blackboard->SetValueAsFloat(TEXT("ObservationConfidence"), Observation.Confidence);
	
	if (Observation.bHasObservation)
	{
		Blackboard->SetValueAsVector(TEXT("LastSeenPlayerLocation"), Observation.LastSeenLocation);
		Blackboard->SetValueAsVector(TEXT("LastSeenPlayerVelocity"), Observation.LastSeenVelocity);
	}
	else
	{
		Blackboard->ClearValue(TEXT("LastSeenPlayerLocation"));
		Blackboard->ClearValue(TEXT("LastSeenPlayerVelocity"));
	}

	const ETwoStageMannequinTier Tier = DetermineChaseTier(*CurrentMannequin, AllMannequins);

	switch (Tier)
	{
	case ETwoStageMannequinTier::Primary:
		{
			// 자신의 관측 정보를 사용한다.
			const FMannequinPlayerObservation& PrimaryObservation = CurrentMannequin->GetPlayerObservation();
			
			FVector TrackingLocation;
			
			if (PrimaryObservation.bDirectlyVisible)
			{
				if (TryProjectTrackingLocation(PrimaryObservation.LastSeenLocation, TrackingLocation))
				{
					CurrentMannequin->SetPredictedPlayerLocation(TrackingLocation);
					
					ApplyChaseState(*Blackboard, TrackingLocation, Tier, EMannequinTrackingEvidence::DirectObservation);
				}
				else
				{
					if (Blackboard->IsVectorValueSet(TEXT("TrackingLocation")))
					{
						const FVector ExistingTrackingLocation = Blackboard->GetValueAsVector(TEXT("TrackingLocation"));
						
						ApplyChaseState(*Blackboard, ExistingTrackingLocation, Tier, EMannequinTrackingEvidence::DirectObservation);
					}
					else
					{
						ApplyRoamingState(*Blackboard, *CurrentMannequin);
					}
				}
			}
			else if (TryResolveObservationTrackingLocation(PrimaryObservation, TrackingLocation))
			{
				CurrentMannequin->SetPredictedPlayerLocation(TrackingLocation);
				
				ApplyChaseState(*Blackboard, TrackingLocation, Tier, EMannequinTrackingEvidence::VelocityPrediction);
			}
			else
			{
				// 새 예상 위치와 마지막 관측 위치를 모두 NavMesh 에 보정하지 못했으면 기존 추적 위치를 유지한다.
				if (Blackboard->IsVectorValueSet(TEXT("TrackingLocation")))
				{
					const FVector ExistingTrackingLocation = Blackboard->GetValueAsVector(TEXT("TrackingLocation"));
					
					ApplyChaseState(*Blackboard, ExistingTrackingLocation, Tier, EMannequinTrackingEvidence::VelocityPrediction);
				}
				else
				{
					ApplyRoamingState(*Blackboard, *CurrentMannequin);
				}
			}
			
			break;
		}
	
	case ETwoStageMannequinTier::Secondary:
		{
			// 주변 1 차의 관측 정보를 지역변수로 전달받는다.
			FMannequinPlayerObservation SharedObservation;
			FVector TrackingLocation;
			
			const bool bFoundSharedObservation = TryFindBestPrimaryObservation(*CurrentMannequin, AllMannequins, false, SharedObservation);
			
			if (!bFoundSharedObservation)
			{
				ApplyRoamingState(*Blackboard, *CurrentMannequin);
				break;
			}
			
			FMannequinPlayerObservation DirectObservation;
			
			// 우선순위 1: 현재 직접 보고 있는 1 차의 최신 위치
			if (TryFindBestPrimaryObservation(*CurrentMannequin, AllMannequins, true, DirectObservation) && TryProjectTrackingLocation(DirectObservation.LastSeenLocation, TrackingLocation))
			{
				ApplyChaseState(*Blackboard, TrackingLocation, Tier, EMannequinTrackingEvidence::DirectObservation);
			
				break;
			}
			
			// 우선순위 2: 플레이어 시선으로 멈춘 마네킹 단서
			if (TryGetCachedIndirectTrackingLocation(*CurrentMannequin, AllMannequins, SharedObservation, TrackingLocation))
			{
				ApplyChaseState(*Blackboard, TrackingLocation, Tier, EMannequinTrackingEvidence::StoppedMannequinClue);
				
				break;
			}
			
			// 우선순위 3: 마지막 위치와 속도를 이용한 예측
			if (TryResolveObservationTrackingLocation(SharedObservation, TrackingLocation))
			{
				ApplyChaseState(*Blackboard, TrackingLocation, Tier, EMannequinTrackingEvidence::VelocityPrediction);
			}
			else
			{
				// 새 예상 위치와 마지막 관측 위치를 사용할 수 없다면 이전에 설정된 추적 위치를 유지한다.
				if (Blackboard->IsVectorValueSet(TEXT("TrackingLocation")))
				{
					const FVector ExistingTrackingLocation = Blackboard->GetValueAsVector(TEXT("TrackingLocation"));
					
					ApplyChaseState(*Blackboard, ExistingTrackingLocation, Tier, EMannequinTrackingEvidence::VelocityPrediction);
				}
				else
				{
					ApplyRoamingState(*Blackboard, *CurrentMannequin);
				}
			}
			
			break;
		}
	
	case ETwoStageMannequinTier::Roaming:
	default:
		ApplyRoamingState(*Blackboard, *CurrentMannequin);
		break;
	}
	
	// 정지 중 정보 전달 유지를 위해 위치를 옮긴 상태
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
}

bool UBTService_TwoStageMannequinChase::IsObservedByPlayerOrHelper(const APlayerCharacter& Player, AMannequinAICharacter& CurrentMannequin, const TArray<AActor*>& AllMannequins, bool& bOutObservedByPlayer) const
{
	bOutObservedByPlayer = false;
	
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

	bOutObservedByPlayer = bInScreen;
	
	return bInScreen || bInHelperRearGuardSight;
}

bool UBTService_TwoStageMannequinChase::CanMannequinSeePlayer(const AMannequinAICharacter& CurrentMannequin, const APlayerCharacter& Player, const TArray<AActor*>& AllMannequins) const
{
	UWorld* World = GetWorld();
	// 1. World가 유효한가?
	if (!IsValid(World))
	{
		return false;
	}
	
	const FVector MannequinLocation = CurrentMannequin.GetActorLocation();
	const FVector PlayerLocation = Player.GetActorLocation();
	const float HeightDifference = FMath::Abs(MannequinLocation.Z - PlayerLocation.Z);
	
	// 2. 플레이어와 높이 차이가 너무 큰가?
	if (HeightDifference > MannequinSightMaximumHeightDifference)
	{
		return false;
	}
	
	const FVector MannequinToPlayer = PlayerLocation - MannequinLocation;
	const double SightRadiusSquared = FMath::Square(static_cast<double>(MannequinSightRadius));
	
	// 3. 거리가 너무 먼가?
	if (MannequinToPlayer.SizeSquared2D() > SightRadiusSquared)
	{
		return false;
	}
	
	const FVector MannequinForward = CurrentMannequin.GetActorForwardVector().GetSafeNormal2D();
	const FVector DirectionToPlayer = MannequinToPlayer.GetSafeNormal2D();
	
	// 4. 플레이어가 마네킹 정면 시야각 안에 있는가?
	if (!DirectionToPlayer.IsNearlyZero())
	{
		const float SightMinimumDot = FMath::Cos(FMath::DegreesToRadians(MannequinSightHalfAngle));
		const float SightDirectionDot = FVector::DotProduct(MannequinForward, DirectionToPlayer);
		
		// 플레이어가 마네킹 정면 시야각 안에 있는지 검사한다.
		if (SightDirectionDot < SightMinimumDot)
		{
			return false;
		}
	}
	
	FVector TraceStart = MannequinLocation;
	
	const USkeletalMeshComponent* MannequinMesh = CurrentMannequin.GetMesh();
	if (IsValid(MannequinMesh) && MannequinMesh->GetBoneIndex(TEXT("head")) != INDEX_NONE)
	{
		TraceStart = MannequinMesh->GetBoneLocation(TEXT("head"));
	}

	FVector TraceEnd = PlayerLocation;
	
	const UCapsuleComponent* PlayerCapsule = Player.GetCapsuleComponent();
	if (IsValid(PlayerCapsule))
	{
		TraceEnd = PlayerCapsule->GetComponentLocation();
	}
	
	FHitResult HitResult;
	FCollisionQueryParams QueryParams;
	
	// 현재 마네킹은 충돌을 무시하도록 한다.
	QueryParams.AddIgnoredActor(&CurrentMannequin);
	
	// 다른 마네킹은 시야 장애물로 처리하지 않는다.
	for (AActor* OtherMannequin : AllMannequins)
	{
		if (OtherMannequin != &CurrentMannequin)
		{
			QueryParams.AddIgnoredActor(OtherMannequin);
		}
	}
	
	const bool bHit = World->LineTraceSingleByChannel(
		HitResult,
		TraceStart,
		TraceEnd,
		ECC_GameTraceChannel1,
		QueryParams
		);
	
	// 5. 머리에서 플레이어까지 Line Trace가 도달하는가?
	return bHit && HitResult.GetActor() == &Player;
}

ETwoStageMannequinTier UBTService_TwoStageMannequinChase::DetermineChaseTier(const AMannequinAICharacter& CurrentMannequin, const TArray<AActor*>& AllMannequins) const
{
	// 자신의 유효한 직접 관측 정보를 가진 마네킹은 1 차다.
	if (CurrentMannequin.HasActivePlayerObservation())
	{
		return ETwoStageMannequinTier::Primary;
	}
	
	FMannequinPlayerObservation SharedObservation;
	
	// 자체 관측 정보가 없지만 주변 1 차의 정보를 받을 수 있으면 2 차다.
	if (TryFindBestPrimaryObservation(CurrentMannequin, AllMannequins, false, SharedObservation))
	{
		return ETwoStageMannequinTier::Secondary;
	}
	
	return ETwoStageMannequinTier::Roaming;
}

bool UBTService_TwoStageMannequinChase::TryFindBestPrimaryObservation(const AMannequinAICharacter& CurrentMannequin, const TArray<AActor*>& AllMannequins, bool bRequireDirectObservation, FMannequinPlayerObservation& OutObservation) const
{
	const FVector CurrentLocation = CurrentMannequin.GetActorLocation();
	const double PropagationRadiusSquared = FMath::Square(static_cast<double>(PrimaryPropagationRadius));
	
	bool bFoundPrimary = false;
	double NewestObservationTime = -1.0;
	
	for (AActor* MannequinActor : AllMannequins)
	{
		const AMannequinAICharacter* OtherMannequin = Cast<AMannequinAICharacter>(MannequinActor);
		
		// 1. 유효한 마네킹인가
		if (!IsValid(OtherMannequin))
		{
			continue;
		}
		
		// 2. 자기 자신은 아닌가
		// 자기 자신을 정보 전달자로 선택하지 않는다.
		if (OtherMannequin == &CurrentMannequin)
		{
			continue;
		}
		
		// 3. 자체 관측 정보를 가진 진짜 1 차인가
		// 플레이어를 직접 관측했거나 아직 유효한 자체 관측 정보를 가진 마네킹만 1 차 정보 전달자가 될 수 있다.
		if (!OtherMannequin->HasActivePlayerObservation())
		{
			continue;
		}
		
		const FVector OtherLocation = OtherMannequin->GetActorLocation();
		const float HeightDifference = FMath::Abs(CurrentLocation.Z - OtherLocation.Z);
		
		// 4. 같은 층인가
		// 다른 층에 있는 마네킹의 정보는 전달받지 않는다.
		if (HeightDifference > MaximumHeightDifference)
		{
			continue;
		}
		
		const double DistanceToPrimarySquared = FVector::DistSquared2D(CurrentLocation, OtherLocation);
		
		// 5. 전파 반경 안인가
		if (DistanceToPrimarySquared > PropagationRadiusSquared)
		{
			continue;
		}
		
		const FMannequinPlayerObservation& OtherObservation = OtherMannequin->GetPlayerObservation();
		
		// 6. 관측 정보가 유효한가
		if (!OtherObservation.bHasObservation || OtherObservation.Confidence <= 0.0f)
		{
			continue;
		}
		
		if (bRequireDirectObservation && !OtherObservation.bDirectlyVisible)
		{
			continue;
		}
		
		// 7. 기존 후보보다 최근 정보인가
		// 여러 1 차 마네킹이 있다면 가장 최근에 플레이어를 본 정보를 사용한다.
		if (!bFoundPrimary || OtherObservation.LastSeenTime > NewestObservationTime)
		{
			OutObservation = OtherObservation;
			NewestObservationTime = OtherObservation.LastSeenTime;
			bFoundPrimary = true;
		}
	}
	
	return bFoundPrimary;
}

bool UBTService_TwoStageMannequinChase::TryCalculateTrackingLocation(const FMannequinPlayerObservation& Observation, FVector& OutTrackingLocation) const
{
	UWorld* World = GetWorld();
	if (!IsValid(World))
	{
		return false;
	}
	
	if (!Observation.bHasObservation || Observation.Confidence <= 0.0f)
	{
		return false;
	}
	
	const double CurrentTime = World->GetTimeSeconds();
	const double ElapsedTime = FMath::Max(0.0, CurrentTime - Observation.LastSeenTime);
	
	const double PredictionTime = FMath::Min(ElapsedTime, static_cast<double>(MaximumPredictionSeconds));
	const FVector PredictedLocation = Observation.LastSeenLocation + Observation.LastSeenVelocity * PredictionTime;
	
	return TryProjectTrackingLocation(PredictedLocation, OutTrackingLocation);
}

void UBTService_TwoStageMannequinChase::ApplyChaseState(UBlackboardComponent& Blackboard, const FVector& TrackingLocation, ETwoStageMannequinTier Tier, EMannequinTrackingEvidence TrackingEvidence)
{
	Blackboard.ClearValue(TEXT("TargetActor"));
	
	const FVector StableTrackingLocation = StabilizeTrackingLocation(TrackingLocation, TrackingEvidence);
	Blackboard.SetValueAsVector(TEXT("TrackingLocation"), StableTrackingLocation);
	
	Blackboard.ClearValue(TEXT("RoamingLocation"));
	
	Blackboard.SetValueAsBool(TEXT("RoamingSequenceCompleted"), false);
	Blackboard.SetValueAsInt(TEXT("ChaseTier"), static_cast<int32>(Tier));
	Blackboard.SetValueAsInt(TEXT("TrackingEvidence"), static_cast<int32>(TrackingEvidence));
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
	// 캐시 초기화
	ResetTrackingLocationStabilization();
	ResetIndirectTrackingCache();
	
	Blackboard.ClearValue(TEXT("TargetActor"));
	Blackboard.ClearValue(TEXT("TrackingLocation"));

	Blackboard.SetValueAsInt(TEXT("ChaseTier"), static_cast<int32>(ETwoStageMannequinTier::Roaming));

	Blackboard.SetValueAsInt(TEXT("TrackingEvidence"), static_cast<int32>(EMannequinTrackingEvidence::None));

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

bool UBTService_TwoStageMannequinChase::TryProjectTrackingLocation(const FVector& CandidateLocation, FVector& OutTrackingLocation) const
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
	
	FNavLocation NavigationLocation;
	
	const bool bProjectedToNavigation = NavigationSystem->ProjectPointToNavigation(CandidateLocation, NavigationLocation, PredictionNavigationExtent);
	if (!bProjectedToNavigation)
	{
		return false;
	}
	
	OutTrackingLocation = NavigationLocation.Location;
	
	return true;
}

bool UBTService_TwoStageMannequinChase::CanCandidateSeeStoppedMannequin(const FVector& CandidateLocation, const AMannequinAICharacter& StoppedMannequin, const TArray<AActor*>& AllMannequins) const
{
	UWorld* World = GetWorld();
	if (!IsValid(World))
	{
		return false;
	}
	
	const FVector TraceStart = CandidateLocation + FVector::UpVector * 80.0f;
	FVector TraceEnd = StoppedMannequin.GetActorLocation() + FVector::UpVector * 60.0f;
	
	const USkeletalMeshComponent* MannequinMesh = StoppedMannequin.GetMesh();
	
	if (IsValid(MannequinMesh) && MannequinMesh->GetBoneIndex(TEXT("head")) != INDEX_NONE)
	{
		TraceEnd = MannequinMesh->GetBoneLocation(TEXT("head"));
	}
	
	FCollisionQueryParams QueryParams;
	
	// 후보와 정지 마네킹 사이의 벽만 검사한다.
	for (AActor* MannequinActor : AllMannequins)
	{
		if (IsValid(MannequinActor))
		{
			QueryParams.AddIgnoredActor(MannequinActor);
		}
	}
	
	FHitResult HitResult;
	
	const bool bBlocked = 
		World->LineTraceSingleByChannel(
			HitResult,
			TraceStart,
			TraceEnd,
			ECC_GameTraceChannel1,
			QueryParams
			);
	
	return !bBlocked;
}

bool UBTService_TwoStageMannequinChase::TryCalculateIndirectTrackingLocation(const AMannequinAICharacter& CurrentMannequin, const TArray<AActor*>& AllMannequins, const FMannequinPlayerObservation& ReferenceObservation, FVector& OutTrackingLocation) const
{
	UWorld* World = GetWorld();
	if (!IsValid(World))
	{
		return false;
	}
	
	if (!ReferenceObservation.bHasObservation || ReferenceObservation.Confidence <= 0.0f)
	{
		return false;
	}
	
	TArray<const AMannequinAICharacter*> StoppedMannequins;
	
	// 현재 간접 추론을 수행하는 마네킹의 위치
	const FVector CurrentLocation = CurrentMannequin.GetActorLocation();
	
	for (AActor* MannequinActor : AllMannequins)
	{
		const AMannequinAICharacter* OtherMannequin = Cast<AMannequinAICharacter>(MannequinActor);
		if (!IsValid(OtherMannequin))
		{
			continue;
		}
		
		// 자기 자신은 간접 단서 대상에서 제외한다.
		if (!OtherMannequin->IsObservedByPlayer())
		{
			continue;
		}
		
		// 플레이어 시선으로 멈춘 마네킹만 간접 단서로 사용한다.
		if (!OtherMannequin->IsObservedByPlayer())
		{
			continue;
		}
		
		const FVector OtherLocation = OtherMannequin->GetActorLocation();
		
		const float HeightDifference = FMath::Abs(CurrentLocation.Z - OtherLocation.Z);
		
		// 다른 층에 있는 정지 마네킹은 간접 단서에서 제외한다.
		if (HeightDifference > MaximumHeightDifference)
		{
			continue;
		}
		
		StoppedMannequins.Add(OtherMannequin);
	}
	
	if (StoppedMannequins.IsEmpty())
	{
		return false;
	}
	
	const double CurrentTime = World->GetTimeSeconds();
	const double ElapsedTime = FMath::Max(0.0, CurrentTime - ReferenceObservation.LastSeenTime);
	
	const double PredictionTime = FMath::Min(ElapsedTime, static_cast<double>(MaximumPredictionSeconds));
	const FVector PredictedLocation = ReferenceObservation.LastSeenLocation + ReferenceObservation.LastSeenVelocity * PredictionTime;
	
	const float MaximumReachableDistance = FMath::Clamp(EstimatedPlayerMaximumSpeed * static_cast<float>(ElapsedTime), IndirectCandidateSpacing, MaximumIndirectSearchRadius);
	
	TArray<FVector> CandidateLocations;
	
	CandidateLocations.Add(ReferenceObservation.LastSeenLocation);
	CandidateLocations.Add(PredictedLocation);
	
	const int32 RingCount = FMath::Max(1, FMath::CeilToInt(MaximumReachableDistance / IndirectCandidateSpacing));
	
	for (int32 RingIndex = 1; RingIndex <= RingCount; ++RingIndex)
	{
		const float RingRadius = FMath::Min(IndirectCandidateSpacing * RingIndex, MaximumReachableDistance);
		
		for (int32 DirectionIndex = 0; DirectionIndex < IndirectCandidateDirectionCount; ++DirectionIndex)
		{
			const float AngleDegrees = 360.0f * static_cast<float>(DirectionIndex) / static_cast<float>(IndirectCandidateDirectionCount);
			
			const FVector Direction = FRotator(0.0f, AngleDegrees, 0.0f).Vector();
			
			CandidateLocations.Add(PredictedLocation + Direction * RingRadius);
		}
	}
	
	bool bFoundCandidate = false;
	double BestCandidateScore = TNumericLimits<double>::Max();
	FVector BestCandidateLocation = FVector::ZeroVector;
	
	for (const FVector& CandidateLocation : CandidateLocations)
	{
		FVector ProjectedLocation;
		
		// 1. NavMesh 위에 있음
		if (!TryProjectTrackingLocation(CandidateLocation, ProjectedLocation))
		{
			continue;
		}
		
		// 2. 마지막 위치에서 이동 가능한 범위 안
		const double DistanceFromLastSeenSquared = FVector::DistSquared2D(ProjectedLocation, ReferenceObservation.LastSeenLocation);
		
		if (DistanceFromLastSeenSquared > FMath::Square(static_cast<double>(MaximumReachableDistance)))
		{
			continue;
		}
		
		// 3. 모든 정지 마네킹의 단서 반경 안
		bool bInsideAllClueRanges = true;
		
		for (const AMannequinAICharacter* StoppedMannequin : StoppedMannequins)
		{
			if (!IsValid(StoppedMannequin))
			{
				bInsideAllClueRanges = false;
				break;
			}
			
			const double DistanceToStoppedSquared = FVector::DistSquared2D(ProjectedLocation, StoppedMannequin->GetActorLocation());
			
			if (DistanceToStoppedSquared > FMath::Square(static_cast<double>(StoppedMannequinClueRadius)))
			{
				bInsideAllClueRanges = false;
				break;
			}
			
			// 4. 정지 마네킹과 후보 사이에 벽이 없음
			if (!CanCandidateSeeStoppedMannequin(ProjectedLocation, *StoppedMannequin, AllMannequins))
			{
				bInsideAllClueRanges = false;
				break;
			}
		}
		
		if (!bInsideAllClueRanges)
		{
			continue;
		}
		
		// 5. 통과 후보 중 속도 예측 위치와 가장 가까운 위치
		// 교집합 후보 중 기존 이동 예측과 가장 가까운 위치를 선택한다.
		const double CandidateScore = FVector::DistSquared2D(ProjectedLocation, PredictedLocation);
		
		if (!bFoundCandidate || CandidateScore < BestCandidateScore)
		{
			BestCandidateLocation = ProjectedLocation;
			BestCandidateScore = CandidateScore;
			bFoundCandidate = true;
		}
	}
	
	if (!bFoundCandidate)
	{
		return false;
	}
	
	OutTrackingLocation = BestCandidateLocation;
	
	return true;
}

bool UBTService_TwoStageMannequinChase::TryGetCachedIndirectTrackingLocation(const AMannequinAICharacter& CurrentMannequin, const TArray<AActor*>& AllMannequins, const FMannequinPlayerObservation& ReferenceObservation, FVector& OutTrackingLocation)
{
	UWorld* World = GetWorld();
	if (!IsValid(World))
	{
		return false;
	}
	
	const double CurrentTime = World->GetTimeSeconds();
	const double ElapsedSinceInference = CurrentTime - LastIndirectInferenceTime;
	
	if (LastIndirectInferenceTime >= 0.0 && ElapsedSinceInference < static_cast<double>(IndirectInferenceInterval))
	{
		if (!bHasCachedIndirectTrackingLocation)
		{
			return false;
		}
		
		OutTrackingLocation = CachedIndirectTrackingLocation;
		
		return true;
	}
	
	LastIndirectInferenceTime = CurrentTime;
	
	FVector NewIndirectTrackingLocation;
	
	const bool bCalculatedIndirectLocation = TryCalculateIndirectTrackingLocation(CurrentMannequin, AllMannequins, ReferenceObservation, NewIndirectTrackingLocation);
	
	if (!bCalculatedIndirectLocation)
	{
		bHasCachedIndirectTrackingLocation = false;
		return false;
	}
	
	CachedIndirectTrackingLocation = NewIndirectTrackingLocation;
	bHasCachedIndirectTrackingLocation = true;
	
	OutTrackingLocation = CachedIndirectTrackingLocation;
	
	return true;
}

void UBTService_TwoStageMannequinChase::ResetIndirectTrackingCache()
{
	LastIndirectInferenceTime = -1.0;
	CachedIndirectTrackingLocation = FVector::ZeroVector;
	bHasCachedIndirectTrackingLocation = false;
}

FVector UBTService_TwoStageMannequinChase::StabilizeTrackingLocation(const FVector& NewTrackingLocation, EMannequinTrackingEvidence TrackingEvidence)
{
	UWorld* World = GetWorld();
	if (!IsValid(World))
	{
		return NewTrackingLocation;
	}

	const double CurrentTime = World->GetTimeSeconds();
	
	// 직접 관측 정보는 즉시 반영한다.
	if (TrackingEvidence == EMannequinTrackingEvidence::DirectObservation)
	{
		StabilizedTrackingLocation = NewTrackingLocation;
		LastTrackingLocationChangeTime = CurrentTime;
		bHasStabilizedTrackingLocation = true;
		StabilizedTrackingEvidence = TrackingEvidence;
		
		return StabilizedTrackingLocation;
	}
	
	// 저장된 목표가 없다면 첫 목표를 바로 사용한다.
	if (!bHasStabilizedTrackingLocation)
	{
		StabilizedTrackingLocation = NewTrackingLocation;
		LastTrackingLocationChangeTime = CurrentTime;
		bHasStabilizedTrackingLocation = true;
		StabilizedTrackingEvidence = TrackingEvidence;
		
		return StabilizedTrackingLocation;
	}
	
	// 사용하는 정보 종류가 바뀌면 즉시 목표를 갱신한다.
	if (StabilizedTrackingEvidence != TrackingEvidence)
	{
		StabilizedTrackingLocation = NewTrackingLocation;
		LastTrackingLocationChangeTime = CurrentTime;
		StabilizedTrackingEvidence = TrackingEvidence;
		
		return StabilizedTrackingLocation;
	}
	
	const double TrackingLocationDistanceSquared = FVector::DistSquared2D(StabilizedTrackingLocation, NewTrackingLocation);
	const double MinimumChangeSquared = FMath::Square(static_cast<double>(MinimumTrackingLocationChange));
	const double ImmediateChangeSquared = FMath::Square(static_cast<double>(ImmediateTrackingLocationChange));
	
	// 변화량이 아주 작다면 기존 목표를 유지한다.
	if (TrackingLocationDistanceSquared <= MinimumChangeSquared)
	{
		return StabilizedTrackingLocation;
	}
	
	const double TimeSinceTrackingLocationChange = CurrentTime - LastTrackingLocationChangeTime;
	
	// 큰 변화거나 최소 유지 시간이 지났으면 새 목표를 사용한다.
	if (TrackingLocationDistanceSquared >= ImmediateChangeSquared || TimeSinceTrackingLocationChange >= static_cast<double>(MinimumTrackingLocationHoldTime))
	{
		StabilizedTrackingLocation = NewTrackingLocation;
		LastTrackingLocationChangeTime = CurrentTime;
	}
	
	return StabilizedTrackingLocation;
}

void UBTService_TwoStageMannequinChase::ResetTrackingLocationStabilization()
{
	StabilizedTrackingLocation = FVector::ZeroVector;
	LastTrackingLocationChangeTime = -1.0;
	bHasStabilizedTrackingLocation = false;
	StabilizedTrackingEvidence = EMannequinTrackingEvidence::None;
}

bool UBTService_TwoStageMannequinChase::TryResolveObservationTrackingLocation(const FMannequinPlayerObservation& Observation, FVector& OutTrackingLocation) const
{
	if (TryCalculateTrackingLocation(Observation, OutTrackingLocation))
	{
		return true;
	}
	
	return TryProjectTrackingLocation(Observation.LastSeenLocation, OutTrackingLocation);
}