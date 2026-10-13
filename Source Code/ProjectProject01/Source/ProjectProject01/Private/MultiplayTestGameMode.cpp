// File: Source/ProjectProject01/Private/MultiplayTestGameMode.cpp
// Build target: ProjectProject01Server / ProjectProject01

#include "MultiplayTestGameMode.h"

#include "MannequinAICharacter.h"
#include "MannequinAIController.h"
#include "AIController.h"
#include "MultiplayTestPlayerController.h"
#include "PlayerCharacter.h"
#include "ProjectProject01TuningData.h"
#include "ProjectProject01DiagnosticsSubsystem.h"
#include "ProjectProject01GameInstance.h"
#include "ProjectProject01RoleSpawnPoint.h"
#include "ProjectProject01VersionContract.h"
#include "Components/CapsuleComponent.h"
#include "Engine/TriggerBox.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "NavigationSystem.h"
#include "Navigation/PathFollowingComponent.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#endif

DEFINE_LOG_CATEGORY(LogProjectProject01Multiplayer);

namespace
{
	struct FMatchResolutionSummary
	{
		int32 SurvivorCount = 0;
		int32 ResolvedCount = 0;
		int32 EliminatedCount = 0;

		bool IsComplete() const { return SurvivorCount > 0 && ResolvedCount == SurvivorCount; }
		bool DidMannequinWin() const { return IsComplete() && EliminatedCount == SurvivorCount; }
	};

	FMatchResolutionSummary EvaluateMatchResolution(
		const TArray<EMultiplayTestSurvivorState>& SurvivorStates)
	{
		FMatchResolutionSummary Summary;
		Summary.SurvivorCount = SurvivorStates.Num();
		for (const EMultiplayTestSurvivorState State : SurvivorStates)
		{
			Summary.ResolvedCount += State == EMultiplayTestSurvivorState::Eliminated ||
				State == EMultiplayTestSurvivorState::Escaped ? 1 : 0;
			Summary.EliminatedCount += State == EMultiplayTestSurvivorState::Eliminated ? 1 : 0;
		}
		return Summary;
	}

	struct FLevelTransitionSummary
	{
		int32 ActiveCount = 0;
		int32 AdvancingCount = 0;

		bool CanTravel() const { return ActiveCount == 0 && AdvancingCount > 0; }
	};

	FLevelTransitionSummary EvaluateLevelTransition(
		const TArray<EMultiplayTestSurvivorState>& SurvivorStates)
	{
		FLevelTransitionSummary Summary;
		for (const EMultiplayTestSurvivorState State : SurvivorStates)
		{
			Summary.ActiveCount += State == EMultiplayTestSurvivorState::Active ? 1 : 0;
			Summary.AdvancingCount += State == EMultiplayTestSurvivorState::TransitionWaiting ? 1 : 0;
		}
		return Summary;
	}

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

	bool IsLocalEditorPlaySession(const UWorld* World)
	{
#if WITH_EDITOR
		return IsValid(World) && World->WorldType == EWorldType::PIE;
#else
		return false;
#endif
	}

	FVector CalculateFollowFormationLocation(
		const FVector& LeaderLocation,
		const FVector& LeaderForward,
		const FVector& LeaderRight,
		const int32 FollowerIndex,
		const int32 FollowerCount,
		const float FollowDistance,
		const float FollowSeparation)
	{
		const float CenteredIndex = static_cast<float>(FollowerIndex) -
			(static_cast<float>(FMath::Max(0, FollowerCount - 1)) * 0.5f);
		return LeaderLocation
			- LeaderForward.GetSafeNormal2D() * FMath::Max(0.0f, FollowDistance)
			+ LeaderRight.GetSafeNormal2D() * CenteredIndex * FMath::Max(0.0f, FollowSeparation);
	}

