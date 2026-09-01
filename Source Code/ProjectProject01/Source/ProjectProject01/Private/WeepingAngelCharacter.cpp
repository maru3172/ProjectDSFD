// Fill out your copyright notice in the Description page of Project Settings.


#include "WeepingAngelCharacter.h"

#include "WeepingAngelPath.h"
#include "Kismet/GameplayStatics.h"

// Sets default values
AWeepingAngelCharacter::AWeepingAngelCharacter()
{
 	// Set this character to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

}

// Called when the game starts or when spawned
void AWeepingAngelCharacter::BeginPlay()
{
	Super::BeginPlay();
	
	TArray<AActor*> PathActors;
	UGameplayStatics::GetAllActorsOfClass(
		GetWorld(),
		AWeepingAngelPath::StaticClass(),
		PathActors
	);

	float ClosestDistance = TNumericLimits<float>::Max();

	for (AActor* PathActor : PathActors)
	{
		AWeepingAngelPath* Path = Cast<AWeepingAngelPath>(PathActor);
		if (Path == nullptr)
		{
			continue;
		}

		float Distance = FVector::Dist(GetActorLocation(), Path->GetAngelPathLocation());

		if (Distance < ClosestDistance)
		{
			ClosestDistance = Distance;
			CurrentPath = Path;
		}
	}
}

// Called every frame
void AWeepingAngelCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

}

// Called to bind functionality to input
void AWeepingAngelCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

}

void AWeepingAngelCharacter::SetFrozen(bool bFrozen)
{
	GetMesh()->bPauseAnims = bFrozen;
}