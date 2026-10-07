// Fill out your copyright notice in the Description page of Project Settings.


#include "PlayerCharacter.h"
#include "ProjectProject01TuningData.h"
#include "ProjectProject01LoginWidget.h"
#include "ProjectProject01GameInstance.h"
#include "HelperRearGuardCharacter.h"
#include "MannequinAICharacter.h"
#include "MultiplayTestGameMode.h"

#include "GameFramework/SpringArmComponent.h"
#include "Camera/CameraComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"
#include "Components/CapsuleComponent.h"
#include "DrawDebugHelpers.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"

// 카메라 B 키 디버깅 관련
#include "GameFramework/PlayerController.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "InputAction.h"
#include "InputCoreTypes.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"
#include "SceneView.h"
#include "SceneViewExtension.h"
#include "PlayerHeartbeatComponent.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#endif

namespace
{
	float CalculateNormalizedVisionRate(
		const float MaximumSightHalfAngleDegrees,
		const float MaximumStamina,
		const float CurrentStaminaRate,
		const float VisionRateMultiplier)
	{
		if (MaximumStamina <= KINDA_SMALL_NUMBER)
		{
			return 0.0f;
		}

		return FMath::Max(0.0f, MaximumSightHalfAngleDegrees) *
			(FMath::Max(0.0f, CurrentStaminaRate) / MaximumStamina) *
			FMath::Max(0.0f, VisionRateMultiplier);
	}

	bool TryConsumeFullStaminaUpdate(float& InOutStamina, const float DrainPerSecond, const float DeltaTime)
	{
		const float RequiredStamina = FMath::Max(0.0f, DrainPerSecond) * FMath::Max(0.0f, DeltaTime);
		if (InOutStamina + KINDA_SMALL_NUMBER < RequiredStamina)
		{
			return false;
		}
		InOutStamina = FMath::Max(0.0f, InOutStamina - RequiredStamina);
		return true;
	}

	float ConsumeStaminaAndGetDepletedTime(
		float& InOutStamina,
		const float DrainPerSecond,
		const float DeltaTime)
	{
		const float SafeDeltaTime = FMath::Max(0.0f, DeltaTime);
		if (DrainPerSecond <= 0.0f)
		{
			return InOutStamina <= KINDA_SMALL_NUMBER ? SafeDeltaTime : 0.0f;
		}

		const float TimeCoveredByStamina = FMath::Min(SafeDeltaTime, InOutStamina / DrainPerSecond);
		InOutStamina = FMath::Max(0.0f, InOutStamina - (DrainPerSecond * TimeCoveredByStamina));
		return SafeDeltaTime - TimeCoveredByStamina;
	}

	float GetRemainingTimeAfterVisionRecovery(
		const float VisionDeficit,
		const float VisionRecoveryPerSecond,
		const float DeltaTime)
	{
		const float SafeDeltaTime = FMath::Max(0.0f, DeltaTime);
		if (VisionDeficit <= KINDA_SMALL_NUMBER)
		{
			return SafeDeltaTime;
		}
		if (VisionRecoveryPerSecond <= 0.0f)
		{
			return 0.0f;
		}
		return FMath::Max(0.0f, SafeDeltaTime - (VisionDeficit / VisionRecoveryPerSecond));
	}
}

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectProject01StaminaRulesTest,
	"ProjectProject01.Player.StaminaRules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectProject01StaminaRulesTest::RunTest(const FString& Parameters)
{
	float MultiplayerRemainder = 0.04f;
	TestFalse(TEXT("Multiplayer rejects an update it cannot fully afford"),
		TryConsumeFullStaminaUpdate(MultiplayerRemainder, 5.0f, 0.01f));
	TestTrue(TEXT("Rejected multiplayer update preserves the remainder"),
		FMath::IsNearlyEqual(MultiplayerRemainder, 0.04f));

	float SingleStamina = 1.0f;
	const float DepletedTime = ConsumeStaminaAndGetDepletedTime(SingleStamina, 5.0f, 1.0f);
	TestTrue(TEXT("Single-player reaches zero stamina"), FMath::IsNearlyZero(SingleStamina));
	TestTrue(TEXT("Only time after depletion affects helper vision"), FMath::IsNearlyEqual(DepletedTime, 0.8f));

	const float RemainingRecoveryTime = GetRemainingTimeAfterVisionRecovery(0.75f, 1.5f, 1.0f);
	TestTrue(TEXT("Stamina receives only the time left after helper vision recovery"),
		FMath::IsNearlyEqual(RemainingRecoveryTime, 0.5f));
	TestTrue(TEXT("A full stamina drain per second drains a full helper half-angle per second"),
		FMath::IsNearlyEqual(CalculateNormalizedVisionRate(60.0f, 100.0f, 100.0f, 1.0f), 60.0f));
	TestTrue(TEXT("Default stamina drain maps proportionally to the helper half-angle"),
		FMath::IsNearlyEqual(CalculateNormalizedVisionRate(60.0f, 100.0f, 5.0f, 1.0f), 3.0f));
	TestTrue(TEXT("Default stamina recovery maps proportionally to the helper half-angle"),
		FMath::IsNearlyEqual(CalculateNormalizedVisionRate(60.0f, 100.0f, 3.0f, 1.0f), 1.8f));
	TestTrue(TEXT("Vision rate multiplier scales the normalized result"),
		FMath::IsNearlyEqual(CalculateNormalizedVisionRate(60.0f, 100.0f, 5.0f, 2.0f), 6.0f));
	TestTrue(TEXT("Zero maximum stamina is handled without division"),
		FMath::IsNearlyZero(CalculateNormalizedVisionRate(60.0f, 0.0f, 5.0f, 1.0f)));
	return true;
}
#endif

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
	
	// 플레이어 심박수 관련 컴포넌트 연결
	HeartbeatComponent = CreateDefaultSubobject<UPlayerHeartbeatComponent>(TEXT("Heartbeat Component"));
	HeartbeatSFXComponent = CreateDefaultSubobject<UHeartbeatSynthComponent>(TEXT("Heartbeat SFX Component"));
	HeartbeatSFXComponent->SetupAttachment(RootComponent);

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

	if (UWorld* World = GetWorld(); IsValid(World) && World->GetNetMode() != NM_Client)
	{
		if (UProjectProject01TuningSubsystem* TuningSubsystem = World->GetSubsystem<UProjectProject01TuningSubsystem>())
		{
			FPlayerTuningRow PlayerTuning;
			if (TuningSubsystem->GetPlayerTuning(PlayerTuning))
			{
				ApplyPlayerTuning(PlayerTuning);
			}

			FMannequinAITuningRow MannequinTuning;
			if (TuningSubsystem->GetMannequinTuning(MannequinTuning))
			{
				ApplyMannequinTuning(MannequinTuning);
			}
		}
	}
	
	InitializeLocalPlayerInput();

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