	EMultiplayMannequinCommand ResolveCommandWithoutDetectedSurvivor(
		const EMultiplayMannequinCommand Command)
	{
		return Command == EMultiplayMannequinCommand::ChaseInDetectionRange
			? EMultiplayMannequinCommand::Patrol
			: Command;
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectProject01MatchResolutionTest,
	"ProjectProject01.Multiplayer.MatchResolution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectProject01MatchResolutionTest::RunTest(const FString& Parameters)
{
	const FMatchResolutionSummary InProgress = EvaluateMatchResolution({
		EMultiplayTestSurvivorState::Eliminated,
		EMultiplayTestSurvivorState::Active });
	TestFalse(TEXT("One active survivor keeps the match in progress"), InProgress.IsComplete());

	const FMatchResolutionSummary MannequinVictory = EvaluateMatchResolution({
		EMultiplayTestSurvivorState::Eliminated,
		EMultiplayTestSurvivorState::Eliminated });
	TestTrue(TEXT("All eliminated survivors complete the match"), MannequinVictory.IsComplete());
	TestTrue(TEXT("All eliminated survivors produce a mannequin victory"), MannequinVictory.DidMannequinWin());

	const FMatchResolutionSummary MixedResolution = EvaluateMatchResolution({
		EMultiplayTestSurvivorState::Eliminated,
		EMultiplayTestSurvivorState::Escaped });
	TestTrue(TEXT("Escaped and eliminated survivors complete the match"), MixedResolution.IsComplete());
	TestFalse(TEXT("Any escaped survivor prevents a mannequin victory"), MixedResolution.DidMannequinWin());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectProject01LevelTransitionResolutionTest,
	"ProjectProject01.Multiplayer.LevelTransitionResolution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectProject01LevelTransitionResolutionTest::RunTest(const FString& Parameters)
{
	const FLevelTransitionSummary StillWaiting = EvaluateLevelTransition({
		EMultiplayTestSurvivorState::TransitionWaiting,
		EMultiplayTestSurvivorState::Active });
	TestFalse(TEXT("An active survivor keeps the first survivor waiting"), StillWaiting.CanTravel());

	const FLevelTransitionSummary BothAdvance = EvaluateLevelTransition({
		EMultiplayTestSurvivorState::TransitionWaiting,
		EMultiplayTestSurvivorState::TransitionWaiting });
	TestTrue(TEXT("Two waiting survivors may travel"), BothAdvance.CanTravel());
	TestEqual(TEXT("Two waiting survivors respawn in the next level"), BothAdvance.AdvancingCount, 2);

	const FLevelTransitionSummary OneSurvivorAfterPartnerDeath = EvaluateLevelTransition({
		EMultiplayTestSurvivorState::TransitionWaiting,
		EMultiplayTestSurvivorState::Eliminated });
	TestTrue(TEXT("One waiting survivor may travel after the partner is eliminated"),
		OneSurvivorAfterPartnerDeath.CanTravel());
	TestEqual(TEXT("Only the waiting survivor respawns after partner death"),
		OneSurvivorAfterPartnerDeath.AdvancingCount, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectProject01MultiplayerMannequinAITest,
	"ProjectProject01.Multiplayer.MannequinAIStateAndFormation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectProject01MultiplayerMannequinAITest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Range chase returns to patrol when no survivor remains detected"),
		ResolveCommandWithoutDetectedSurvivor(EMultiplayMannequinCommand::ChaseInDetectionRange),
		EMultiplayMannequinCommand::Patrol);
	TestEqual(TEXT("Follow command remains follow when its temporary detection override ends"),
		ResolveCommandWithoutDetectedSurvivor(EMultiplayMannequinCommand::FollowMannequinPlayer),
		EMultiplayMannequinCommand::FollowMannequinPlayer);

	const FVector LeaderLocation = FVector::ZeroVector;
	const FVector First = CalculateFollowFormationLocation(
		LeaderLocation, FVector::ForwardVector, FVector::RightVector, 0, 3, 300.0f, 175.0f);
	const FVector Second = CalculateFollowFormationLocation(
		LeaderLocation, FVector::ForwardVector, FVector::RightVector, 1, 3, 300.0f, 175.0f);
	const FVector Third = CalculateFollowFormationLocation(
		LeaderLocation, FVector::ForwardVector, FVector::RightVector, 2, 3, 300.0f, 175.0f);
	TestTrue(TEXT("Every formation slot keeps at least the configured leader distance"),
		First.Size2D() >= 300.0f && Second.Size2D() >= 300.0f && Third.Size2D() >= 300.0f);
	TestTrue(TEXT("Adjacent formation slots keep the configured follower separation"),
		FVector::Dist2D(First, Second) >= 175.0f && FVector::Dist2D(Second, Third) >= 175.0f);
	return true;
}
#endif

AMultiplayTestGameMode::AMultiplayTestGameMode()
{
	PrimaryActorTick.bCanEverTick = true;
	// 맵 구간 이동 때 접속을 끊거나 일회용 티켓을 다시 소비하지 않고 컨트롤러를 넘긴다.
	bUseSeamlessTravel = true;

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
	MannequinPawnClass = AMannequinAICharacter::StaticClass();
	static ConstructorHelpers::FClassFinder<AMannequinAICharacter> MannequinPawnClassFinder(
		TEXT("/Game/MyProject/BP_MannequinAICharacter"));
	if (MannequinPawnClassFinder.Succeeded())
	{
		MannequinPawnClass = MannequinPawnClassFinder.Class;
	}
}

void AMultiplayTestGameMode::BeginPlay()
{
	Super::BeginPlay();
	AuthoritativeMatchPhase = EMultiplayTestMatchPhase::Waiting;
	InitializeRoleSpawnPoints();
	BindEscapeTriggers();
	if (GetNetMode() == NM_DedicatedServer)
	{
		if (UProjectProject01GameInstance* GameInstance = Cast<UProjectProject01GameInstance>(GetGameInstance()))
		{
			GameInstance->SendGameServerHeartbeat();
		}
	}

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

void AMultiplayTestGameMode::InitializeRoleSpawnPoints()
{
	if (!HasAuthority())
	{
		return;
	}

	const int32 SurvivorPointCount = GetRoleSpawnPoints(EProjectProject01SpawnRole::Survivor).Num();
	if (SurvivorPointCount < SurvivorSpawnCount)
	{
		UE_LOG(LogProjectProject01Multiplayer, Warning,
			TEXT("MultiplayTest needs at least %d survivor role spawn points, but found %d. Missing players will use default PlayerStarts."),
			SurvivorSpawnCount, SurvivorPointCount);
	}
	InitializeMannequinSpawns();
}

TArray<AProjectProject01RoleSpawnPoint*> AMultiplayTestGameMode::GetRoleSpawnPoints(
	const EProjectProject01SpawnRole SpawnRole) const
{
	TArray<AProjectProject01RoleSpawnPoint*> Result;
	if (!IsValid(GetWorld()))
	{
		return Result;
	}
	for (TActorIterator<AProjectProject01RoleSpawnPoint> It(GetWorld()); It; ++It)
	{
		AProjectProject01RoleSpawnPoint* SpawnPoint = *It;
		if (IsValid(SpawnPoint) && SpawnPoint->GetSpawnRole() == SpawnRole)
		{
			Result.Add(SpawnPoint);
		}
	}
	return Result;
}

void AMultiplayTestGameMode::InitializeMannequinSpawns()
{
	UWorld* World = GetWorld();
	if (!HasAuthority() || !IsValid(World))
	{
		return;
	}

	TArray<AProjectProject01RoleSpawnPoint*> SpawnPoints =
		GetRoleSpawnPoints(EProjectProject01SpawnRole::Mannequin);
	if (SpawnPoints.Num() < MannequinSpawnCount)
	{
		UE_LOG(LogProjectProject01Multiplayer, Warning,
			TEXT("MultiplayTest needs at least %d mannequin role spawn points, but found %d. Keeping the legacy map mannequin placement unchanged."),
			MannequinSpawnCount, SpawnPoints.Num());
		return;
	}

	for (int32 Index = SpawnPoints.Num() - 1; Index > 0; --Index)
	{
		SpawnPoints.Swap(Index, FMath::RandRange(0, Index));
	}
	SpawnPoints.SetNum(MannequinSpawnCount);

	TArray<AMannequinAICharacter*> Mannequins;
	for (TActorIterator<AMannequinAICharacter> It(World); It; ++It)
	{
		if (AMannequinAICharacter* Mannequin = *It; IsValid(Mannequin))
		{
			Mannequins.Add(Mannequin);
		}
	}
	Mannequins.Sort([](const AMannequinAICharacter& Left, const AMannequinAICharacter& Right)
	{
		return Left.GetName() < Right.GetName();
	});

	while (Mannequins.Num() < MannequinSpawnCount)
	{
		if (!MannequinPawnClass)
		{
			UE_LOG(LogProjectProject01Multiplayer, Error,
				TEXT("Cannot create the required mannequins because MannequinPawnClass is not configured."));
			break;
		}
		FActorSpawnParameters SpawnParameters;
		SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AMannequinAICharacter* Spawned = World->SpawnActor<AMannequinAICharacter>(
			MannequinPawnClass, SpawnPoints[Mannequins.Num()]->GetActorTransform(), SpawnParameters);
		if (!IsValid(Spawned))
		{
			UE_LOG(LogProjectProject01Multiplayer, Error, TEXT("Failed to spawn a required mannequin actor."));
			break;
		}
		Mannequins.Add(Spawned);
	}

	for (int32 Index = 0; Index < Mannequins.Num(); ++Index)
	{
		AMannequinAICharacter* Mannequin = Mannequins[Index];
		if (!IsValid(Mannequin))
		{
			continue;
		}
		if (Index >= MannequinSpawnCount)
		{
			Mannequin->Destroy();
			continue;
		}

		Mannequin->SetActorTransform(SpawnPoints[Index]->GetActorTransform(), false, nullptr,
			ETeleportType::TeleportPhysics);
		Mannequin->SetControlSlotForGameMode(Index);
		if (!IsValid(Mannequin->GetController()))
		{
			Mannequin->SpawnDefaultController();
		}
		Mannequin->ForceNetUpdate();
	}

	UE_LOG(LogProjectProject01Multiplayer, Log,
		TEXT("Randomly assigned %d mannequins to %d configured mannequin spawn candidates."),
		FMath::Min(Mannequins.Num(), MannequinSpawnCount), SpawnPoints.Num());
}

void AMultiplayTestGameMode::ApplyMannequinTuning(const FMannequinAITuningRow& Tuning)
{
	SurvivorVisionCheckIntervalSeconds = FMath::Max(0.0f, Tuning.SurvivorVisionCheckIntervalSeconds);
	SurvivorVisionHalfAngleDegrees = FMath::Max(0.0f, Tuning.SurvivorVisionHalfAngleDegrees);
	// 멀티 전용 AI는 기존 싱글 추적 로직과 분리되어 있지만, 검증된 수치만 DataTable에서 받는다.
	MultiplayerDetectionRadius = FMath::Max(0.0f, Tuning.DirectChaseRadius);
	MultiplayerPatrolRadius = FMath::Max(0.0f, Tuning.MultiplayerPatrolRadius);
	MultiplayerFollowDistance = FMath::Max(0.0f, Tuning.MultiplayerFollowDistance);
	MultiplayerFollowSeparation = FMath::Max(0.0f, Tuning.MultiplayerFollowSeparation);
	MultiplayerRepathInterval = FMath::Max(0.01f, Tuning.MultiplayerRepathInterval);
	MultiplayerAcceptanceRadius = FMath::Max(0.0f, Tuning.MultiplayerAcceptanceRadius);
}

bool AMultiplayTestGameMode::CanMannequinDetectSurvivor(
	const AMannequinAICharacter* Mannequin,
	const APlayerCharacter* Survivor) const
{
	if (!IsValid(Mannequin) || !IsActiveSurvivorTarget(Survivor) ||
		FVector::DistSquared2D(Mannequin->GetActorLocation(), Survivor->GetActorLocation()) >
		FMath::Square(static_cast<double>(MultiplayerDetectionRadius)))
	{
		return false;
	}

	const AAIController* AIController = Cast<AAIController>(Mannequin->GetController());
	if (!IsValid(AIController))
	{
		return false;
	}

	// AAIController의 시야 검사는 장애물을 고려한다. 감지 반경은 빨간 원과 같은 DirectChaseRadius다.
	return const_cast<AAIController*>(AIController)->LineOfSightTo(Survivor);
}

bool AMultiplayTestGameMode::IsActiveSurvivorTarget(const APlayerCharacter* Survivor) const
{
	if (!IsValid(Survivor) || Survivor->IsGameOver())
	{
		return false;
	}

	const APlayerController* SurvivorController = Cast<APlayerController>(Survivor->GetController());
	if (!IsSurvivorController(SurvivorController))
	{
		return false;
	}

	const AMultiplayTestPlayerController* MultiplayerController =
		Cast<AMultiplayTestPlayerController>(SurvivorController);
	return !IsValid(MultiplayerController) ||
		MultiplayerController->GetSurvivorState() == EMultiplayTestSurvivorState::Active;
}

APlayerCharacter* AMultiplayTestGameMode::FindDetectedSurvivor(const AMannequinAICharacter* Mannequin) const
{
	APlayerCharacter* Closest = nullptr;
	double ClosestDistanceSquared = TNumericLimits<double>::Max();
	for (TActorIterator<APlayerCharacter> It(GetWorld()); It; ++It)
	{
		APlayerCharacter* Survivor = *It;
		const APlayerController* SurvivorController = IsValid(Survivor)
			? Cast<APlayerController>(Survivor->GetController())
			: nullptr;
		if (!IsSurvivorController(SurvivorController) || !CanMannequinDetectSurvivor(Mannequin, Survivor))
		{
			continue;
		}

		const double DistanceSquared = FVector::DistSquared2D(
			Mannequin->GetActorLocation(), Survivor->GetActorLocation());
		if (DistanceSquared < ClosestDistanceSquared)
		{
			ClosestDistanceSquared = DistanceSquared;
			Closest = Survivor;
		}
	}
	return Closest;
}

AMannequinAICharacter* AMultiplayTestGameMode::GetMannequinPlayerFocusPawn() const
{
	AMultiplayTestPlayerController* Controller = MannequinController.Get();
	if (!IsValid(Controller))
	{
		return nullptr;
	}
	if (AMannequinAICharacter* Controlled = Cast<AMannequinAICharacter>(Controller->GetPawn()); IsValid(Controlled))
	{
		return Controlled;
	}
	return Controller->GetViewedMannequin();
}

void AMultiplayTestGameMode::StopMannequinAI(AMannequinAICharacter* Mannequin) const
{
	if (!IsValid(Mannequin))
	{
		return;
	}
	if (AAIController* AIController = Cast<AAIController>(Mannequin->GetController()); IsValid(AIController))
	{
		AIController->StopMovement();
	}
	if (UCharacterMovementComponent* Movement = Mannequin->GetCharacterMovement(); IsValid(Movement))
	{
		Movement->StopMovementImmediately();
	}
}

void AMultiplayTestGameMode::MoveMannequinToPatrolTarget(
	AMannequinAICharacter* Mannequin,
	AAIController* AIController)
{
	if (!IsValid(Mannequin) || !IsValid(AIController) || MultiplayerPatrolRadius <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	FVector* ExistingTarget = MultiplayerPatrolTargets.Find(Mannequin);
	if (ExistingTarget && FVector::DistSquared2D(Mannequin->GetActorLocation(), *ExistingTarget) >
		FMath::Square(static_cast<double>(MultiplayerAcceptanceRadius)))
	{
		// 생존자 시야로 StopMovement가 호출됐더라도 시야가 풀리면 같은 목적지로
		// A* 요청을 다시 넣어 정찰이 한 번만 수행되고 멈추는 현상을 막는다.
		const EPathFollowingRequestResult::Type ResumeResult = AIController->MoveToLocation(
			*ExistingTarget, MultiplayerAcceptanceRadius, true, true, true, true, nullptr, true);
		if (ResumeResult != EPathFollowingRequestResult::Failed)
		{
			return;
		}
		MultiplayerPatrolTargets.Remove(Mannequin);
	}
	else if (ExistingTarget)
	{
		MultiplayerPatrolTargets.Remove(Mannequin);
	}

	UNavigationSystemV1* Navigation = UNavigationSystemV1::GetCurrent(GetWorld());
	if (!IsValid(Navigation))
	{
		return;
	}
	FNavLocation RandomLocation;
	bool bFoundUsefulLocation = false;
	for (int32 Attempt = 0; Attempt < 8; ++Attempt)
	{
		// 현재 위치를 기준으로 다음 점을 계속 뽑으므로 최초 스폰 지점 주변 한 바퀴로
		// 끝나지 않고 연결된 NavMesh 전역을 향해 연속 정찰한다.
		if (Navigation->GetRandomReachablePointInRadius(
			Mannequin->GetActorLocation(), MultiplayerPatrolRadius, RandomLocation) &&
			FVector::DistSquared2D(Mannequin->GetActorLocation(), RandomLocation.Location) >
			FMath::Square(static_cast<double>(FMath::Max(MultiplayerAcceptanceRadius * 2.0f, 100.0f))))
		{
			bFoundUsefulLocation = true;
			break;
		}
	}
	if (!bFoundUsefulLocation)
	{
		return;
	}

	MultiplayerPatrolTargets.FindOrAdd(Mannequin) = RandomLocation.Location;
	if (AIController->MoveToLocation(RandomLocation.Location, MultiplayerAcceptanceRadius,
		true, true, true, true, nullptr, true) == EPathFollowingRequestResult::Failed)
	{
		MultiplayerPatrolTargets.Remove(Mannequin);
	}
}

void AMultiplayTestGameMode::MoveMannequinToFollowTarget(
	AMannequinAICharacter* Mannequin,
	AAIController* AIController,
	AMannequinAICharacter* Leader,
	const TArray<AMannequinAICharacter*>& Followers)
{
	if (!IsValid(Mannequin) || !IsValid(AIController) || !IsValid(Leader) || Mannequin == Leader)
	{
		return;
	}

	const int32 FollowerIndex = Followers.IndexOfByKey(Mannequin);
	FVector DesiredLocation = CalculateFollowFormationLocation(
		Leader->GetActorLocation(), Leader->GetActorForwardVector(), Leader->GetActorRightVector(),
		FollowerIndex, Followers.Num(), MultiplayerFollowDistance, MultiplayerFollowSeparation);

	// 대형의 기본 슬롯 외에도 현재 위치가 겹치면 반발 벡터를 더해 밀집을 해소한다.
	FVector SeparationOffset = FVector::ZeroVector;
	for (const AMannequinAICharacter* Other : Followers)
	{
		if (!IsValid(Other) || Other == Mannequin)
		{
			continue;
		}
		FVector Away = Mannequin->GetActorLocation() - Other->GetActorLocation();
		Away.Z = 0.0f;
		const float Distance = Away.Size();
		if (Distance < MultiplayerFollowSeparation)
		{
			FVector SeparationDirection;
			if (Distance > KINDA_SMALL_NUMBER)
			{
				SeparationDirection = Away / Distance;
			}
			else
			{
				const int32 OtherIndex = Followers.IndexOfByKey(Other);
				SeparationDirection = OtherIndex < FollowerIndex
					? Leader->GetActorRightVector().GetSafeNormal2D()
					: -Leader->GetActorRightVector().GetSafeNormal2D();
			}
			SeparationOffset += SeparationDirection * (MultiplayerFollowSeparation - Distance);
		}
	}
	DesiredLocation += SeparationOffset;

	// 리더와 너무 가까워졌다면 현재 방사 방향으로 먼저 밀어내어 리더 주위를
	// 빙빙 돌면서 달라붙는 경로 재계산을 방지한다.
	FVector LeaderToFollower = Mannequin->GetActorLocation() - Leader->GetActorLocation();
	LeaderToFollower.Z = 0.0f;
	if (LeaderToFollower.SizeSquared() < FMath::Square(static_cast<double>(MultiplayerFollowDistance)))
	{
		FVector AwayFromLeader = LeaderToFollower.GetSafeNormal();
		if (AwayFromLeader.IsNearlyZero())
		{
			AwayFromLeader = -Leader->GetActorForwardVector().GetSafeNormal2D();
		}
		DesiredLocation = Leader->GetActorLocation() + AwayFromLeader * MultiplayerFollowDistance + SeparationOffset;
	}

	if (UNavigationSystemV1* Navigation = UNavigationSystemV1::GetCurrent(GetWorld()); IsValid(Navigation))
	{
		FNavLocation Projected;
		if (Navigation->ProjectPointToNavigation(DesiredLocation, Projected))
		{
			DesiredLocation = Projected.Location;
		}
	}
	const float FormationDeadZone = FMath::Max(MultiplayerAcceptanceRadius, MultiplayerFollowDistance * 0.15f);
	if (FVector::DistSquared2D(Mannequin->GetActorLocation(), DesiredLocation) <=
		FMath::Square(static_cast<double>(FormationDeadZone)))
	{
		StopMannequinAI(Mannequin);
		return;
	}
	AIController->MoveToLocation(DesiredLocation, MultiplayerAcceptanceRadius,
		true, true, true, true, nullptr, true);
}

void AMultiplayTestGameMode::UpdateMultiplayerMannequinAI()
{
	if (!HasAuthority() || !IsValid(GetWorld()))
	{
		return;
	}

	TArray<AMannequinAICharacter*> Mannequins;
	TArray<AMannequinAICharacter*> Followers;
	for (TActorIterator<AMannequinAICharacter> It(GetWorld()); It; ++It)
	{
		AMannequinAICharacter* Mannequin = *It;
		if (!IsValid(Mannequin))
		{
			continue;
		}
		Mannequins.Add(Mannequin);
		if (Mannequin->GetMultiplayCommand() == EMultiplayMannequinCommand::FollowMannequinPlayer)
		{
			Followers.Add(Mannequin);
		}
	}
	Followers.Sort([](const AMannequinAICharacter& Left, const AMannequinAICharacter& Right)
	{
		return Left.GetControlSlot() < Right.GetControlSlot();
	});
	AMannequinAICharacter* Leader = GetMannequinPlayerFocusPawn();
	Followers.Remove(Leader);

	for (AMannequinAICharacter* Mannequin : Mannequins)
	{
		if (!IsValid(Mannequin) || Mannequin->IsManualControlEnabled())
		{
			continue;
		}
		AAIController* AIController = Cast<AAIController>(Mannequin->GetController());
		if (!IsValid(AIController))
		{
			continue;
		}

		const EMultiplayMannequinCommand Command = Mannequin->GetMultiplayCommand();
		if (Mannequin->IsFrozenBySurvivorVision() || Command == EMultiplayMannequinCommand::HoldPosition)
		{
			StopMannequinAI(Mannequin);
			continue;
		}

		// 정지 외의 모든 명령보다 각 마네킹 자신의 빨간 원 감지를 우선한다.
		APlayerCharacter* ChaseTarget = FindDetectedSurvivor(Mannequin);
		if (!IsValid(ChaseTarget) && Command == EMultiplayMannequinCommand::Patrol)
		{
			if (TWeakObjectPtr<APlayerCharacter>* ObservedTarget = MultiplayerObservedChaseTargets.Find(Mannequin);
				ObservedTarget && IsActiveSurvivorTarget(ObservedTarget->Get()))
			{
				// 생존자가 마네킹을 본 동안에는 정지하고, 시선을 돌린 직후 기억한 생존자를 추격한다.
				ChaseTarget = ObservedTarget->Get();
			}
			else
			{
				MultiplayerObservedChaseTargets.Remove(Mannequin);
			}
		}
		if (IsValid(ChaseTarget))
		{
			AIController->MoveToActor(ChaseTarget, MultiplayerAcceptanceRadius,
				true, true, true, nullptr, true);
			continue;
		}

		switch (Command)
		{
		case EMultiplayMannequinCommand::Patrol:
			MoveMannequinToPatrolTarget(Mannequin, AIController);
			break;
		case EMultiplayMannequinCommand::ChaseInDetectionRange:
			// 추격 대상이 잡히거나 사망한 경우 다른 범위 내 생존자는 위에서 즉시 이어서 추격한다.
			// 유효한 범위 내 생존자가 하나도 없으면 명령을 포기하고 연속 정찰로 복귀한다.
			Mannequin->SetMultiplayCommand(ResolveCommandWithoutDetectedSurvivor(Command));
			MultiplayerObservedChaseTargets.Remove(Mannequin);
			MultiplayerPatrolTargets.Remove(Mannequin);
			MoveMannequinToPatrolTarget(Mannequin, AIController);
			break;
		case EMultiplayMannequinCommand::FollowMannequinPlayer:
			if (IsValid(Leader) && Leader != Mannequin)
			{
				MoveMannequinToFollowTarget(Mannequin, AIController, Leader, Followers);
			}
			else
			{
				StopMannequinAI(Mannequin);
			}
			break;
		case EMultiplayMannequinCommand::HoldPosition:
		default:
			StopMannequinAI(Mannequin);
			break;
		}
	}

	for (auto It = MultiplayerPatrolTargets.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid())
		{
			It.RemoveCurrent();
		}
	}
	for (auto It = MultiplayerObservedChaseTargets.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid() || !IsActiveSurvivorTarget(It.Value().Get()))
		{
			It.RemoveCurrent();
		}
	}
}

void AMultiplayTestGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!HasAuthority() || DeltaSeconds <= 0.0f)
	{
		return;
	}
	if (GetNetMode() == NM_DedicatedServer)
	{
		GameServerHeartbeatAccumulatorSeconds += DeltaSeconds;
		if (GameServerHeartbeatAccumulatorSeconds >= 3.0f)
		{
			GameServerHeartbeatAccumulatorSeconds = 0.0f;
			if (UProjectProject01GameInstance* GameInstance = Cast<UProjectProject01GameInstance>(GetGameInstance()))
			{
				GameInstance->SendGameServerHeartbeat();
			}
		}
	}

