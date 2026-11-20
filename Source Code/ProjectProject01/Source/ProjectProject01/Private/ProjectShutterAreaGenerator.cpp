// Fill out your copyright notice in the Description page of Project Settings.


#include "ProjectShutterAreaGenerator.h"

#include "Components/SceneComponent.h"

// Sets default values
AProjectShutterAreaGenerator::AProjectShutterAreaGenerator()
{
 	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);
	
	SetCanBeDamaged(false);
}

// Called when the game starts or when spawned
void AProjectShutterAreaGenerator::BeginPlay()
{
	Super::BeginPlay();
	
}

// Called every frame
void AProjectShutterAreaGenerator::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

}

