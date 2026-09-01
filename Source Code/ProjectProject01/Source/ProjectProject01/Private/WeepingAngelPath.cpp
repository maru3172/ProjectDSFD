// Fill out your copyright notice in the Description page of Project Settings.


#include "WeepingAngelPath.h"

#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "WeepingAngelCharacter.h"
#include "Components/BillboardComponent.h"

// Sets default values
AWeepingAngelPath::AWeepingAngelPath()
{
 	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;
	
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root Component"));
	RootComponent = Root;

	PlayerViewPoint = CreateDefaultSubobject<USceneComponent>(TEXT("Player View Point Component"));
	PlayerViewPoint->SetupAttachment(Root);

	AngelPathPoint = CreateDefaultSubobject<USceneComponent>(TEXT("Angel Path Point Component"));
	AngelPathPoint->SetupAttachment(Root);

	// 에디터에서 액터를 쉽게 선택하기 위한 아이콘
	Billboard = CreateDefaultSubobject<UBillboardComponent>(TEXT("Editor Billboard Component"));
	Billboard->SetupAttachment(Root);

	Billboard->bIsScreenSizeScaled = true;
	Billboard->SetRelativeLocation(FVector::ZeroVector);
}

// Called when the game starts or when spawned
void AWeepingAngelPath::BeginPlay()
{
	Super::BeginPlay();

}

// Called every frame
void AWeepingAngelPath::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	CalculateWeight();
}

void AWeepingAngelPath::CalculateWeight()
{
	APlayerController* PlayerController =
        UGameplayStatics::GetPlayerController(GetWorld(), 0);

    if (PlayerController == nullptr)
    {
        Weight = 100000.0f;
        return;
    }

    APlayerCameraManager* CameraManager =
        PlayerController->PlayerCameraManager;

    if (CameraManager == nullptr)
    {
        Weight = 100000.0f;
        return;
    }

    APawn* PlayerPawn = PlayerController->GetPawn();

    if (PlayerPawn == nullptr)
    {
        Weight = 100000.0f;
        return;
    }

    const FVector CameraLocation = CameraManager->GetCameraLocation();
    const FVector CameraForward = CameraManager->GetCameraRotation().Vector();
    const FVector ViewPointLocation = PlayerViewPoint->GetComponentLocation();
    const FVector ToViewPoint = (ViewPointLocation - CameraLocation).GetSafeNormal();
    const float ViewFactor = FVector::DotProduct(CameraForward, ToViewPoint);
    const float ClampedViewFactor = FMath::Clamp(ViewFactor, 0.0f, 1.0f);

    // 화면 안에 있는지 검사
    FVector2D ScreenPosition;

    const bool bProjected =
        PlayerController->ProjectWorldLocationToScreen(
            ViewPointLocation,
            ScreenPosition
        );

    int32 SizeX = 0;
    int32 SizeY = 0;

    PlayerController->GetViewportSize(SizeX, SizeY);

    const float ScreenMarginRatio = 0.15f;
    const float MarginX = SizeX * ScreenMarginRatio;
    const float MarginY = SizeY * ScreenMarginRatio;

    bool bInScreen = false;

    if (bProjected)
    {
        bInScreen =
            ScreenPosition.X >= -MarginX &&
            ScreenPosition.X <= SizeX + MarginX &&
            ScreenPosition.Y >= -MarginY &&
            ScreenPosition.Y <= SizeY + MarginY;
    }

    // 카메라와 Path 사이가 가려져 있는지 검사
    bool bVisible = false;

    if (bInScreen && ClampedViewFactor > 0.0f)
    {
        FCollisionQueryParams QueryParams;

        // 플레이어 자신을 Trace에서 제외
        QueryParams.AddIgnoredActor(PlayerPawn);

        // Path 자신도 제외
        QueryParams.AddIgnoredActor(this);

        TArray<AActor*> AllAngels;

        UGameplayStatics::GetAllActorsOfClass(
			GetWorld(),
            AWeepingAngelCharacter::StaticClass(),
            AllAngels
        );

        for (AActor* Angel : AllAngels)
        {
            QueryParams.AddIgnoredActor(Angel);
        }

        FHitResult HitResult;

        const bool bHit =
            GetWorld()->LineTraceSingleByChannel(
                HitResult,
                CameraLocation,
                ViewPointLocation,
                ECC_GameTraceChannel2,
                QueryParams
            );

        // 아무것에도 막히지 않았다면 통로가 보이는 상태
        bVisible = !bHit;
    }

    bVisibleToPlayer = bVisible;

    // 가중치 계산
    const float VisibilityWeight = bVisible ? ClampedViewFactor : 0.0f;

    const FVector PlayerLocation = PlayerPawn->GetActorLocation();
    const FVector PathLocation = AngelPathPoint->GetComponentLocation();
    const float Distance = FVector::Dist(PlayerLocation, PathLocation);

    // Clamp를 제거하여 5000cm보다 멀어도 거리 차이를 유지
	const float PlayerDistanceWeight = Distance / 5000.0f;

	// 보고 있는 통로일수록 큰 비용
	const float VisibilityPenalty = bVisible ? ClampedViewFactor * 10.0f : 0.0f;

	// 낮을수록 좋은 통로
    Weight = PlayerDistanceWeight + VisibilityPenalty;
}