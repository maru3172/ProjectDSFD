// Fill out your copyright notice in the Description page of Project Settings.


#include "MannequinAICharacter.h"

#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Net/UnrealNetwork.h"
#include "ProjectProject01TuningData.h"
#include "MultiplayTestGameMode.h"
#include "MultiplayTestPlayerController.h"
#include "PlayerCharacter.h"
#include "UObject/ConstructorHelpers.h"

// 바닥 검사 및 재생 헤더
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundAttenuation.h"
#include "ProjectProject01SpatialAudio.h"
#include "FootstepSynthComponent.h"
#include "ProjectReplicationBudgetComponent.h"


// Sets default values
AMannequinAICharacter::AMannequinAICharacter()
{
 	// Set this character to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;
	CreateDefaultSubobject<UProjectReplicationBudgetComponent>(TEXT("Replication Budget"));
	FootstepSFXComponent = CreateDefaultSubobject<UFootstepSynthComponent>(TEXT("Footstep SFX Component"));
	FootstepSFXComponent->SetupAttachment(RootComponent);
	FootstepSFXComponent->Configure(EProjectProject01FootstepStyle::MarblePlastic, 0.9f, false);
	ControlSlotLabel = CreateDefaultSubobject<UTextRenderComponent>(TEXT("Control Slot Label"));
	ControlSlotLabel->SetupAttachment(RootComponent);
	ControlSlotLabel->SetRelativeLocation(FVector(0.0f, 0.0f, 125.0f));
	ControlSlotLabel->SetHorizontalAlignment(EHTA_Center);
	ControlSlotLabel->SetVerticalAlignment(EVRTA_TextCenter);
	ControlSlotLabel->SetWorldSize(42.0f);
	ControlSlotLabel->SetTextRenderColor(FColor(255, 64, 64));
	ControlSlotLabel->SetCastShadow(false);
	ControlSlotLabel->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ControlSlotLabel->SetVisibility(false);

	static ConstructorHelpers::FObjectFinder<UInputMappingContext> MannequinInputContext(
		TEXT("/Game/MyProject/Input/IMC_PlayerControl.IMC_PlayerControl"));
	if (MannequinInputContext.Succeeded())
	{
		IMC_MannequinControl = MannequinInputContext.Object;
	}

	static ConstructorHelpers::FObjectFinder<UInputAction> MannequinMoveAction(
		TEXT("/Game/MyProject/Input/IA_Player_Move.IA_Player_Move"));
	if (MannequinMoveAction.Succeeded())
	{
		IA_MannequinMove = MannequinMoveAction.Object;
	}

	static ConstructorHelpers::FObjectFinder<UInputAction> MannequinLookAction(
		TEXT("/Game/MyProject/Input/IA_Player_Look.IA_Player_Look"));
	if (MannequinLookAction.Succeeded())
	{
		IA_MannequinLook = MannequinLookAction.Object;
	}

}

void AMannequinAICharacter::SetControlSlotForGameMode(const int32 InControlSlot)
{
	if (!HasAuthority())
	{
		return;
	}
	ControlSlot = FMath::Clamp(InControlSlot, -1, 9);
	RefreshControlSlotLabelVisibility();
	ForceNetUpdate();
}

void AMannequinAICharacter::RefreshControlSlotLabelVisibility()
{
	if (!IsValid(ControlSlotLabel) || !IsValid(GetWorld()))
	{
		return;
	}

	bool bVisibleToLocalMannequinPlayer = false;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		const AMultiplayTestPlayerController* LocalController = Cast<AMultiplayTestPlayerController>(It->Get());
		if (IsValid(LocalController) && LocalController->IsLocalController() &&
			IsValid(LocalController->GetViewedMannequin()))
		{
			bVisibleToLocalMannequinPlayer = true;
			break;
		}
	}

	ControlSlotLabel->SetText(ControlSlot >= 0 ? FText::AsNumber(ControlSlot) : FText::GetEmpty());
	ControlSlotLabel->SetVisibility(bVisibleToLocalMannequinPlayer && ControlSlot >= 0);
}

void AMannequinAICharacter::OnRep_ControlSlot()
{
	RefreshControlSlotLabelVisibility();
}

