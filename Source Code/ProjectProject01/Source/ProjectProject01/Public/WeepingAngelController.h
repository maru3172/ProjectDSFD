// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "WeepingAngelController.generated.h"

/**
 * 
 */
UCLASS()
class PROJECTPROJECT01_API AWeepingAngelController : public AAIController
{
	GENERATED_BODY()

protected:
	virtual void BeginPlay() override;
	
private:
	UPROPERTY(EditAnywhere)
	class UBehaviorTree* AIBehavior;
};