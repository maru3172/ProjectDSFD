// Fill out your copyright notice in the Description page of Project Settings.


#include "BTService_MannequinAI.h"

#include "Kismet/GameplayStatics.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "GameFramework/Pawn.h"
#include "AIController.h"
#include "Camera/PlayerCameraManager.h"
#include "MannequinAICharacter.h"
#include "HelperRearGuardCharacter.h"
#include "MultiplayTestGameMode.h"

#include "PlayerCharacter.h"
#include "ProjectProject01TuningData.h"
#include "NavigationSystem.h"
#include "NavigationData.h"

#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"

UBTService_MannequinAI::UBTService_MannequinAI()
{
	NodeName = TEXT("Mannequin AI Behavior");

	bCreateNodeInstance = true;

    Interval = 0.0f;
    RandomDeviation = 0.0f;

    PlayerSeeMannequin = false;
    MannequinSeePlayer = false;
}

bool UBTService_MannequinAI::TryFindRoamingDestination(
    const FVector& MannequinLocation,
    const FVector& PlayerLocation,
    float InnerRadius,
    float OuterRadius,
    APawn& MannequinPawn,
    FVector& OutDestination) const
{
    UWorld* World = GetWorld();
    if (!IsValid(World) || OuterRadius <= InnerRadius)
    {
        return false;
    }

    UNavigationSystemV1* NavigationSystem = UNavigationSystemV1::GetCurrent(World);
    if (!IsValid(NavigationSystem))
    {
        return false;
    }

    ANavigationData* NavigationData = NavigationSystem->GetNavDataForProps(
        MannequinPawn.GetNavAgentPropertiesRef(), MannequinLocation);
    if (!IsValid(NavigationData))
    {
        return false;
    }

    const double DistanceToPlayer = FVector::Dist2D(MannequinLocation, PlayerLocation);
    const float SearchRadius = OuterRadius + static_cast<float>(DistanceToPlayer);
    const double InnerRadiusSquared = FMath::Square(static_cast<double>(InnerRadius));
    const double OuterRadiusSquared = FMath::Square(static_cast<double>(OuterRadius));

    // NavMesh에서 현재 AI가 도달 가능한 점을 뽑고, 플레이어 중심의 고리 영역인지 재검사한다.
    constexpr int32 MaxRandomPointAttempts = 16;
    for (int32 Attempt = 0; Attempt < MaxRandomPointAttempts; ++Attempt)
    {
        FNavLocation Candidate;
        if (!NavigationSystem->GetRandomReachablePointInRadius(
                MannequinLocation, SearchRadius, Candidate, NavigationData))
        {
            continue;
        }

        const double CandidateDistanceSquared = FVector::DistSquared2D(Candidate.Location, PlayerLocation);
        if (CandidateDistanceSquared > InnerRadiusSquared && CandidateDistanceSquared <= OuterRadiusSquared)
        {
            OutDestination = Candidate.Location;
            return true;
        }
    }

    return false;
}

void UBTService_MannequinAI::ClearRoamingState(UBlackboardComponent& Blackboard)
{
    bHasRoamingDestination = false;
    bRoamingCommitted = false;
    bLoggedRoamingQueryFailure = false;
    NextRoamingQueryTime = 0.0;
    RoamingDestination = FVector::ZeroVector;

    Blackboard.ClearValue(TEXT("RoamingLocation"));
}