// Called when the game starts or when spawned
void AMannequinAICharacter::BeginPlay()
{
	Super::BeginPlay();

	if (UWorld* World = GetWorld(); IsValid(World) && World->GetNetMode() != NM_Client)
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

// Called every frame
void AMannequinAICharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (IsValid(FootstepSFXComponent))
	{
		FootstepSFXComponent->UpdateFootsteps(DeltaTime);
	}
	if (HasAuthority())
	{
		RemoveSeparatedCatchContacts();
	}
	if (IsValid(ControlSlotLabel) && ControlSlotLabel->IsVisible())
	{
		if (const APlayerController* LocalController = GetWorld()->GetFirstPlayerController();
			IsValid(LocalController) && IsValid(LocalController->PlayerCameraManager))
		{
			const FVector ToCamera = LocalController->PlayerCameraManager->GetCameraLocation()
				- ControlSlotLabel->GetComponentLocation();
			if (!ToCamera.IsNearlyZero())
			{
				ControlSlotLabel->SetWorldRotation(ToCamera.Rotation());
			}
		}
	}
}

void AMannequinAICharacter::NotifyHit(UPrimitiveComponent* MyComp, AActor* Other, UPrimitiveComponent* OtherComp,
	const bool bSelfMoved, FVector HitLocation, FVector HitNormal, FVector NormalImpulse, const FHitResult& Hit)
{
	Super::NotifyHit(MyComp, Other, OtherComp, bSelfMoved, HitLocation, HitNormal, NormalImpulse, Hit);
	TryCatchSurvivor(Other);
}

// Called to bind functionality to input
void AMannequinAICharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	if (!IsValid(EnhancedInputComponent))
	{
		UE_LOG(LogTemp, Error, TEXT("Mannequin input component is missing on %s."), *GetName());
		return;
	}

	if (!IsValid(IA_MannequinMove) || !IsValid(IA_MannequinLook))
	{
		ensureMsgf(false, TEXT("Mannequin input actions are missing on %s."), *GetName());
		UE_LOG(LogTemp, Error, TEXT("Mannequin input actions are missing on %s."), *GetName());
		return;
	}

	EnhancedInputComponent->BindAction(IA_MannequinMove, ETriggerEvent::Triggered, this, &AMannequinAICharacter::Move);
	EnhancedInputComponent->BindAction(IA_MannequinLook, ETriggerEvent::Triggered, this, &AMannequinAICharacter::Look);
}

void AMannequinAICharacter::SetFrozen(bool bFrozen)
{
	bLegacyAnimationFrozen = bFrozen;
	RefreshFrozenAnimationState();
}

void AMannequinAICharacter::ApplyMannequinTuning(const FMannequinAITuningRow& Tuning)
{
	MannequinWalkSpeed = FMath::Max(0.0f, Tuning.MannequinWalkSpeed);
	ApplyMannequinWalkSpeed();

	if (HasAuthority())
	{
		ForceNetUpdate();
	}
}

void AMannequinAICharacter::SetFrozenBySurvivorVision(bool bFrozen)
{
	if (!HasAuthority() || bFrozenBySurvivorVision == bFrozen)
	{
		return;
	}

	bFrozenBySurvivorVision = bFrozen;
	UCharacterMovementComponent* Movement = GetCharacterMovement();
	if (!ensureMsgf(IsValid(Movement), TEXT("Mannequin %s has no CharacterMovementComponent."), *GetName()))
	{
		return;
	}

	Movement->StopMovementImmediately();
	if (bFrozenBySurvivorVision)
	{
		MovementModeBeforeSurvivorVisionFreeze = Movement->MovementMode;
		CustomMovementModeBeforeSurvivorVisionFreeze = Movement->CustomMovementMode;
		Movement->DisableMovement();
	}
	else
	{
		const EMovementMode RestoredMovementMode = MovementModeBeforeSurvivorVisionFreeze == MOVE_None
			? MOVE_Walking
			: MovementModeBeforeSurvivorVisionFreeze.GetValue();
		Movement->SetMovementMode(RestoredMovementMode, CustomMovementModeBeforeSurvivorVisionFreeze);
	}

	RefreshFrozenAnimationState();
	ForceNetUpdate();
}

