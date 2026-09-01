// Fill out your copyright notice in the Description page of Project Settings.


#include "BTService_WeepingAngelExample01.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/Pawn.h"
#include "AIController.h"

#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/KismetMathLibrary.h"

#include "WeepingAngelCharacter.h"
#include "Components/CapsuleComponent.h"

#include "Components/SkeletalMeshComponent.h"

#include "NavigationSystem.h"
#include "WeepingAngelPath.h"


UBTService_WeepingAngelExample01::UBTService_WeepingAngelExample01()
{
	NodeName = TEXT("Weeping Angel Behavior Example01");

    // Behavior Tree Service가 일정한 시간 간격이 아니라
    // 매 프레임에 가깝게 TickNode()를 실행하도록 설정한다.
    // 0.0f로 설정하면 별도의 대기 시간 없이 계속 검사한다.
    // 플레이어가 천사를 바라보는 순간 즉시 반응해야 하므로 사용한다.
    Interval = 0.0f;

    // Service의 Tick 간격에 추가되는 무작위 시간 차이를 설정한다.
    // 0.0f로 설정하여 Tick 간격에 랜덤한 지연이 발생하지 않도록 한다.
    // 따라서 매번 일정한 간격으로 Service가 실행된다.
    RandomDeviation = 0.0f;

    // 시작할 때는 모두 서로를 볼 수 없기에 false 로 설정한다.
    PlayerSeeAngel = false;
    AngelSeePlayer = false;
}

