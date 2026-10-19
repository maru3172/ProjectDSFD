// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/EngineTypes.h"
#include "Engine/NetSerialization.h"
#include "PlayerInteractionComponent.generated.h"

class AActor;
class UInteractionPromptWidget;

UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent) )
class PROJECTPROJECT01_API UPlayerInteractionComponent : public UActorComponent
{
	GENERATED_BODY()
	friend class FProjectElevatorTest;

public:	
	// Sets default values for this component's properties
	UPlayerInteractionComponent();
	
	// 카메라 방향으로 Line Trace 를 실행하고 맞은 대상을 상호작용 실행
	UFUNCTION(BlueprintCallable, Category = "Interaction")
	void TryInteract();
	
protected:
	virtual void BeginPlay() override;

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	/** 엘리베이터만 서버에서 시선/거리/역할을 다시 검증하여 실행합니다. */
	UFUNCTION(Server, Reliable)
	void ServerInteractElevator(class AProjectProject01Elevator* Elevator, FVector_NetQuantize ViewOrigin, FVector_NetQuantizeNormal ViewDirection);
	double LastElevatorRequestSeconds = -1.0;

	// 카메라 방향으로 라인 트레이스를 실행하고 현재 상호작용 가능한 액터를 반환한다.
	AActor* FindInteractableActor(bool bDrawDebug) const;

	// 현재 바라보는 대상을 확인하고 UI 를 갱신한다.
	void UpdateInteractionTarget();

	// 현재 바라보고 있는 상호작용 대상
	UPROPERTY(Transient)
	TObjectPtr<AActor> CurrentInteractable;

	// 생성된 상호작용 안내 위젯
	UPROPERTY(Transient)
	TObjectPtr<UInteractionPromptWidget> InteractionWidget;

	// 생성할 상호작용 위젯 클래스
	UPROPERTY(EditDefaultsOnly, Category = "Interaction|UI")
	TSubclassOf<UInteractionPromptWidget> InteractionWidgetClass;
	
	// 상호작용 가능한 최대 거리
	UPROPERTY(EditAnywhere, Category = "Interaction", meta = (ClampMin = "0.0", UIMin = "0.0", Units = "cm"))
	float InteractionDistance = 300.0f;
	
	// 상호작용 탐새게 사용할 충돌 채널
	UPROPERTY(EditAnywhere, Category = "Interaction")
	TEnumAsByte<ECollisionChannel> TraceChannel = ECC_Visibility;
	
	// 플레이 중 Line Trace 를 홤녀에 표시할지 결정
	UPROPERTY(EditAnywhere, Category = "Interaction|Debug")
	bool bDrawDebugTrace = true;
	
};
