// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/Services/BTService_BlackboardBase.h"
#include "BTService_WeepingAngelExample01.generated.h"


/**
 * 
 */
UCLASS()
class PROJECTPROJECT01_API UBTService_WeepingAngelExample01 : public UBTService_BlackboardBase
{
	GENERATED_BODY()
	
public:
	UBTService_WeepingAngelExample01();

protected:
	virtual void TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;

private:
	// 플레이어의 화면에 천사가 확인이 되었는지 여부를 저장한다.
	bool PlayerSeeAngel;

	// 천사가 플레이어를 발견했는지 저장한다.
	bool AngelSeePlayer;
};