void AMannequinAICharacter::OnRep_SurvivorVisionFrozen()
{
	if (UCharacterMovementComponent* Movement = GetCharacterMovement(); IsValid(Movement))
	{
		Movement->StopMovementImmediately();
	}
	RefreshFrozenAnimationState();
}

void AMannequinAICharacter::OnRep_MannequinWalkSpeed()
{
	ApplyMannequinWalkSpeed();
}

void AMannequinAICharacter::ApplyMannequinWalkSpeed()
{
	UCharacterMovementComponent* Movement = GetCharacterMovement();
	if (!ensureMsgf(IsValid(Movement), TEXT("Mannequin %s has no CharacterMovementComponent."), *GetName()))
	{
		return;
	}

	Movement->MaxWalkSpeed = FMath::Max(0.0f, MannequinWalkSpeed);
}

void AMannequinAICharacter::SetManualControlEnabled(bool bEnabled)
{
	if (!HasAuthority())
	{
		return;
	}

	bManualControlEnabled = bEnabled;
	ApplyManualRotationSettings();
	if (bManualControlEnabled)
	{
		SetMultiplayActivity(EMultiplayMannequinActivity::ManualControl);
		bPostPossessionChaseCommandQueued = false;
		ActivePostPossessionCommand = EPostPossessionCommand::None;
	}
	ForceNetUpdate();
}

void AMannequinAICharacter::SetMultiplayCommand(const EMultiplayMannequinCommand NewCommand)
{
	if (!HasAuthority())
	{
		return;
	}

	MultiplayCommand = NewCommand;
	ForceNetUpdate();
}

void AMannequinAICharacter::SetMultiplayActivity(const EMultiplayMannequinActivity NewActivity)
{
	if (!HasAuthority() || MultiplayActivity == NewActivity)
	{
		return;
	}
	MultiplayActivity = NewActivity;
	ForceNetUpdate();
}

bool AMannequinAICharacter::QueuePostPossessionChaseCommand()
{
	if (!HasAuthority() || !bManualControlEnabled)
	{
		return false;
	}

	bPostPossessionChaseCommandQueued = true;
	return true;
}

void AMannequinAICharacter::ActivatePostPossessionCommand()
{
	if (!HasAuthority())
	{
		return;
	}

	ActivePostPossessionCommand = bPostPossessionChaseCommandQueued
		? EPostPossessionCommand::ChaseNearestSurvivor
		: EPostPossessionCommand::HoldPosition;
	bPostPossessionChaseCommandQueued = false;
}

bool AMannequinAICharacter::IsPostPossessionCommandActive() const
{
	return ActivePostPossessionCommand != EPostPossessionCommand::None;
}

bool AMannequinAICharacter::ShouldHoldPostPossessionCommand() const
{
	return IsPostPossessionCommandActive() &&
		ActivePostPossessionCommand == EPostPossessionCommand::HoldPosition;
}

bool AMannequinAICharacter::ShouldChasePostPossessionCommand() const
{
	return IsPostPossessionCommandActive() &&
		ActivePostPossessionCommand == EPostPossessionCommand::ChaseNearestSurvivor;
}

FString AMannequinAICharacter::GetDiagnosticCommandState() const
{
	if (bPostPossessionChaseCommandQueued) return TEXT("QueuedChase");
	if (ShouldHoldPostPossessionCommand()) return TEXT("HoldPosition");
	if (ShouldChasePostPossessionCommand()) return TEXT("ChaseNearestSurvivor");
	return TEXT("None");
}

void AMannequinAICharacter::GetDiagnosticAppliedTuning(FMannequinAITuningRow& OutTuning) const
{
	OutTuning.MannequinWalkSpeed = IsValid(GetCharacterMovement()) ? GetCharacterMovement()->MaxWalkSpeed : 0.0f;
}

