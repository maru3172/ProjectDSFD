// Fill out your copyright notice in the Description page of Project Settings.


#include "WeepingAngelSurroundManager.h"

#include "WeepingAngelCharacter.h"
#include "WeepingAngelPath.h"

#include "AIController.h"
#include "BehaviorTree/BlackboardComponent.h"

#include "Kismet/GameplayStatics.h"
#include "Algo/Sort.h"

#include "TimerManager.h"

// Sets default values
AWeepingAngelSurroundManager::AWeepingAngelSurroundManager()
{
 	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

}

// Called when the game starts or when spawned
void AWeepingAngelSurroundManager::BeginPlay()
{
	Super::BeginPlay();
	
	// 게임 시작 직후 한 번 실행
	UpdateAssignments();

	// 이후 일정한 간격으로 포위 배정 갱신
	GetWorldTimerManager().SetTimer(
		AssignmentTimerHandle,
		this,
		&AWeepingAngelSurroundManager::UpdateAssignments,
		AssignmentInterval,
		true
	);
}

// Called every frame
void AWeepingAngelSurroundManager::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

}

// 천사가 추격 중인지 확인
bool AWeepingAngelSurroundManager::IsAngelChasing(AWeepingAngelCharacter* Angel) const
{
	if (Angel == nullptr)
	{
		return false;
	}

	AAIController* AIController = Cast<AAIController>(Angel->GetController());
	if (AIController == nullptr)
	{
		return false;
	}

	UBlackboardComponent* Blackboard = AIController->GetBlackboardComponent();
	if (Blackboard == nullptr)
	{
		return false;
	}

	return Blackboard->GetValueAsBool(TEXT("ChaseStarted"));
}

// 천사에게 접근 통로 배정
void AWeepingAngelSurroundManager::SetAngelAssignment(AWeepingAngelCharacter* Angel, AWeepingAngelPath* AssignedPath)
{
	if (Angel == nullptr)
	{
		return;
	}

	Angel->SetAssignedApproachPath(AssignedPath);

	AAIController* AIController = Cast<AAIController>(Angel->GetController());
	if (AIController == nullptr)
	{
		return;
	}

	UBlackboardComponent* Blackboard = AIController->GetBlackboardComponent();
	if (Blackboard == nullptr)
	{
		return;
	}

	if (AssignedPath != nullptr)
	{
		Blackboard->SetValueAsObject(TEXT("AssignedApproachPath"), AssignedPath);
	}
	else
	{
		Blackboard->ClearValue(TEXT("AssignedApproachPath"));
	}
}

// 두 통로 사이의 그래프 거리 계산
float AWeepingAngelSurroundManager::GetGraphDistance(AWeepingAngelPath* StartPath, AWeepingAngelPath* GoalPath) const
{
	if (StartPath == nullptr || GoalPath == nullptr)
	{
		return TNumericLimits<float>::Max();
	}

	if (StartPath == GoalPath)
	{
		return 0.0f;
	}

	TMap<AWeepingAngelPath*, float> Distances;
	TSet<AWeepingAngelPath*> VisitedPaths;

	Distances.Add(StartPath, 0.0f);

	while (true)
	{
		AWeepingAngelPath* CurrentPath = nullptr;
		float CurrentDistance = TNumericLimits<float>::Max();

		// 아직 방문하지 않은 Path 중 거리가 가장 가까운 Path 선택
		for (const TPair<AWeepingAngelPath*, float>& Pair : Distances)
		{
			if (VisitedPaths.Contains(Pair.Key))
			{
				continue;
			}

			if (Pair.Value < CurrentDistance)
			{
				CurrentPath = Pair.Key;
				CurrentDistance = Pair.Value;
			}
		}

		// 더 이상 방문 가능한 Path가 없음
		if (CurrentPath == nullptr)
		{
			break;
		}

		// 목적지에 도착
		if (CurrentPath == GoalPath)
		{
			return CurrentDistance;
		}

		VisitedPaths.Add(CurrentPath);

		const TArray<TObjectPtr<AWeepingAngelPath>>& ConnectedPaths = CurrentPath->GetConnectedPaths();

		for (const TObjectPtr<AWeepingAngelPath>& ConnectedPathPointer : ConnectedPaths)
		{
			AWeepingAngelPath* ConnectedPath = ConnectedPathPointer.Get();
			if (ConnectedPath == nullptr)
			{
				continue;
			}

			const float EdgeDistance = FVector::Dist2D(CurrentPath->GetAngelPathLocation(), ConnectedPath->GetAngelPathLocation());

			const float NewDistance = CurrentDistance + EdgeDistance;

			float* ExistingDistance = Distances.Find(ConnectedPath);
			if (ExistingDistance == nullptr)
			{
				Distances.Add(ConnectedPath, NewDistance);
			}
			else if (NewDistance < *ExistingDistance)
			{
				*ExistingDistance = NewDistance;
			}
		}
	}

	// 연결된 경로를 찾지 못함
	return TNumericLimits<float>::Max();
}

