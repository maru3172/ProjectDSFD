// Fill out your copyright notice in the Description page of Project Settings.


#include "ProjectPracticeGameModeBase.h"

#include "HelperRearGuardCharacter.h"
#include "PlayerCharacter.h"
#include "Engine/TriggerBox.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"

void AProjectPracticeGameModeBase::BeginPlay()
{
	Super::BeginPlay();

	UWorld* World = GetWorld();
	if (!ensureMsgf(IsValid(World), TEXT("ProjectPracticeGameModeBase requires a valid world to create HelperRearGuardCharacter.")))
	{
		return;
	}
	BindEscapeTriggers();

	SpawnedHelperRearGuard = Cast<AHelperRearGuardCharacter>(
		UGameplayStatics::GetActorOfClass(World, AHelperRearGuardCharacter::StaticClass())
	);

	if (IsValid(SpawnedHelperRearGuard))
	{
		return;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(World, 0);
	const FVector SpawnLocation = IsValid(PlayerPawn)
		? PlayerPawn->GetActorLocation()
		: FVector::ZeroVector;
	const FRotator SpawnRotation = IsValid(PlayerPawn)
		? PlayerPawn->GetActorRotation()
		: FRotator::ZeroRotator;

	SpawnedHelperRearGuard = World->SpawnActor<AHelperRearGuardCharacter>(
		AHelperRearGuardCharacter::StaticClass(),
		SpawnLocation,
		SpawnRotation,
		SpawnParameters
	);

	ensureMsgf(
		IsValid(SpawnedHelperRearGuard),
		TEXT("Failed to create HelperRearGuardCharacter. Check the ProjectProject01 game mode and world state.")
	);
}

void AProjectPracticeGameModeBase::BindEscapeTriggers()
{
	UWorld* World = GetWorld();
	if (!HasAuthority() || !IsValid(World) || World->GetNetMode() != NM_Standalone)
	{
		return;
	}

	int32 BoundTriggerCount = 0;
	for (TActorIterator<ATriggerBox> It(World); It; ++It)
	{
		ATriggerBox* Trigger = *It;
		if (!IsValid(Trigger) || !Trigger->ActorHasTag(EscapeTriggerActorTag))
		{
			continue;
		}
		Trigger->OnActorBeginOverlap.AddUniqueDynamic(
			this, &AProjectPracticeGameModeBase::HandleEscapeTriggerBeginOverlap);
		++BoundTriggerCount;
	}

	UE_LOG(LogTemp, Log, TEXT("Single-player match bound %d escape trigger(s) tagged %s."),
		BoundTriggerCount, *EscapeTriggerActorTag.ToString());
}

void AProjectPracticeGameModeBase::HandleEscapeTriggerBeginOverlap(
	AActor* OverlappedActor,
	AActor* OtherActor)
{
	(void)OverlappedActor;
	if (APlayerCharacter* Player = Cast<APlayerCharacter>(OtherActor); IsValid(Player) && !Player->IsGameOver())
	{
		Player->PresentSinglePlayerResult(true);
	}
}