	if (AuthoritativeMatchPhase == EMultiplayTestMatchPhase::InProgress &&
		MatchTimeLimitSeconds > 0.0f && GetElapsedMatchSeconds() >= MatchTimeLimitSeconds)
	{
		FinishAuthoritativeMatch(TEXT("TimeLimit"));
	}
	ProcessReconnectTimeouts();

	MultiplayerAIUpdateAccumulatorSeconds += DeltaSeconds;
	if (MultiplayerAIUpdateAccumulatorSeconds >= FMath::Max(0.01f, MultiplayerRepathInterval))
	{
		MultiplayerAIUpdateAccumulatorSeconds = 0.0f;
		UpdateMultiplayerMannequinAI();
	}

	SurvivorVisionCheckAccumulatorSeconds += DeltaSeconds;
	if (SurvivorVisionCheckAccumulatorSeconds < FMath::Max(SurvivorVisionCheckIntervalSeconds, 0.01f))
	{
		return;
	}

	SurvivorVisionCheckAccumulatorSeconds = 0.0f;
	UpdateSurvivorVisionFrozenStates();
}

void AMultiplayTestGameMode::PreLogin(
	const FString& Options,
	const FString& Address,
	const FUniqueNetIdRepl& UniqueId,
	FString& ErrorMessage)
{
	Super::PreLogin(Options, Address, UniqueId, ErrorMessage);
	if (!ErrorMessage.IsEmpty())
	{
		return;
	}
	if (GetNetMode() == NM_Standalone || !bRequireGameJoinTicket || IsLocalEditorPlaySession(GetWorld()))
	{
		if (GetNumPlayers() >= ExpectedMatchPlayers)
		{
			ErrorMessage = TEXT("The match is full.");
		}
		return;
	}

	const FString Ticket = UGameplayStatics::ParseOption(Options, TEXT("GameTicket"));
	const FString RequestedRoomId = UGameplayStatics::ParseOption(Options, TEXT("LobbyRoomId"));
	FProjectProject01ValidatedJoinClaim Claim;
	if (Ticket.IsEmpty() || !UProjectProject01GameInstance::ConsumeValidatedJoinClaim(Ticket, Claim))
	{
		ErrorMessage = TEXT("A valid one-time game connection ticket is required.");
		UE_LOG(LogProjectProject01Multiplayer, Warning,
			TEXT("Security rejected a connection from %s without a validated game ticket."), *Address);
		return;
	}
	if (Claim.RoomId.IsEmpty() || Claim.MatchId.IsEmpty() ||
		(Claim.Role != TEXT("Mannequin") && Claim.Role != TEXT("Survivor")) ||
		(!RequestedRoomId.IsEmpty() && !RequestedRoomId.Equals(Claim.RoomId, ESearchCase::CaseSensitive)))
	{
		ErrorMessage = TEXT("The game connection ticket does not match this room.");
		UE_LOG(LogProjectProject01Multiplayer, Warning,
			TEXT("Security rejected a game ticket with inconsistent room or role data."));
		return;
	}
	FString VersionError;
	if (!FProjectProject01VersionContract::Matches(
		Claim.ClientBuildVersion, Claim.DedicatedServerBuildVersion, Claim.ApiVersion, Claim.GameDataVersion,
		Claim.NetworkProtocolVersion, VersionError))
	{
		ErrorMessage = VersionError;
		UE_LOG(LogProjectProject01Multiplayer, Warning,
			TEXT("Security rejected an incompatible client: %s"), *VersionError);
		return;
	}

	for (const TPair<TWeakObjectPtr<AMultiplayTestPlayerController>, FMultiplayTestServerPlayerRecord>& Pair : MatchPlayerRecords)
	{
		if (!Claim.UserId.IsEmpty() && Pair.Value.UserId == Claim.UserId && Pair.Key.IsValid())
		{
			ErrorMessage = TEXT("This account is already connected to the match.");
			return;
		}
	}
	const FMultiplayTestServerPlayerRecord* DisconnectedRecord =
		Claim.UserId.IsEmpty() ? nullptr : DisconnectedMatchRecords.Find(Claim.UserId);
	const FMultiplayTestMatchResult* CompletedResult =
		Claim.UserId.IsEmpty() ? nullptr : CompletedResultsByUserId.Find(Claim.UserId);
	const bool bKnownReconnect =
		(DisconnectedRecord != nullptr && !DisconnectedRecord->bForfeited &&
		 DisconnectedRecord->MatchId == Claim.MatchId && DisconnectedRecord->Role == Claim.Role) ||
		(CompletedResult != nullptr && CompletedResult->MatchId == Claim.MatchId && CompletedResult->Role == Claim.Role);
	if ((AuthoritativeMatchPhase == EMultiplayTestMatchPhase::InProgress ||
		 AuthoritativeMatchPhase == EMultiplayTestMatchPhase::Ending ||
		 AuthoritativeMatchPhase == EMultiplayTestMatchPhase::Results) && !bKnownReconnect)
	{
		ErrorMessage = TEXT("The match is already in progress and this account has no reconnect slot.");
		return;
	}
	if (!bKnownReconnect && GetNumPlayers() >= ExpectedMatchPlayers)
	{
		ErrorMessage = TEXT("The match is full.");
		return;
	}

	PendingValidatedJoinClaims.Add(Ticket, MoveTemp(Claim));
}

FString AMultiplayTestGameMode::InitNewPlayer(
	APlayerController* NewPlayerController,
	const FUniqueNetIdRepl& UniqueId,
	const FString& Options,
	const FString& Portal)
{
	AMultiplayTestPlayerController* MultiplayController = Cast<AMultiplayTestPlayerController>(NewPlayerController);
	FString LobbyRole;
	const FString Ticket = UGameplayStatics::ParseOption(Options, TEXT("GameTicket"));
	if (FProjectProject01ValidatedJoinClaim* Claim = PendingValidatedJoinClaims.Find(Ticket))
	{
		LobbyRole = Claim->Role;
		if (IsValid(MultiplayController))
		{
			MultiplayController->SetAuthenticatedLobbyIdentity(*Claim);
		}
		PendingValidatedJoinClaims.Remove(Ticket);
	}
	else if (!bRequireGameJoinTicket || GetNetMode() == NM_Standalone || IsLocalEditorPlaySession(GetWorld()))
	{
		// 티켓 검증을 명시적으로 끈 로컬 호환 모드에서만 과거 URL 역할 옵션을 허용한다.
		LobbyRole = UGameplayStatics::ParseOption(Options, TEXT("LobbyRole"));
	}
	if (IsValid(MultiplayController) &&
		(LobbyRole.Equals(TEXT("Mannequin"), ESearchCase::IgnoreCase) ||
		 LobbyRole.Equals(TEXT("Survivor"), ESearchCase::IgnoreCase)))
	{
		ExplicitRoleControllers.Add(MultiplayController);
		if (LobbyRole.Equals(TEXT("Mannequin"), ESearchCase::IgnoreCase))
		{
			RequestedMannequinControllers.Add(MultiplayController);
		}
		UE_LOG(LogProjectProject01Multiplayer, Log,
			TEXT("%s received validated lobby role %s."), *GetNameSafe(MultiplayController), *LobbyRole);
	}
	return Super::InitNewPlayer(NewPlayerController, UniqueId, Options, Portal);
}

