// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "WeepingAngelCharacter.generated.h"


class AWeepingAngelPath;

UCLASS()
class PROJECTPROJECT01_API AWeepingAngelCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	// Sets default values for this character's properties
	AWeepingAngelCharacter();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;

	// Called to bind functionality to input
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

	void SetFrozen(bool bFrozen);

	AWeepingAngelPath* GetCurrentPath() const
	{
		return CurrentPath;
	}

	void SetCurrentPath(AWeepingAngelPath* NewPath)
	{
		CurrentPath = NewPath;
	}

	AWeepingAngelPath* GetAssignedApproachPath() const
	{
		return AssignedApproachPath;
	}

	void SetAssignedApproachPath(AWeepingAngelPath* NewPath)
	{
		AssignedApproachPath = NewPath;
	}

private:
	// 천사가 현재 도착한 Path
	UPROPERTY(EditAnywhere)
	TObjectPtr<AWeepingAngelPath> CurrentPath;

	// 관리자가 천사에게 배정한 최종 입구
	UPROPERTY(Transient)
	TObjectPtr<AWeepingAngelPath> AssignedApproachPath = nullptr;
};