// Fill out your copyright notice in the Description page of Project Settings.


#include "MannequinAICharacter.h"

// Sets default values
AMannequinAICharacter::AMannequinAICharacter()
{
 	// Set this character to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

}

// Called when the game starts or when spawned
void AMannequinAICharacter::BeginPlay()
{
	Super::BeginPlay();
	
}

// Called every frame
void AMannequinAICharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

}

// Called to bind functionality to input
void AMannequinAICharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

}

void AMannequinAICharacter::SetFrozen(bool bFrozen)
{
	GetMesh()->bPauseAnims = bFrozen;
}