// Fill out your copyright notice in the Description page of Project Settings.


#include "PlayerCharacter.h"

#include "GameFramework/SpringArmComponent.h"
#include "Camera/CameraComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Components/CapsuleComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"

// 카메라 B 키 디버깅 관련
#include "GameFramework/PlayerController.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "InputAction.h"
#include "UObject/ConstructorHelpers.h"
#include "SceneView.h"
#include "SceneViewExtension.h"


// =========================================================================================================================
// 카메라 B 키 디버깅 관련
// =========================================================================================================================

// SetupView는 GameThread에서 렌더링용 View에만 적용된다.
// SetupViewPoint/SetupViewProjectionMatrix를 변경하지 않아 AI의 투영은 유지된다.
class FProjectProject01TopViewExtension final : public FWorldSceneViewExtension
{
public:
    FProjectProject01TopViewExtension(const FAutoRegister& AutoRegister, APlayerCharacter* InPlayer)
        : FWorldSceneViewExtension(AutoRegister, InPlayer->GetWorld()), Player(InPlayer)
    {
    }

    virtual void SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView) override
    {
        APlayerCharacter* Owner = Player.Get();
        if (!IsValid(Owner) || !Owner->IsLocallyControlled() ||
            InView.bIsSceneCapture || InView.ViewActor.ActorUniqueId != Owner->GetUniqueID())
        {
            return;
        }

        UWorld* PlayerWorld = Owner->GetWorld();
        UCameraComponent* DebugCamera = Owner->DebugTopViewCamera.Get();
        const bool bUseTopView = IsValid(PlayerWorld) && PlayerWorld->WorldType == EWorldType::PIE &&
            Owner->bUseTopViewInPIE && IsValid(DebugCamera);
        if (bUseTopView != bWasTopView)
        {
            InView.bCameraCut = true;
            bWasTopView = bUseTopView;
        }
        if (!bUseTopView)
        {
            return;
        }

        // 렌즈/FOV는 기존 화면 설정을 유지하고 관찰 위치와 방향만 교체한다.
        InView.ViewLocation = DebugCamera->GetComponentLocation();
        InView.ViewRotation = DebugCamera->GetComponentRotation();
        InView.UpdateViewMatrix();
        InView.CullingOrigin = InView.ViewLocation;
        InView.bHasNearClippingPlane = InView.ViewMatrices.GetWorldToClip().GetFrustumNearPlane(InView.NearClippingPlane);

        // GameThread에서 실제 표시할 화면의 역행렬을 다음 입력 처리에 제공한다.
        Owner->DebugClipToWorld = InView.ViewMatrices.GetClipToWorld();
        Owner->DebugViewRect = InView.UnscaledViewRect;
        Owner->bHasDebugView = true;
    }

private:
    // 프레임 끝까지 렌더러가 확장을 보관해도 Pawn 수명을 연장하지 않는다.
    TWeakObjectPtr<APlayerCharacter> Player;
    bool bWasTopView = false;
};

// =========================================================================================================================
// 기본 설정
// =========================================================================================================================

// Sets default values
APlayerCharacter::APlayerCharacter()
{
 	// Set this character to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

	SpringArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("Spring Arm"));
	SpringArm->SetupAttachment(RootComponent);

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(SpringArm);

	// 카메라 B 키 디버깅 관련
	static ConstructorHelpers::FObjectFinder<UInputAction> DebugCameraAction(
        TEXT("/Game/MyProject/Input/IA_Player_DebugCamera.IA_Player_DebugCamera"));
    if (DebugCameraAction.Succeeded())
    {
        IA_Player_DebugCamera = DebugCameraAction.Object;
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("Missing input action: /Game/MyProject/Input/IA_Player_DebugCamera"));
    }

    DebugTopViewCamera = CreateEditorOnlyDefaultSubobject<UCameraComponent>(TEXT("Debug Top View Camera"));
    if (DebugTopViewCamera)
    {
        DebugTopViewCamera->SetupAttachment(RootComponent);
        DebugTopViewCamera->SetAutoActivate(false);
        DebugTopViewCamera->bUsePawnControlRotation = false;
        DebugTopViewCamera->SetAbsolute(false, true, false);
        DebugTopViewCamera->SetRelativeLocation(FVector(0.0f, 0.0f, 5000.0f));
        DebugTopViewCamera->SetRelativeRotation(FRotator(-90.0f, 0.0f, 0.0f));
    }
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

    // 카메라 B 키 디버깅 관련
    // PIE를 시작할 때마다 반드시 기본 플레이어 화면에서 시작한다.
    bUseTopViewInPIE = false;
    if (IsValid(GetWorld()) && GetWorld()->WorldType == EWorldType::PIE)
    {
        if (IsValid(DebugTopViewCamera))
        {
            // Blueprint 설정으로 Auto Activate가 바뀌어도 원래 카메라를 유지한다.
            DebugTopViewCamera->Deactivate();
            TopViewExtension = FSceneViewExtensions::NewExtension<FProjectProject01TopViewExtension>(this);
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("PIE top view unavailable: missing debug camera on %s."), *GetName());
        }
    }
}

void APlayerCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);
	
    bUseTopViewInPIE = false;
    RestoreDebugControls();
    TopViewExtension.Reset();
}

// Called every frame
void APlayerCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// 디버그 원 그리기
	DrawAIRangeDebug();

	// 카메라 디버그 관련
	UpdateDebugCursorAim();
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

		if (IsValid(GetWorld()) && GetWorld()->WorldType == EWorldType::PIE)
        {
            if (IsValid(IA_Player_DebugCamera))
            {
                // Started는 누르기 시작할 때 한 번만 발생한다.
                EnhancedInputComponent->BindAction(
                    IA_Player_DebugCamera.Get(), ETriggerEvent::Started, this, &APlayerCharacter::ToggleDebugCamera);
            }
            else
            {
                UE_LOG(LogTemp, Warning, TEXT("Debug camera input not bound: IA_Player_DebugCamera is missing on %s."), *GetName());
            }
        }
	}
}

void APlayerCharacter::Move(const FInputActionValue& Value)
{
	const FVector2D MovementVector = Value.Get<FVector2D>();

    // 카메라 B 키 디버깅 관련
    if (bUseTopViewInPIE)
    {
        if (IsValid(DebugTopViewCamera))
        {
            // 수직 탑뷰에서는 카메라 Forward가 아래를 향하므로 화면 위쪽인 Up을 사용한다.
            const FVector ScreenForward = DebugTopViewCamera->GetUpVector().GetSafeNormal2D();
            const FVector ScreenRight = DebugTopViewCamera->GetRightVector().GetSafeNormal2D();
            AddMovementInput(ScreenForward, MovementVector.X);
            AddMovementInput(ScreenRight, MovementVector.Y);
        }
        return;
    }

	AddMovementInput(GetActorForwardVector(), MovementVector.X);
	AddMovementInput(GetActorRightVector(), MovementVector.Y);
}

