// Fill out your copyright notice in the Description page of Project Settings.


#include "BTService_TwoStageMannequinChase.h"

#include "AIController.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Kismet/GameplayStatics.h"
#include "NavigationSystem.h"

#include "MannequinAICharacter.h"
#include "PlayerCharacter.h"

UBTService_TwoStageMannequinChase::UBTService_TwoStageMannequinChase()
{
	NodeName = TEXT("Two Stage Mannequin Chase");
	
	// 각 마네킹이 독립된 서비스 상태를 갖도록 한다.
	bCreateNodeInstance = true;
	
	// 0.2 초마다 역할을 다시 검사한다.
	Interval = 0.2f;
	
	// 모든 마네킹이 같은 프레임에 검사하는 것을 완화한다.
	RandomDeviation = 0.05f;
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

	APlayerCharacter* Player =Cast<APlayerCharacter>(UGameplayStatics::GetActorOfClass(GetWorld(), APlayerCharacter::StaticClass()));
	// 플레이어가 없으면 모든 추적 상태를 제거한다.
	if (!IsValid(Player))
	{
		Blackboard->ClearValue(TEXT("TargetActor"));
		Blackboard->ClearValue(TEXT("RoamingLocation"));
		Blackboard->SetValueAsInt(TEXT("ChaseTier"),static_cast<int32>(ETwoStageMannequinTier::Roaming));

		return;
	}

	TArray<AActor*> AllMannequins;

	UGameplayStatics::GetAllActorsOfClass(GetWorld(), AMannequinAICharacter::StaticClass(), AllMannequins);

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