void AMultiplayTestGameMode::PostLogin(APlayerController* NewPlayer)
{
	AMultiplayTestPlayerController* NewMultiplayController = Cast<AMultiplayTestPlayerController>(NewPlayer);
	const bool bHasExplicitRole = IsValid(NewMultiplayController) && ExplicitRoleControllers.Contains(NewMultiplayController);
	const bool bRequestedMannequin = IsValid(NewMultiplayController) && RequestedMannequinControllers.Contains(NewMultiplayController);
	if (IsValid(NewMultiplayController) && !MannequinController.IsValid() &&
		(bRequestedMannequin || !bHasExplicitRole))
	{
		MannequinController = NewMultiplayController;
		UE_LOG(LogProjectProject01Multiplayer, Log,
			TEXT("%s was assigned as the mannequin controller (%s)."), *NewMultiplayController->GetName(),
			bRequestedMannequin ? TEXT("lobby role") : TEXT("legacy first-player fallback"));
	}
	else if (!IsValid(NewMultiplayController))
	{
		UE_LOG(LogProjectProject01Multiplayer, Error,
			TEXT("MultiplayTest requires AMultiplayTestPlayerController, but %s connected."), *GetNameSafe(NewPlayer));
	}

	Super::PostLogin(NewPlayer);

	if (IsValid(NewMultiplayController))
	{
		if (const FMultiplayTestMatchResult* CompletedResult = CompletedResultsByUserId.Find(
			NewMultiplayController->GetAuthenticatedUserId()))
		{
			NewMultiplayController->SetServerMatchState(EMultiplayTestMatchPhase::Results,
				CompletedResult->Role == TEXT("Survivor") ? EMultiplayTestSurvivorState::Eliminated : EMultiplayTestSurvivorState::NotApplicable);
			NewMultiplayController->DeliverMatchResultToOwner(*CompletedResult);
			return;
		}
		if (RestoreDisconnectedMatchPlayer(NewMultiplayController))
		{
			return;
		}
	}

	// 마네킹 역할은 Pawn을 빙의하지 않고 시작한다. 서버가 유효한 0~9 슬롯 중 하나를
	// 무작위로 골라 시점만 배정하며, 해당 마네킹의 AIController는 R 입력 전까지 유지된다.
	if (IsMannequinController(NewMultiplayController))
	{
		AssignRandomInitialMannequinView(NewMultiplayController);
	}

	RegisterMatchPlayer(NewMultiplayController);
	TryStartAuthoritativeMatch();
}

void AMultiplayTestGameMode::HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer)
{
	const AMultiplayTestPlayerController* MultiplayController =
		Cast<AMultiplayTestPlayerController>(NewPlayer);
	if (IsMannequinController(MultiplayController))
	{
		// 마네킹 역할은 일반 생존자 Pawn을 생성하지 않지만, OnlySpectator로 만들지도 않는다.
		// OnlySpectator는 APlayerController::OnPossess에서 이후 R 빙의를 거부하므로 시점 전용 대기만 수행한다.
		return;
	}
	if (IsValid(MultiplayController) && MultiplayController->IsLevelTransitionCarryover() &&
		!MultiplayController->ShouldSpawnAfterLevelTransition())
	{
		// 이전 맵에서 탈락했거나 다음 구간에 도착하지 못한 생존자는 새 Pawn을 만들지 않는다.
		return;
	}

	Super::HandleStartingNewPlayer_Implementation(NewPlayer);
}

void AMultiplayTestGameMode::HandleSeamlessTravelPlayer(AController*& Controller)
{
	Super::HandleSeamlessTravelPlayer(Controller);

	AMultiplayTestPlayerController* MultiplayController =
		Cast<AMultiplayTestPlayerController>(Controller);
	if (!IsValid(MultiplayController))
	{
		return;
	}

	ExplicitRoleControllers.Add(MultiplayController);
	if (MultiplayController->GetAuthenticatedRole().Equals(TEXT("Mannequin"), ESearchCase::IgnoreCase))
	{
		RequestedMannequinControllers.Add(MultiplayController);
		MannequinController = MultiplayController;
		AssignRandomInitialMannequinView(MultiplayController);
	}

	RegisterMatchPlayer(MultiplayController);
	TryStartAuthoritativeMatch();
}

AActor* AMultiplayTestGameMode::ChoosePlayerStart_Implementation(AController* Player)
{
	if (!HasAuthority() || !IsValid(Player) || IsMannequinController(Cast<AMultiplayTestPlayerController>(Player)))
	{
		return Super::ChoosePlayerStart_Implementation(Player);
	}

	if (const TWeakObjectPtr<AActor>* Existing = AssignedSurvivorSpawnPoints.Find(Player);
		Existing != nullptr && Existing->IsValid())
	{
		return Existing->Get();
	}

	TSet<const AActor*> AlreadyAssigned;
	for (const TPair<TWeakObjectPtr<AController>, TWeakObjectPtr<AActor>>& Pair : AssignedSurvivorSpawnPoints)
	{
		if (Pair.Value.IsValid())
		{
			AlreadyAssigned.Add(Pair.Value.Get());
		}
	}

	TArray<AProjectProject01RoleSpawnPoint*> Available;
	for (AProjectProject01RoleSpawnPoint* SpawnPoint : GetRoleSpawnPoints(EProjectProject01SpawnRole::Survivor))
	{
		if (IsValid(SpawnPoint) && !AlreadyAssigned.Contains(SpawnPoint))
		{
			Available.Add(SpawnPoint);
		}
	}
	if (Available.IsEmpty())
	{
		UE_LOG(LogProjectProject01Multiplayer, Error,
			TEXT("No unclaimed survivor role spawn point is available. Falling back to the default PlayerStart selection."));
		return Super::ChoosePlayerStart_Implementation(Player);
	}

	AProjectProject01RoleSpawnPoint* Selected = Available[FMath::RandHelper(Available.Num())];
	AssignedSurvivorSpawnPoints.Add(Player, Selected);
	UE_LOG(LogProjectProject01Multiplayer, Log, TEXT("Assigned survivor %s to random spawn point %s."),
		*GetNameSafe(Player), *GetNameSafe(Selected));
	return Selected;
}

void AMultiplayTestGameMode::Logout(AController* Exiting)
{
	AMultiplayTestPlayerController* ExitingMultiplayController = Cast<AMultiplayTestPlayerController>(Exiting);
	FMultiplayTestServerPlayerRecord LeavingRecord;
	bool bHadMatchRecord = false;
	if (FMultiplayTestServerPlayerRecord* Record = MatchPlayerRecords.Find(ExitingMultiplayController))
	{
		SnapshotPlayerRecord(ExitingMultiplayController, *Record);
		LeavingRecord = *Record;
		bHadMatchRecord = true;
	}
	RequestedMannequinControllers.Remove(ExitingMultiplayController);
	ExplicitRoleControllers.Remove(ExitingMultiplayController);
	AssignedSurvivorSpawnPoints.Remove(Exiting);
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

	if (bHadMatchRecord && AuthoritativeMatchPhase == EMultiplayTestMatchPhase::InProgress &&
		!LeavingRecord.bForfeited && !ExitingMultiplayController->bVoluntaryExitDeclared)
	{
		LeavingRecord.bDisconnected = true;
		LeavingRecord.ReconnectDeadlineWorldSeconds = GetWorld()->GetTimeSeconds() + ReconnectGraceSeconds;
		DisconnectedMatchRecords.Add(LeavingRecord.UserId, LeavingRecord);
		UE_LOG(LogProjectProject01Multiplayer, Warning,
			TEXT("%s disconnected unexpectedly; reconnect is allowed for %.1f seconds."),
			*LeavingRecord.UserId, ReconnectGraceSeconds);
	}
	else if (bHadMatchRecord && LeavingRecord.bForfeited && !LeavingRecord.UserId.IsEmpty())
	{
		DisconnectedMatchRecords.Add(LeavingRecord.UserId, LeavingRecord);
	}
	if (AuthoritativeMatchPhase != EMultiplayTestMatchPhase::Ending &&
		AuthoritativeMatchPhase != EMultiplayTestMatchPhase::Results)
	{
		MatchPlayerRecords.Remove(ExitingMultiplayController);
		ResultVerificationStatus.Remove(ExitingMultiplayController);
	}

	Super::Logout(Exiting);

	// 방에 남은 모든 사용자가 복귀한 뒤 다음 경기를 시작하면 이전 경기의 Actor 상태가
	// 남지 않도록, 마지막 접속자가 빠진 데디케이티드 서버 월드를 초기 맵으로 다시 연다.
	// 짧은 지연은 같은 프레임의 Logout/PlayerArray 정리가 끝난 뒤 인원 수를 확인하기 위함이다.
	if (UWorld* World = GetWorld(); IsValid(World) && World->GetNetMode() == NM_DedicatedServer)
	{
		World->GetTimerManager().ClearTimer(EmptyDedicatedServerResetTimer);
		World->GetTimerManager().SetTimer(
			EmptyDedicatedServerResetTimer,
			this,
			&AMultiplayTestGameMode::ResetDedicatedServerWorldIfEmpty,
			1.0f,
			false);
	}
}

void AMultiplayTestGameMode::DeclareVoluntaryExit(AMultiplayTestPlayerController* RequestingController)
{
	if (!HasAuthority() || !IsValid(RequestingController) ||
		AuthoritativeMatchPhase != EMultiplayTestMatchPhase::InProgress)
	{
		return;
	}
	if (FMultiplayTestServerPlayerRecord* Record = MatchPlayerRecords.Find(RequestingController))
	{
		SnapshotPlayerRecord(RequestingController, *Record);
		ApplyForfeit(*Record, TEXT("VoluntaryExit"));
		RequestingController->SetServerMatchState(AuthoritativeMatchPhase, Record->SurvivorState);
		if (Record->Role == TEXT("Survivor") &&
			AuthoritativeMatchPhase == EMultiplayTestMatchPhase::InProgress)
		{
			EnterSurvivorSpectator(RequestingController);
		}
	}
}

void AMultiplayTestGameMode::BindEscapeTriggers()
{
	if (!HasAuthority() || !IsValid(GetWorld()))
	{
		return;
	}

	int32 BoundTriggerCount = 0;
	for (TActorIterator<ATriggerBox> It(GetWorld()); It; ++It)
	{
		ATriggerBox* Trigger = *It;
		if (!IsValid(Trigger) || !Trigger->ActorHasTag(EscapeTriggerActorTag))
		{
			continue;
		}

		Trigger->OnActorBeginOverlap.AddUniqueDynamic(
			this, &AMultiplayTestGameMode::HandleEscapeTriggerBeginOverlap);
		++BoundTriggerCount;
	}

	UE_LOG(LogProjectProject01Multiplayer, Log,
		TEXT("Bound %d escape TriggerBox actor(s) tagged %s."),
		BoundTriggerCount, *EscapeTriggerActorTag.ToString());
}

void AMultiplayTestGameMode::HandleEscapeTriggerBeginOverlap(AActor* OverlappedActor, AActor* OtherActor)
{
	MarkSurvivorEscaped(Cast<APlayerCharacter>(OtherActor));
}

