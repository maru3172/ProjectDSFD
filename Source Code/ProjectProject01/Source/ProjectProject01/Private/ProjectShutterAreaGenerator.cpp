// Fill out your copyright notice in the Description page of Project Settings.


#include "ProjectShutterAreaGenerator.h"

#include "Components/SceneComponent.h"
#include "Components/BoxComponent.h"

// Sets default values
AProjectShutterAreaGenerator::AProjectShutterAreaGenerator()
{
 	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);
	
	SetCanBeDamaged(false);

	
	GenerationArea = CreateDefaultSubobject<UBoxComponent>(TEXT("GenerationArea"));
	GenerationArea->SetupAttachment(SceneRoot);
	GenerationArea->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GenerationArea->SetGenerateOverlapEvents(false);
	GenerationArea->SetCanEverAffectNavigation(false);
	GenerationArea->SetHiddenInGame(true);
	GenerationArea->SetLineThickness(2.0f);
	GenerationArea->ShapeColor = FColor(80, 180, 255);
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

void AProjectShutterAreaGenerator::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	RefreshAreaVisualization();
}

void AProjectShutterAreaGenerator::RefreshAreaVisualization()
{
	if (!GenerationArea)
	{
		return;
	}
	
	const float SafeSizeX = FMath::Max(AreaSize.X, 300.0f);
	const float SafeSizeY = FMath::Max(AreaSize.Y, 300.0f);
	const float SafeHeight = FMath::Max(VisualizationHeight, 1.0f);
	
	GenerationArea->SetBoxExtent(FVector(SafeSizeX * 0.5, SafeSizeY * 0.5, SafeHeight * 0.5f));
	GenerationArea->SetRelativeLocation(FVector(0.0f, 0.0f, SafeHeight * 0.5f));
	GenerationArea->SetVisibility(bShowGenerationArea);
}