void APlayerCharacter::Look(const FInputActionValue& Value)
{
    const FVector2D LookVector = Value.Get<FVector2D>();

    // 카메라 B 키 디버깅 관련
    if (bUseTopViewInPIE)
    {
        return;
    }
    // 상대 마우스 입력으로 복귀할 때 남아 있는 첫 델타로 화면이 튀는 것을 방지한다.
    if (bSkipNextLookInput)
    {
        bSkipNextLookInput = false;
        return;
    }

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

// =========================================================================================================================
// 카메라 B 키 디버깅 관련
// =========================================================================================================================

void APlayerCharacter::ToggleDebugCamera()
{
    if (!IsValid(GetWorld()) || GetWorld()->WorldType != EWorldType::PIE || !IsLocallyControlled())
    {
        return;
    }
    if (bUseTopViewInPIE)
    {
        bUseTopViewInPIE = false;
        RestoreDebugControls();
        return;
    }

    APlayerController* PlayerController = Cast<APlayerController>(GetController());
    UCharacterMovementComponent* Movement = GetCharacterMovement();
    if (!IsValid(PlayerController) || !IsValid(Movement) || !IsValid(Camera) ||
        !IsValid(DebugTopViewCamera) || !TopViewExtension.IsValid())
    {
        UE_LOG(LogTemp, Warning, TEXT("Debug camera controls unavailable on %s."), *GetName());
        return;
    }

    DebugInputController = PlayerController;
    SavedControlRotation = PlayerController->GetControlRotation();
    SavedCameraRelativeRotation = Camera->GetRelativeRotation();
    bSavedUseControllerRotationYaw = bUseControllerRotationYaw;
    bSavedOrientRotationToMovement = Movement->bOrientRotationToMovement;
    bSavedUseControllerDesiredRotation = Movement->bUseControllerDesiredRotation;
    bSavedCameraUsePawnControlRotation = Camera->bUsePawnControlRotation;
    bDebugControlsActive = true;

    bUseControllerRotationYaw = true;
    Movement->bOrientRotationToMovement = false;
    Movement->bUseControllerDesiredRotation = false;
    Camera->bUsePawnControlRotation = true;
    PlayerController->SetControlRotation(FRotator(0.0f, SavedControlRotation.Yaw, 0.0f));

    FInputModeGameAndUI DebugInputMode;
    DebugInputMode.SetHideCursorDuringCapture(false);
    DebugInputMode.SetLockMouseToViewportBehavior(EMouseLockMode::LockAlways);
    PlayerController->SetInputMode(DebugInputMode);
    PlayerController->bShowMouseCursor = true;

    int32 SizeX = 0;
    int32 SizeY = 0;
    PlayerController->GetViewportSize(SizeX, SizeY);
    if (SizeX > 0 && SizeY > 0)
    {
        PlayerController->SetMouseLocation(SizeX / 2, SizeY / 2);
    }
    bHasDebugView = false;
    bUseTopViewInPIE = true;
}

void APlayerCharacter::RestoreDebugControls()
{
    bHasDebugView = false;
    if (!bDebugControlsActive)
    {
        return;
    }

    bUseControllerRotationYaw = bSavedUseControllerRotationYaw;
    if (UCharacterMovementComponent* Movement = GetCharacterMovement(); IsValid(Movement))
    {
        Movement->bOrientRotationToMovement = bSavedOrientRotationToMovement;
        Movement->bUseControllerDesiredRotation = bSavedUseControllerDesiredRotation;
    }
    if (IsValid(Camera))
    {
        Camera->bUsePawnControlRotation = bSavedCameraUsePawnControlRotation;
        Camera->SetRelativeRotation(SavedCameraRelativeRotation);
    }
    if (APlayerController* PlayerController = DebugInputController.Get(); IsValid(PlayerController))
    {
        // 조준한 수평 방향은 유지하고 기존 카메라의 상하 각도와 입력 방식을 복원한다.
        PlayerController->SetControlRotation(FRotator(
            SavedControlRotation.Pitch, PlayerController->GetControlRotation().Yaw, SavedControlRotation.Roll));
        PlayerController->bShowMouseCursor = false;
        PlayerController->SetInputMode(FInputModeGameOnly());
    }
    DebugInputController.Reset();
    bDebugControlsActive = false;
    bSkipNextLookInput = true;
}

void APlayerCharacter::UpdateDebugCursorAim()
{
    if (!bUseTopViewInPIE || !bHasDebugView || !IsLocallyControlled())
    {
        return;
    }

    APlayerController* PlayerController = DebugInputController.Get();
    UCapsuleComponent* Capsule = GetCapsuleComponent();
    if (!IsValid(PlayerController) || PlayerController->GetPawn() != this || !IsValid(Capsule))
    {
        return;
    }

    float MouseX = 0.0f;
    float MouseY = 0.0f;
    if (!PlayerController->GetMousePosition(MouseX, MouseY) ||
        DebugViewRect.Width() <= 0 || DebugViewRect.Height() <= 0 ||
        MouseX < DebugViewRect.Min.X || MouseX >= DebugViewRect.Max.X ||
        MouseY < DebugViewRect.Min.Y || MouseY >= DebugViewRect.Max.Y)
    {
        return;
    }

    FVector RayOrigin;
    FVector RayDirection;
    // 실제 표시한 탑뷰 행렬을 사용한다. 기본 카메라의 Deproject 함수는 사용하지 않는다.
    FSceneView::DeprojectScreenToWorld(
        FVector2D(MouseX, MouseY), DebugViewRect, DebugClipToWorld, RayOrigin, RayDirection);
    if (RayOrigin.ContainsNaN() || RayDirection.ContainsNaN() || FMath::Abs(RayDirection.Z) < 0.0001)
    {
        return;
    }

    const double GroundHeight = GetActorLocation().Z - Capsule->GetScaledCapsuleHalfHeight();
    const double RayDistance = (GroundHeight - RayOrigin.Z) / RayDirection.Z;
    if (!FMath::IsFinite(RayDistance) || RayDistance < 0.0)
    {
        return;
    }
    FVector AimDirection = RayOrigin + RayDirection * RayDistance - GetActorLocation();
    AimDirection.Z = 0.0;
    // 커서가 캐릭터 중심에 가까우면 작은 위치 오차로 방향이 뒤집히지 않도록 유지한다.
    if (AimDirection.ContainsNaN() || AimDirection.SizeSquared() < 25.0 * 25.0)
    {
        return;
    }

    const FRotator AimRotation(0.0f, AimDirection.Rotation().Yaw, 0.0f);
    PlayerController->SetControlRotation(AimRotation);
    SetActorRotation(AimRotation);
}