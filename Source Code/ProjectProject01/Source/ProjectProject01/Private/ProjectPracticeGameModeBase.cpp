// Fill out your copyright notice in the Description page of Project Settings.


#include "ProjectPracticeGameModeBase.h"

#include "HelperRearGuardCharacter.h"
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

	SpawnedHelperRearGuard = World->SpawnActor<AHelperRearGuardCharacter>(
		AHelperRearGuardCharacter::StaticClass(),
		FVector::ZeroVector,
		FRotator::ZeroRotator,
		SpawnParameters
	);

	ensureMsgf(
		IsValid(SpawnedHelperRearGuard),
		TEXT("Failed to create HelperRearGuardCharacter. Check the ProjectProject01 game mode and world state.")
	);
}
