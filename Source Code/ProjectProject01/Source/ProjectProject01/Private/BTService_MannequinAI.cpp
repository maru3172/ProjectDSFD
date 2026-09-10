// Fill out your copyright notice in the Description page of Project Settings.


#include "BTService_MannequinAI.h"

#include "Kismet/GameplayStatics.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "GameFramework/Pawn.h"
#include "AIController.h"
#include "Camera/PlayerCameraManager.h"
#include "MannequinAICharacter.h"

#include "PlayerCharacter.h"
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
    bIsRoamingWaiting = false;
    NextRoamingQueryTime = 0.0;
    RoamingResumeTime = 0.0;
    Blackboard.ClearValue(TEXT("RoamingLocation"));
}

void UBTService_MannequinAI::TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	Super::TickNode(OwnerComp, NodeMemory, DeltaSeconds);

    // 플레이어 캐릭터
    APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(GetWorld(), 0);
    APlayerCharacter* PlayerCharacter = Cast<APlayerCharacter>(PlayerPawn);
    if (!IsValid(PlayerCharacter))
    {
        return;
    }

    // 플레이어 컨트롤러
    APlayerController* PlayerController = UGameplayStatics::GetPlayerController(GetWorld(), 0);
    if (PlayerController == nullptr)
    {
        return;
    }

    // Blackboard
    UBlackboardComponent* Blackboard = OwnerComp.GetBlackboardComponent();
    if (Blackboard == nullptr)
    {
        return;
    }

    // PlayerController가 사용하는 CameraManager
    APlayerCameraManager* CameraManager = PlayerController->PlayerCameraManager;
    if (CameraManager == nullptr)
    {
        return;
    }

    // 현재 플레이어 카메라의 월드 위치, 카메라가 바라보는 방향을 가져온다.
    FVector CameraLocation = CameraManager->GetCameraLocation();
    FRotator CameraRotation = CameraManager->GetCameraRotation();

    // 현재 AI Controller가 조종하고 있는 Pawn(AI 캐릭터)
    APawn* MannequinPawn = OwnerComp.GetAIOwner()->GetPawn();
    if (MannequinPawn == nullptr)
    {
        return;
    }

    // AMannequinAICharacter
    AMannequinAICharacter* Mannequin = Cast<AMannequinAICharacter>(MannequinPawn);
    if (Mannequin == nullptr)
    {
        return;
    }

    // Mannequin 의 AI 컨트롤러
    AAIController* AIController = OwnerComp.GetAIOwner();
    if (AIController == nullptr)
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
    const float DetectionMargin = 30.0f;

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

    // 현재 화면의 크기를 가져옴
    int32 SizeX = 0;
    int32 SizeY = 0;
    PlayerController->GetViewportSize(SizeX, SizeY);
    if (SizeX <= 0 || SizeY <= 0)
    {
        return;
    }

    // 화면의 15%를 여유 공간으로 설정
    const float ScreenMarginRaito = 0.15f;
    const float MarginX = SizeX * ScreenMarginRaito;
    const float MarginY = SizeY * ScreenMarginRaito;

    // 화면의 실제 영역 + 15% 의 여유 영역 안에 AI가 있는지 확인
    for (const FVector& Point : Points)
    {
        // AI의 월드 좌표를 화면 좌표(X, Y)로 변환한 값을 저장한다.
        FVector2D ScreenPosition;

        // AI의 월드 위치를 화면 좌표로 변환
        bool bProjected = PlayerController->ProjectWorldLocationToScreen(Point, ScreenPosition);

        // 화면 뒤에 있으면 다음 점 검사
        if (!bProjected)
        {
            continue;
        }

        // 화면 안에 있는지 검사
        bool bThisPointInScreen = 
            ScreenPosition.X >= -MarginX && 
            ScreenPosition.X <= SizeX + MarginX && 
            ScreenPosition.Y >= -MarginY && 
            ScreenPosition.Y <= SizeY + MarginY;

        // 하나라도 화면에 있으면
        if (bThisPointInScreen)
        {
            // 카메라에서 Mannequin 의 지점까지 Line Trace를 했을 때, 어떤 물체에 먼저 부딪혔는지에 대한 정보를 저장한다.
            FHitResult HitResult;

            // Line Trace를 수행할 때 사용할 충돌 설정을 만든다.
            FCollisionQueryParams QueryParams;
            // 플레이어 캐릭터 자신은 Line Trace의 충돌 대상에서 제외한다.
            // 카메라가 플레이어 캐릭터의 충돌에 먼저 걸리는 것을 방지한다.
            QueryParams.AddIgnoredActor(PlayerPawn);

            // 현재 월드에 존재하는 모든 Mannequin 을 가져온다.
            TArray<AActor*> AllMannequins;
            UGameplayStatics::GetAllActorsOfClass(GetWorld(), AMannequinAICharacter::StaticClass(), AllMannequins);

            // 현재 Mannequin 이 아닌 다른 Mannequin 들을 Line Trace에서 제외한다.
            for (AActor* OtherMannequin : AllMannequins)
            {
                if (OtherMannequin != Mannequin)
                {
                    QueryParams.AddIgnoredActor(OtherMannequin);
                }
            }

            // 플레이어 카메라에서 현재 검사 중인 Mannequin 의 지점까지 직선으로 Line Trace 를 수행한다.
            bool bHit = GetWorld()->LineTraceSingleByChannel(
                HitResult,
                CameraManager->GetCameraLocation(),
                Point,
                ECC_GameTraceChannel1,
                QueryParams
            );

            // Line Trace가 무언가에 부딪혔고, 가장 먼저 부딪힌 대상이 Mannequin 라면 플레이어가 실제로 Mannequin 을 볼 수 있다고 판단한다.
            // 만약 Mannequin 와 플레이어 사이에 벽이 있다면 Line Trace는 Mannequin 보다 벽에 먼저 부딪히므로 HitResult.GetActor() == MannequinPawn 조건이 false가 된다.
            if (bHit && HitResult.GetActor() == MannequinPawn)
            {
                // Mannequin 이 화면에 있고, Mannequin 까지의 시야도 막혀 있지 않으므로 플레이어가 Mannequin 을 바라보고 있다고 판단한다.
                bInScreen = true;

                // 이미 Mannequin 을 볼 수 있는 지점을 하나 찾았으므로 나머지 지점은 검사할 필요가 없다.
                break;
            }
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
            FVector2D GuardScreenPosition;
            if (!PlayerController->ProjectWorldLocationToScreen(GuardPoint, GuardScreenPosition))
            {
                continue;
            }
            if (GuardScreenPosition.X < -MarginX || GuardScreenPosition.X > SizeX + MarginX ||
                GuardScreenPosition.Y < -MarginY || GuardScreenPosition.Y > SizeY + MarginY)
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
    Blackboard->SetValueAsBool(TEXT("PlayerLookingAtMannequin"), bInScreen);

    if (bInScreen)
    {
        // 정지용 여유 영역이 보이는 순간 현재 이동 요청을 중단한다.
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

    const float DirectChaseRadius = PlayerCharacter->GetDirectChaseRadius();
    const float RoamingOuterRadius = PlayerCharacter->GetRoamingOuterRadius();
    const double PlayerDistanceSquared = FVector::DistSquared2D(MannequinLocation, PlayerLocation);
    const bool bIsInsideInnerRange =
        PlayerDistanceSquared <= FMath::Square(static_cast<double>(DirectChaseRadius));
    const bool bIsBetweenPlayerRanges = !bIsInsideInnerRange &&
        PlayerDistanceSquared <= FMath::Square(static_cast<double>(RoamingOuterRadius));
    const bool bIsOutsideOuterRange = !bIsInsideInnerRange && !bIsBetweenPlayerRanges;

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
    for (AActor* OtherAngel : AllMannequins)
    {
        if (OtherAngel != MannequinPawn)
        {
            QueryParams.AddIgnoredActor(OtherAngel);
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
    Blackboard->SetValueAsBool(TEXT("IsOutsideOuterRange"), bMutuallyDetected && bIsOutsideOuterRange);
    Blackboard->SetValueAsBool(TEXT("IsBetweenPlayerRanges"), bMutuallyDetected && bIsBetweenPlayerRanges);
    Blackboard->SetValueAsBool(TEXT("IsInsideInnerRange"), bMutuallyDetected && bIsInsideInnerRange);

    if (!bMutuallyDetected)
    {
        Blackboard->ClearValue(TEXT("TargetActor"));
        ClearRoamingState(*Blackboard);
        bRoamingTriggerArmed = true;
        return;
    }

    if (bIsInsideInnerRange || bIsOutsideOuterRange)
    {
        Blackboard->SetValueAsObject(TEXT("TargetActor"), PlayerPawn);
        ClearRoamingState(*Blackboard);
        return;
    }

    const double CurrentTime = GetWorld()->GetTimeSeconds();
    const double InnerRadiusSquared = FMath::Square(static_cast<double>(DirectChaseRadius));
    const double OuterRadiusSquared = FMath::Square(static_cast<double>(RoamingOuterRadius));

    // 한 번 시작한 배회는 주변 마네킹 수가 줄어도 목적지 도착과 휴식 완료까지 유지한다.
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
            bIsRoamingWaiting = false;
            RoamingResumeTime = 0.0;
            Blackboard->ClearValue(TEXT("RoamingLocation"));
        }

        const bool bReachedDestination = bHasRoamingDestination &&
            FVector::DistSquared2D(MannequinLocation, RoamingDestination) <= FMath::Square(100.0);
        if (bReachedDestination && !bIsRoamingWaiting)
        {
            const float SafeMinWaitTime = FMath::Max(0.0f, MinRoamingWaitTime);
            const float SafeMaxWaitTime = FMath::Max(SafeMinWaitTime, MaxRoamingWaitTime);
            bIsRoamingWaiting = true;
            RoamingResumeTime = CurrentTime + FMath::FRandRange(SafeMinWaitTime, SafeMaxWaitTime);
        }

        if (bIsRoamingWaiting)
        {
            if (CurrentTime >= RoamingResumeTime)
            {
                ClearRoamingState(*Blackboard);
                Blackboard->SetValueAsObject(TEXT("TargetActor"), PlayerPawn);
            }
            return;
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
                if (!bLoggedRoamingQueryFailure)
                {
                    UE_LOG(LogTemp, Warning,
                        TEXT("Committed mannequin roaming destination unavailable for %s: check NavMesh coverage and player range settings."),
                        *GetNameSafe(MannequinPawn));
                    bLoggedRoamingQueryFailure = true;
                }
            }
        }
        return;
    }

    const float SafeGatherRadius = FMath::Max(0.0f, MannequinGatherRadius);
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

    const int32 SafeRequiredCount = FMath::Max(1, RequiredMannequinCount);
    if (NearbyMannequinCount < SafeRequiredCount)
    {
        // 3명 미만이 된 뒤에만 다음 집결 시 배회 1회를 다시 허용한다.
        bRoamingTriggerArmed = true;
        Blackboard->ClearValue(TEXT("RoamingLocation"));
        Blackboard->SetValueAsObject(TEXT("TargetActor"), PlayerPawn);
        return;
    }

    if (!bRoamingTriggerArmed)
    {
        // 같은 집결 상태에서 배회를 반복하지 않고 플레이어를 추격한다.
        Blackboard->ClearValue(TEXT("RoamingLocation"));
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
        bRoamingTriggerArmed = false;
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