void AMultiplayTestGameMode::RegisterMatchPlayer(AMultiplayTestPlayerController* Controller)
{
	if (!HasAuthority() || !IsValid(Controller) || MatchPlayerRecords.Contains(Controller))
	{
		return;
	}

	FMultiplayTestServerPlayerRecord Record;
	Record.UserId = Controller->GetAuthenticatedUserId();
	Record.MatchId = Controller->GetAuthenticatedMatchId();
	// 티켓 검증을 생략하는 Standalone/로컬 PIE에서도 통신 기능을 서로 격리된
	// 하나의 테스트 매치로 묶는다. 실제 티켓 기반 접속에서는 인증된 값을 그대로 쓴다.
	if (Record.UserId.IsEmpty() &&
		(GetNetMode() == NM_Standalone || !bRequireGameJoinTicket || IsLocalEditorPlaySession(GetWorld())))
	{
		Record.UserId = FString::Printf(TEXT("LocalPIE-%d"), Controller->GetUniqueID());
	}
	if (Record.MatchId.IsEmpty() &&
		(GetNetMode() == NM_Standalone || !bRequireGameJoinTicket || IsLocalEditorPlaySession(GetWorld())))
	{
		if (AuthoritativeMatchId.IsEmpty())
		{
			AuthoritativeMatchId = FString::Printf(TEXT("LocalPIE-%s"), *GetWorld()->GetMapName());
		}
		Record.MatchId = AuthoritativeMatchId;
	}
	Record.Role = IsMannequinController(Controller) ? TEXT("Mannequin") : TEXT("Survivor");
	Record.SurvivorState = Record.Role == TEXT("Survivor")
		? (Controller->IsLevelTransitionCarryover() && !Controller->ShouldSpawnAfterLevelTransition()
			? EMultiplayTestSurvivorState::Eliminated
			: EMultiplayTestSurvivorState::Active)
		: EMultiplayTestSurvivorState::NotApplicable;
	Record.SurvivorPawn = Record.Role == TEXT("Survivor")
		? Cast<APlayerCharacter>(Controller->GetPawn())
		: nullptr;

	if (AuthoritativeMatchId.IsEmpty() && !Record.MatchId.IsEmpty())
	{
		AuthoritativeMatchId = Record.MatchId;
	}
	MatchPlayerRecords.Add(Controller, MoveTemp(Record));
	Controller->SetServerMatchState(
		EMultiplayTestMatchPhase::RoleAssignment,
		MatchPlayerRecords.FindChecked(Controller).SurvivorState);
	Controller->ClientConfigureSurvivorCommunication(
		MatchPlayerRecords.FindChecked(Controller).UserId,
		MatchPlayerRecords.FindChecked(Controller).MatchId,
		MatchPlayerRecords.FindChecked(Controller).Role);
}

bool AMultiplayTestGameMode::RestoreDisconnectedMatchPlayer(AMultiplayTestPlayerController* Controller)
{
	if (!HasAuthority() || !IsValid(Controller) || Controller->GetAuthenticatedUserId().IsEmpty())
	{
		return false;
	}
	FMultiplayTestServerPlayerRecord* Saved = DisconnectedMatchRecords.Find(Controller->GetAuthenticatedUserId());
	if (Saved == nullptr || Saved->MatchId != Controller->GetAuthenticatedMatchId() || Saved->bForfeited)
	{
		return false;
	}

	FMultiplayTestServerPlayerRecord Restored = *Saved;
	DisconnectedMatchRecords.Remove(Controller->GetAuthenticatedUserId());
	Restored.bDisconnected = false;
	Restored.ReconnectDeadlineWorldSeconds = 0.0;
	if (Restored.Role == TEXT("Survivor"))
	{
		APlayerCharacter* Survivor = Cast<APlayerCharacter>(Controller->GetPawn());
		Restored.SurvivorPawn = Survivor;
		if (IsValid(Survivor))
		{
			Survivor->SetActorTransform(Restored.LastPawnTransform, false, nullptr, ETeleportType::TeleportPhysics);
			Survivor->SetRemainingDeathCountForGameMode(Restored.RemainingDeathCount);
			if (Restored.SurvivorState != EMultiplayTestSurvivorState::Active)
			{
				if (UCharacterMovementComponent* Movement = Survivor->GetCharacterMovement(); IsValid(Movement))
				{
					Movement->StopMovementImmediately();
					Movement->DisableMovement();
				}
				Survivor->SetActorEnableCollision(false);
				Survivor->SetActorHiddenInGame(true);
			}
		}
	}
	MatchPlayerRecords.Add(Controller, Restored);
	Controller->SetServerMatchState(AuthoritativeMatchPhase, Restored.SurvivorState);
	Controller->ClientConfigureSurvivorCommunication(
		Restored.UserId, Restored.MatchId, Restored.Role);

	if (Restored.Role == TEXT("Mannequin"))
	{
		MannequinController = Controller;
		if (Restored.ViewedMannequinSlot >= 0)
		{
			TryPossessMannequin(Controller, Restored.ViewedMannequinSlot);
			if (Restored.bWasManualMannequinControl)
			{
				TryEnableMannequinManualControl(Controller);
			}
		}
	}
	else if (Restored.SurvivorState == EMultiplayTestSurvivorState::Eliminated ||
		Restored.SurvivorState == EMultiplayTestSurvivorState::Escaped)
	{
		APlayerCharacter* Target = FindLivingSurvivorSpectatorTarget(Controller, 1);
		Controller->EnterSurvivorSpectator(
			Target, Restored.SurvivorState == EMultiplayTestSurvivorState::Escaped && IsValid(Target));
	}

	UE_LOG(LogProjectProject01Multiplayer, Log,
		TEXT("Restored reconnecting %s player %s for match %s."),
		*Restored.Role, *Restored.UserId, *Restored.MatchId);
	return true;
}

void AMultiplayTestGameMode::SnapshotPlayerRecord(
	AMultiplayTestPlayerController* Controller,
	FMultiplayTestServerPlayerRecord& Record)
{
	if (!IsValid(Controller))
	{
		return;
	}
	if (Record.Role == TEXT("Survivor"))
	{
		APlayerCharacter* Survivor = Cast<APlayerCharacter>(Controller->GetPawn());
		if (!IsValid(Survivor))
		{
			Survivor = Record.SurvivorPawn.Get();
		}
		if (IsValid(Survivor))
		{
			Record.LastPawnTransform = Survivor->GetActorTransform();
			Record.RemainingDeathCount = Survivor->GetRemainingDeathCount();
		}
	}
	else
	{
		if (AMannequinAICharacter* Viewed = Controller->GetViewedMannequin(); IsValid(Viewed))
		{
			Record.ViewedMannequinSlot = Viewed->GetControlSlot();
		}
		Record.bWasManualMannequinControl = Cast<AMannequinAICharacter>(Controller->GetPawn()) != nullptr;
	}
}

void AMultiplayTestGameMode::ApplyForfeit(FMultiplayTestServerPlayerRecord& Record, const FString& Reason)
{
	if (Record.bForfeited || AuthoritativeMatchPhase != EMultiplayTestMatchPhase::InProgress)
	{
		return;
	}
	Record.bForfeited = true;
	Record.bDisconnected = false;
	UE_LOG(LogProjectProject01Multiplayer, Warning,
		TEXT("Player %s (%s) forfeited: %s."), *Record.UserId, *Record.Role, *Reason);
	if (Reason == TEXT("ReconnectTimeout"))
	{
		if (UProjectProject01GameInstance* GameInstance = GetGameInstance<UProjectProject01GameInstance>();
			IsValid(GameInstance))
		{
			GameInstance->NotifyAuthoritativePlayerForfeit(
				AuthoritativeMatchId, Record.UserId, Record.Role);
		}
	}

	if (Record.Role == TEXT("Mannequin"))
	{
		bMatchEndedByForfeit = true;
		bSurvivorVictoryByMannequinForfeit = true;
		FinishAuthoritativeMatch(TEXT("MannequinForfeit"));
		return;
	}

	Record.SurvivorState = EMultiplayTestSurvivorState::Eliminated;
	if (APlayerCharacter* Survivor = Record.SurvivorPawn.Get(); IsValid(Survivor))
	{
		if (UCharacterMovementComponent* Movement = Survivor->GetCharacterMovement(); IsValid(Movement))
		{
			Movement->StopMovementImmediately();
			Movement->DisableMovement();
		}
		Survivor->SetActorEnableCollision(false);
		Survivor->SetActorHiddenInGame(true);
	}
	CheckForMatchCompletion();
}

void AMultiplayTestGameMode::ProcessReconnectTimeouts()
{
	if (AuthoritativeMatchPhase != EMultiplayTestMatchPhase::InProgress || !IsValid(GetWorld()))
	{
		return;
	}
	const double Now = GetWorld()->GetTimeSeconds();
	for (TPair<FString, FMultiplayTestServerPlayerRecord>& Pair : DisconnectedMatchRecords)
	{
		if (!Pair.Value.bForfeited && Pair.Value.ReconnectDeadlineWorldSeconds > 0.0 &&
			Now >= Pair.Value.ReconnectDeadlineWorldSeconds)
		{
			ApplyForfeit(Pair.Value, TEXT("ReconnectTimeout"));
			if (AuthoritativeMatchPhase != EMultiplayTestMatchPhase::InProgress)
			{
				break;
			}
		}
	}
}

void AMultiplayTestGameMode::TryStartAuthoritativeMatch()
{
	if (!HasAuthority() || AuthoritativeMatchPhase == EMultiplayTestMatchPhase::InProgress ||
		AuthoritativeMatchPhase == EMultiplayTestMatchPhase::Ending ||
		AuthoritativeMatchPhase == EMultiplayTestMatchPhase::Results ||
		MatchPlayerRecords.Num() < ExpectedMatchPlayers)
	{
		return;
	}

	int32 MannequinCount = 0;
	int32 SurvivorCount = 0;
	FString ValidatedMatchId;
	for (const TPair<TWeakObjectPtr<AMultiplayTestPlayerController>, FMultiplayTestServerPlayerRecord>& Pair : MatchPlayerRecords)
	{
		const FMultiplayTestServerPlayerRecord& Record = Pair.Value;
		MannequinCount += Record.Role == TEXT("Mannequin") ? 1 : 0;
		SurvivorCount += Record.Role == TEXT("Survivor") ? 1 : 0;
		if (!Record.MatchId.IsEmpty())
		{
			if (ValidatedMatchId.IsEmpty())
			{
				ValidatedMatchId = Record.MatchId;
			}
			else if (ValidatedMatchId != Record.MatchId)
			{
				UE_LOG(LogProjectProject01Multiplayer, Error,
					TEXT("Cannot start: authenticated players have different MatchId values."));
				return;
			}
		}
	}

	if (MannequinCount != 1 || SurvivorCount != ExpectedMatchPlayers - 1)
	{
		return;
	}

	AuthoritativeMatchId = ValidatedMatchId.IsEmpty()
		? FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower)
		: ValidatedMatchId;
	MatchStartWorldSeconds = GetWorld()->GetTimeSeconds();
	MatchEndWorldSeconds = 0.0;
	bFinishCommitted = false;
	bMatchEndedByForfeit = false;
	bSurvivorVictoryByMannequinForfeit = false;
	DisconnectedMatchRecords.Reset();
	CompletedResultsByUserId.Reset();
	ResultVerificationStatus.Reset();
	SetAuthoritativeMatchPhase(EMultiplayTestMatchPhase::InProgress);
	FinalizeLevelTransitionArrivals();
	UE_LOG(LogProjectProject01Multiplayer, Log,
		TEXT("Authoritative match %s started with %d player(s)."),
		*AuthoritativeMatchId, MatchPlayerRecords.Num());
}