void APlayerCharacter::PawnClientRestart()
{
	Super::PawnClientRestart();

	// 네트워크 클라이언트는 BeginPlay보다 소유 Controller 복제가 늦을 수 있다.
	// APawn::PawnClientRestart가 로컬 입력 컴포넌트를 구성한 직후 매핑을 다시 보장한다.
	InitializeLocalPlayerInput();
}

void APlayerCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);

	UWorld* World = GetWorld();
	if (!HasAuthority() || !IsValid(World))
	{
		return;
	}

	UProjectProject01TuningSubsystem* TuningSubsystem = World->GetSubsystem<UProjectProject01TuningSubsystem>();
	FPlayerTuningRow PlayerTuning;
	if (IsValid(TuningSubsystem) && TuningSubsystem->GetPlayerTuning(PlayerTuning))
	{
		// PossessedBy 이후에는 소유 클라이언트 RPC가 올바른 연결로 전달된다.
		ApplyPlayerTuning(PlayerTuning);
	}

	FMannequinAITuningRow MannequinTuning;
	if (IsValid(TuningSubsystem) && TuningSubsystem->GetMannequinTuning(MannequinTuning))
	{
		ApplyMannequinTuning(MannequinTuning);
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
	
	const UCharacterMovementComponent* MovementComponent = GetCharacterMovement();
	if (!IsValid(MovementComponent))
	{
		return;
	}
	
	// 플레이어의 방향키 입력을 기준으로 디버그 원 방향 조절 가능하도록 설정하기
	const FVector InputMovementDirection = MovementComponent->GetCurrentAcceleration().GetSafeNormal2D();
	if (!InputMovementDirection.IsNearlyZero())
	{
		LastAIMovementDirection = InputMovementDirection;
	}

	UpdateStamina(DeltaTime);
	DrawStaminaDebug();

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
	if (!ensureMsgf(IsValid(PlayerInputComponent), TEXT("Player %s has no InputComponent for sprint binding."), *GetName()))
	{
		return;
	}

	UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent);

	if (EnhancedInputComponent != nullptr)
	{
		EnhancedInputComponent->BindAction(IA_Move, ETriggerEvent::Triggered, this, &APlayerCharacter::Move);
		//EnhancedInputComponent->BindAction(IA_Move, ETriggerEvent::Completed, this, &APlayerCharacter::Move);
		EnhancedInputComponent->BindAction(IA_Look, ETriggerEvent::Triggered, this, &APlayerCharacter::Look);
		//EnhancedInputComponent->BindAction(IA_Look, ETriggerEvent::Completed, this, &APlayerCharacter::Look);
		EnhancedInputComponent->BindAction(IA_Sprint, ETriggerEvent::Started, this, &APlayerCharacter::StartSprint);
		EnhancedInputComponent->BindAction(IA_Sprint, ETriggerEvent::Completed, this, &APlayerCharacter::StopSprint);
		EnhancedInputComponent->BindAction(IA_Sprint, ETriggerEvent::Canceled, this, &APlayerCharacter::StopSprint);
		if (GetNetMode() == NM_Standalone)
		{
			PlayerInputComponent->BindKey(EKeys::F1, IE_Pressed, this, &APlayerCharacter::ToggleSessionMenu);
		}

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

void APlayerCharacter::ToggleSessionMenu()
{
	if (GetNetMode() != NM_Standalone)
	{
		return;
	}

	APlayerController* PlayerController = Cast<APlayerController>(GetController());
	if (!IsValid(PlayerController) || !PlayerController->IsLocalController())
	{
		return;
	}
	if (IsValid(SessionMenuWidget) && SessionMenuWidget->IsInViewport())
	{
		SessionMenuWidget->CloseMenu();
		return;
	}

	SessionMenuWidget = CreateWidget<UProjectProject01SessionMenuWidget>(
		PlayerController, UProjectProject01SessionMenuWidget::StaticClass());
	if (!ensureMsgf(IsValid(SessionMenuWidget), TEXT("Player %s could not create the session menu."), *GetName()))
	{
		return;
	}
	SessionMenuWidget->ConfigureForSession(false);
	SessionMenuWidget->AddToViewport(500);
	UGameplayStatics::SetGamePaused(this, true);

	PlayerController->bShowMouseCursor = true;
	FInputModeGameAndUI InputMode;
	InputMode.SetWidgetToFocus(SessionMenuWidget->TakeWidget());
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	InputMode.SetHideCursorDuringCapture(false);
	PlayerController->SetInputMode(InputMode);
}

void APlayerCharacter::InitializeLocalPlayerInput()
{
	APlayerController* PlayerController = Cast<APlayerController>(GetController());
	if (!IsValid(PlayerController) || !PlayerController->IsLocalController())
	{
		return;
	}

	// Front-end maps use UI-only input. Explicitly restore gameplay focus whenever this
	// locally controlled gameplay pawn starts or is restarted after level travel.
	PlayerController->bShowMouseCursor = false;
	PlayerController->SetInputMode(FInputModeGameOnly());

	ULocalPlayer* LocalPlayer = PlayerController->GetLocalPlayer();
	UEnhancedInputLocalPlayerSubsystem* InputSubsystem = IsValid(LocalPlayer)
		? ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(LocalPlayer)
		: nullptr;
	if (!ensureMsgf(IsValid(InputSubsystem),
		TEXT("Player %s cannot initialize local input because its Enhanced Input subsystem is unavailable."),
		*GetName()))
	{
		return;
	}

	RegisterRuntimeSprintMapping(InputSubsystem);
}

void APlayerCharacter::ApplyUserInputSettings()
{
	InitializeLocalPlayerInput();
}

void APlayerCharacter::Move(const FInputActionValue& Value)
{
	if (bGameOver)
	{
		return;
	}

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
	if (bGameOver)
	{
		return;
	}

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

	const UProjectProject01GameUserSettings* Settings = UProjectProject01GameUserSettings::Get();
	const float Sensitivity = IsValid(Settings) ? Settings->GetMouseSensitivity() : 1.0f;
	AddControllerYawInput(LookVector.X * Sensitivity);
	AddControllerPitchInput(LookVector.Y * Sensitivity);
}

void APlayerCharacter::StartSprint()
{
	SetSprinting(true);
}

void APlayerCharacter::StopSprint()
{
	SetSprinting(false);
}

void APlayerCharacter::SetSprinting(const bool bNewSprinting)
{
	UWorld* World = GetWorld();
	const bool bIsStandalone = IsValid(World) && World->GetNetMode() == NM_Standalone;
	const bool bHasMultiplayerStamina = !bIsStandalone &&
		(StaminaDrainPerSecond <= KINDA_SMALL_NUMBER || CurrentStamina > KINDA_SMALL_NUMBER);
	const bool bAllowedSprinting = bNewSprinting && !bGameOver && (bIsStandalone || bHasMultiplayerStamina);
	ApplySprintingState(bAllowedSprinting);

	if (!HasAuthority())
	{
		ServerSetSprinting(bNewSprinting);
	}
}

void APlayerCharacter::ServerSetSprinting_Implementation(const bool bNewSprinting)
{
	const UWorld* World = GetWorld();
	if (!IsValid(World))
	{
		return;
	}
	const float Now = World->GetTimeSeconds();
	if (Now - SprintRpcWindowStartSeconds >= 1.0f)
	{
		SprintRpcWindowStartSeconds = Now;
		SprintRpcWindowCallCount = 0;
	}
	if (++SprintRpcWindowCallCount > 20)
	{
		UE_LOG(LogProjectProject01Tuning, Warning,
			TEXT("Security rejected excessive sprint RPCs from %s."), *GetNameSafe(this));
		ClientCorrectSprinting(bIsSprinting);
		return;
	}
	const bool bIsStandalone = IsValid(World) && World->GetNetMode() == NM_Standalone;
	const bool bAllowedSprinting = bNewSprinting && !bGameOver &&
		(bIsStandalone || StaminaDrainPerSecond <= KINDA_SMALL_NUMBER || CurrentStamina > KINDA_SMALL_NUMBER);
	ApplySprintingState(bAllowedSprinting);
	ClientCorrectSprinting(bAllowedSprinting);
}

bool APlayerCharacter::ServerSetSprinting_Validate(const bool bNewSprinting)
{
	return bNewSprinting == true || bNewSprinting == false;
}

void APlayerCharacter::ClientCorrectSprinting_Implementation(const bool bAuthoritativeSprinting)
{
	ApplySprintingState(bAuthoritativeSprinting);
}

void APlayerCharacter::ApplySprintingState(const bool bNewSprinting)
{
	if (bIsSprinting == bNewSprinting)
	{
		return;
	}

	bIsSprinting = bNewSprinting;
	ApplyPlayerWalkSpeed();
	UE_LOG(LogProjectProject01Tuning, Log, TEXT("Player %s sprint=%s; speed=%.1f cm/s."),
		*GetName(), bIsSprinting ? TEXT("true") : TEXT("false"), bIsSprinting ? PlayerSprintSpeed : PlayerWalkSpeed);

	if (HasAuthority())
	{
		ForceNetUpdate();
	}
}

void APlayerCharacter::RegisterRuntimeSprintMapping(UEnhancedInputLocalPlayerSubsystem* InputSubsystem)
{
	if (!ensureMsgf(IsValid(InputSubsystem) && IsValid(IMC_PlayerInput) && IsValid(IA_Move) && IsValid(IA_Sprint),
		TEXT("Player %s cannot register user input mappings because an input asset is missing."), *GetName()))
	{
		return;
	}

	if (InputSubsystem->HasMappingContext(IMC_PlayerInput))
	{
		InputSubsystem->RemoveMappingContext(IMC_PlayerInput);
	}
	if (IsValid(RuntimeSprintInputContext) && InputSubsystem->HasMappingContext(RuntimeSprintInputContext))
	{
		InputSubsystem->RemoveMappingContext(RuntimeSprintInputContext);
	}

	RuntimeSprintInputContext = NewObject<UInputMappingContext>(this);
	if (!ensureMsgf(IsValid(RuntimeSprintInputContext),
		TEXT("Player %s could not create its runtime input mapping context."), *GetName()))
	{
		return;
	}

	const UProjectProject01GameUserSettings* Settings = UProjectProject01GameUserSettings::Get();
	const FKey ForwardKey = IsValid(Settings) ? Settings->GetMoveForwardKey() : EKeys::W;
	const FKey BackwardKey = IsValid(Settings) ? Settings->GetMoveBackwardKey() : EKeys::S;
	const FKey LeftKey = IsValid(Settings) ? Settings->GetMoveLeftKey() : EKeys::A;
	const FKey RightKey = IsValid(Settings) ? Settings->GetMoveRightKey() : EKeys::D;
	const FKey SprintKey = IsValid(Settings) ? Settings->GetSprintKey() : EKeys::LeftShift;

	for (const FEnhancedActionKeyMapping& SourceMapping : IMC_PlayerInput->GetMappings())
	{
		if (!IsValid(SourceMapping.Action) || SourceMapping.Action == IA_Sprint)
		{
			continue;
		}

		FKey MappedKey = SourceMapping.Key;
		if (SourceMapping.Action == IA_Move)
		{
			if (SourceMapping.Key == EKeys::W) MappedKey = ForwardKey;
			else if (SourceMapping.Key == EKeys::S) MappedKey = BackwardKey;
			else if (SourceMapping.Key == EKeys::A) MappedKey = LeftKey;
			else if (SourceMapping.Key == EKeys::D) MappedKey = RightKey;
		}

		FEnhancedActionKeyMapping& RuntimeMapping = RuntimeSprintInputContext->MapKey(SourceMapping.Action, MappedKey);
		RuntimeMapping.Modifiers = SourceMapping.Modifiers;
		RuntimeMapping.Triggers = SourceMapping.Triggers;
	}
	RuntimeSprintInputContext->MapKey(IA_Sprint, SprintKey);
	InputSubsystem->AddMappingContext(RuntimeSprintInputContext, 0);
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
	return FMath::Max(0.0f, RoamingOuterRadius);
}

float APlayerCharacter::GetDirectChaseHalfAngleDegrees() const
{
	return FMath::Clamp(DirectChaseHalfAngleDegrees, 0.0f, 180.0f);
}

float APlayerCharacter::GetDirectChaseSectorRadius() const
{
	const float InnerRadius = GetDirectChaseRadius();
	const float OuterRadius = FMath::Max(InnerRadius, GetRoamingOuterRadius());

	return FMath::Clamp(
		DirectChaseSectorRadius,
		InnerRadius,
		OuterRadius);
}

FVector APlayerCharacter::GetAIMovementDirection() const
{
	if (!LastAIMovementDirection.IsNearlyZero())
	{
		return LastAIMovementDirection;
	}
	
	return GetActorForwardVector().GetSafeNormal2D();
}

void APlayerCharacter::ApplyMannequinTuning(const FMannequinAITuningRow& Tuning)
{
	DirectChaseRadius = FMath::Max(0.0f, Tuning.DirectChaseRadius);
	RoamingOuterRadius = FMath::Max(0.0f, Tuning.RoamingOuterRadius);
	DirectChaseHalfAngleDegrees = FMath::Max(0.0f, Tuning.DirectChaseHalfAngleDegrees);
	DirectChaseSectorRadius = FMath::Max(0.0f, Tuning.DirectChaseSectorRadius);

	if (HasAuthority())
	{
		ForceNetUpdate();
	}
}

AHelperRearGuardCharacter* APlayerCharacter::ResolveStaminaHelper()
{
	if (StaminaHelper.IsValid() && StaminaHelper->IsGuardingPlayer(this))
	{
		return StaminaHelper.Get();
	}

	StaminaHelper.Reset();
	UWorld* World = GetWorld();
	if (!IsValid(World))
	{
		return nullptr;
	}

	for (TActorIterator<AHelperRearGuardCharacter> It(World); It; ++It)
	{
		AHelperRearGuardCharacter* Helper = *It;
		if (IsValid(Helper) && Helper->IsGuardingPlayer(this))
		{
			StaminaHelper = Helper;
			return Helper;
		}
	}
	return nullptr;
}

void APlayerCharacter::UpdateStamina(const float DeltaTime)
{
	UWorld* World = GetWorld();
	const UCharacterMovementComponent* Movement = GetCharacterMovement();
	if (!HasAuthority() || !IsValid(World) || !IsValid(Movement) || DeltaTime <= 0.0f)
	{
		return;
	}

	CurrentStamina = FMath::Clamp(CurrentStamina, 0.0f, MaxStamina);
	const bool bHasMovementIntent = !Movement->GetCurrentAcceleration().GetSafeNormal2D().IsNearlyZero();
	const bool bActivelySprinting = bIsSprinting && bHasMovementIntent;
	const bool bIsStandalone = World->GetNetMode() == NM_Standalone;

	if (!bIsStandalone)
	{
		if (bActivelySprinting)
		{
			if (!TryConsumeFullStaminaUpdate(CurrentStamina, StaminaDrainPerSecond, DeltaTime))
			{
				// 현재 업데이트 전체 소모량을 낼 수 없으면 잔량을 부분 소모하지 않고 즉시 걷기로 돌아간다.
				ApplySprintingState(false);
				ClientCorrectSprinting(false);
			}
		}
		else
		{
			CurrentStamina = FMath::Min(MaxStamina, CurrentStamina + (StaminaRecoveryPerSecond * DeltaTime));
		}
		return;
	}

	AHelperRearGuardCharacter* Helper = ResolveStaminaHelper();
	if (bActivelySprinting)
	{
		const float TimeAtZeroStamina = ConsumeStaminaAndGetDepletedTime(
			CurrentStamina, StaminaDrainPerSecond, DeltaTime);

		if (IsValid(Helper) && TimeAtZeroStamina > 0.0f)
		{
			const float ProportionalVisionDrainRate = CalculateNormalizedVisionRate(
				Helper->GetConfiguredGuardSightHalfAngleDegrees(),
				MaxStamina,
				StaminaDrainPerSecond,
				Helper->GetStaminaVisionDrainMultiplier());
			Helper->ReduceCurrentGuardSightHalfAngle(ProportionalVisionDrainRate * TimeAtZeroStamina);
		}
		return;
	}

	float RemainingRecoveryTime = DeltaTime;
	if (IsValid(Helper))
	{
		const float VisionDeficit = FMath::Max(
			0.0f,
			Helper->GetConfiguredGuardSightHalfAngleDegrees() - Helper->GetCurrentGuardSightHalfAngleDegrees());
		if (VisionDeficit > KINDA_SMALL_NUMBER)
		{
			const float VisionRecoveryRate = CalculateNormalizedVisionRate(
				Helper->GetConfiguredGuardSightHalfAngleDegrees(),
				MaxStamina,
				StaminaRecoveryPerSecond,
				Helper->GetStaminaVisionRecoveryMultiplier());
			if (VisionRecoveryRate <= 0.0f)
			{
				return;
			}

			const float TimeAfterVisionRecovery = GetRemainingTimeAfterVisionRecovery(
				VisionDeficit, VisionRecoveryRate, RemainingRecoveryTime);
			const float VisionRecoveryTime = RemainingRecoveryTime - TimeAfterVisionRecovery;
			Helper->RestoreCurrentGuardSightHalfAngle(VisionRecoveryRate * VisionRecoveryTime);
			RemainingRecoveryTime = TimeAfterVisionRecovery;
		}
	}

	if (RemainingRecoveryTime > 0.0f)
	{
		CurrentStamina = FMath::Min(MaxStamina, CurrentStamina + (StaminaRecoveryPerSecond * RemainingRecoveryTime));
	}
}

void APlayerCharacter::DrawStaminaDebug() const
{
#if ENABLE_DRAW_DEBUG
	UWorld* World = GetWorld();
	if (!bShowStaminaDebug || !IsLocallyControlled() || !IsValid(World) || World->WorldType != EWorldType::PIE)
	{
		return;
	}

	FString HelperVisionText(TEXT("N/A"));
	if (const AHelperRearGuardCharacter* Helper = StaminaHelper.Get(); IsValid(Helper))
	{
		HelperVisionText = FString::Printf(TEXT("%.1f/%.1f deg"),
			Helper->GetCurrentGuardSightHalfAngleDegrees(), Helper->GetConfiguredGuardSightHalfAngleDegrees());
	}
	const FString DebugText = FString::Printf(TEXT("Stamina %.1f/%.1f | Sprint %s | Helper half-angle %s"),
		CurrentStamina, MaxStamina, bIsSprinting ? TEXT("ON") : TEXT("OFF"), *HelperVisionText);
	DrawDebugString(World, GetActorLocation() + FVector(0.0f, 0.0f, 120.0f), DebugText, nullptr, FColor::Cyan, 0.0f, false);
#endif
}

void APlayerCharacter::ApplyPlayerTuning(const FPlayerTuningRow& Tuning)
{
	PlayerWalkSpeed = FMath::Max(0.0f, Tuning.PlayerWalkSpeed);
	PlayerSprintSpeed = FMath::Max(0.0f, Tuning.PlayerSprintSpeed);
	MaxStamina = FMath::Max(0.0f, Tuning.MaxStamina);
	StaminaDrainPerSecond = FMath::Max(0.0f, Tuning.StaminaDrainPerSecond);
	StaminaRecoveryPerSecond = FMath::Max(0.0f, Tuning.StaminaRecoveryPerSecond);
	CurrentStamina = bStaminaInitialized ? FMath::Clamp(CurrentStamina, 0.0f, MaxStamina) : MaxStamina;
	bStaminaInitialized = true;
	ApplyPlayerWalkSpeed();

	if (ensureMsgf(IsValid(HeartbeatComponent), TEXT("Player %s has no HeartbeatComponent."), *GetName()))
	{
		HeartbeatComponent->ApplyHeartbeatTuning(Tuning);
	}

	if (HasAuthority())
	{
		ForceNetUpdate();
		if (!IsLocallyControlled())
		{
			ClientApplyPlayerTuning(Tuning);
		}
	}
}

void APlayerCharacter::GetDiagnosticAppliedPlayerTuning(FPlayerTuningRow& OutTuning) const
{
	// 달리는 중 MaxWalkSpeed는 PlayerSprintSpeed이므로, 현재 이동 컴포넌트 값 대신 실제 적용 설정을 비교한다.
	OutTuning.PlayerWalkSpeed = PlayerWalkSpeed;
	OutTuning.PlayerSprintSpeed = PlayerSprintSpeed;
	OutTuning.MaxStamina = MaxStamina;
	OutTuning.StaminaDrainPerSecond = StaminaDrainPerSecond;
	OutTuning.StaminaRecoveryPerSecond = StaminaRecoveryPerSecond;
	if (IsValid(HeartbeatComponent)) HeartbeatComponent->GetDiagnosticAppliedTuning(OutTuning);
}

void APlayerCharacter::ClientApplyPlayerTuning_Implementation(const FPlayerTuningRow& Tuning)
{
	PlayerWalkSpeed = FMath::Max(0.0f, Tuning.PlayerWalkSpeed);
	PlayerSprintSpeed = FMath::Max(0.0f, Tuning.PlayerSprintSpeed);
	MaxStamina = FMath::Max(0.0f, Tuning.MaxStamina);
	StaminaDrainPerSecond = FMath::Max(0.0f, Tuning.StaminaDrainPerSecond);
	StaminaRecoveryPerSecond = FMath::Max(0.0f, Tuning.StaminaRecoveryPerSecond);
	CurrentStamina = bStaminaInitialized ? FMath::Clamp(CurrentStamina, 0.0f, MaxStamina) : MaxStamina;
	bStaminaInitialized = true;
	ApplyPlayerWalkSpeed();

	if (!ensureMsgf(IsValid(HeartbeatComponent), TEXT("Player %s has no HeartbeatComponent for client tuning."), *GetName()))
	{
		return;
	}

	HeartbeatComponent->ApplyHeartbeatTuning(Tuning);
}

void APlayerCharacter::ApplyPlayerWalkSpeed()
{
	UCharacterMovementComponent* Movement = GetCharacterMovement();
	if (!ensureMsgf(IsValid(Movement), TEXT("Player %s has no CharacterMovementComponent."), *GetName()))
	{
		return;
	}

	Movement->MaxWalkSpeed = bIsSprinting ? PlayerSprintSpeed : PlayerWalkSpeed;
}

void APlayerCharacter::OnRep_PlayerWalkSpeed()
{
	ApplyPlayerWalkSpeed();
}

void APlayerCharacter::OnRep_PlayerSprintSpeed()
{
	ApplyPlayerWalkSpeed();
}

void APlayerCharacter::OnRep_IsSprinting()
{
	ApplyPlayerWalkSpeed();
}

bool APlayerCharacter::HandleMannequinCatch(AMannequinAICharacter* CatchingMannequin)
{
	if (!HasAuthority() || !IsValid(CatchingMannequin) || bGameOver)
	{
		return false;
	}

	if (RemainingDeathCount > 0)
	{
		--RemainingDeathCount;
		UE_LOG(LogTemp, Log, TEXT("Player %s was caught by mannequin %s. Remaining death count: %d."),
			*GetName(), *CatchingMannequin->GetName(), RemainingDeathCount);

		if (RemainingDeathCount == 0)
		{
			RemoveGuardingHelpers();
		}

		ForceNetUpdate();
		return true;
	}

	bGameOver = true;
	ApplyGameOverState();
	ForceNetUpdate();
	if (UWorld* World = GetWorld(); IsValid(World))
	{
		if (AMultiplayTestGameMode* MultiplayGameMode = World->GetAuthGameMode<AMultiplayTestGameMode>();
			IsValid(MultiplayGameMode))
		{
			MultiplayGameMode->NotifyExistingMannequinCatch(this);
		}
	}
	UE_LOG(LogTemp, Warning, TEXT("Player %s reached game over after being caught by mannequin %s."),
		*GetName(), *CatchingMannequin->GetName());
	return true;
}

void APlayerCharacter::SetRemainingDeathCountForGameMode(int32 NewDeathCount)
{
	if (!HasAuthority())
	{
		return;
	}

	RemainingDeathCount = FMath::Max(0, NewDeathCount);
	ForceNetUpdate();
}

void APlayerCharacter::PresentSinglePlayerResult(const bool bEscaped)
{
	UWorld* World = GetWorld();
	if (bSinglePlayerResultPresented || !IsValid(World) || World->GetNetMode() != NM_Standalone ||
		!IsLocallyControlled())
	{
		return;
	}

	bSinglePlayerResultPresented = true;
	ApplySprintingState(false);
	if (UCharacterMovementComponent* Movement = GetCharacterMovement(); IsValid(Movement))
	{
		Movement->StopMovementImmediately();
		Movement->DisableMovement();
	}

	APlayerController* PlayerController = Cast<APlayerController>(GetController());
	if (!ensureMsgf(IsValid(PlayerController), TEXT("Single-player result requires a local player controller.")))
	{
		return;
	}
	PlayerController->SetIgnoreMoveInput(true);
	PlayerController->SetIgnoreLookInput(true);
	if (IsValid(SessionMenuWidget))
	{
		SessionMenuWidget->RemoveFromParent();
		SessionMenuWidget = nullptr;
	}
	if (IsValid(SinglePlayerResultWidget))
	{
		SinglePlayerResultWidget->RemoveFromParent();
	}

	SinglePlayerResultWidget = CreateWidget<UProjectProject01MatchResultWidget>(
		PlayerController, UProjectProject01MatchResultWidget::StaticClass());
	if (!ensureMsgf(IsValid(SinglePlayerResultWidget), TEXT("Unable to create the single-player result widget.")))
	{
		return;
	}
	SinglePlayerResultWidget->ConfigureSinglePlayerResult(bEscaped);
	SinglePlayerResultWidget->AddToViewport(700);
	PlayerController->bShowMouseCursor = true;
	FInputModeGameAndUI InputMode;
	InputMode.SetWidgetToFocus(SinglePlayerResultWidget->TakeWidget());
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	InputMode.SetHideCursorDuringCapture(false);
	PlayerController->SetInputMode(InputMode);
}

bool APlayerCharacter::HandlePartnerPushDeath(AHelperRearGuardCharacter* PushingHelper)
{
	if (!HasAuthority() || !IsValid(PushingHelper) || bGameOver || RemainingDeathCount <= 0)
	{
		return false;
	}

	--RemainingDeathCount;
	UE_LOG(LogTemp, Warning, TEXT("Player %s lost one death count after being pushed by helper %s. Remaining death count: %d."),
		*GetName(), *PushingHelper->GetName(), RemainingDeathCount);
	if (RemainingDeathCount == 0)
	{
		RemoveGuardingHelpers();
	}

	ForceNetUpdate();
	return true;
}

void APlayerCharacter::RemoveGuardingHelpers()
{
	UWorld* World = GetWorld();
	if (!IsValid(World))
	{
		ensureMsgf(false, TEXT("Player %s could not remove its partner because its world is unavailable."), *GetName());
		return;
	}

	TArray<AActor*> HelperActors;
	UGameplayStatics::GetAllActorsOfClass(World, AHelperRearGuardCharacter::StaticClass(), HelperActors);
	for (AActor* HelperActor : HelperActors)
	{
		if (AHelperRearGuardCharacter* Helper = Cast<AHelperRearGuardCharacter>(HelperActor);
			IsValid(Helper) && Helper->IsGuardingPlayer(this))
		{
			Helper->Destroy();
		}
	}
}

void APlayerCharacter::ApplyGameOverState()
{
	ApplySprintingState(false);
	if (UCharacterMovementComponent* Movement = GetCharacterMovement(); IsValid(Movement))
	{
		Movement->StopMovementImmediately();
		Movement->DisableMovement();
	}
	else
	{
		ensureMsgf(false, TEXT("Player %s has no CharacterMovementComponent for game over."), *GetName());
	}

	PresentSinglePlayerResult(false);
}

void APlayerCharacter::OnRep_GameOver()
{
	if (bGameOver)
	{
		ApplyGameOverState();
	}
}

void APlayerCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(APlayerCharacter, DirectChaseRadius);
	DOREPLIFETIME(APlayerCharacter, RoamingOuterRadius);
	DOREPLIFETIME(APlayerCharacter, PlayerWalkSpeed);
	DOREPLIFETIME(APlayerCharacter, PlayerSprintSpeed);
	DOREPLIFETIME(APlayerCharacter, bIsSprinting);
	DOREPLIFETIME_CONDITION(APlayerCharacter, CurrentStamina, COND_OwnerOnly);
	DOREPLIFETIME(APlayerCharacter, DirectChaseHalfAngleDegrees);
	DOREPLIFETIME(APlayerCharacter, DirectChaseSectorRadius);
	DOREPLIFETIME(APlayerCharacter, RemainingDeathCount);
	DOREPLIFETIME(APlayerCharacter, bGameOver);
}

