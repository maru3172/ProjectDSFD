// Fill out your copyright notice in the Description page of Project Settings.


#include "ProjectPracticeGameModeBase.h"

#include "HelperRearGuardCharacter.h"
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