void AMultiplayTestGameMode::FinalizeLevelTransitionArrivals()
{
	for (TPair<TWeakObjectPtr<AMultiplayTestPlayerController>, FMultiplayTestServerPlayerRecord>& Pair :
		MatchPlayerRecords)
	{
		AMultiplayTestPlayerController* Controller = Pair.Key.Get();
		if (!IsValid(Controller) || !Controller->IsLevelTransitionCarryover())
		{
			continue;
		}

		const bool bCanPlay = Pair.Value.Role == TEXT("Mannequin") ||
			Pair.Value.SurvivorState == EMultiplayTestSurvivorState::Active;
		AActor* InitialViewTarget = nullptr;
		if (Pair.Value.Role == TEXT("Mannequin"))
		{
			InitialViewTarget = Controller->GetViewedMannequin();
		}
		else if (bCanPlay)
		{
			InitialViewTarget = Controller->GetPawn();
		}
		else
		{
			InitialViewTarget = FindLivingSurvivorSpectatorTarget(Controller, 1);
		}
		Controller->CompleteLevelTransitionCarryover(InitialViewTarget, bCanPlay);
	}
}

void AMultiplayTestGameMode::SetAuthoritativeMatchPhase(const EMultiplayTestMatchPhase NewPhase)
{
	AuthoritativeMatchPhase = NewPhase;
	for (TPair<TWeakObjectPtr<AMultiplayTestPlayerController>, FMultiplayTestServerPlayerRecord>& Pair : MatchPlayerRecords)
	{
		if (AMultiplayTestPlayerController* Controller = Pair.Key.Get(); IsValid(Controller))
		{
			Controller->SetServerMatchState(NewPhase, Pair.Value.SurvivorState);
		}
	}
}

FMultiplayTestServerPlayerRecord* AMultiplayTestGameMode::FindSurvivorRecord(APlayerCharacter* Survivor)
{
	for (TPair<TWeakObjectPtr<AMultiplayTestPlayerController>, FMultiplayTestServerPlayerRecord>& Pair : MatchPlayerRecords)
	{
		AMultiplayTestPlayerController* Controller = Pair.Key.Get();
		if (Pair.Value.Role == TEXT("Survivor") &&
			(Pair.Value.SurvivorPawn.Get() == Survivor ||
			 (IsValid(Controller) && Controller->GetPawn() == Survivor)))
		{
			Pair.Value.SurvivorPawn = Survivor;
			return &Pair.Value;
		}
	}
	return nullptr;
}

const FMultiplayTestServerPlayerRecord* AMultiplayTestGameMode::FindSurvivorRecord(
	const APlayerCharacter* Survivor) const
{
	for (const TPair<TWeakObjectPtr<AMultiplayTestPlayerController>, FMultiplayTestServerPlayerRecord>& Pair : MatchPlayerRecords)
	{
		const AMultiplayTestPlayerController* Controller = Pair.Key.Get();
		if (Pair.Value.Role == TEXT("Survivor") &&
			(Pair.Value.SurvivorPawn.Get() == Survivor ||
			 (IsValid(Controller) && Controller->GetPawn() == Survivor)))
		{
			return &Pair.Value;
		}
	}
	return nullptr;
}

APlayerCharacter* AMultiplayTestGameMode::FindLivingSurvivorSpectatorTarget(
	const AMultiplayTestPlayerController* RequestingController,
	const int32 Direction) const
{
	TArray<APlayerCharacter*> LivingSurvivors;
	for (const TPair<TWeakObjectPtr<AMultiplayTestPlayerController>, FMultiplayTestServerPlayerRecord>& Pair : MatchPlayerRecords)
	{
		if (Pair.Key.Get() == RequestingController || Pair.Value.Role != TEXT("Survivor") ||
			Pair.Value.SurvivorState != EMultiplayTestSurvivorState::Active)
		{
			continue;
		}
		APlayerCharacter* Candidate = Pair.Value.SurvivorPawn.Get();
		if (!IsValid(Candidate) && Pair.Key.IsValid())
		{
			Candidate = Cast<APlayerCharacter>(Pair.Key->GetPawn());
		}
		if (IsValid(Candidate))
		{
			LivingSurvivors.Add(Candidate);
		}
	}
	if (LivingSurvivors.IsEmpty())
	{
		return nullptr;
	}

	const AActor* CurrentTarget = IsValid(RequestingController) ? RequestingController->GetViewTarget() : nullptr;
	int32 CurrentIndex = LivingSurvivors.IndexOfByKey(CurrentTarget);
	if (CurrentIndex == INDEX_NONE)
	{
		return LivingSurvivors[0];
	}
	const int32 Step = Direction < 0 ? -1 : 1;
	return LivingSurvivors[(CurrentIndex + Step + LivingSurvivors.Num()) % LivingSurvivors.Num()];
}

void AMultiplayTestGameMode::EnterSurvivorSpectator(AMultiplayTestPlayerController* Controller)
{
	if (!HasAuthority() || !IsValid(Controller))
	{
		return;
	}
	Controller->EnterSurvivorSpectator(FindLivingSurvivorSpectatorTarget(Controller, 1));
}

void AMultiplayTestGameMode::CycleSurvivorSpectator(
	AMultiplayTestPlayerController* RequestingController,
	const int32 Direction)
{
	if (!HasAuthority() || !IsValid(RequestingController))
	{
		return;
	}
	const FMultiplayTestServerPlayerRecord* Record = MatchPlayerRecords.Find(RequestingController);
	if (Record == nullptr || Record->Role != TEXT("Survivor") ||
		(Record->SurvivorState != EMultiplayTestSurvivorState::Eliminated &&
		 Record->SurvivorState != EMultiplayTestSurvivorState::Escaped &&
		 Record->SurvivorState != EMultiplayTestSurvivorState::TransitionWaiting))
	{
		return;
	}
	RequestingController->EnterSurvivorSpectator(
		FindLivingSurvivorSpectatorTarget(RequestingController, Direction));
}

bool AMultiplayTestGameMode::TryBroadcastSurvivorPing(
	AMultiplayTestPlayerController* RequestingController,
	const EProjectProject01PingType Type,
	const FVector& Location)
{
	if (!HasAuthority() || !IsValid(RequestingController) || AuthoritativeMatchPhase != EMultiplayTestMatchPhase::InProgress)
	{
		return false;
	}
	const FMultiplayTestServerPlayerRecord* SenderRecord = MatchPlayerRecords.Find(RequestingController);
	if (SenderRecord == nullptr || !SenderRecord->Role.Equals(TEXT("Survivor"), ESearchCase::IgnoreCase) ||
		SenderRecord->SurvivorState != EMultiplayTestSurvivorState::Active || !IsValid(RequestingController->GetPawn()) ||
		FVector::DistSquared(RequestingController->GetPawn()->GetActorLocation(), Location) > FMath::Square(3100.0f))
	{
		return false;
	}
	const FString SenderName = IsValid(RequestingController->PlayerState)
		? RequestingController->PlayerState->GetPlayerName() : TEXT("생존자");
	for (const TPair<TWeakObjectPtr<AMultiplayTestPlayerController>, FMultiplayTestServerPlayerRecord>& Pair : MatchPlayerRecords)
	{
		AMultiplayTestPlayerController* Target = Pair.Key.Get();
		if (IsValid(Target) && Pair.Value.Role.Equals(TEXT("Survivor"), ESearchCase::IgnoreCase) &&
			Pair.Value.SurvivorState == EMultiplayTestSurvivorState::Active)
		{
			Target->ClientReceiveTeamPing(Type, Location, SenderName);
		}
	}
	return true;
}

void AMultiplayTestGameMode::NotifyExistingMannequinCatch(APlayerCharacter* Survivor)
{
	if (!HasAuthority() || AuthoritativeMatchPhase != EMultiplayTestMatchPhase::InProgress ||
		!IsValid(Survivor) || !Survivor->IsGameOver())
	{
		return;
	}

	FMultiplayTestServerPlayerRecord* SurvivorRecord = FindSurvivorRecord(Survivor);
	if (SurvivorRecord == nullptr || SurvivorRecord->SurvivorState != EMultiplayTestSurvivorState::Active)
	{
		return;
	}

	SurvivorRecord->SurvivorState = EMultiplayTestSurvivorState::Eliminated;
	const double CaptureSeconds = GetElapsedMatchSeconds();
	if (UCharacterMovementComponent* Movement = Survivor->GetCharacterMovement(); IsValid(Movement))
	{
		Movement->StopMovementImmediately();
		Movement->DisableMovement();
	}
	Survivor->SetActorEnableCollision(false);
	for (TPair<TWeakObjectPtr<AMultiplayTestPlayerController>, FMultiplayTestServerPlayerRecord>& Pair : MatchPlayerRecords)
	{
		if (Pair.Value.Role == TEXT("Mannequin"))
		{
			++Pair.Value.CaptureCount;
			if (Pair.Value.FirstCaptureSeconds <= 0.0)
			{
				Pair.Value.FirstCaptureSeconds = CaptureSeconds;
			}
		}
		if (Pair.Value.SurvivorPawn.Get() == Survivor)
		{
			if (AMultiplayTestPlayerController* Controller = Pair.Key.Get(); IsValid(Controller))
			{
				Controller->SetServerMatchState(AuthoritativeMatchPhase, SurvivorRecord->SurvivorState);
			}
		}
	}

	UE_LOG(LogProjectProject01Multiplayer, Log,
		TEXT("Survivor %s was authoritatively eliminated at %.2f seconds."),
		*GetNameSafe(Survivor), CaptureSeconds);
	AMultiplayTestPlayerController* EliminatedController =
		Cast<AMultiplayTestPlayerController>(Survivor->GetController());
	const TWeakObjectPtr<APlayerCharacter> WeakSurvivor(Survivor);
	const TWeakObjectPtr<AMultiplayTestPlayerController> WeakController(EliminatedController);
	if (UWorld* World = GetWorld(); IsValid(World))
	{
		FTimerHandle DeathSequenceTimer;
		World->GetTimerManager().SetTimer(
			DeathSequenceTimer,
			FTimerDelegate::CreateWeakLambda(this, [this, WeakSurvivor, WeakController]()
			{
				APlayerCharacter* EliminatedSurvivor = WeakSurvivor.Get();
				AMultiplayTestPlayerController* Controller = WeakController.Get();
				if (IsValid(EliminatedSurvivor))
				{
					EliminatedSurvivor->SetActorHiddenInGame(true);
				}
				if (IsValid(Controller))
				{
					// 사망 카메라가 쓰러지고 암전된 뒤에만 살아 있는 생존자 관전으로 전환한다.
					Controller->EnterSurvivorSpectator(
						FindLivingSurvivorSpectatorTarget(Controller, 1), true);
				}
				CheckForMatchCompletion();
			}),
			Survivor->GetDeathSequenceHandoffDelaySeconds(), false);
	}
}

void AMultiplayTestGameMode::NotifyMannequinCaptureFeedback(const bool bElimination)
{
	if (!HasAuthority() || AuthoritativeMatchPhase != EMultiplayTestMatchPhase::InProgress)
	{
		return;
	}
	if (AMultiplayTestPlayerController* Controller = MannequinController.Get(); IsValid(Controller))
	{
		Controller->ClientPlayMannequinCaptureFeedback(bElimination);
	}
}

bool AMultiplayTestGameMode::MarkSurvivorEscaped(APlayerCharacter* Survivor)
{
	if (!HasAuthority() || AuthoritativeMatchPhase != EMultiplayTestMatchPhase::InProgress || !IsValid(Survivor))
	{
		return false;
	}

	FMultiplayTestServerPlayerRecord* Record = FindSurvivorRecord(Survivor);
	if (Record == nullptr || Record->SurvivorState != EMultiplayTestSurvivorState::Active)
	{
		return false;
	}

	Record->SurvivorState = EMultiplayTestSurvivorState::Escaped;
	Record->EscapeSeconds = GetElapsedMatchSeconds();
	if (UCharacterMovementComponent* Movement = Survivor->GetCharacterMovement(); IsValid(Movement))
	{
		Movement->StopMovementImmediately();
		Movement->DisableMovement();
	}
	Survivor->SetActorEnableCollision(false);
	Survivor->SetActorHiddenInGame(true);
	if (AMultiplayTestPlayerController* Controller = Cast<AMultiplayTestPlayerController>(Survivor->GetController());
		IsValid(Controller))
	{
		Controller->SetServerMatchState(AuthoritativeMatchPhase, Record->SurvivorState);
		if (APlayerCharacter* LivingSurvivor = FindLivingSurvivorSpectatorTarget(Controller, 1);
			IsValid(LivingSurvivor))
		{
			Controller->EnterSurvivorSpectator(LivingSurvivor, true);
		}
	}

	UE_LOG(LogProjectProject01Multiplayer, Log,
		TEXT("Survivor %s escaped at %.2f seconds."), *GetNameSafe(Survivor), Record->EscapeSeconds);
	CheckForMatchCompletion();
	return true;
}