void AMannequinAICharacter::TryCatchSurvivor(AActor* OtherActor)
{
	if (!HasAuthority())
	{
		return;
	}

	APlayerCharacter* PlayerCharacter = Cast<APlayerCharacter>(OtherActor);
	if (!IsValid(PlayerCharacter) || PlayerCharacter->IsGameOver())
	{
		return;
	}
	const bool bDirectMultiplayerControl = bManualControlEnabled &&
		IsValid(Cast<AMultiplayTestPlayerController>(GetController()));
	if (!bCatchDamageEnabled && !bDirectMultiplayerControl)
	{
		return;
	}

	const bool bAlreadyCaughtInCurrentContact = CaughtSurvivorsInCurrentContact.ContainsByPredicate(
		[PlayerCharacter](const TWeakObjectPtr<APlayerCharacter>& ExistingPlayer)
		{
			return ExistingPlayer.Get() == PlayerCharacter;
		});
	if (bAlreadyCaughtInCurrentContact)
	{
		return;
	}

	CaughtSurvivorsInCurrentContact.Add(PlayerCharacter);
	if (!PlayerCharacter->HandleMannequinCatch(this))
	{
		CaughtSurvivorsInCurrentContact.RemoveSingleSwap(PlayerCharacter);
	}
}

void AMannequinAICharacter::SetCatchDamageEnabled(const bool bEnabled)
{
	if (!HasAuthority())
	{
		return;
	}
	bCatchDamageEnabled = bEnabled;
	ForceNetUpdate();
}

bool AMannequinAICharacter::ToggleCatchDamageEnabled()
{
	if (HasAuthority())
	{
		SetCatchDamageEnabled(!bCatchDamageEnabled);
	}
	return bCatchDamageEnabled;
}

void AMannequinAICharacter::RemoveSeparatedCatchContacts()
{
	const UCapsuleComponent* MannequinCapsule = GetCapsuleComponent();
	if (!IsValid(MannequinCapsule))
	{
		ensureMsgf(false, TEXT("Mannequin %s has no capsule for catch-contact cleanup."), *GetName());
		CaughtSurvivorsInCurrentContact.Reset();
		return;
	}

	const float MannequinRadius = MannequinCapsule->GetScaledCapsuleRadius();
	CaughtSurvivorsInCurrentContact.RemoveAll([this, MannequinRadius](const TWeakObjectPtr<APlayerCharacter>& CaughtPlayer)
	{
		const APlayerCharacter* PlayerCharacter = CaughtPlayer.Get();
		const UCapsuleComponent* PlayerCapsule = IsValid(PlayerCharacter) ? PlayerCharacter->GetCapsuleComponent() : nullptr;
		if (!IsValid(PlayerCapsule))
		{
			return true;
		}

		FVector HorizontalSeparation = PlayerCharacter->GetActorLocation() - GetActorLocation();
		HorizontalSeparation.Z = 0.0f;
		const float RecatchDistance = MannequinRadius + PlayerCapsule->GetScaledCapsuleRadius() + 10.0f;
		return HorizontalSeparation.SizeSquared() > FMath::Square(RecatchDistance);
	});
}

void AMannequinAICharacter::RefreshFrozenAnimationState()
{
	if (USkeletalMeshComponent* MannequinMesh = GetMesh())
	{
		MannequinMesh->bPauseAnims = bLegacyAnimationFrozen || bFrozenBySurvivorVision;
	}
}

int32 AMannequinAICharacter::GetControlSlot() const
{
	return FMath::Clamp(ControlSlot, -1, 9);
}

void AMannequinAICharacter::PawnClientRestart()
{
	Super::PawnClientRestart();
	RegisterLocalInputMapping();
}

void AMannequinAICharacter::OnRep_ManualControlEnabled()
{
	ApplyManualRotationSettings();
}

