// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/EngineTypes.h"
#include "PlayerInteractionComponent.generated.h"


UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent) )
class PROJECTPROJECT01_API UPlayerInteractionComponent : public UActorComponent
{
	GENERATED_BODY()

public:	
	// Sets default values for this component's properties
	UPlayerInteractionComponent();
	
	// 카메라 방향으로 Line Trace 를 실행하고 맞은 대상을 상호작용 실행
	UFUNCTION(BlueprintCallable, Category = "Interaction")
	void TryInteract();

protected:
	// 상호작용 가능한 최대 거리
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction", meta = (ClampMin = "0.0", UImin = "0.0", Units = "cm"))
	float InteractionDistance = 300.0f;
	
	// 상호작용 탐새게 사용할 충돌 채널
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction")
	TEnumAsByte<ECollisionChannel> TraceChannel = ECC_Visibility;
	
	// 플레이 중 Line Trace 를 홤녀에 표시할지 결정
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction|Debug")
	bool bDrawDebugTrace = true;
	
};