void APlayerCharacter::DrawAIRangeDebug()
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
	constexpr float LineThickness = 7.5f;
	const FVector CircleAxisX = FVector::ForwardVector;
	const FVector CircleAxisY = FVector::RightVector;
	const FColor OuterRangeColor = FColor::Blue;
	const FColor SectorArcColor = FColor::Green;

	DrawDebugCircle(
		GetWorld(), CircleCenter, GetDirectChaseRadius(), CircleSegments, FColor::Red,
		false, 0.0f, 0, LineThickness, CircleAxisX, CircleAxisY, false);
	DrawDebugCircle(
		GetWorld(), CircleCenter, GetRoamingOuterRadius(), CircleSegments, OuterRangeColor,
		false, 0.0f, 0, LineThickness, CircleAxisX, CircleAxisY, false);
	
	const FVector MovementDirection = GetAIMovementDirection();
	const float HalfAngleDegrees = GetDirectChaseHalfAngleDegrees();
	const float InnerRadius = GetDirectChaseRadius();
	const float SectorRadius = GetDirectChaseSectorRadius();
	
	const FVector LeftBoundaryDirection = MovementDirection.RotateAngleAxis(-HalfAngleDegrees, FVector::UpVector).GetSafeNormal2D();
	const FVector RightBoundaryDirection = MovementDirection.RotateAngleAxis(HalfAngleDegrees, FVector::UpVector).GetSafeNormal2D();

	// 내부원 경계부터 설정한 부채꼴 반경까지만 좌우 경계선을 그린다.
	DrawDebugLine(
		GetWorld(), CircleCenter + LeftBoundaryDirection * InnerRadius,
		CircleCenter + LeftBoundaryDirection * SectorRadius,
		SectorArcColor, false, 0.0f, 0, LineThickness);
	DrawDebugLine(
		GetWorld(), CircleCenter + RightBoundaryDirection * InnerRadius,
		CircleCenter + RightBoundaryDirection * SectorRadius,
		SectorArcColor, false, 0.0f, 0, LineThickness);

	// 부채꼴의 끝 반경을 확인할 수 있도록 바깥쪽 원호를 그린다.
	constexpr int32 SectorArcSegments = 24;
	FVector PreviousArcPoint = CircleCenter + LeftBoundaryDirection * SectorRadius;
	for (int32 SegmentIndex = 1; SegmentIndex <= SectorArcSegments; ++SegmentIndex)
	{
		const float Alpha = static_cast<float>(SegmentIndex) / static_cast<float>(SectorArcSegments);
		const float AngleDegrees = FMath::Lerp(-HalfAngleDegrees, HalfAngleDegrees, Alpha);
		const FVector ArcDirection = MovementDirection.RotateAngleAxis(
			AngleDegrees, FVector::UpVector).GetSafeNormal2D();
		const FVector CurrentArcPoint = CircleCenter + ArcDirection * SectorRadius;

		DrawDebugLine(
			GetWorld(), PreviousArcPoint, CurrentArcPoint,
			SectorArcColor, false, 0.0f, 0, LineThickness);

		PreviousArcPoint = CurrentArcPoint;
	}

	// 심박 컴포넌트의 실제 발견 거리와 각도를 노란색 부채꼴로 표시한다.
	if (IsValid(HeartbeatComponent))
	{
		AController* PlayerController = GetController();
		if (!IsValid(PlayerController))
		{
			return;
		}

		FVector ViewLocation;
		FRotator ViewRotation;
		PlayerController->GetPlayerViewPoint(ViewLocation, ViewRotation);
		FVector HeartbeatDirection = ViewRotation.Vector();
		HeartbeatDirection.Z = 0.0f;
		HeartbeatDirection.Normalize();
		if (HeartbeatDirection.IsNearlyZero())
		{
			return;
		}

		const FColor HeartbeatColor = FColor::Yellow;

		const auto DrawHeartbeatViewSector = [this, &CircleCenter, &HeartbeatDirection, HeartbeatColor](
			const float Radius, const float HalfAngleDegrees, const float Thickness)
		{
			if (Radius <= 0.0f)
			{
				return;
			}
			const FVector LeftDirection = HeartbeatDirection.RotateAngleAxis(-HalfAngleDegrees, FVector::UpVector);
			const FVector RightDirection = HeartbeatDirection.RotateAngleAxis(HalfAngleDegrees, FVector::UpVector);

			DrawDebugLine(GetWorld(), CircleCenter, CircleCenter + LeftDirection * Radius,
				HeartbeatColor, false, 0.0f, 0, Thickness);
			DrawDebugLine(GetWorld(), CircleCenter, CircleCenter + RightDirection * Radius,
				HeartbeatColor, false, 0.0f, 0, Thickness);

			FVector PreviousHeartbeatPoint = CircleCenter + LeftDirection * Radius;
			for (int32 SegmentIndex = 1; SegmentIndex <= SectorArcSegments; ++SegmentIndex)
			{
				const float Alpha = static_cast<float>(SegmentIndex) / static_cast<float>(SectorArcSegments);
				const float Angle = FMath::Lerp(-HalfAngleDegrees, HalfAngleDegrees, Alpha);
				const FVector Direction = HeartbeatDirection.RotateAngleAxis(Angle, FVector::UpVector);
				const FVector CurrentPoint = CircleCenter + Direction * Radius;
				DrawDebugLine(GetWorld(), PreviousHeartbeatPoint, CurrentPoint,
					HeartbeatColor, false, 0.0f, 0, Thickness);
				PreviousHeartbeatPoint = CurrentPoint;
			}
		};

		DrawHeartbeatViewSector(
			HeartbeatComponent->GetEncounterVisionRange(),
			HeartbeatComponent->GetEncounterVisionHalfAngleDegrees(),
			LineThickness);
		DrawHeartbeatViewSector(
			HeartbeatComponent->GetBaseVisionRange(),
			HeartbeatComponent->GetBaseVisionHalfAngleDegrees(),
			LineThickness * 0.65f);
	}
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