void AMannequinAICharacter::ApplyManualRotationSettings()
{
	UCharacterMovementComponent* Movement=GetCharacterMovement();
	if (!Movement) return;
	if (bManualControlEnabled && !bHasSavedRotationSettings)
	{
		bHasSavedRotationSettings=true;
		bSavedUseControllerYaw=bUseControllerRotationYaw;
		bSavedOrientRotationToMovement=Movement->bOrientRotationToMovement;
		bSavedUseControllerDesiredRotation=Movement->bUseControllerDesiredRotation;
		bUseControllerRotationYaw=true;
		Movement->bOrientRotationToMovement=false;
		if (auto* PC=Cast<AMultiplayTestPlayerController>(GetController());PC && PC->IsLocalController())
		{
			PC->ResolveMannequinCameraRotation(this,0);
			Movement->bUseControllerDesiredRotation=PC->IsManualMannequinCameraReady();
		}
		else Movement->bUseControllerDesiredRotation=true;
	}
	else if (!bManualControlEnabled && bHasSavedRotationSettings)
	{
		bUseControllerRotationYaw=bSavedUseControllerYaw;
		Movement->bOrientRotationToMovement=bSavedOrientRotationToMovement;
		Movement->bUseControllerDesiredRotation=bSavedUseControllerDesiredRotation;
		bHasSavedRotationSettings=false;
	}
}

void AMannequinAICharacter::CalcCamera(float DeltaTime,FMinimalViewInfo& OutResult)
{
	Super::CalcCamera(DeltaTime,OutResult);
	if (!GetWorld()) return;
	for(FConstPlayerControllerIterator It=GetWorld()->GetPlayerControllerIterator();It;++It)
		if(auto* PC=Cast<AMultiplayTestPlayerController>(It->Get());PC && PC->IsLocalController() && PC->GetViewedMannequin()==this)
		{
			OutResult.Rotation=PC->ResolveMannequinCameraRotation(this,DeltaTime);
			return; // Visual view only: never write the AI's controller/actor rotation here.
		}
}

void AMannequinAICharacter::FaceRotation(FRotator NewControlRotation,float DeltaTime)
{
	if (auto* PC=Cast<AMultiplayTestPlayerController>(GetController());PC && bManualControlEnabled)
	{
		if (bFrozenBySurvivorVision || (PC->IsLocalController() && !PC->IsManualMannequinCameraReady())) return;
		SetActorRotation(FRotator(0,NewControlRotation.Yaw,0));return;
	}
	Super::FaceRotation(NewControlRotation,DeltaTime);
}

void AMannequinAICharacter::Move(const FInputActionValue& Value)
{
	if (!IsLocallyControlled() || !bManualControlEnabled || bFrozenBySurvivorVision)
	{
		return;
	}
	if (const auto* PC=Cast<AMultiplayTestPlayerController>(GetController());PC && PC->IsMannequinCameraAligning()) return;

	const FVector2D MovementVector = Value.Get<FVector2D>();
	AddMovementInput(GetActorForwardVector(), MovementVector.X);
	AddMovementInput(GetActorRightVector(), MovementVector.Y);
}

void AMannequinAICharacter::Look(const FInputActionValue& Value)
{
	// Multiplayer owns its look binding even when this pawn remains AI-controlled.
	if (Cast<AMultiplayTestPlayerController>(GetController())) return;
	if (!IsLocallyControlled())
	{
		return;
	}

	const FVector2D LookVector = Value.Get<FVector2D>();
	AddControllerYawInput(LookVector.X);
	AddControllerPitchInput(LookVector.Y);
}

void AMannequinAICharacter::RegisterLocalInputMapping()
{
	if (!IsLocallyControlled())
	{
		return;
	}

	APlayerController* PlayerController = Cast<APlayerController>(GetController());
	if (!ensureMsgf(IsValid(PlayerController), TEXT("Mannequin %s has no local PlayerController."), *GetName()) ||
		!ensureMsgf(IsValid(IMC_MannequinControl), TEXT("Mannequin %s is missing its input mapping context."), *GetName()))
	{
		return;
	}

	UEnhancedInputLocalPlayerSubsystem* InputSubsystem =
		ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PlayerController->GetLocalPlayer());
	if (!ensureMsgf(IsValid(InputSubsystem), TEXT("Mannequin %s could not access the Enhanced Input subsystem."), *GetName()))
	{
		return;
	}

	InputSubsystem->AddMappingContext(IMC_MannequinControl, 0);
}

void AMannequinAICharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AMannequinAICharacter, bFrozenBySurvivorVision);
	DOREPLIFETIME(AMannequinAICharacter, MannequinWalkSpeed);
	DOREPLIFETIME(AMannequinAICharacter, bManualControlEnabled);
	DOREPLIFETIME(AMannequinAICharacter, MultiplayCommand);
	DOREPLIFETIME(AMannequinAICharacter, MultiplayActivity);
	DOREPLIFETIME(AMannequinAICharacter, bCatchDamageEnabled);
	DOREPLIFETIME(AMannequinAICharacter, ControlSlot);
}