bool AMultiplayTestGameMode::MarkSurvivorWaitingForLevelTransition(APlayerCharacter* Survivor)
{
	if (!HasAuthority() || AuthoritativeMatchPhase != EMultiplayTestMatchPhase::InProgress ||
		!IsValid(Survivor))
	{
		return false;
	}

	FMultiplayTestServerPlayerRecord* Record = FindSurvivorRecord(Survivor);
	if (Record == nullptr || Record->SurvivorState != EMultiplayTestSurvivorState::Active)
	{
		return false;
	}

	Record->SurvivorState = EMultiplayTestSurvivorState::TransitionWaiting;
	if (UCharacterMovementComponent* Movement = Survivor->GetCharacterMovement(); IsValid(Movement))
	{
		Movement->StopMovementImmediately();
		Movement->DisableMovement();
	}
	Survivor->SetActorEnableCollision(false);
	Survivor->SetActorHiddenInGame(true);

	AMultiplayTestPlayerController* Controller =
		Cast<AMultiplayTestPlayerController>(Survivor->GetController());
	if (IsValid(Controller))
	{
		Controller->SetServerMatchState(AuthoritativeMatchPhase, Record->SurvivorState);
		Controller->EnterSurvivorSpectator(
			FindLivingSurvivorSpectatorTarget(Controller, 1), true);
	}

	UE_LOG(LogProjectProject01Multiplayer, Log,
		TEXT("Survivor %s is waiting for the next level."), *GetNameSafe(Survivor));
	return true;
}

bool AMultiplayTestGameMode::CanCommitLevelTransition() const
{
	TArray<EMultiplayTestSurvivorState> SurvivorStates;
	for (const TPair<TWeakObjectPtr<AMultiplayTestPlayerController>, FMultiplayTestServerPlayerRecord>& Pair :
		MatchPlayerRecords)
	{
		if (Pair.Value.Role == TEXT("Survivor"))
		{
			SurvivorStates.Add(Pair.Value.SurvivorState);
		}
	}
	return EvaluateLevelTransition(SurvivorStates).CanTravel();
}

void AMultiplayTestGameMode::PrepareLevelTransitionTravel()
{
	if (!HasAuthority())
	{
		return;
	}

	for (TPair<TWeakObjectPtr<AMultiplayTestPlayerController>, FMultiplayTestServerPlayerRecord>& Pair :
		MatchPlayerRecords)
	{
		if (AMultiplayTestPlayerController* Controller = Pair.Key.Get(); IsValid(Controller))
		{
			const bool bSpawnInNextLevel = Pair.Value.Role == TEXT("Survivor") &&
				Pair.Value.SurvivorState == EMultiplayTestSurvivorState::TransitionWaiting;
			Controller->PrepareLevelTransitionCarryover(bSpawnInNextLevel);
		}
	}
}

void AMultiplayTestGameMode::CheckForMatchCompletion()
{
	TArray<EMultiplayTestSurvivorState> SurvivorStates;
	bool bAnySurvivorForfeited = false;
	for (const TPair<TWeakObjectPtr<AMultiplayTestPlayerController>, FMultiplayTestServerPlayerRecord>& Pair : MatchPlayerRecords)
	{
		if (Pair.Value.Role == TEXT("Survivor"))
		{
			SurvivorStates.Add(Pair.Value.SurvivorState);
			bAnySurvivorForfeited |= Pair.Value.bForfeited;
		}
	}
	for (const TPair<FString, FMultiplayTestServerPlayerRecord>& Pair : DisconnectedMatchRecords)
	{
		if (Pair.Value.Role == TEXT("Survivor"))
		{
			SurvivorStates.Add(Pair.Value.SurvivorState);
			bAnySurvivorForfeited |= Pair.Value.bForfeited;
		}
	}

	const FMatchResolutionSummary Summary = EvaluateMatchResolution(SurvivorStates);
	if (Summary.IsComplete())
	{
		if (Summary.DidMannequinWin())
		{
			bMatchEndedByForfeit = bAnySurvivorForfeited;
			for (TPair<TWeakObjectPtr<AMultiplayTestPlayerController>, FMultiplayTestServerPlayerRecord>& Pair : MatchPlayerRecords)
			{
				if (Pair.Value.Role == TEXT("Mannequin"))
				{
					Pair.Value.AllCapturedSeconds = GetElapsedMatchSeconds();
				}
			}
			for (TPair<FString, FMultiplayTestServerPlayerRecord>& Pair : DisconnectedMatchRecords)
			{
				if (Pair.Value.Role == TEXT("Mannequin"))
				{
					Pair.Value.AllCapturedSeconds = GetElapsedMatchSeconds();
				}
			}
		}
		FinishAuthoritativeMatch(Summary.DidMannequinWin()
			? TEXT("AllSurvivorsEliminated") : TEXT("AllSurvivorsResolved"));
	}
}

double AMultiplayTestGameMode::GetElapsedMatchSeconds() const
{
	const UWorld* World = GetWorld();
	if (!IsValid(World) || MatchStartWorldSeconds <= 0.0)
	{
		return 0.001;
	}
	const double EndSeconds = MatchEndWorldSeconds > 0.0
		? MatchEndWorldSeconds
		: static_cast<double>(World->GetTimeSeconds());
	return FMath::Max(0.001, EndSeconds - MatchStartWorldSeconds);
}

void AMultiplayTestGameMode::FinishAuthoritativeMatch(const FString& Reason)
{
	if (!HasAuthority() || bFinishCommitted || AuthoritativeMatchPhase != EMultiplayTestMatchPhase::InProgress)
	{
		return;
	}

	bFinishCommitted = true;
	MatchEndWorldSeconds = GetWorld()->GetTimeSeconds();
	SetAuthoritativeMatchPhase(EMultiplayTestMatchPhase::Ending);
	for (const TPair<TWeakObjectPtr<AMultiplayTestPlayerController>, FMultiplayTestServerPlayerRecord>& Pair : MatchPlayerRecords)
	{
		if (AMultiplayTestPlayerController* Controller = Pair.Key.Get(); IsValid(Controller))
		{
			if (APawn* Pawn = Controller->GetPawn(); IsValid(Pawn))
			{
				if (UCharacterMovementComponent* Movement = Cast<UCharacterMovementComponent>(Pawn->GetMovementComponent());
					IsValid(Movement))
				{
					Movement->StopMovementImmediately();
					Movement->DisableMovement();
				}
			}
		}
	}

	UE_LOG(LogProjectProject01Multiplayer, Log,
		TEXT("Authoritative match %s finished once: %s."), *AuthoritativeMatchId, *Reason);
	SubmitAuthoritativeResults();
}

FMultiplayTestMatchResult AMultiplayTestGameMode::BuildFinalResult(
	const AMultiplayTestPlayerController* Controller,
	const FMultiplayTestServerPlayerRecord& Record) const
{
	FMultiplayTestMatchResult Result;
	Result.MatchId = AuthoritativeMatchId;
	Result.Role = Record.Role;
	Result.MatchDurationSeconds = GetElapsedMatchSeconds();
	Result.CaptureCount = Record.CaptureCount;
	Result.FirstCaptureSeconds = Record.FirstCaptureSeconds;
	Result.AllCapturedSeconds = Record.AllCapturedSeconds;
	Result.RescueCount = Record.RescueCount;
	Result.EscapeSeconds = Record.EscapeSeconds;
	if (Record.Role == TEXT("Mannequin"))
	{
		Result.bSuccess = !Record.bForfeited && !bSurvivorVictoryByMannequinForfeit && Record.AllCapturedSeconds > 0.0;
		Result.Outcome = Record.bForfeited ? TEXT("Forfeited")
			: Result.bSuccess && bMatchEndedByForfeit ? TEXT("SurvivorForfeitVictory")
			: Result.bSuccess ? TEXT("MannequinVictory") : TEXT("SurvivorVictory");
	}
	else
	{
		Result.bSuccess = !Record.bForfeited &&
			(Record.SurvivorState == EMultiplayTestSurvivorState::Escaped || bSurvivorVictoryByMannequinForfeit);
		Result.Outcome = Record.bForfeited ? TEXT("Forfeited")
			: bSurvivorVictoryByMannequinForfeit ? TEXT("OpponentForfeit")
			: Result.bSuccess
			? TEXT("Escaped")
			: Record.SurvivorState == EMultiplayTestSurvivorState::Eliminated
				? TEXT("Eliminated")
				: TEXT("NotEscaped");
	}
	if (!bMatchEndedByForfeit && !Record.bForfeited)
	{
		if (const bool* Verification = ResultVerificationStatus.Find(Controller))
		{
			Result.bLeaderboardVerificationReady = *Verification;
		}
	}
	return Result;
}

void AMultiplayTestGameMode::SubmitAuthoritativeResults()
{
	if (bMatchEndedByForfeit)
	{
		PendingResultSubmissions = 0;
		PresentFinalResults();
		return;
	}
	UProjectProject01GameInstance* GameInstance = GetGameInstance<UProjectProject01GameInstance>();
	ResultVerificationStatus.Reset();
	struct FPendingAuthoritativeSubmission
	{
		TWeakObjectPtr<AMultiplayTestPlayerController> Controller;
		FString UserId;
		FMultiplayTestMatchResult Result;
	};
	TArray<FPendingAuthoritativeSubmission> Submissions;

	for (const TPair<TWeakObjectPtr<AMultiplayTestPlayerController>, FMultiplayTestServerPlayerRecord>& Pair : MatchPlayerRecords)
	{
		AMultiplayTestPlayerController* Controller = Pair.Key.Get();
		if (!IsValid(Controller) || Pair.Value.bForfeited)
		{
			continue;
		}
		ResultVerificationStatus.Add(Controller, false);
		if (!IsValid(GameInstance) || Pair.Value.UserId.IsEmpty() || AuthoritativeMatchId.IsEmpty())
		{
			continue;
		}

		FPendingAuthoritativeSubmission& Submission = Submissions.AddDefaulted_GetRef();
		Submission.Controller = Controller;
		Submission.UserId = Pair.Value.UserId;
		Submission.Result = BuildFinalResult(Controller, Pair.Value);
	}

	PendingResultSubmissions = Submissions.Num();
	if (PendingResultSubmissions == 0)
	{
		PresentFinalResults();
		return;
	}

	for (const FPendingAuthoritativeSubmission& Submission : Submissions)
	{
		const TWeakObjectPtr<AMultiplayTestGameMode> WeakThis(this);
		const TWeakObjectPtr<AMultiplayTestPlayerController> WeakController = Submission.Controller;
		GameInstance->SubmitAuthoritativeMatchResultWithCallback(
			Submission.Result.MatchId,
			Submission.UserId,
			Submission.Result.Role,
			Submission.Result.bSuccess,
			Submission.Result.CaptureCount,
			Submission.Result.FirstCaptureSeconds,
			Submission.Result.AllCapturedSeconds,
			Submission.Result.RescueCount,
			Submission.Result.EscapeSeconds,
			[WeakThis, WeakController](const bool bSucceeded)
			{
				if (AMultiplayTestGameMode* GameMode = WeakThis.Get(); IsValid(GameMode))
				{
					GameMode->HandleResultSubmissionComplete(WeakController, bSucceeded);
				}
			});
	}
}

void AMultiplayTestGameMode::HandleResultSubmissionComplete(
	const TWeakObjectPtr<AMultiplayTestPlayerController> Controller,
	const bool bSucceeded)
{
	if (ResultVerificationStatus.Contains(Controller))
	{
		ResultVerificationStatus.FindChecked(Controller) = bSucceeded;
	}
	PendingResultSubmissions = FMath::Max(0, PendingResultSubmissions - 1);
	if (PendingResultSubmissions == 0)
	{
		PresentFinalResults();
	}
}

