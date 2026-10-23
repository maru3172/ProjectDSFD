// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "InteractionActorBase.h"
#include "FireShutterActor.generated.h"

class APawn;

/**
 * CurrentOpenAlpha
 * 0.0 = 완전히 닫힘
 * 1.0 = 완전히 열림
 */
UCLASS()
class PROJECTPROJECT01_API AFireShutterActor : public AInteractionActorBase
{
	GENERATED_BODY()
	
public:
	AFireShutterActor();
	
	virtual void Tick(float DeltaTime) override;
	
protected:
	virtual void BeginPlay() override;
	
	// 상호작용이 허용된 후 셔터의 이동 방향을 전환한다.
	virtual void HandleInteraction(APawn* Interactor) override;
	
	// 셔터가 시작 위치로부터 올라갈 높이
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shutter|Movement", meta = (ClampMin = "0.0", Units = "cm"))
	float OpenHeight = 300.0f;
	
	// 셔터의 초당 이동 거리
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shutter|Movement", meta = (ClampMin = "0.0", Units = "cm/s"))
	float MoveSpeed = 150.0f;
	
	// 셔터가 목표 위치에 도착했다고 판단할 오차 범위
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shutter|Movement", meta = (ClampmMin = "0.001"))
	float ArrivalTolerance = 0.001f;
	
private:
	// 셔터가 완전히 내려가 있을 때의 상대 위치
	FVector ClosedLocation = FVector::ZeroVector;
	
	// 셔터가 완전히 올라가 있을 때의 상대 위치
	FVector OpenLocation = FVector::ZeroVector;
	
	// 현재 셔터 위치를 0 ~ 1 범위로 나타낸 값
	float CurrentOpenAlpha = 0.0f;
	
	// 현재 이동 목표
	// 0.0 이면 닫기, 1.0 이면 열기
	float TargetOpenAlpha = 0.0f;
};
