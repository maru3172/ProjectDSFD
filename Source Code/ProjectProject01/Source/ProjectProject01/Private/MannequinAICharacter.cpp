// Fill out your copyright notice in the Description page of Project Settings.


#include "MannequinAICharacter.h"

#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Net/UnrealNetwork.h"
#include "ProjectProject01TuningData.h"
#include "PlayerCharacter.h"
#include "UObject/ConstructorHelpers.h"

// Sets default values
AMannequinAICharacter::AMannequinAICharacter()
{
 	// Set this character to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

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
	if (HasAuthority())
	{
		RemoveSeparatedCatchContacts();
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
	PostPossessionCommandDurationSeconds = FMath::Max(0.0f, Tuning.PostPossessionCommandDurationSeconds);
	if (ActivePostPossessionCommand != EPostPossessionCommand::None)
	{
		PostPossessionCommandEndTimeSeconds = PostPossessionCommandStartTimeSeconds + PostPossessionCommandDurationSeconds;
	}
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
	if (bManualControlEnabled)
	{
		bPostPossessionChaseCommandQueued = false;
		ActivePostPossessionCommand = EPostPossessionCommand::None;
		PostPossessionCommandStartTimeSeconds = 0.0;
		PostPossessionCommandEndTimeSeconds = 0.0;
	}
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

	const UWorld* World = GetWorld();
	const double CurrentTimeSeconds = IsValid(World) ? World->GetTimeSeconds() : 0.0;
	PostPossessionCommandStartTimeSeconds = CurrentTimeSeconds;
	PostPossessionCommandEndTimeSeconds = CurrentTimeSeconds + FMath::Max(0.0f, PostPossessionCommandDurationSeconds);
	ActivePostPossessionCommand = bPostPossessionChaseCommandQueued
		? EPostPossessionCommand::ChaseNearestSurvivor
		: EPostPossessionCommand::HoldPosition;
	bPostPossessionChaseCommandQueued = false;
}

bool AMannequinAICharacter::IsPostPossessionCommandActive(double ServerTimeSeconds) const
{
	return ActivePostPossessionCommand != EPostPossessionCommand::None &&
		ServerTimeSeconds < PostPossessionCommandEndTimeSeconds;
}

bool AMannequinAICharacter::ShouldHoldPostPossessionCommand(double ServerTimeSeconds) const
{
	return IsPostPossessionCommandActive(ServerTimeSeconds) &&
		ActivePostPossessionCommand == EPostPossessionCommand::HoldPosition;
}

bool AMannequinAICharacter::ShouldChasePostPossessionCommand(double ServerTimeSeconds) const
{
	return IsPostPossessionCommandActive(ServerTimeSeconds) &&
		ActivePostPossessionCommand == EPostPossessionCommand::ChaseNearestSurvivor;
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

void AMannequinAICharacter::Move(const FInputActionValue& Value)
{
	if (!IsLocallyControlled() || !bManualControlEnabled || bFrozenBySurvivorVision)
	{
		return;
	}

	const FVector2D MovementVector = Value.Get<FVector2D>();
	AddMovementInput(GetActorForwardVector(), MovementVector.X);
	AddMovementInput(GetActorRightVector(), MovementVector.Y);
}

void AMannequinAICharacter::Look(const FInputActionValue& Value)
{
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
}
