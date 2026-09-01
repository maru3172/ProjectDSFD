// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/Services/BTService_BlackboardBase.h"
#include "BTService_WeepingAngel.generated.h"

class APawn;
class AWeepingAngelCharacter;

/**
 * 
 */
UCLASS()
class PROJECTPROJECT01_API UBTService_WeepingAngel : public UBTService_BlackboardBase
{
	GENERATED_BODY()
	
public:
	UBTService_WeepingAngel();

protected:
	virtual void TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
};