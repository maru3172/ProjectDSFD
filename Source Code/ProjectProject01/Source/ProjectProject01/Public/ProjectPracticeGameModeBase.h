// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "ProjectPracticeGameModeBase.generated.h"

class AHelperRearGuardCharacter;

/**
 * 
 */
UCLASS()
class PROJECTPROJECT01_API AProjectPracticeGameModeBase : public AGameModeBase
{
	GENERATED_BODY()

protected:
	virtual void BeginPlay() override;

private:
	// 레벨에 수동 배치된 조력자가 없을 때만 런타임에 생성한 후방 경계 조력자입니다.
	UPROPERTY(Transient)
	TObjectPtr<AHelperRearGuardCharacter> SpawnedHelperRearGuard;
};