void AWeepingAngelSurroundManager::UpdateAssignments()
{
	APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(GetWorld(), 0);
	if (PlayerPawn == nullptr)
	{
		return;
	}

	const FVector PlayerLocation = PlayerPawn->GetActorLocation();

	// 모든 천사 가져오기
	TArray<AActor*> AngelActors;

	UGameplayStatics::GetAllActorsOfClass(GetWorld(), AWeepingAngelCharacter::StaticClass(), AngelActors);

	TArray<AWeepingAngelCharacter*> ChasingAngels;

	for (AActor* AngelActor : AngelActors)
	{
		AWeepingAngelCharacter* Angel = Cast<AWeepingAngelCharacter>(AngelActor);
		if (Angel == nullptr)
		{
			return;
		}

		if (IsAngelChasing(Angel))
		{
			ChasingAngels.Add(Angel);
		}
		else
		{
			// 추격 중이 아니라면 기존 배정 해제
			SetAngelAssignment(Angel, nullptr);
		}
	}

	if (ChasingAngels.Num() == 0)
	{
		return;
	}

	// 플레이어 주변의 사용 가능한 입구 찾기
	TArray<AActor*> PathActors;

	UGameplayStatics::GetAllActorsOfClass(GetWorld(), AWeepingAngelPath::StaticClass(), PathActors);

	// 사용할 수 있는 입구가 하나도 없는 경우
	if (PathActors.Num() == 0)
	{
		for (AWeepingAngelCharacter* Angel : ChasingAngels)
		{
			SetAngelAssignment(Angel, nullptr);
		}

		return;
	}

	// 플레이어와 가장 가까운 Path 찾기
	AWeepingAngelPath* ClosestPlayerPath = nullptr;

	float ClosestPlayerPathDistance = TNumericLimits<float>::Max();

	for (AActor* PathActor : PathActors)
	{
		AWeepingAngelPath* Path = Cast<AWeepingAngelPath>(PathActor);
		if (Path == nullptr)
		{
			continue;
		}

		const float Distance = FVector::Dist2D(PlayerLocation, Path->GetAngelPathLocation());

		if (Distance < ClosestPlayerPathDistance)
		{
			ClosestPlayerPathDistance = Distance;
			ClosestPlayerPath = Path;
		}
	}

	if (ClosestPlayerPath == nullptr)
	{
		for (AWeepingAngelCharacter* Angel : ChasingAngels)
		{
			SetAngelAssignment(Angel, nullptr);
		}

		return;
	}

	// 플레이어의 CurrentPath 갱신
	if (!IsValid(PlayerCurrentPath.Get()))
	{
		PlayerCurrentPath = ClosestPlayerPath;
	}
	else if (PlayerCurrentPath.Get() != ClosestPlayerPath)
	{
		const float CurrentPlayerPathDistance = FVector::Dist2D(PlayerLocation, PlayerCurrentPath->GetAngelPathLocation());

		// 새로운 Path가 기존 Path보다 일정 거리 이상 가까울 때만 변경
        // 두 Path의 경계에서 계속 바뀌는 현상을 방지한다.
		const bool bNewPathIsClearlyCloser = (ClosestPlayerPathDistance + PlayerPathSwitchMargin) < CurrentPlayerPathDistance;

		if (bNewPathIsClearlyCloser)
		{
			PlayerCurrentPath = ClosestPlayerPath;
		}
	}

	// PlayerCurrentPath에 연결된 Path를 포위 입구로 사용
	TArray<AWeepingAngelPath*> AvailableEntrances;

	const TArray<TObjectPtr<AWeepingAngelPath>>& ConnectedPlayerPaths = PlayerCurrentPath->GetConnectedPaths();

	for (const TObjectPtr<AWeepingAngelPath>& ConnectedPathPointer : ConnectedPlayerPaths)
	{
		AWeepingAngelPath* ConnectedPath = ConnectedPathPointer.Get();
		if (ConnectedPath == nullptr)
		{
			continue;
		}

		// 플레이어가 현재 보고 있는 통로는 포위 후보에서 제외
		if (ConnectedPath->IsVisibleToPlayer())
		{
			continue;
		}

		AvailableEntrances.AddUnique(ConnectedPath);
	}

	// 플레이어가 모든 연결 통로를 보고 있는 경우
	if (AvailableEntrances.Num() == 0)
	{
		return;
	}

	// 플레이어에게 가까운 입구 순서로 정렬
	Algo::SortBy(AvailableEntrances, 
		[PlayerLocation](const AWeepingAngelPath* Path)
		{
			return FVector::DistSquared2D(PlayerLocation, Path->GetAngelPathLocation());
		}
	);

	// 입구별 천사 정원 계산
	struct FEntranceAllocation
	{
		AWeepingAngelPath* Path = nullptr;
		int32 RemainingCapacity = 0;
	};

	TArray<FEntranceAllocation> EntranceAllocations;

	const int32 AngelCount = ChasingAngels.Num();

	const int32 EntranceCount = AvailableEntrances.Num();

	const int32 BaseCount = AngelCount / EntranceCount;

	const int32 ExtraCount = AngelCount % EntranceCount;

	for (int32 Index = 0; Index < EntranceCount; ++Index)
	{
		FEntranceAllocation Allocation;

		Allocation.Path = AvailableEntrances[Index];

		// 예: 16마리 / 3개 입구
        // 첫 번째 입구 6마리, 나머지 입구 5마리
		Allocation.RemainingCapacity = BaseCount + (Index < ExtraCount ? 1 : 0);

		EntranceAllocations.Add(Allocation);
	}

	// 천사와 입구의 최적 조합 찾기
	TArray<AWeepingAngelCharacter*> UnassignedAngels = ChasingAngels;

	while (UnassignedAngels.Num() > 0)
	{
		int32 BestAngelIndex = INDEX_NONE;
		int32 BestEntranceIndex = INDEX_NONE;
		float BestScore = TNumericLimits<float>::Max();

		// 남아 있는 모든 천사와 모든 입구 조합 비교
		for (int32 AngelIndex = 0; AngelIndex < UnassignedAngels.Num(); ++AngelIndex)
		{
			AWeepingAngelCharacter* Angel = UnassignedAngels[AngelIndex];
			if (Angel == nullptr)
			{
				continue;
			}

			AWeepingAngelPath* CurrentPath = Angel->GetCurrentPath();
			if (CurrentPath == nullptr)
			{
				continue;
			}

			for (int32 EntranceIndex = 0; EntranceIndex < EntranceAllocations.Num(); ++EntranceIndex)
			{
				const FEntranceAllocation& Allocation = EntranceAllocations[EntranceIndex];

				// 해당 입구의 정원이 모두 찼음
				if (Allocation.RemainingCapacity <= 0)
				{
					continue;
				}

				if (Allocation.Path == nullptr)
				{
					continue;
				}

				// 천사의 CurrentPath에서 입구까지의 그래프 거리
				float Score = GetGraphDistance(CurrentPath, Allocation.Path);

				// 해당 입구까지 연결된 경로가 없음
				if (Score == TNumericLimits<float>::Max())
				{
					continue;
				}

				// 기존 담당 입구를 유지하도록 우대
                // 매 갱신마다 담당 입구가 바뀌는 현상을 줄인다.
				if (Angel->GetAssignedApproachPath() == Allocation.Path)
				{
					Score -= KeepAssignmentBonus;
				}

				if (Score < BestScore)
				{
					BestScore = Score;
					BestAngelIndex = AngelIndex;
					BestEntranceIndex = EntranceIndex;
				}
			}
		}

		// 남은 천사가 도달할 수 있는 입구가 없는 경우
		if (BestAngelIndex == INDEX_NONE || BestEntranceIndex == INDEX_NONE)
		{
			break;
		}

		// 선택된 천사와 입구
		AWeepingAngelCharacter* SelectedAngel = UnassignedAngels[BestAngelIndex];
		FEntranceAllocation& SelectedAllocation = EntranceAllocations[BestEntranceIndex];

		// 천사에게 담당 입구 배정
		SetAngelAssignment(SelectedAngel, SelectedAllocation.Path);

		// 해당 입구에 남은 자리 감소
		--SelectedAllocation.RemainingCapacity;

		// 배정된 천사를 미배정 목록에서 제거
		UnassignedAngels.RemoveAtSwap(BestAngelIndex);
	}

	// 균등 배정 과정에서 남은 천사에게 도달 가능한 입구를 추가로 배정
	// 이 단계에서는 정원보다 AI가 멈추지 않는 것을 우선한다.
	for (AWeepingAngelCharacter* Angel : UnassignedAngels)
	{
		if (Angel == nullptr)
		{
			continue;
		}

		AWeepingAngelPath* CurrentPath = Angel->GetCurrentPath();

		if (CurrentPath == nullptr)
		{
			UE_LOG(
				LogTemp,
				Error,
				TEXT("Assignment Failed: %s has no CurrentPath"),
				*Angel->GetName()
			);

			continue;
		}

		AWeepingAngelPath* BestFallbackEntrance = nullptr;
		float BestFallbackDistance = TNumericLimits<float>::Max();

		for (AWeepingAngelPath* Entrance : AvailableEntrances)
		{
			if (Entrance == nullptr)
			{
				continue;
			}

			const float GraphDistance =
				GetGraphDistance(CurrentPath, Entrance);

			if (GraphDistance < BestFallbackDistance)
			{
				BestFallbackDistance = GraphDistance;
				BestFallbackEntrance = Entrance;
			}
		}

		if (BestFallbackEntrance != nullptr)
		{
			SetAngelAssignment(Angel, BestFallbackEntrance);

			UE_LOG(
				LogTemp,
				Warning,
				TEXT(
					"Fallback Assignment: "
					"Angel=%s, Current=%s, Assigned=%s"
				),
				*Angel->GetName(),
				*CurrentPath->GetName(),
				*BestFallbackEntrance->GetName()
			);
		}
		else
		{
			// 기존 담당 통로가 유효하다면 제거하지 않고 유지
			AWeepingAngelPath* PreviousAssignment =
				Angel->GetAssignedApproachPath();

			const bool bPreviousAssignmentReachable =
				IsValid(PreviousAssignment) &&
				!PreviousAssignment->IsVisibleToPlayer() &&
				GetGraphDistance(
					CurrentPath,
					PreviousAssignment
				) != TNumericLimits<float>::Max();

			if (bPreviousAssignmentReachable)
			{
				UE_LOG(
					LogTemp,
					Warning,
					TEXT(
						"No new route for %s. "
						"Keeping previous assignment: %s"
					),
					*Angel->GetName(),
					*PreviousAssignment->GetName()
				);
			}
			else
			{
				SetAngelAssignment(Angel, nullptr);
				
				UE_LOG(
					LogTemp,
					Error,
					TEXT(
						"No reachable entrance: "
						"Angel=%s, CurrentPath=%s"
					),
					*Angel->GetName(),
					*CurrentPath->GetName()
				);
			}
		}
	}
}