void UBTService_WeepingAngelExample01::TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	Super::TickNode(OwnerComp, NodeMemory, DeltaSeconds);

    // 현재 플레이어 캐릭터를 가져온다.
    // GetPlayerPawn()의 0은 첫 번째 플레이어(싱글 플레이 기준 플레이어)를 의미한다.
    APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(GetWorld(), 0);

    // 플레이어를 가져오지 못했다면 더 이상 진행할 수 없으므로 함수를 종료한다.
    if (PlayerPawn == nullptr)
    {
        return;
    }

    // 현재 실행 중인 Behavior Tree가 사용하는 Blackboard Component를 가져온다.
    // Blackboard에는 TargetActor와 같은 AI의 정보를 저장한다.
    UBlackboardComponent* Blackboard = OwnerComp.GetBlackboardComponent();
    // Blackboard를 가져오지 못했다면 값을 저장하거나 삭제할 수 없으므로 함수를 종료한다.
    if (Blackboard == nullptr)
    {
        return;
    }

    // 현재 플레이어의 PlayerController를 가져온다.
    // GetWorld() : 현재 게임 월드
    // 0 : 첫 번째 플레이어
    APlayerController* PlayerController = UGameplayStatics::GetPlayerController(GetWorld(), 0);
    // PlayerController를 가져오지 못했다면 함수를 종료한다.
    if (PlayerController == nullptr)
    {
        return;
    }

    // PlayerController가 사용하는 CameraManager를 가져온다.
    // CameraManager를 통해 현재 플레이어 카메라의 위치와 회전 등을 확인할 수 있다. 
    APlayerCameraManager* CameraManager = PlayerController->PlayerCameraManager;
    // CameraManager가 없다면 함수를 종료한다.
    if (CameraManager == nullptr)
    {
        return;
    }

    // 현재 플레이어 카메라의 월드 위치를 가져온다.
    FVector CameraLocation = CameraManager->GetCameraLocation();
    // 현재 플레이어 카메라가 바라보는 방향을 가져온다.
    FRotator CameraRotation = CameraManager->GetCameraRotation();

    AAIController* AIController =
        OwnerComp.GetAIOwner();

    if (AIController == nullptr)
    {
        return;
    }

    // 현재 AI Controller가 조종하고 있는 Pawn(AI 캐릭터)을 가져온다.
    APawn* AngelPawn = OwnerComp.GetAIOwner()->GetPawn();
    // AI Pawn이 없다면 함수를 종료한다.
    if (AngelPawn == nullptr)
    {
        return;
    }

    // 현재 AI Pawn을 우는 천사 캐릭터 클래스인 AWeepingAngelCharacter 타입으로 변환한다.
    // AngelPawn은 APawn 타입이기 때문에, 우는 천사 캐릭터에서 만든 SetFrozen() 등의 함수를 사용하려면 AWeepingAngelCharacter 타입으로 변환해야 한다.
    AWeepingAngelCharacter* Angel = Cast<AWeepingAngelCharacter>(AngelPawn);
    // AI Pawn이 우는 천사 캐릭터로 변환되지 않았다면 이후에 천사 전용 함수를 사용할 수 없으므로 함수를 종료한다.
    if (Angel == nullptr)
    {
        return;
    }

    // 천사의 캡슐 컴포넌트를 가져온다.
    UCapsuleComponent* AngelCapsule = Angel->GetCapsuleComponent();
    if (AngelCapsule == nullptr)
    {
        return;
    }

    // 캡슐의 중심 위치를 가져온다.
    FVector CapsuleCenter = AngelCapsule->GetComponentLocation();
    // 캡슐의 절반 높이를 가져온다.
    float HalfHeight = AngelCapsule->GetScaledCapsuleHalfHeight();
    // 캡슐의 반지름을 가져온다.
    float Radius = AngelCapsule->GetScaledCapsuleRadius();

    // 화면 판정을 위한 캡슐의 여유 범위
    const float DetectionMargin = 30.0f;

    // 캡슐의 판정 반지름과 높이에 여유를 추가한다.
    float DetectionRadius = Radius + DetectionMargin;           // 사용하려면 해도 좋다.
    float DetectionHalfHeight = HalfHeight + DetectionMargin;   // 높이는 굳이 사용할 필요가 없을 것 같다. 사용하려면 해도 좋다.

    // 캡슐의 방향을 가져온다.
    FVector Up = AngelCapsule->GetUpVector();
    FVector Right = AngelCapsule->GetRightVector();
    FVector Forward = AngelCapsule->GetForwardVector();

    // 천사의 Skeletal Mesh Component를 가져온다.
    USkeletalMeshComponent* AngelMesh = Angel->GetMesh();
    // Skeletal Mesh를 가져오지 못했다면 함수를 종료한다.
    if (AngelMesh == nullptr)
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

    // 화면에 노출되었는지 확인할 천사의 여러 위치를 저장한다.
    TArray<FVector> Points;

    // 캡슐의 중심 위치를 추가한다.
    Points.Add(CapsuleCenter);

    // 캡슐의 아래쪽과 위쪽 위치를 추가한다.
    Points.Add(CapsuleCenter - Up * HalfHeight);
    Points.Add(CapsuleCenter + Up * HalfHeight);

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
        if (AngelMesh->GetBoneIndex(BoneName) != INDEX_NONE)
        {
            // 현재 애니메이션에서 해당 Bone의 월드 위치를 검사 목록에 추가한다.
            Points.Add(AngelMesh->GetBoneLocation(BoneName));
        }
    }

    // 천사가 화면에 보이는지 여부를 저장한다.
    bool bInScreen = false;

    // 현재 화면의 크기를 가져옴
    int32 SizeX;
    int32 SizeY;
    PlayerController->GetViewportSize(SizeX, SizeY);

    // 화면의 15%를 여유 공간으로 설정
    const float ScreenMarginRaito = 0.15f;
    const float MarginX = SizeX * ScreenMarginRaito;
    const float MarginY = SizeY * ScreenMarginRaito;

    // 화면의 실제 영역 + 15%의 여유 영역 안에 AI가 있는지 확인
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
            // 카메라에서 천사의 지점까지 Line Trace를 했을 때, 어떤 물체에 먼저 부딪혔는지에 대한 정보를 저장한다.
            FHitResult HitResult;

            // Line Trace를 수행할 때 사용할 충돌 설정을 만든다.
            FCollisionQueryParams QueryParams;
            // 플레이어 캐릭터 자신은 Line Trace의 충돌 대상에서 제외한다.
            // 카메라가 플레이어 캐릭터의 충돌에 먼저 걸리는 것을 방지한다.
            QueryParams.AddIgnoredActor(PlayerPawn);

            // 현재 월드에 존재하는 모든 우는 천사를 가져온다.
            TArray<AActor*> AllAngels;
            UGameplayStatics::GetAllActorsOfClass(GetWorld(), AWeepingAngelCharacter::StaticClass(), AllAngels);

            // 현재 천사가 아닌 다른 천사들을 Line Trace에서 제외한다.
            for (AActor* OtherAngel : AllAngels)
            {
                if (OtherAngel != Angel)
                {
                    QueryParams.AddIgnoredActor(OtherAngel);
                }
            }

            // 플레이어 카메라에서 현재 검사 중인 천사의 지점까지
            // 직선으로 Line Trace를 수행한다.
            //
            // HitResult : Line Trace가 어떤 물체에 부딪혔는지 저장한다.
            // CameraManager->GetCameraLocation() : Trace의 시작 위치인 플레이어 카메라 위치
            // Point : Trace의 도착 위치인 현재 검사 중인 천사의 지점
            // ECC_GameTraceChannel1 : 시야 판정에 사용하는 Collision Channel -> 전용 Angel 충돌 채널
            // QueryParams : Trace에서 제외할 액터 등의 정보를 전달한다.
            bool bHit = GetWorld()->LineTraceSingleByChannel(
                HitResult,
                CameraManager->GetCameraLocation(),
                Point,
                ECC_GameTraceChannel1,
                QueryParams
            );

            // Line Trace가 무언가에 부딪혔고,
            // 가장 먼저 부딪힌 대상이 우는 천사라면
            // 플레이어가 실제로 천사를 볼 수 있다고 판단한다.
            //
            // 만약 천사와 플레이어 사이에 벽이 있다면
            // Line Trace는 천사보다 벽에 먼저 부딪히므로
            // HitResult.GetActor() == AngelPawn 조건이 false가 된다.
            if (bHit && HitResult.GetActor() == AngelPawn)
            {
                // 천사가 화면에 있고, 천사까지의 시야도 막혀 있지 않으므로 플레이어가 천사를 바라보고 있다고 판단한다.
                bInScreen = true;

                // 이미 천사를 볼 수 있는 지점을 하나 찾았으므로 나머지 지점은 검사할 필요가 없다.
                break;
            }
        }
    }

    // 플레이어가 현재 천사를 바라보고 있는지 여부를 Blackboard의 PlayerLookingAtAngel Key에 저장한다.
    // bInScreen이 true라면 천사가 현재 화면의 판정 범위 안에 있다는 의미이고, false라면 천사가 화면의 판정 범위 밖에 있다는 의미이다.
    Blackboard->SetValueAsBool(TEXT("PlayerLookingAtAngel"), bInScreen);

    if (bInScreen)
    {
        // // 플레이어가 천사를 보고 있으므로 AI의 이동을 즉시 중단한다.
        // OwnerComp.GetAIOwner()->StopMovement();
        // 현재 재생 중인 애니메이션을 현재 프레임에서 그대로 정지한다.
        Angel->SetFrozen(true);

        // 플레이어의 화면에 보이지도 않고, 천사가 플레이어를 감지하기만 하면 쫓아오는 건 불합리한 죽음을 당할 수 있기 때문에 이도 조건에 포함했다.
        PlayerSeeAngel = true;
    }
    else
    {
        // 플레이어가 천사를 보고 있지 않으므로 애니메이션 정지를 해제한다
        Angel->SetFrozen(false);
    }

    // 천사와 플레이어의 위치를 가져온다.
    FVector AngelLocation = Angel->GetActorLocation();
    FVector PlayerLocation = PlayerPawn->GetActorLocation();

    // Line Trace의 충돌 정보를 저장할 변수이다.
    FHitResult HitResult;

    // Line Trace에 사용할 충돌 설정을 생성한다.
    FCollisionQueryParams QueryParams;

    // 현재 천사는 Line Trace에서 제외한다.
    QueryParams.AddIgnoredActor(AngelPawn);

    // 현재 월드에 존재하는 모든 우는 천사를 가져온다.
    TArray<AActor*> AllAngels;
    UGameplayStatics::GetAllActorsOfClass(GetWorld(), AWeepingAngelCharacter::StaticClass(), AllAngels);

    // 현재 천사가 아닌 다른 천사들을 Line Trace에서 제외한다.
    for (AActor* OtherAngel : AllAngels)
    {
        if (OtherAngel != AngelPawn)
        {
            QueryParams.AddIgnoredActor(OtherAngel);
        }
    }

    // 천사에서 플레이어까지 Line Trace를 수행한다.
    bool bHit = GetWorld()->LineTraceSingleByChannel(
        HitResult,
        AngelLocation,
        PlayerLocation,
        ECC_GameTraceChannel1,
        QueryParams
    );

    // Line Trace가 플레이어에게 도달했다면 발견한 것으로 판단한다.
    if (bHit && HitResult.GetActor() == PlayerPawn)
    {
        AngelSeePlayer = true;
    }

    // 플레이어 직접 추격 가능 여부
    const bool bHasDirectLineToPlayer = bHit && HitResult.GetActor() == PlayerPawn;

    // 천사가 플레이어를 한 번 발견했다면 추적 대상으로 설정한다.
    // 플레이어의 화면에 보이지도 않고, 천사가 플레이어를 감지하기만 하면 쫓아오는 건 불합리한 죽음을 당할 수 있기 때문에 이도 조건에 포함했다.
    const bool bChaseStarted = PlayerSeeAngel && AngelSeePlayer;
    Blackboard->SetValueAsBool(TEXT("ChaseStarted"), bChaseStarted);

    if (!bChaseStarted)
    {
        Blackboard->SetValueAsBool(TEXT("CanDirectChase"), false);
        Blackboard->ClearValue(TEXT("TargetActor"));
        Blackboard->ClearValue(TEXT("AngelChaseStart"));

        return;
    }

    // 플레이어를 기본 추격 대상으로 저장
    Blackboard->SetValueAsObject(TEXT("TargetActor"), PlayerPawn);

    // AngelChaseStart를 Behavior Tree에서 사용하고 있다면 유지
    Blackboard->SetValueAsObject(TEXT("AngelChaseStart"), PlayerPawn);

    // 이전 프레임의 직접 추격 상태를 먼저 가져온다.
    const bool bWasDirectChasing = Blackboard->GetValueAsBool(TEXT("CanDirectChase"));

    // 거리와 관계없이 천사와 플레이어 사이에 벽이 없다면 직접 추격 상태를 유지한다.
    const bool bCanDirectChase = bHasDirectLineToPlayer;

    Blackboard->SetValueAsBool(TEXT("CanDirectChase"), bCanDirectChase);

    // 직접 추격이 가능하면 통로를 새로 선택하지 않는다.
    if (bCanDirectChase)
    {
        return;
    }

    // 직접 추격이 끝난 순간 CurrentPath 재설정
    // 이전 프레임에는 직접 추격했지만 지금은 벽에 가려져 직접 추격할 수 없어진 상황
    if (bWasDirectChasing)
    {
        TArray<AActor*> PathActors;

        UGameplayStatics::GetAllActorsOfClass(GetWorld(), AWeepingAngelPath::StaticClass(), PathActors);

        AWeepingAngelPath* ClosestPath = nullptr;
        float ClosestDistanceSquared = TNumericLimits<float>::Max();

        for (AActor* PathActor : PathActors)
        {
            AWeepingAngelPath* Path = Cast<AWeepingAngelPath>(PathActor);
            if (Path == nullptr)
            {
                continue;
            }

            const float DistanceSquared = FVector::DistSquared2D(AngelLocation, Path->GetAngelPathLocation());

            if (DistanceSquared < ClosestDistanceSquared)
            {
                ClosestDistanceSquared = DistanceSquared;
                ClosestPath = Path;
            }
        }

        if (ClosestPath != nullptr)
        {
            Angel->SetCurrentPath(ClosestPath);

            Blackboard->ClearValue(TEXT("NextPath"));

            UE_LOG(
                LogTemp,
                Warning,
                TEXT(
                    "Direct Chase Ended: "
                    "CurrentPath = %s"
                ),
                *ClosestPath->GetName()
            );
        }
    }

    AWeepingAngelPath* CurrentPath = Angel->GetCurrentPath();
    if (CurrentPath == nullptr)
    {
        return;
    }

    // 현재 Path와 연결된 Path 가져오기
    const TArray<TObjectPtr<AWeepingAngelPath>>& ConnectedPaths = CurrentPath->GetConnectedPaths();

    if (ConnectedPaths.Num() == 0)
    {
        return;
    }

    TArray<AWeepingAngelPath*> CandidatePaths;

    // 천사와 마지막 도착 Path 사이의 거리
    const float DistanceFromCurrentPath = FVector::Dist2D(Angel->GetActorLocation(), CurrentPath->GetAngelPathLocation());

    // CurrentPath에서 어느 정도 벗어난 상태라면 되돌아가는 선택지를 후보에 추가
    const float ReturnPathMinimumDistance = 100.0f;

    if (DistanceFromCurrentPath > ReturnPathMinimumDistance)
    {
        CandidatePaths.Add(CurrentPath);
    }

    for (const TObjectPtr<AWeepingAngelPath>& ConnectedPath : ConnectedPaths)
    {
        if (ConnectedPath != nullptr)
        {
            CandidatePaths.AddUnique(ConnectedPath.Get());
        }
    }

    if (CandidatePaths.Num() == 0)
    {
        return;
    }

    // 가장 좋은 다음 Path 찾기
    AWeepingAngelPath* BestPath = nullptr;
    float BestWeight = TNumericLimits<float>::Max();

    for (AWeepingAngelPath* Path : CandidatePaths)
    {
        if (Path == nullptr)
        {
            continue;
        }

        const float PathWeight = Path->GetWeight();

        if (PathWeight < BestWeight)
        {
            BestWeight = PathWeight;
            BestPath = Path;
        }
    }


    if (BestPath == nullptr)
    {
        return;
    }

    // 현재 Blackboard의 NextPath 가져오기
    AWeepingAngelPath* CurrentNextPath = Cast<AWeepingAngelPath>(Blackboard->GetValueAsObject(TEXT("NextPath")));

    // NextPath가 변경되었을 때만 갱신
    if (CurrentNextPath != BestPath)
    {
        // 새로운 NextPath
        Blackboard->SetValueAsObject(TEXT("NextPath"), BestPath);

        // 새로운 이동 위치
        Blackboard->SetValueAsVector(TEXT("NextPathLocation"), BestPath->GetAngelPathLocation());

        UE_LOG(
            LogTemp,
            Warning,
            TEXT(
                "Angel Path Changed : %s -> %s"
            ),
            CurrentNextPath
                ? *CurrentNextPath->GetName()
                : TEXT("None"),

            *BestPath->GetName()
        );
    }
}