void UBTService_MannequinAI::TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	Super::TickNode(OwnerComp, NodeMemory, DeltaSeconds);

	FMannequinAITuningRow ActiveTuning;
	const UProjectProject01TuningSubsystem* TuningSubsystem = IsValid(GetWorld())
		? GetWorld()->GetSubsystem<UProjectProject01TuningSubsystem>()
		: nullptr;
	const bool bHasActiveTuning = IsValid(TuningSubsystem) && TuningSubsystem->GetMannequinTuning(ActiveTuning);

    // MultiplayTest에서는 첫 접속자가 마네킹 조종자이므로 PlayerPawn(0)을 생존자로 가정하지 않는다.
    // 실제 생존자 캐릭터를 사용해 기존 BT 활동을 유지한다.
    APlayerCharacter* PlayerCharacter = Cast<APlayerCharacter>(
        UGameplayStatics::GetActorOfClass(GetWorld(), APlayerCharacter::StaticClass()));
    APlayerController* PlayerController = IsValid(PlayerCharacter)
        ? Cast<APlayerController>(PlayerCharacter->GetController())
        : nullptr;
    if (!IsValid(PlayerController))
    {
        return;
    }
    APawn* PlayerPawn = PlayerCharacter;

    // Blackboard
    UBlackboardComponent* Blackboard = OwnerComp.GetBlackboardComponent();
    if (Blackboard == nullptr)
    {
        return;
    }

    // 서버에도 클라이언트의 화면 해상도는 존재하지 않는다. 뷰포트 좌표 대신
    // PlayerController가 제공하는 동기화된 시점과 카메라 FOV로 같은 시야 판정을 수행한다.
    FVector CameraLocation = FVector::ZeroVector;
    FRotator CameraRotation = FRotator::ZeroRotator;
    PlayerController->GetPlayerViewPoint(CameraLocation, CameraRotation);
    if (CameraLocation.ContainsNaN() || CameraRotation.ContainsNaN())
    {
        ensureMsgf(false, TEXT("Player view point is invalid for %s."), *GetNameSafe(PlayerController));
        return;
    }

    APlayerCameraManager* CameraManager = PlayerController->PlayerCameraManager;
    const float CameraFOVDegrees = IsValid(CameraManager)
        ? CameraManager->GetFOVAngle()
        : 90.0f;
    // 기존 화면 가장자리 15% 여유를 FOV에 적용한다. 수직 해상도에 의존하지 않아 서버에서도 동일하다.
    const float VisionFovMarginMultiplier = bHasActiveTuning ? ActiveTuning.VisionFovMarginMultiplier : 1.15f;
    const float ExpandedHalfFOVDegrees = FMath::Max(0.0f, CameraFOVDegrees * 0.5f * VisionFovMarginMultiplier);
    const float ViewConeMinimumDot = FMath::Cos(FMath::DegreesToRadians(ExpandedHalfFOVDegrees));
    const FVector CameraForward = CameraRotation.Vector().GetSafeNormal();

    // Mannequin의 AIController와 제어 중인 Pawn을 안전하게 가져온다.
    AAIController* AIController = OwnerComp.GetAIOwner();
    if (!IsValid(AIController))
    {
        return;
    }

    APawn* MannequinPawn = AIController->GetPawn();
    if (!IsValid(MannequinPawn))
    {
        return;
    }

    AMannequinAICharacter* Mannequin = Cast<AMannequinAICharacter>(MannequinPawn);
    if (!IsValid(Mannequin))
    {
        return;
    }

    // Mannequin 의 캡슐 컴포넌트
    UCapsuleComponent* MannequinCapsule = Mannequin->GetCapsuleComponent();
    if (MannequinCapsule == nullptr)
    {
        return;
    }

    // 캡슐의 중심 위치, 절반 높이, 반지름을 가져온다.
    FVector CapsuleCenter = MannequinCapsule->GetComponentLocation();
    float HalfHeight = MannequinCapsule->GetScaledCapsuleHalfHeight();
    float Radius = MannequinCapsule->GetScaledCapsuleRadius();

    // 화면 판정을 위한 캡슐의 여유 범위
    const float DetectionMargin = bHasActiveTuning ? ActiveTuning.DetectionMargin : 30.0f;

    // 캡슐의 판정 반지름과 높이에 여유를 추가한다.
    const float DetectionRadius = Radius + DetectionMargin;           // 사용하려면 해도 좋다.
    const float DetectionHalfHeight = HalfHeight + DetectionMargin;   // 높이는 굳이 사용할 필요가 없을 것 같다. 사용하려면 해도 좋다.

    // 캡슐의 방향을 가져온다.
    FVector Up = MannequinCapsule->GetUpVector();
    FVector Right = MannequinCapsule->GetRightVector();
    FVector Forward = MannequinCapsule->GetForwardVector();

    // Mannequin 의 Skeletal Mesh Component 
    USkeletalMeshComponent* MannequinMesh = Mannequin->GetMesh();
    if (MannequinMesh == nullptr)
    {
        return;
    }

    // 화면 노출 여부를 확인할 주요 Bone의 이름을 지정한다.
    const FName BoneNames[] = 
    {
        TEXT("head"),
        TEXT("pelvis"),
        TEXT("hand_l"),
        TEXT("hand_r"),
        TEXT("foot_l"),
        TEXT("foot_r"),
    };

    // 화면에 노출되었는지 확인할 Mannequin 의 여러 위치를 저장한다.
    TArray<FVector> Points;

    // 캡슐의 중심 위치를 추가한다.
    Points.Add(CapsuleCenter);

    // 캡슐의 아래쪽과 위쪽 위치를 추가한다.
    Points.Add(CapsuleCenter - Up);
    Points.Add(CapsuleCenter + Up);
    // Points.Add(CapsuleCenter - Up * HalfHeight);
    // Points.Add(CapsuleCenter + Up * HalfHeight);

    // 캡슐의 왼쪽과 오른쪽 위치를 추가한다.
    Points.Add(CapsuleCenter - Right * Radius);
    Points.Add(CapsuleCenter + Right * Radius);

    // 캡슐의 앞쪽과 뒤쪽 위치를 추가한다.
    Points.Add(CapsuleCenter - Forward * Radius);
    Points.Add(CapsuleCenter + Forward * Radius);

    // 지정한 Bone들의 위치를 검사 목록에 추가한다.
    for (const FName& BoneName : BoneNames)
    {
        // 해당 Bone이 실제 Skeletal Mesh에 존재하는지 확인한다.
        if (MannequinMesh->GetBoneIndex(BoneName) != INDEX_NONE)
        {
            // 현재 애니메이션에서 해당 Bone의 월드 위치를 검사 목록에 추가한다.
            Points.Add(MannequinMesh->GetBoneLocation(BoneName));
        }
    }

    // Mannequin 이 화면에 보이는지 여부를 저장한다.
    bool bInScreen = false;

    // 화면 좌표가 없는 서버에서도 같은 방향 시야를 판정한다.
    for (const FVector& Point : Points)
    {
        const FVector CameraToPoint = (Point - CameraLocation).GetSafeNormal();
        if (CameraToPoint.IsNearlyZero() || FVector::DotProduct(CameraForward, CameraToPoint) < ViewConeMinimumDot)
        {
            continue;
        }

        // 카메라에서 Mannequin 의 지점까지 Line Trace를 했을 때, 어떤 물체에 먼저 부딪혔는지에 대한 정보를 저장한다.
        FHitResult HitResult;
        FCollisionQueryParams QueryParams;
        QueryParams.AddIgnoredActor(PlayerPawn);

        TArray<AActor*> AllMannequins;
        UGameplayStatics::GetAllActorsOfClass(GetWorld(), AMannequinAICharacter::StaticClass(), AllMannequins);
        for (AActor* OtherMannequin : AllMannequins)
        {
            if (OtherMannequin != Mannequin)
            {
                QueryParams.AddIgnoredActor(OtherMannequin);
            }
        }

        const bool bHit = GetWorld()->LineTraceSingleByChannel(
            HitResult, CameraLocation, Point, ECC_GameTraceChannel1, QueryParams);
        if (bHit && HitResult.GetActor() == MannequinPawn)
        {
            bInScreen = true;
            break;
        }
    }

    // 실제 신체 노출과 정지용 여유 영역을 구분한다.
    // 여유 영역만 보였다고 최초 발견/추격을 시작하지 않는다.
    const bool bBodyVisible = bInScreen;
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

        // 캡슐 밖으로 움직이는 손발에도 수평 여유를 둔다.
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
        GuardQueryParams.AddIgnoredActor(PlayerPawn);
        GuardQueryParams.AddIgnoredActor(Mannequin);
        TArray<AActor*> GuardIgnoredMannequins;
        UGameplayStatics::GetAllActorsOfClass(GetWorld(), AMannequinAICharacter::StaticClass(), GuardIgnoredMannequins);
        for (AActor* OtherMannequin : GuardIgnoredMannequins)
        {
            if (IsValid(OtherMannequin))
            {
                GuardQueryParams.AddIgnoredActor(OtherMannequin);
            }
        }

        for (const FVector& GuardPoint : GuardPoints)
        {
            const FVector CameraToGuardPoint = (GuardPoint - CameraLocation).GetSafeNormal();
            if (CameraToGuardPoint.IsNearlyZero() ||
                FVector::DotProduct(CameraForward, CameraToGuardPoint) < ViewConeMinimumDot)
            {
                continue;
            }

            FHitResult GuardHit;
            // 가상 검사점에는 충돌체가 없으므로 가로막는 물체가 없는지 확인한다.
            const bool bGuardBlocked = GetWorld()->LineTraceSingleByChannel(
                GuardHit, CameraLocation, GuardPoint, ECC_GameTraceChannel1, GuardQueryParams);
            if (!bGuardBlocked)
            {
                bInScreen = true;
                break;
            }
        }
    }

    // 플레이어가 현재 Mannequin 을 바라보고 있는지 여부를 Blackboard의 PlayerLookingAtMannequin Key에 저장한다.
    // bInScreen 이 true 라면 Mannequin 이 현재 화면의 판정 범위 안에 있다는 의미이고, false 라면 Mannequin 이 화면의 판정 범위 밖에 있다는 의미이다.
    AHelperRearGuardCharacter* HelperRearGuard = Cast<AHelperRearGuardCharacter>(
        UGameplayStatics::GetActorOfClass(GetWorld(), AHelperRearGuardCharacter::StaticClass())
    );
    const bool bInHelperRearGuardSight =
        IsValid(HelperRearGuard) && HelperRearGuard->CanObserveActor(Mannequin);
    const bool bObservedByPlayerOrHelper = bInScreen || bInHelperRearGuardSight;

    Blackboard->SetValueAsBool(TEXT("PlayerLookingAtMannequin"), bObservedByPlayerOrHelper);

    if (bObservedByPlayerOrHelper)
    {
        // 플레이어 또는 조력자 시야에 들어온 즉시 현재 이동 요청을 중단한다.
        AIController->StopMovement();
        // 현재 재생 중인 애니메이션을 현재 프레임에서 그대로 정지한다.
        Mannequin->SetFrozen(true);

        PlayerSeeMannequin = PlayerSeeMannequin || bBodyVisible;
    }
    else
    {
        // 플레이어가 Mannequin 을 보고 있지 않으므로 애니메이션 정지를 해제한다
        Mannequin->SetFrozen(false);
    }

    // Mannequin 과 플레이어의 위치를 가져온다.
    FVector MannequinLocation = Mannequin->GetActorLocation();
    FVector PlayerLocation = PlayerPawn->GetActorLocation();

    const float DirectChaseRadius = bHasActiveTuning
        ? FMath::Max(0.0f, ActiveTuning.DirectChaseRadius)
        : PlayerCharacter->GetDirectChaseRadius();
    const float RoamingOuterRadius = bHasActiveTuning
        ? FMath::Max(0.0f, ActiveTuning.RoamingOuterRadius)
        : PlayerCharacter->GetRoamingOuterRadius();
    const double PlayerDistanceSquared = FVector::DistSquared2D(MannequinLocation, PlayerLocation);
    const bool bIsInsideInnerRange =
        PlayerDistanceSquared <= FMath::Square(static_cast<double>(DirectChaseRadius));
    const bool bIsBetweenPlayerRanges = !bIsInsideInnerRange &&
        PlayerDistanceSquared <= FMath::Square(static_cast<double>(RoamingOuterRadius));
    const bool bIsOutsideOuterRange = !bIsInsideInnerRange && !bIsBetweenPlayerRanges;
    
    // 플레이어의 실제 이동 방향을 가져오기
    FVector PlayerMovementDirection = PlayerPawn->GetVelocity().GetSafeNormal2D();
    
    if (!PlayerMovementDirection.IsNearlyZero())
    {
        // 플레이어가 현재 이동 중이라면 현재 이동 방향을 저장한다.
        LastPlayerMovementDirection = PlayerMovementDirection;
    }
    else if (LastPlayerMovementDirection.IsNearlyZero())
    {
        // 게임 시작 직후에 아직 이동한 적이 없다면 캐릭터의 정면을 바라보도록 설정한다.
        LastPlayerMovementDirection = PlayerPawn->GetActorForwardVector().GetSafeNormal2D();
    }
    
    const FVector PlayerToMannequin = (MannequinLocation - PlayerLocation).GetSafeNormal2D();
    const float MovementDirectionDot = FVector::DotProduct(LastPlayerMovementDirection, PlayerToMannequin);
    
    // 이동 방향 쪽 반원:
    // 플레이어가 왼쪽으로 이동하면 플레이어 왼쪽에 있는 마네킹이다.
    const bool bIsInFrontHalf = bIsBetweenPlayerRanges && MovementDirectionDot >= 0.0f;
    // 이동 반대 방향 쪽 반원:
    // 플레이어가 왼쪽으로 이동하면 플레이어 오른쪽에 있는 마네킹이다.
    const bool bIsInRearHalf = bIsBetweenPlayerRanges && MovementDirectionDot < 0.0f;

    // Line Trace의 충돌 정보를 저장할 변수이다.
    FHitResult HitResult;

    // Line Trace에 사용할 충돌 설정을 생성한다.
    FCollisionQueryParams QueryParams;

    // 현재 Mannequin 은 Line Trace에서 제외한다.
    QueryParams.AddIgnoredActor(MannequinPawn);

    // 현재 월드에 존재하는 모든 Mannequin 을 가져온다.
    TArray<AActor*> AllMannequins;
    UGameplayStatics::GetAllActorsOfClass(GetWorld(), AMannequinAICharacter::StaticClass(), AllMannequins);

    // 현재 Mannequin 이 아닌 다른 Mannequin 들을 Line Trace에서 제외한다.
    for (AActor* OtherMannequin : AllMannequins)
    {
        if (OtherMannequin != MannequinPawn)
        {
            QueryParams.AddIgnoredActor(OtherMannequin);
        }
    }

    // Mannequin 에서 플레이어까지 Line Trace를 수행한다.
    bool bHit = GetWorld()->LineTraceSingleByChannel(
        HitResult,
        MannequinLocation,
        PlayerLocation,
        ECC_GameTraceChannel1,
        QueryParams
    );

    // Line Trace가 플레이어에게 도달했다면 발견한 것으로 판단한다.
    if (bHit && HitResult.GetActor() == PlayerPawn)
    {
        MannequinSeePlayer = true;
    }

    const bool bMutuallyDetected = PlayerSeeMannequin && MannequinSeePlayer;
    Blackboard->SetValueAsBool(TEXT("IsOutsideOuterRange"), bIsOutsideOuterRange);
    Blackboard->SetValueAsBool(TEXT("IsBetweenPlayerRanges"), bIsBetweenPlayerRanges);
    Blackboard->SetValueAsBool(TEXT("IsInsideInnerRange"), bIsInsideInnerRange);
    
    Blackboard->SetValueAsBool(TEXT("IsInFrontHalf"), bIsInFrontHalf);
    Blackboard->SetValueAsBool(TEXT("IsInRearHalf"), bIsInRearHalf);

    // 외부원 밖에서는 멈추도록 설정!
    if (bIsOutsideOuterRange)
    {
        AIController->StopMovement();
        Mannequin->SetFrozen(true);
        
        Blackboard->ClearValue(TEXT("TargetActor"));
        ClearRoamingState(*Blackboard);
        return;
    }
    
    // MultiplayTest는 생존자 시야 정지를 GameMode가 별도로 판정한다.
    // 따라서 마네킹을 플레이어가 풀어 준 직후처럼 감지 상태가 초기화되어도
    // 상호 감지 실패만으로 AI의 기존 추격/배회 로직을 중단하지 않는다.
    // 다른 게임모드는 기존 상호 감지 규칙을 그대로 유지한다.
    const AGameModeBase* ActiveGameMode = IsValid(GetWorld()) ? GetWorld()->GetAuthGameMode() : nullptr;
    const bool bRequiresMutualDetection =
        !IsValid(ActiveGameMode) || !ActiveGameMode->IsA<AMultiplayTestGameMode>();
    if (bRequiresMutualDetection && !bMutuallyDetected)
    {
        Blackboard->ClearValue(TEXT("TargetActor"));
        ClearRoamingState(*Blackboard);
        return;
    }

    // 내부원 안에서는 무조건 추격 설정!
    if (bIsInsideInnerRange)
    {
        Blackboard->SetValueAsObject(TEXT("TargetActor"), PlayerPawn);
        ClearRoamingState(*Blackboard);
        return;
    }
    
    const bool bRoamingSequenceCompleted = Blackboard->GetValueAsBool(TEXT("RoamingSequenceCompleted"));
    if (bRoamingSequenceCompleted)
    {
        Blackboard->SetValueAsBool(TEXT("RoamingSequenceCompleted"), false);
        ClearRoamingState(*Blackboard);

        // 여기서 return하지 않는다!
        // 현재 조건에 따라 추격 또는 새 랜덤 이동을 다시 결정한다.
    }
    
    const double CurrentTime = GetWorld()->GetTimeSeconds();
    const double InnerRadiusSquared = FMath::Square(static_cast<double>(DirectChaseRadius));
    const double OuterRadiusSquared = FMath::Square(static_cast<double>(RoamingOuterRadius));

    // 한 번 시작한 배회는 주변 마네킹 수가 줄어도 목적지 도착을 유지한다.
    if (bRoamingCommitted)
    {
        Blackboard->ClearValue(TEXT("TargetActor"));

        const double DestinationDistanceSquared = bHasRoamingDestination
            ? FVector::DistSquared2D(RoamingDestination, PlayerLocation)
            : 0.0;
        const bool bDestinationOutsideRing = bHasRoamingDestination &&
            (DestinationDistanceSquared <= InnerRadiusSquared || DestinationDistanceSquared > OuterRadiusSquared);
        
        if (bDestinationOutsideRing)
        {
            // 플레이어 이동으로 목적지가 고리 밖이 되면 배회 약속은 유지한 채 목적지만 다시 찾는다.
            bHasRoamingDestination = false;

            Blackboard->ClearValue(TEXT("RoamingLocation"));
        }

        if (!bHasRoamingDestination && CurrentTime >= NextRoamingQueryTime)
        {
            bHasRoamingDestination = TryFindRoamingDestination(
                MannequinLocation,
                PlayerLocation,
                DirectChaseRadius,
                RoamingOuterRadius,
                *MannequinPawn,
                RoamingDestination);

            if (bHasRoamingDestination)
            {
                bLoggedRoamingQueryFailure = false;
                Blackboard->SetValueAsVector(TEXT("RoamingLocation"), RoamingDestination);
            }
            else
            {
                NextRoamingQueryTime = CurrentTime + 1.0;
            }
        }
        return;
    }

    const float SafeGatherRadius = bHasActiveTuning
        ? FMath::Max(0.0f, ActiveTuning.MannequinGatherRadius)
        : FMath::Max(0.0f, MannequinGatherRadius);
    const double GatherRadiusSquared = FMath::Square(static_cast<double>(SafeGatherRadius));
    int32 NearbyMannequinCount = 0;
    for (AActor* OtherMannequin : AllMannequins)
    {
        if (IsValid(OtherMannequin) &&
            FVector::DistSquared2D(MannequinLocation, OtherMannequin->GetActorLocation()) <= GatherRadiusSquared)
        {
            // 현재 Mannequin 자신도 집결 인원에 포함한다.
            ++NearbyMannequinCount;
        }
    }

    const int32 SafeRequiredCount = bHasActiveTuning
        ? FMath::Max(1, ActiveTuning.RequiredMannequinCount)
        : FMath::Max(1, RequiredMannequinCount);
    const bool bHasGatheredMannequins = NearbyMannequinCount >= SafeRequiredCount;

    // 후방은 인원수와 관계없이 랜덤 이동한다.
    // 전방은 3인 이상 모였을 때만 랜덤 이동하여 서로 흩어진다.
    const bool bShouldRoam = bIsInRearHalf || bHasGatheredMannequins;
    if (!bShouldRoam)
    {
        ClearRoamingState(*Blackboard);
        Blackboard->SetValueAsObject(TEXT("TargetActor"), PlayerPawn);
        return;
    }

    Blackboard->ClearValue(TEXT("TargetActor"));
    
    if (CurrentTime < NextRoamingQueryTime)
    {
        return;
    }

    bHasRoamingDestination = TryFindRoamingDestination(
        MannequinLocation,
        PlayerLocation,
        DirectChaseRadius,
        RoamingOuterRadius,
        *MannequinPawn,
        RoamingDestination);
    
    if (bHasRoamingDestination)
    {
        bRoamingCommitted = true;
        bLoggedRoamingQueryFailure = false;
        
        Blackboard->SetValueAsVector(TEXT("RoamingLocation"), RoamingDestination);
    }
    else
    {
        NextRoamingQueryTime = CurrentTime + 1.0;
        Blackboard->ClearValue(TEXT("RoamingLocation"));
        
        if (!bLoggedRoamingQueryFailure)
        {
            UE_LOG(LogTemp, Warning,
                TEXT("Mannequin roaming destination unavailable for %s: check NavMesh coverage and player range settings."),
                *GetNameSafe(MannequinPawn));
            bLoggedRoamingQueryFailure = true;
        }
    }
}