void AMultiplayTestGameMode::PresentFinalResults()
{
	if (AuthoritativeMatchPhase == EMultiplayTestMatchPhase::Results)
	{
		return;
	}
	SetAuthoritativeMatchPhase(EMultiplayTestMatchPhase::Results);
	for (const TPair<TWeakObjectPtr<AMultiplayTestPlayerController>, FMultiplayTestServerPlayerRecord>& Pair : MatchPlayerRecords)
	{
		const FMultiplayTestMatchResult Result = BuildFinalResult(Pair.Key.Get(), Pair.Value);
		if (!Pair.Value.UserId.IsEmpty())
		{
			CompletedResultsByUserId.Add(Pair.Value.UserId, Result);
		}
		if (AMultiplayTestPlayerController* Controller = Pair.Key.Get(); IsValid(Controller))
		{
			Controller->DeliverMatchResultToOwner(Result);
		}
	}
	for (const TPair<FString, FMultiplayTestServerPlayerRecord>& Pair : DisconnectedMatchRecords)
	{
		CompletedResultsByUserId.Add(Pair.Key, BuildFinalResult(nullptr, Pair.Value));
	}
}

void AMultiplayTestGameMode::ResetDedicatedServerWorldIfEmpty()
{
	UWorld* World = GetWorld();
	if (!ensureMsgf(IsValid(World), TEXT("Dedicated server reset requires a valid world.")) ||
		World->GetNetMode() != NM_DedicatedServer || GetNumPlayers() > 0)
	{
		return;
	}

	static const FString MultiplayTestMap = TEXT("/Game/MyProject/Level/MultiplayTest");
	UE_LOG(LogProjectProject01Multiplayer, Log,
		TEXT("No players remain. Resetting the dedicated server world to %s."), *MultiplayTestMap);
	if (!World->ServerTravel(MultiplayTestMap, true))
	{
		UE_LOG(LogProjectProject01Multiplayer, Error,
			TEXT("Failed to reset the dedicated server world to %s."), *MultiplayTestMap);
	}
}

void AMultiplayTestGameMode::SetPlayerDefaults(APawn* PlayerPawn)
{
	Super::SetPlayerDefaults(PlayerPawn);

	if (APlayerCharacter* SurvivorPlayer = Cast<APlayerCharacter>(PlayerPawn))
	{
		// MultiplayTest에는 조력자가 없으므로 생존자는 추가 포획 허용 횟수 없이 시작한다.
		SurvivorPlayer->SetRemainingDeathCountForGameMode(0);
	}
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
		// 직접 조작 중 Z/X/C 명령을 지정하지 않았다면, 손을 뗀 마네킹은 그대로 정지한다.
		if (!RequestingController->bManualMannequinCommandIssued)
		{
			PreviousManuallyControlledMannequin->SetMultiplayCommand(EMultiplayMannequinCommand::HoldPosition);
			MultiplayerPatrolTargets.Remove(PreviousManuallyControlledMannequin);
		}
		RequestingController->bManualMannequinCommandIssued = false;
		PreviousManuallyControlledMannequin->SetManualControlEnabled(false);
		RequestingController->UnPossess();
		UProjectProject01DiagnosticsSubsystem::RecordWorldEvent(GetWorld(), EProjectProject01DiagnosticSeverity::Normal,
			TEXT("MannequinPlayerUnPossess"), FString::Printf(TEXT("Controller=%s; NextSlot=%d; Command=%s"),
				*GetNameSafe(RequestingController), Slot,
				*UEnum::GetValueAsString(PreviousManuallyControlledMannequin->GetMultiplayCommand())),
			PreviousManuallyControlledMannequin);
		QueueDefaultAIControllerRestore(PreviousManuallyControlledMannequin);
	}

	// 번호키 단계에서는 AIController를 유지하고, 플레이어는 시점만 공유한다.
	RequestingController->SetViewedMannequin(TargetMannequin);
	UProjectProject01DiagnosticsSubsystem::RecordWorldEvent(GetWorld(), EProjectProject01DiagnosticSeverity::Normal,
		TEXT("MannequinViewSelectionChanged"), FString::Printf(TEXT("Controller=%s; Slot=%d"), *GetNameSafe(RequestingController), Slot), TargetMannequin);
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
		if (!TargetMannequin->IsManualControlEnabled())
		{
			RequestingController->bManualMannequinCommandIssued = false;
		}
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
	RequestingController->bManualMannequinCommandIssued = false;
	UProjectProject01DiagnosticsSubsystem::RecordWorldEvent(GetWorld(), EProjectProject01DiagnosticSeverity::Normal,
		TEXT("MannequinPlayerPossess"), FString::Printf(TEXT("Controller=%s; Slot=%d"), *GetNameSafe(RequestingController), TargetMannequin->GetControlSlot()), TargetMannequin);
	TargetMannequin->ForceNetUpdate();
	UE_LOG(LogProjectProject01Multiplayer, Log,
		TEXT("%s enabled manual control of mannequin %s."),
		*RequestingController->GetName(), *TargetMannequin->GetName());
	return true;
}

bool AMultiplayTestGameMode::TryIssueMannequinCommand(
	AMultiplayTestPlayerController* RequestingController,
	const EMultiplayMannequinCommand Command)
{
	if (!HasAuthority() || !IsMannequinController(RequestingController))
	{
		return false;
	}

	AMannequinAICharacter* Target = Cast<AMannequinAICharacter>(RequestingController->GetPawn());
	if (!IsValid(Target) || !Target->IsManualControlEnabled() ||
		Target != RequestingController->GetViewedMannequin())
	{
		UE_LOG(LogProjectProject01Multiplayer, Warning,
			TEXT("Rejected mannequin command because %s is not directly controlling its viewed mannequin."),
			*GetNameSafe(RequestingController));
		return false;
	}

	// 직접 조작은 계속 유지하고, 빙의 해제 뒤 AI가 이어서 수행할 명령만 저장한다.
	Target->SetMultiplayCommand(Command);
	MultiplayerPatrolTargets.Remove(Target);
	MultiplayerObservedChaseTargets.Remove(Target);
	RequestingController->bManualMannequinCommandIssued = true;
	UProjectProject01DiagnosticsSubsystem::RecordWorldEvent(GetWorld(), EProjectProject01DiagnosticSeverity::Normal,
		TEXT("MultiplayerMannequinCommand"),
		FString::Printf(TEXT("Controller=%s; Slot=%d; Command=%s"),
			*GetNameSafe(RequestingController), Target->GetControlSlot(), *UEnum::GetValueAsString(Command)), Target);
	return true;
}

bool AMultiplayTestGameMode::TryResetFollowingMannequins(AMultiplayTestPlayerController* RequestingController)
{
	const AMannequinAICharacter* Controlled = IsValid(RequestingController)
		? Cast<AMannequinAICharacter>(RequestingController->GetPawn()) : nullptr;
	if (!HasAuthority() || !IsMannequinController(RequestingController) ||
		!IsValid(Controlled) || !Controlled->IsManualControlEnabled())
	{
		return false;
	}
	bool bChangedAny = false;
	for (TActorIterator<AMannequinAICharacter> It(GetWorld()); It; ++It)
	{
		AMannequinAICharacter* Mannequin = *It;
		if (IsValid(Mannequin) &&
			Mannequin->GetMultiplayCommand() == EMultiplayMannequinCommand::FollowMannequinPlayer)
		{
			Mannequin->SetMultiplayCommand(EMultiplayMannequinCommand::Patrol);
			MultiplayerPatrolTargets.Remove(Mannequin);
			MultiplayerObservedChaseTargets.Remove(Mannequin);
			bChangedAny = true;
		}
	}
	return bChangedAny;
}

bool AMultiplayTestGameMode::ToggleMannequinCatchDamage(AMultiplayTestPlayerController* RequestingController)
{
	const AMannequinAICharacter* Controlled = IsValid(RequestingController)
		? Cast<AMannequinAICharacter>(RequestingController->GetPawn()) : nullptr;
	if (!HasAuthority() || !IsMannequinController(RequestingController) ||
		!IsValid(Controlled) || !Controlled->IsManualControlEnabled())
	{
		return bMannequinCatchDamageEnabled;
	}
	bMannequinCatchDamageEnabled = !bMannequinCatchDamageEnabled;
	UE_LOG(LogProjectProject01Multiplayer, Log, TEXT("Mannequin catch damage is now %s."),
		bMannequinCatchDamageEnabled ? TEXT("enabled") : TEXT("disabled"));
	return bMannequinCatchDamageEnabled;
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
		UProjectProject01DiagnosticsSubsystem::RecordWorldEvent(RestoredMannequin->GetWorld(), EProjectProject01DiagnosticSeverity::Normal,
			TEXT("MannequinAIControlRestored"), FString::Printf(TEXT("Controller=%s"), *GetNameSafe(RestoredAIController)), RestoredMannequin);
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

void AMultiplayTestGameMode::AssignRandomInitialMannequinView(
	AMultiplayTestPlayerController* Controller)
{
	if (!HasAuthority() || !IsMannequinController(Controller))
	{
		return;
	}

	TArray<int32> AvailableSlots;
	AvailableSlots.Reserve(10);
	for (int32 Slot = 0; Slot <= 9; ++Slot)
	{
		if (IsValid(FindMannequinBySlot(Slot)))
		{
			AvailableSlots.Add(Slot);
		}
	}

	if (AvailableSlots.IsEmpty())
	{
		UE_LOG(LogProjectProject01Multiplayer, Error,
			TEXT("No valid mannequin control slot is available for the initial view of %s."),
			*GetNameSafe(Controller));
		return;
	}

	const int32 SelectedSlot = AvailableSlots[FMath::RandHelper(AvailableSlots.Num())];
	if (!ensureMsgf(TryPossessMannequin(Controller, SelectedSlot),
		TEXT("Failed to assign mannequin slot %d as the initial view for %s."),
		SelectedSlot, *GetNameSafe(Controller)))
	{
		return;
	}

	UE_LOG(LogProjectProject01Multiplayer, Log,
		TEXT("%s received random initial mannequin view slot %d."),
		*GetNameSafe(Controller), SelectedSlot);
}

bool AMultiplayTestGameMode::IsMannequinController(const AMultiplayTestPlayerController* Controller) const
{
	return IsValid(Controller) &&
		(MannequinController.Get() == Controller ||
		 Controller->GetAuthenticatedRole().Equals(TEXT("Mannequin"), ESearchCase::IgnoreCase));
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
	FProjectProject01DiagnosticWorkScope DiagnosticScope(GetWorld());
	DiagnosticScope.AddVisionCheck();
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
		DiagnosticScope.AddLineTrace();
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
		APlayerCharacter* ClosestObservingSurvivor = nullptr;
		double ClosestObservingDistanceSquared = TNumericLimits<double>::Max();
		for (const APlayerController* SurvivorController : SurvivorControllers)
		{
			if (CanSurvivorSeeMannequin(SurvivorController, Mannequin))
			{
				bObservedByAnySurvivor = true;
				APlayerCharacter* ObservingSurvivor = Cast<APlayerCharacter>(SurvivorController->GetPawn());
				if (IsActiveSurvivorTarget(ObservingSurvivor))
				{
					const double DistanceSquared = FVector::DistSquared2D(
						Mannequin->GetActorLocation(), ObservingSurvivor->GetActorLocation());
					if (DistanceSquared < ClosestObservingDistanceSquared)
					{
						ClosestObservingDistanceSquared = DistanceSquared;
						ClosestObservingSurvivor = ObservingSurvivor;
					}
				}
			}
		}
		if (Mannequin->GetMultiplayCommand() == EMultiplayMannequinCommand::Patrol &&
			IsValid(ClosestObservingSurvivor))
		{
			MultiplayerObservedChaseTargets.FindOrAdd(Mannequin) = ClosestObservingSurvivor;
		}

		Mannequin->SetFrozenBySurvivorVision(bObservedByAnySurvivor);
	}
}