bool AMannequinAICharacter::IsNetRelevantFor(const AActor* RealViewer, const AActor* ViewTarget, const FVector& SrcLocation) const
{
	// The mannequin player needs all numbered bodies, even outside the current
	// camera's cull distance. Do not change survivor or single-player relevancy.
	const auto* Viewer = Cast<AMultiplayTestPlayerController>(RealViewer);
	if (ControlSlot >= 0 && Viewer && Viewer->GetWorld() == GetWorld() &&
		Viewer->GetAuthenticatedRole().Equals(TEXT("Mannequin"), ESearchCase::IgnoreCase)) return true;
	return Super::IsNetRelevantFor(RealViewer, ViewTarget, SrcLocation);
}

// 발소리 함수
void AMannequinAICharacter::PlayFootstep(FName FootBoneName)
{
	UWorld* World = GetWorld();
	USkeletalMeshComponent* MannequinMesh = GetMesh();
	UCharacterMovementComponent* Movement = GetCharacterMovement();
	
	if (!IsValid(World) || !IsValid(MannequinMesh) || !IsValid(Movement))
	{
		return;
	}
	
	// 정지하거나 공중에 있을 때 잘못 재생되는 Notify 를 방지한다.
	const float HorizontalSpeed = GetVelocity().Size2D();
	if (HorizontalSpeed < 10.0f || !Movement->IsMovingOnGround())
	{
		return;
	}
	
	// Blend Space 등에서 거의 동시에 들어오는 중복 Notify 를 방지한다.
	const double CurrentTime = World->GetTimeSeconds();
	if (CurrentTime - LastFootstepTimeSeconds < MinimumFootstepInterval)
	{
		return;
	}
	
	if (!MannequinMesh->DoesSocketExist(FootBoneName))
	{
		UE_LOG(LogTemp, Warning, TEXT("Foot bone or socket '%s' was not found on %s."), *FootBoneName.ToString(), *GetName());
		return;
	}
	
	const FVector FootLocation = MannequinMesh->GetSocketLocation(FootBoneName);
	const FVector TraceStart = FootLocation + FVector::UpVector * FootstepTraceUpDistance;
	const FVector TraceEnd = FootLocation - FVector::UpVector * FootstepTraceDownDistance;
	
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(MannequinFootstep), true, this);
	QueryParams.bReturnPhysicalMaterial = true;
	
	FHitResult Hit;
	const bool bHit = World->LineTraceSingleByChannel(
		Hit,
		TraceStart,
		TraceEnd,
		ECC_Visibility,
		QueryParams);
	
	if (!bHit)
	{
		return;
	}
	
	LastFootstepTimeSeconds = CurrentTime;
	
	if (IsValid(FootstepSFXComponent))
	{
		// Keep the source just above the floor so the floor itself does not occlude every step.
		FootstepSFXComponent->SetWorldLocation(Hit.ImpactPoint+Hit.ImpactNormal*8.0f);
		FootstepSFXComponent->TriggerFootstep(HorizontalSpeed >= 700.0f);
	}
	else if (IsValid(DefaultFootstepSound))
	{
		if(!IsValid(OccludedFootstepAttenuation)||OcclusionFootstepSource!=FootstepAttenuation)
		{
			FSoundAttenuationSettings Fallback;
			OcclusionFootstepSource=FootstepAttenuation;
			OccludedFootstepAttenuation=ProjectProject01SpatialAudio::MakeOccludedCopy(this,FootstepAttenuation,Fallback);
		}
		// 기존 Blueprint가 별도 음원을 지정한 경우를 위한 안전한 하위 호환 경로입니다.
		UGameplayStatics::PlaySoundAtLocation(
			this,
			DefaultFootstepSound,
			Hit.ImpactPoint+Hit.ImpactNormal*8.0f,
			1.0f,
			FMath::FRandRange(0.95f, 1.05f),
			0.0f,
			OccludedFootstepAttenuation);
	}
}
