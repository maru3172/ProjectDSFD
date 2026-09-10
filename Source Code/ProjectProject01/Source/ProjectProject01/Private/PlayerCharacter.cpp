// Fill out your copyright notice in the Description page of Project Settings.


#include "PlayerCharacter.h"

#include "GameFramework/SpringArmComponent.h"
#include "Camera/CameraComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Components/CapsuleComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"

// Sets default values
APlayerCharacter::APlayerCharacter()
{
 	// Set this character to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

	SpringArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("Spring Arm"));
	SpringArm->SetupAttachment(RootComponent);

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(SpringArm);
}

// Called when the game starts or when spawned
void APlayerCharacter::BeginPlay()
{
	Super::BeginPlay();
	
	// 현재 플레이어가 소유한 컨트롤러를 가져온다.
	//APlayerController* PlayerController = GetWorld()->GetFirstPlayerController();
	// 이보다 현재 캐릭터가 가지고 있는 컨트롤러를 가져오는 게 좋다.
	APlayerController* PlayerController = Cast<APlayerController>(GetController());
	
	// 만일, 플레이어 컨트롤러 변수에 값이 들어 있다면...
	if (PlayerController != nullptr)
	{
		// 플레이어 컨트롤러로부터 입력 서브 시스템 정보를 가져온다.
		UEnhancedInputLocalPlayerSubsystem* Subsystem = 
		ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PlayerController->GetLocalPlayer());

		if (Subsystem != nullptr)
		{
			// 입력 서브 시스템에 IMC 파일 변수를 연결한다.
			Subsystem->AddMappingContext(IMC_PlayerInput, 0);
		}
	}
}

// Called every frame
void APlayerCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// 디버그 원 그리기
	DrawAIRangeDebug();
}

// =========================================================================================================================
// 플레이어 입력 함수 관련
// =========================================================================================================================

// Called to bind functionality to input
void APlayerCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent);

	if (EnhancedInputComponent != nullptr)
	{
		EnhancedInputComponent->BindAction(IA_Move, ETriggerEvent::Triggered, this, &APlayerCharacter::Move);
		//EnhancedInputComponent->BindAction(IA_Move, ETriggerEvent::Completed, this, &APlayerCharacter::Move);
		EnhancedInputComponent->BindAction(IA_Look, ETriggerEvent::Triggered, this, &APlayerCharacter::Look);
		//EnhancedInputComponent->BindAction(IA_Look, ETriggerEvent::Completed, this, &APlayerCharacter::Look);
	}
}

void APlayerCharacter::Move(const FInputActionValue& Value)
{
	const FVector2D MovementVector = Value.Get<FVector2D>();

	AddMovementInput(GetActorForwardVector(), MovementVector.X);
	AddMovementInput(GetActorRightVector(), MovementVector.Y);
}

void APlayerCharacter::Look(const FInputActionValue& Value)
{
	const FVector2D LookVector = Value.Get<FVector2D>();

	AddControllerYawInput(LookVector.X);
	AddControllerPitchInput(LookVector.Y);
}

// =========================================================================================================================
// 디버그 원 그리기
// =========================================================================================================================

float APlayerCharacter::GetDirectChaseRadius() const
{
	return FMath::Max(0.0f, DirectChaseRadius);
}

float APlayerCharacter::GetRoamingOuterRadius() const
{
	return FMath::Max(GetDirectChaseRadius(), RoamingOuterRadius);
}

void APlayerCharacter::DrawAIRangeDebug() const
{
	if (!bShowAIRangeDebug)
	{
		return;
	}

	const UCapsuleComponent* Capsule = GetCapsuleComponent();
	if (!IsValid(GetWorld()) || !IsValid(Capsule))
	{
		return;
	}

	// 바닥과의 Z-fighting을 줄이기 위해 캡슐 바닥보다 약간 위에 수평 원을 그린다.
	FVector CircleCenter = GetActorLocation();
	CircleCenter.Z -= Capsule->GetScaledCapsuleHalfHeight();
	CircleCenter.Z += 5.0f;

	constexpr int32 CircleSegments = 64;
	constexpr float LineThickness = 3.0f;
	const FVector CircleAxisX = FVector::ForwardVector;
	const FVector CircleAxisY = FVector::RightVector;

	DrawDebugCircle(
		GetWorld(), CircleCenter, GetDirectChaseRadius(), CircleSegments, FColor::Red,
		false, 0.0f, 0, /*LineThickness*/ 7.5f, CircleAxisX, CircleAxisY, false);
	DrawDebugCircle(
		GetWorld(), CircleCenter, GetRoamingOuterRadius(), CircleSegments, FColor::Blue,
		false, 0.0f, 0, /*LineThickness*/ 7.5f, CircleAxisX, CircleAxisY, false);
}